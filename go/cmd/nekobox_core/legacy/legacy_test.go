package legacy

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func migrateFile(t *testing.T, name string, sets ...string) (map[string]any, []string) {
	t.Helper()
	input, err := os.ReadFile(filepath.Join("testdata", name))
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	for _, set := range sets {
		if err := os.WriteFile(filepath.Join(dir, set+".srs"), []byte("x"), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	result, err := Migrate(input, Options{RuleSetDirs: []string{dir}})
	if err != nil {
		t.Fatal(err)
	}
	var root map[string]any
	if err := json.Unmarshal(result.Config, &root); err != nil {
		t.Fatal(err)
	}
	return root, result.Warnings
}

func list(v any) []any {
	l, _ := v.([]any)
	return l
}

func object(v any) map[string]any {
	o, _ := v.(map[string]any)
	return o
}

func TestGuiTunConfig(t *testing.T) {
	root, warnings := migrateFile(t, "gui_tun.json", "geosite-category-ru", "geosite-yandex", "geoip-ru")
	text, _ := json.Marshal(root)
	for _, legacyKey := range []string{`"inet4_address":`, `"sniff":`, `"sniff_override_destination":`, `"domain_strategy":`, `"geosite":`, `"geoip":`, `"address_resolver":`, `"independent_cache":`, `"endpoint_independent_nat":`, `rcode://`, `"type":"block"`, `"type":"dns"`} {
		if strings.Contains(string(text), legacyKey) {
			t.Errorf("legacy item %s is still in the config", legacyKey)
		}
	}
	// 3 of the 5 geosite codes have no rule-set file here
	if len(warnings) != 3 {
		t.Errorf("expected 3 warnings about missing rule-sets, got %v", warnings)
	}

	route := object(root["route"])
	rules := list(route["rules"])
	first := object(rules[0])
	if first["action"] != "sniff" {
		t.Errorf("the first rule must be sniff, got %v", first)
	}
	var sawHijack, sawReject, sawGeoip, sawGeosite bool
	for _, item := range rules {
		rule := object(item)
		if rule["action"] == "hijack-dns" && rule["protocol"] == "dns" {
			sawHijack = true
		}
		if rule["action"] == "reject" {
			sawReject = true
			if _, has := rule["outbound"]; has {
				t.Errorf("reject rule kept its outbound: %v", rule)
			}
		}
		for _, set := range list(rule["rule_set"]) {
			if set == "geoip-ru" {
				sawGeoip = true
			}
			if set == "geosite-yandex" {
				sawGeosite = true
				if rule["outbound"] != "bypass" {
					t.Errorf("geosite rule lost its outbound: %v", rule)
				}
			}
		}
	}
	if !sawHijack || !sawReject || !sawGeoip || !sawGeosite {
		t.Errorf("missing rules: hijack=%v reject=%v geoip=%v geosite=%v", sawHijack, sawReject, sawGeoip, sawGeosite)
	}
	if len(list(route["rule_set"])) != 3 {
		t.Errorf("expected 3 rule-sets, got %v", route["rule_set"])
	}
	if object(route["default_domain_resolver"])["server"] != "dns-direct" {
		t.Errorf("wrong default resolver: %v", route["default_domain_resolver"])
	}

	dns := object(root["dns"])
	byTag := map[string]map[string]any{}
	for _, item := range list(dns["servers"]) {
		server := object(item)
		byTag[server["tag"].(string)] = server
	}
	if _, has := byTag["dns-block"]; has {
		t.Error("the rcode server must be replaced by a rule action")
	}
	remote := byTag["dns-remote"]
	if remote["type"] != "https" || remote["server"] != "dns.google" || remote["detour"] != "proxy" {
		t.Errorf("wrong remote server: %v", remote)
	}
	direct := byTag["dns-direct"]
	if _, has := direct["detour"]; has || direct["type"] != "https" {
		t.Errorf("wrong direct server: %v", direct)
	}
	if byTag["dns-local"]["type"] != "local" {
		t.Errorf("wrong local server: %v", byTag["dns-local"])
	}
	for _, item := range list(dns["rules"]) {
		rule := object(item)
		if _, has := rule["outbound"]; has {
			t.Errorf("legacy outbound DNS rule left: %v", rule)
		}
		if _, isBlock := rule["query_type"]; isBlock && rule["action"] != "predefined" {
			t.Errorf("block DNS rule not converted: %v", rule)
		}
	}

	for _, item := range list(root["inbounds"]) {
		inbound := object(item)
		if inbound["type"] == "tun" && len(list(inbound["address"])) != 1 {
			t.Errorf("tun address not converted: %v", inbound)
		}
	}
}

func TestRuleWithOnlyMissingGeoIsDropped(t *testing.T) {
	input := `{"outbounds":[{"type":"direct","tag":"direct"}],"route":{"rules":[
		{"domain":[],"geosite":["nope"],"outbound":"direct"},
		{"domain_suffix":["ru"],"geosite":["nope"],"outbound":"direct"}]}}`
	result, err := Migrate([]byte(input), Options{})
	if err != nil {
		t.Fatal(err)
	}
	var root map[string]any
	json.Unmarshal(result.Config, &root)
	rules := list(object(root["route"])["rules"])
	if len(rules) != 1 {
		t.Fatalf("a rule without conditions would match everything; rules: %v", rules)
	}
}

func TestHysteriaHopPorts(t *testing.T) {
	root, _ := migrateFile(t, "gui_test.json")
	out := object(list(root["outbounds"])[0])
	ports := list(out["server_ports"])
	if len(ports) != 1 || ports[0] != "20000:30000" || out["hop_interval"] != "10s" {
		t.Errorf("hop ports not converted: %v", out)
	}
	if _, has := out["hop_ports"]; has {
		t.Error("hop_ports left")
	}
}

func TestNewFormatIsUntouched(t *testing.T) {
	input := `{"dns":{"servers":[{"type":"local","tag":"local"}]},"inbounds":[{"type":"mixed","tag":"in","listen":"127.0.0.1","listen_port":1}],"outbounds":[{"type":"direct","tag":"direct"}],"route":{"rules":[{"action":"sniff"},{"protocol":"dns","action":"hijack-dns"}],"default_domain_resolver":"local"}}`
	result, err := Migrate([]byte(input), Options{})
	if err != nil {
		t.Fatal(err)
	}
	var before, after any
	json.Unmarshal([]byte(input), &before)
	json.Unmarshal(result.Config, &after)
	a, _ := json.Marshal(before)
	b, _ := json.Marshal(after)
	if string(a) != string(b) {
		t.Errorf("a new-format config was changed:\n%s\n%s", a, b)
	}
}

func TestCommentsAndTrailingCommas(t *testing.T) {
	input := "{\n // comment\n \"log\": {\"level\": \"info\",}, /* x */ \"outbounds\": [{\"type\":\"direct\",\"tag\":\"a//b\"},]\n}"
	result, err := Migrate([]byte(input), Options{})
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(result.Config), `a//b`) {
		t.Errorf("string content damaged: %s", result.Config)
	}
}
