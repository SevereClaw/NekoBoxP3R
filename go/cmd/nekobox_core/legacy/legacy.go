// Package legacy converts the sing-box 1.9-style configuration that the NekoBoxP3R GUI
// (and old user-made custom JSON) produces into the format of current sing-box.
//
// It works on plain JSON maps and depends only on the standard library, so it can be tested
// without the core. Every step only touches legacy fields: a config that is already in the
// new format passes through unchanged.
package legacy

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

type Options struct {
	// Directories searched for compiled rule-sets: geosite-<code>.srs / geoip-<code>.srs
	RuleSetDirs []string
}

type Result struct {
	Config   []byte
	Warnings []string
}

type obj = map[string]any

type migrator struct {
	opts     Options
	warnings []string

	root obj

	blockOutbounds map[string]bool // tags of removed "block" outbounds
	dnsOutbounds   map[string]bool // tags of removed "dns" outbounds
	directOutbound map[string]bool // tags of plain "direct" outbounds
	rcodeServers   map[string]string
	ruleSets       map[string]string // tag -> path
	ruleSetOrder   []string
	missingSets    map[string]bool
	prefixRules    []any
}

// Migrate converts a configuration. Unknown fields are kept as they are.
func Migrate(input []byte, opts Options) (*Result, error) {
	decoder := json.NewDecoder(bytes.NewReader(stripComments(input)))
	decoder.UseNumber()
	var root obj
	if err := decoder.Decode(&root); err != nil {
		return nil, fmt.Errorf("parse config: %w", err)
	}
	m := &migrator{
		opts:           opts,
		root:           root,
		blockOutbounds: map[string]bool{},
		dnsOutbounds:   map[string]bool{},
		directOutbound: map[string]bool{},
		rcodeServers:   map[string]string{},
		ruleSets:       map[string]string{},
		missingSets:    map[string]bool{},
	}
	m.inbounds()
	resolverStrategy := m.outbounds()
	defaultResolver := m.dns()
	m.route(defaultResolver, resolverStrategy)

	out, err := json.Marshal(root)
	if err != nil {
		return nil, err
	}
	return &Result{Config: out, Warnings: m.warnings}, nil
}

// stripComments removes // and /* */ comments and trailing commas outside of strings
// (hand-edited configs used to rely on the lenient parser of the old core).
func stripComments(input []byte) []byte {
	out := make([]byte, 0, len(input))
	inString := false
	for i := 0; i < len(input); i++ {
		c := input[i]
		if inString {
			out = append(out, c)
			if c == '\\' && i+1 < len(input) {
				i++
				out = append(out, input[i])
			} else if c == '"' {
				inString = false
			}
			continue
		}
		switch {
		case c == '"':
			inString = true
			out = append(out, c)
		case c == '/' && i+1 < len(input) && input[i+1] == '/':
			for i < len(input) && input[i] != '\n' {
				i++
			}
			out = append(out, '\n')
		case c == '/' && i+1 < len(input) && input[i+1] == '*':
			i += 2
			for i+1 < len(input) && !(input[i] == '*' && input[i+1] == '/') {
				i++
			}
			i++
		case c == '}' || c == ']':
			// drop a trailing comma before the closing bracket
			j := len(out) - 1
			for j >= 0 && (out[j] == ' ' || out[j] == '\t' || out[j] == '\n' || out[j] == '\r') {
				j--
			}
			if j >= 0 && out[j] == ',' {
				out = append(out[:j], out[j+1:]...)
			}
			out = append(out, c)
		default:
			out = append(out, c)
		}
	}
	return out
}

func (m *migrator) warn(format string, args ...any) {
	m.warnings = append(m.warnings, fmt.Sprintf(format, args...))
}

// ---------- helpers ----------

func asObj(v any) (obj, bool) {
	o, ok := v.(map[string]any)
	return o, ok
}

func asList(v any) []any {
	switch t := v.(type) {
	case nil:
		return nil
	case []any:
		return t
	default:
		return []any{t}
	}
}

func asString(v any) string {
	s, _ := v.(string)
	return s
}

func asBool(v any) bool {
	b, _ := v.(bool)
	return b
}

func stringList(v any) []string {
	var out []string
	for _, item := range asList(v) {
		if s, ok := item.(string); ok && s != "" {
			out = append(out, s)
		}
	}
	return out
}

func validStrategy(s string) string {
	switch s {
	case "prefer_ipv4", "prefer_ipv6", "ipv4_only", "ipv6_only":
		return s
	}
	return ""
}

// ---------- inbounds ----------

func mergeInto(o obj, target string, keys ...string) {
	var merged []any
	if existing, ok := o[target]; ok {
		merged = append(merged, asList(existing)...)
	}
	found := false
	for _, key := range keys {
		if value, ok := o[key]; ok {
			found = true
			merged = append(merged, asList(value)...)
			delete(o, key)
		}
	}
	if found && len(merged) > 0 {
		o[target] = merged
	}
}

func (m *migrator) inbounds() {
	list := asList(m.root["inbounds"])
	var sniffRules, resolveRules []any
	for i, item := range list {
		in, ok := asObj(item)
		if !ok {
			continue
		}
		if asString(in["type"]) == "tun" {
			mergeInto(in, "address", "inet4_address", "inet6_address")
			mergeInto(in, "route_address", "inet4_route_address", "inet6_route_address")
			mergeInto(in, "route_exclude_address", "inet4_route_exclude_address", "inet6_route_exclude_address")
			delete(in, "gso")
			delete(in, "endpoint_independent_nat")
		}

		_, hasSniff := in["sniff"]
		_, hasStrategy := in["domain_strategy"]
		_, hasOverride := in["sniff_override_destination"]
		_, hasTimeout := in["sniff_timeout"]
		if !hasSniff && !hasStrategy && !hasOverride && !hasTimeout {
			continue
		}
		tag := asString(in["tag"])
		if tag == "" {
			tag = fmt.Sprintf("in-%d", i)
			in["tag"] = tag
		}
		if asBool(in["sniff"]) {
			rule := obj{"inbound": []any{tag}, "action": "sniff"}
			if timeout := asString(in["sniff_timeout"]); timeout != "" {
				rule["timeout"] = timeout
			}
			sniffRules = append(sniffRules, rule)
		}
		if strategy := validStrategy(asString(in["domain_strategy"])); strategy != "" {
			resolveRules = append(resolveRules, obj{"inbound": []any{tag}, "action": "resolve", "strategy": strategy})
		}
		delete(in, "sniff")
		delete(in, "sniff_override_destination")
		delete(in, "sniff_timeout")
		delete(in, "domain_strategy")
		delete(in, "udp_disable_domain_unmapping")
	}
	m.prefixRules = append(sniffRules, resolveRules...)
}

// ---------- outbounds ----------

// outbounds returns the per-outbound domain strategy found (the GUI writes the same one everywhere).
func (m *migrator) outbounds() string {
	list := asList(m.root["outbounds"])
	var kept []any
	var endpoints []any
	strategy := ""
	for _, item := range list {
		out, ok := asObj(item)
		if !ok {
			kept = append(kept, item)
			continue
		}
		tag := asString(out["tag"])
		switch asString(out["type"]) {
		case "block":
			m.blockOutbounds[tag] = true
			continue
		case "dns":
			m.dnsOutbounds[tag] = true
			continue
		case "direct":
			_, hasAddress := out["override_address"]
			_, hasPort := out["override_port"]
			if hasAddress || hasPort {
				m.warn("outbound %q: override_address/override_port are no longer supported and were removed", tag)
				delete(out, "override_address")
				delete(out, "override_port")
			}
			if _, hasDetour := out["detour"]; !hasDetour {
				m.directOutbound[tag] = true
			}
		case "wireguard":
			if endpoint := m.wireguard(out); endpoint != nil {
				endpoints = append(endpoints, endpoint)
				continue
			}
		case "hysteria", "hysteria2":
			if hop := asString(out["hop_ports"]); hop != "" {
				var ports []any
				for _, part := range strings.Split(hop, ",") {
					part = strings.TrimSpace(part)
					if part == "" {
						continue
					}
					part = strings.ReplaceAll(part, "-", ":")
					if !strings.Contains(part, ":") {
						part = part + ":" + part
					}
					ports = append(ports, part)
				}
				if len(ports) > 0 {
					out["server_ports"] = ports
				}
			}
			delete(out, "hop_ports")
			if interval, ok := out["hop_interval"]; ok {
				switch value := interval.(type) {
				case json.Number:
					if n, err := value.Int64(); err == nil && n > 0 {
						out["hop_interval"] = fmt.Sprintf("%ds", n)
					} else {
						delete(out, "hop_interval")
					}
				case string:
					if value == "" {
						delete(out, "hop_interval")
					} else if _, err := strconv.Atoi(value); err == nil {
						out["hop_interval"] = value + "s"
					}
				}
				if _, hasPorts := out["server_ports"]; !hasPorts {
					delete(out, "hop_interval")
				}
			}
		}
		if value, ok := out["domain_strategy"]; ok {
			if s := validStrategy(asString(value)); s != "" {
				strategy = s
			}
			delete(out, "domain_strategy")
		}
		kept = append(kept, out)
	}
	if list != nil {
		m.root["outbounds"] = kept
	}
	if len(endpoints) > 0 {
		m.root["endpoints"] = append(asList(m.root["endpoints"]), endpoints...)
	}
	return strategy
}

func (m *migrator) wireguard(out obj) obj {
	endpoint := obj{"type": "wireguard"}
	for _, key := range []string{"tag", "mtu", "private_key", "system", "name", "workers", "detour", "bind_interface", "inet4_bind_address", "inet6_bind_address", "routing_mark", "connect_timeout", "domain_resolver"} {
		if value, ok := out[key]; ok {
			endpoint[key] = value
		}
	}
	if value, ok := out["interface_name"]; ok {
		endpoint["name"] = value
	}
	if value, ok := out["system_interface"]; ok {
		endpoint["system"] = value
	}
	if address, ok := out["local_address"]; ok {
		endpoint["address"] = asList(address)
	} else if address, ok := out["address"]; ok {
		endpoint["address"] = asList(address)
	}
	var peers []any
	if existing := asList(out["peers"]); len(existing) > 0 {
		for _, item := range existing {
			peer, ok := asObj(item)
			if !ok {
				continue
			}
			converted := obj{}
			if v, ok := peer["server"]; ok {
				converted["address"] = v
			}
			if v, ok := peer["server_port"]; ok {
				converted["port"] = v
			}
			for _, key := range []string{"public_key", "pre_shared_key", "allowed_ips", "reserved", "persistent_keepalive_interval"} {
				if v, ok := peer[key]; ok {
					converted[key] = v
				}
			}
			if _, ok := converted["allowed_ips"]; !ok {
				converted["allowed_ips"] = []any{"0.0.0.0/0", "::/0"}
			}
			peers = append(peers, converted)
		}
	} else {
		peer := obj{"allowed_ips": []any{"0.0.0.0/0", "::/0"}}
		if v, ok := out["server"]; ok {
			peer["address"] = v
		}
		if v, ok := out["server_port"]; ok {
			peer["port"] = v
		}
		if v, ok := out["peer_public_key"]; ok {
			peer["public_key"] = v
		}
		for _, key := range []string{"pre_shared_key", "reserved", "persistent_keepalive_interval"} {
			if v, ok := out[key]; ok {
				peer[key] = v
			}
		}
		peers = append(peers, peer)
	}
	endpoint["peers"] = peers
	return endpoint
}

// ---------- rule-sets (geoip / geosite) ----------

func (m *migrator) ruleSet(kind, code string) (string, bool) {
	code = strings.ToLower(strings.TrimSpace(code))
	tag := kind + "-" + code
	if _, ok := m.ruleSets[tag]; ok {
		return tag, true
	}
	if m.missingSets[tag] {
		return "", false
	}
	for _, dir := range m.opts.RuleSetDirs {
		path := filepath.Join(dir, tag+".srs")
		if info, err := os.Stat(path); err == nil && !info.IsDir() {
			absolute, err := filepath.Abs(path)
			if err != nil {
				absolute = path
			}
			m.ruleSets[tag] = absolute
			m.ruleSetOrder = append(m.ruleSetOrder, tag)
			return tag, true
		}
	}
	m.missingSets[tag] = true
	m.warn("%s:%s is not available (no %s.srs), this item was skipped", kind, code, tag)
	return "", false
}

var actionKeys = map[string]bool{
	"outbound": true, "action": true, "server": true, "type": true, "mode": true, "invert": true,
	"disable_cache": true, "rewrite_ttl": true, "client_subnet": true, "strategy": true,
	"method": true, "no_drop": true, "rcode": true, "answer": true, "ns": true, "extra": true,
	"timeout": true, "sniffer": true,
}

// geoRule replaces geoip/geosite items by rule_set references.
// It returns false if the rule lost all of its conditions and must be dropped.
func (m *migrator) geoRule(rule obj, isDNS bool) bool {
	if asString(rule["type"]) == "logical" {
		var kept []any
		for _, item := range asList(rule["rules"]) {
			sub, ok := asObj(item)
			if !ok || m.geoRule(sub, isDNS) {
				kept = append(kept, item)
			}
		}
		if len(kept) == 0 {
			return false
		}
		rule["rules"] = kept
		return true
	}
	_, hasGeosite := rule["geosite"]
	_, hasGeoip := rule["geoip"]
	_, hasSourceGeoip := rule["source_geoip"]
	if !hasGeosite && !hasGeoip && !hasSourceGeoip {
		return true
	}
	sets := stringList(rule["rule_set"])
	for _, code := range stringList(rule["geosite"]) {
		if tag, ok := m.ruleSet("geosite", code); ok {
			sets = append(sets, tag)
		}
	}
	delete(rule, "geosite")
	if isDNS {
		if hasGeoip {
			m.warn("geoip in a DNS rule is no longer supported and was removed")
		}
	} else {
		for _, code := range stringList(rule["geoip"]) {
			if strings.EqualFold(code, "private") {
				rule["ip_is_private"] = true
				continue
			}
			if tag, ok := m.ruleSet("geoip", code); ok {
				sets = append(sets, tag)
			}
		}
	}
	delete(rule, "geoip")
	if hasSourceGeoip {
		m.warn("source_geoip is no longer supported and was removed")
		delete(rule, "source_geoip")
	}
	if len(sets) > 0 {
		list := make([]any, len(sets))
		for i, s := range sets {
			list[i] = s
		}
		rule["rule_set"] = list
	}
	// drop empty lists the GUI writes next to geo items, then see if anything is left to match on
	conditions := 0
	for key, value := range rule {
		if actionKeys[key] {
			continue
		}
		if list, ok := value.([]any); ok && len(list) == 0 {
			delete(rule, key)
			continue
		}
		conditions++
	}
	return conditions > 0
}

// ---------- DNS ----------

func isIP(host string) bool {
	return net.ParseIP(strings.Trim(host, "[]")) != nil
}

func splitHostPort(hostport string) (string, any) {
	if host, port, err := net.SplitHostPort(hostport); err == nil {
		if n, err := strconv.Atoi(port); err == nil {
			return host, n
		}
		return host, nil
	}
	return strings.Trim(hostport, "[]"), nil
}

// dnsServer converts one legacy server ({"address": "..."}). It returns nil if the server is replaced by a rule action.
func (m *migrator) dnsServer(server obj, localTag string) obj {
	address := strings.TrimSpace(asString(server["address"]))
	tag := asString(server["tag"])
	converted := obj{}
	if tag != "" {
		converted["tag"] = tag
	}
	if value, ok := server["client_subnet"]; ok {
		converted["client_subnet"] = value
	}
	lower := strings.ToLower(address)
	needDial := true
	switch {
	case lower == "local" || strings.HasPrefix(lower, "underlying://") || lower == "":
		converted["type"] = "local"
		needDial = false
	case lower == "fakeip":
		converted["type"] = "fakeip"
		if fake, ok := asObj(asObj2(m.root["dns"])["fakeip"]); ok {
			for _, key := range []string{"inet4_range", "inet6_range"} {
				if value, ok := fake[key]; ok {
					converted[key] = value
				}
			}
		}
		if _, ok := converted["inet4_range"]; !ok {
			converted["inet4_range"] = "198.18.0.0/15"
		}
		return converted
	case strings.HasPrefix(lower, "rcode://"):
		m.rcodeServers[tag] = strings.TrimPrefix(lower, "rcode://")
		return nil
	case strings.HasPrefix(lower, "dhcp://"):
		converted["type"] = "dhcp"
		if iface := address[len("dhcp://"):]; iface != "" && iface != "auto" {
			converted["interface"] = iface
		}
		needDial = false
	default:
		scheme := "udp"
		rest := address
		if index := strings.Index(address, "://"); index > 0 {
			scheme = strings.ToLower(address[:index])
			rest = address[index+3:]
		}
		path := ""
		if scheme == "https" || scheme == "h3" {
			if parsed, err := url.Parse(address); err == nil {
				rest = parsed.Host
				if parsed.Path != "" && parsed.Path != "/dns-query" {
					path = parsed.Path
				}
			}
		} else if index := strings.IndexAny(rest, "/?"); index >= 0 {
			rest = rest[:index]
		}
		switch scheme {
		case "udp", "tcp", "tls", "quic", "https", "h3":
		default:
			m.warn("DNS server %q: unknown scheme %q, using UDP", tag, scheme)
			scheme = "udp"
		}
		host, port := splitHostPort(rest)
		converted["type"] = scheme
		converted["server"] = host
		if port != nil {
			converted["server_port"] = port
		}
		if path != "" {
			converted["path"] = path
		}
		if !isIP(host) {
			resolver := asString(server["address_resolver"])
			if resolver == "" || resolver == tag {
				resolver = localTag
			}
			resolverObj := obj{"server": resolver}
			if strategy := validStrategy(asString(server["address_strategy"])); strategy != "" {
				resolverObj["strategy"] = strategy
			}
			converted["domain_resolver"] = resolverObj
		}
	}
	if needDial {
		detour := asString(server["detour"])
		if detour == "" {
			// legacy servers used the default outbound when no detour was given
			detour = m.defaultOutbound()
		}
		if detour != "" && !m.directOutbound[detour] {
			converted["detour"] = detour
		}
	}
	return converted
}

func asObj2(v any) obj {
	if o, ok := v.(map[string]any); ok {
		return o
	}
	return obj{}
}

func (m *migrator) defaultOutbound() string {
	if route, ok := asObj(m.root["route"]); ok {
		if final := asString(route["final"]); final != "" {
			return final
		}
	}
	for _, item := range asList(m.root["outbounds"]) {
		if out, ok := asObj(item); ok {
			return asString(out["tag"])
		}
	}
	return ""
}

// dns returns the tag of the server that should resolve the addresses of proxy servers.
func (m *migrator) dns() string {
	dns, ok := asObj(m.root["dns"])
	if !ok {
		return ""
	}
	delete(dns, "independent_cache")

	servers := asList(dns["servers"])
	// a local server is needed to resolve the names of the other servers
	localTag := ""
	hasLegacy := false
	for _, item := range servers {
		server, ok := asObj(item)
		if !ok {
			continue
		}
		_, typed := server["type"]
		_, legacy := server["address"]
		if typed && asString(server["type"]) == "local" && localTag == "" {
			localTag = asString(server["tag"])
		}
		if legacy && !typed {
			hasLegacy = true
			address := strings.ToLower(asString(server["address"]))
			if (address == "local" || strings.HasPrefix(address, "underlying://")) && localTag == "" {
				localTag = asString(server["tag"])
			}
		}
	}
	strategies := map[string]string{}
	if hasLegacy {
		addLocal := false
		if localTag == "" {
			localTag = "dns-local-auto"
			addLocal = true
		}
		var converted []any
		usedAutoLocal := false
		for _, item := range servers {
			server, ok := asObj(item)
			if !ok {
				continue
			}
			if _, typed := server["type"]; typed {
				converted = append(converted, server)
				continue
			}
			if _, legacy := server["address"]; !legacy {
				converted = append(converted, server)
				continue
			}
			if strategy := validStrategy(asString(server["strategy"])); strategy != "" {
				strategies[asString(server["tag"])] = strategy
			}
			if result := m.dnsServer(server, localTag); result != nil {
				if resolver, ok := asObj(result["domain_resolver"]); ok && asString(resolver["server"]) == "dns-local-auto" {
					usedAutoLocal = true
				}
				converted = append(converted, result)
			}
		}
		if addLocal && usedAutoLocal {
			converted = append(converted, obj{"type": "local", "tag": "dns-local-auto"})
		} else if addLocal {
			localTag = ""
		}
		dns["servers"] = converted
		delete(dns, "fakeip")
	}

	// rules
	fakeTags := map[string]bool{}
	for _, item := range asList(dns["servers"]) {
		if server, ok := asObj(item); ok && asString(server["type"]) == "fakeip" {
			fakeTags[asString(server["tag"])] = true
		}
	}
	defaultResolver := ""
	var rules []any
	for _, item := range asList(dns["rules"]) {
		rule, ok := asObj(item)
		if !ok {
			rules = append(rules, item)
			continue
		}
		if _, legacyOutbound := rule["outbound"]; legacyOutbound && asString(rule["type"]) != "logical" {
			// {"outbound": "any", "server": X}: X resolves the proxy server addresses
			if defaultResolver == "" {
				defaultResolver = asString(rule["server"])
			}
			continue
		}
		if !m.geoRule(rule, true) {
			continue
		}
		if fakeTags[asString(rule["server"])] {
			if _, has := rule["query_type"]; !has {
				rule["query_type"] = []any{"A", "AAAA"}
			}
		}
		if rcode, blocked := m.rcodeServers[asString(rule["server"])]; blocked {
			delete(rule, "server")
			rule["action"] = "predefined"
			rule["rcode"] = rcodeName(rcode)
		}
		rules = append(rules, rule)
	}
	if _, hadRules := dns["rules"]; hadRules {
		dns["rules"] = rules
	}
	if final := asString(dns["final"]); final != "" {
		if _, blocked := m.rcodeServers[final]; blocked {
			delete(dns, "final")
		}
	}

	// the per-server strategy is gone: keep one global strategy
	if _, has := dns["strategy"]; !has && len(strategies) > 0 {
		chosen := ""
		for _, item := range asList(dns["servers"]) {
			if server, ok := asObj(item); ok {
				if s := strategies[asString(server["tag"])]; s != "" {
					chosen = s
					break
				}
			}
		}
		if chosen != "" {
			dns["strategy"] = chosen
		}
	}

	if _, blocked := m.rcodeServers[defaultResolver]; blocked {
		defaultResolver = ""
	}
	if defaultResolver == "" {
		defaultResolver = m.pickResolver(dns, localTag)
	}
	return defaultResolver
}

// pickResolver chooses a DNS server that does not itself go through a proxy.
func (m *migrator) pickResolver(dns obj, localTag string) string {
	first := ""
	for _, item := range asList(dns["servers"]) {
		server, ok := asObj(item)
		if !ok {
			continue
		}
		tag := asString(server["tag"])
		kind := asString(server["type"])
		if tag == "" || kind == "fakeip" {
			continue
		}
		if tag == "dns-direct" {
			return tag
		}
		if _, hasDetour := server["detour"]; !hasDetour && first == "" && kind != "local" {
			first = tag
		}
	}
	if first != "" {
		return first
	}
	return localTag
}

func rcodeName(code string) string {
	switch code {
	case "success":
		return "NOERROR"
	case "format_error":
		return "FORMERR"
	case "server_failure":
		return "SERVFAIL"
	case "name_error":
		return "NXDOMAIN"
	case "not_implemented":
		return "NOTIMP"
	case "refused":
		return "REFUSED"
	}
	return "NOERROR"
}

// ---------- route ----------

func (m *migrator) routeRule(rule obj) bool {
	if asString(rule["type"]) == "logical" {
		// conditions of sub-rules may hold geo items; the action is on the outer rule
		var kept []any
		for _, item := range asList(rule["rules"]) {
			sub, ok := asObj(item)
			if !ok || m.geoRule(sub, false) {
				kept = append(kept, item)
			}
		}
		if len(kept) == 0 {
			return false
		}
		rule["rules"] = kept
	} else if !m.geoRule(rule, false) {
		return false
	}
	if _, hasAction := rule["action"]; !hasAction {
		outbound := asString(rule["outbound"])
		switch {
		case m.blockOutbounds[outbound]:
			delete(rule, "outbound")
			rule["action"] = "reject"
		case m.dnsOutbounds[outbound]:
			delete(rule, "outbound")
			rule["action"] = "hijack-dns"
		}
	}
	return true
}

func (m *migrator) route(defaultResolver, resolverStrategy string) {
	route, ok := asObj(m.root["route"])
	if !ok {
		if len(m.prefixRules) == 0 && defaultResolver == "" && len(m.blockOutbounds) == 0 {
			return
		}
		route = obj{}
		m.root["route"] = route
	}
	delete(route, "geoip")
	delete(route, "geosite")

	rules := make([]any, 0, len(m.prefixRules))
	rules = append(rules, m.prefixRules...)
	for _, item := range asList(route["rules"]) {
		rule, ok := asObj(item)
		if !ok {
			rules = append(rules, item)
			continue
		}
		if m.routeRule(rule) {
			rules = append(rules, rule)
		}
	}
	if len(rules) > 0 {
		route["rules"] = rules
	}

	if final := asString(route["final"]); final != "" && (m.blockOutbounds[final] || m.dnsOutbounds[final]) {
		delete(route, "final")
		if m.blockOutbounds[final] {
			route["rules"] = append(asList(route["rules"]), obj{"action": "reject"})
		}
	}

	if len(m.ruleSetOrder) > 0 {
		sets := asList(route["rule_set"])
		for _, tag := range m.ruleSetOrder {
			sets = append(sets, obj{"tag": tag, "type": "local", "format": "binary", "path": m.ruleSets[tag]})
		}
		route["rule_set"] = sets
	}

	if _, has := route["default_domain_resolver"]; !has && defaultResolver != "" {
		resolver := obj{"server": defaultResolver}
		if resolverStrategy != "" {
			resolver["strategy"] = resolverStrategy
		}
		route["default_domain_resolver"] = resolver
	}
}
