package main

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"io"
	"math/big"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"testing"
	"time"

	"grpc_server/gen"
)

// These tests drive the core the way the GUI does: an old-format config goes into Start/Test,
// and the traffic really passes through a local Trojan (WebSocket + TLS) server.

func freePort(t *testing.T) int {
	t.Helper()
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	return listener.Addr().(*net.TCPAddr).Port
}

func writeCert(t *testing.T, dir string) (string, string) {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	template := &x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject:      pkix.Name{CommonName: "de.test.local"},
		DNSNames:     []string{"de.test.local"},
		NotBefore:    time.Now().Add(-time.Hour),
		NotAfter:     time.Now().Add(24 * time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
	}
	der, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	keyDer, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		t.Fatal(err)
	}
	certPath := filepath.Join(dir, "cert.pem")
	keyPath := filepath.Join(dir, "key.pem")
	os.WriteFile(certPath, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), 0o644)
	os.WriteFile(keyPath, pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: keyDer}), 0o600)
	return certPath, keyPath
}

// startServer runs a Trojan server (new config format) inside this process.
func startServer(t *testing.T, bindDefaultInterface bool) int {
	t.Helper()
	certPath, keyPath := writeCert(t, t.TempDir())
	port := freePort(t)
	config := map[string]any{
		"log": map[string]any{"level": "warn"},
		"inbounds": []any{map[string]any{
			"type": "trojan", "tag": "trojan-in", "listen": "127.0.0.1", "listen_port": port,
			"users":     []any{map[string]any{"password": "pw"}},
			"tls":       map[string]any{"enabled": true, "server_name": "de.test.local", "certificate_path": certPath, "key_path": keyPath},
			"transport": map[string]any{"type": "ws", "path": "/ws"},
		}},
		"outbounds": []any{map[string]any{"type": "direct", "tag": "direct"}},
		"route":     map[string]any{"auto_detect_interface": bindDefaultInterface},
	}
	content, _ := json.Marshal(config)
	created, cancel, err := createBox(content, nil, nil)
	if err != nil {
		t.Fatal("server:", err)
	}
	t.Cleanup(func() {
		cancel()
		created.Close()
	})
	return port
}

// guiConfig builds the config the way db/ConfigBuilder.cpp does (sing-box 1.9 format).
func guiConfig(serverPort int, mixedPort int, tun bool, forTest bool) string {
	proxy := map[string]any{
		"type": "trojan", "tag": "proxy", "server": "127.0.0.1", "server_port": serverPort, "password": "pw", "domain_strategy": "",
		"tls":       map[string]any{"enabled": true, "insecure": true, "server_name": "de.test.local", "utls": map[string]any{"enabled": true, "fingerprint": "chrome"}},
		"transport": map[string]any{"type": "ws", "path": "/ws", "headers": map[string]any{"Host": "de.test.local"}},
	}
	outbounds := []any{proxy,
		map[string]any{"type": "direct", "tag": "direct"},
		map[string]any{"type": "direct", "tag": "bypass"},
		map[string]any{"type": "block", "tag": "block"},
	}
	var inbounds []any
	dnsServers := []any{}
	dnsRules := []any{map[string]any{"outbound": "any", "server": "dns-direct"}}
	routeRules := []any{}
	if !forTest {
		outbounds = append(outbounds, map[string]any{"type": "dns", "tag": "dns-out"})
		inbounds = append(inbounds, map[string]any{"tag": "mixed-in", "type": "mixed", "listen": "127.0.0.1", "listen_port": mixedPort,
			"sniff": true, "sniff_override_destination": false, "domain_strategy": ""})
		if tun {
			inbounds = append(inbounds, map[string]any{"tag": "tun-in", "type": "tun", "interface_name": "neko-tun", "auto_route": true,
				"endpoint_independent_nat": true, "mtu": 9000, "stack": "mixed", "strict_route": false, "inet4_address": "172.19.0.1/28",
				"sniff": true, "sniff_override_destination": false, "domain_strategy": ""})
		}
		dnsServers = append(dnsServers, map[string]any{"tag": "dns-remote", "address_resolver": "dns-local", "strategy": "", "address": "https://dns.google/dns-query", "detour": "proxy"})
		dnsRules = append(dnsRules,
			map[string]any{"domain": []any{}, "domain_suffix": []any{"ru", "su"}, "domain_keyword": []any{}, "domain_regex": []any{}, "geosite": []any{"category-ru", "yandex"}, "server": "dns-direct"},
			map[string]any{"query_type": []any{32, 33}, "server": "dns-block"},
			map[string]any{"domain_suffix": ".lan", "server": "dns-block"})
		routeRules = append(routeRules,
			map[string]any{"protocol": "dns", "outbound": "dns-out"},
			map[string]any{"domain": []any{}, "domain_suffix": []any{"ru", "su"}, "domain_keyword": []any{}, "domain_regex": []any{}, "geosite": []any{"category-ru", "yandex"}, "outbound": "bypass"},
			map[string]any{"ip_cidr": []any{}, "geoip": []any{"ru"}, "outbound": "bypass"})
	}
	dnsServers = append(dnsServers, map[string]any{"tag": "dns-direct", "address_resolver": "dns-local", "strategy": "", "address": "https://common.dot.dns.yandex.net/dns-query", "detour": "direct"})
	if !forTest {
		dnsServers = append(dnsServers, map[string]any{"tag": "dns-block", "address": "rcode://success"})
	}
	dnsServers = append(dnsServers, map[string]any{"tag": "dns-local", "address": "local", "detour": "direct"})
	routeRules = append(routeRules,
		map[string]any{"network": "udp", "port": []any{135, 137, 138, 139, 5353}, "outbound": "block"},
		map[string]any{"ip_cidr": []any{"224.0.0.0/3", "ff00::/8"}, "outbound": "block"},
		map[string]any{"source_ip_cidr": []any{"224.0.0.0/3", "ff00::/8"}, "outbound": "block"})
	route := map[string]any{"rules": routeRules, "auto_detect_interface": tun,
		"geoip": map[string]any{"path": "geoip.db"}, "geosite": map[string]any{"path": "geosite.db"}}
	if !forTest {
		route["final"] = "proxy"
	}
	config := map[string]any{
		"log":       map[string]any{"level": "info"},
		"inbounds":  inbounds,
		"outbounds": outbounds,
		"dns":       map[string]any{"servers": dnsServers, "rules": dnsRules, "independent_cache": true},
		"route":     route,
	}
	content, _ := json.Marshal(config)
	return string(content)
}

func TestMain(m *testing.M) {
	dir, err := os.MkdirTemp("", "nekobox_core_test")
	if err == nil {
		os.Chdir(dir)
	}
	setupCore()
	code := m.Run()
	os.Exit(code)
}

func TestStartTestAndStats(t *testing.T) {
	target := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusNoContent)
	}))
	defer target.Close()

	serverPort := startServer(t, false)
	mixedPort := freePort(t)
	s := &server{}
	ctx := context.Background()

	started, _ := s.Start(ctx, &gen.LoadConfigReq{CoreConfig: guiConfig(serverPort, mixedPort, false, false), StatsOutbounds: []string{"proxy", "bypass"}})
	if started.Error != "" {
		t.Fatal("Start:", started.Error)
	}
	defer s.Stop(ctx, &gen.EmptyReq{})

	// the watchdog check: URL test through the running instance
	running, _ := s.Test(ctx, &gen.TestReq{Mode: gen.TestMode_UrlTest, Timeout: 6000, Url: target.URL})
	if running.Error != "" || running.Ms <= 0 {
		t.Fatalf("test of the running instance failed: %q %d ms", running.Error, running.Ms)
	}

	// the ping test of a server from the list: its own temporary instance
	single, _ := s.Test(ctx, &gen.TestReq{Mode: gen.TestMode_UrlTest, Timeout: 10000, Url: target.URL,
		Config: &gen.LoadConfigReq{CoreConfig: guiConfig(serverPort, 0, false, true)}})
	if single.Error != "" || single.Ms <= 0 {
		t.Fatalf("test instance failed: %q %d ms", single.Error, single.Ms)
	}

	// a dead server must give an error, not a fake result
	dead, _ := s.Test(ctx, &gen.TestReq{Mode: gen.TestMode_UrlTest, Timeout: 3000, Url: target.URL,
		Config: &gen.LoadConfigReq{CoreConfig: guiConfig(freePort(t), 0, false, true)}})
	if dead.Error == "" {
		t.Fatalf("a dead server passed the test: %d ms", dead.Ms)
	}

	// real traffic through the local proxy port, then the traffic counters
	proxyURL, _ := url.Parse(fmt.Sprintf("http://127.0.0.1:%d", mixedPort))
	client := &http.Client{Transport: &http.Transport{Proxy: http.ProxyURL(proxyURL)}, Timeout: 10 * time.Second}
	response, err := client.Get(target.URL)
	if err != nil {
		t.Fatal("request through the proxy port:", err)
	}
	io.Copy(io.Discard, response.Body)
	response.Body.Close()
	if response.StatusCode != http.StatusNoContent {
		t.Fatal("unexpected status:", response.StatusCode)
	}
	client.CloseIdleConnections()
	time.Sleep(300 * time.Millisecond)
	up, _ := s.QueryStats(ctx, &gen.QueryStatsReq{Tag: "proxy", Direct: "uplink"})
	down, _ := s.QueryStats(ctx, &gen.QueryStatsReq{Tag: "proxy", Direct: "downlink"})
	if up.Traffic <= 0 || down.Traffic <= 0 {
		t.Fatalf("traffic was not counted: up %d down %d", up.Traffic, down.Traffic)
	}

	stopped, _ := s.Stop(ctx, &gen.EmptyReq{})
	if stopped.Error != "" {
		t.Fatal("Stop:", stopped.Error)
	}
	// and it must start again (reconnect)
	again, _ := s.Start(ctx, &gen.LoadConfigReq{CoreConfig: guiConfig(serverPort, mixedPort, false, false)})
	if again.Error != "" {
		t.Fatal("second Start:", again.Error)
	}
}

// TestTun needs administrator/root rights and real internet: set P3R_TEST_TUN=1.
// All traffic of the machine goes into the TUN adapter, through the local Trojan server and out again.
func TestTun(t *testing.T) {
	if os.Getenv("P3R_TEST_TUN") != "1" {
		t.Skip("set P3R_TEST_TUN=1 to run")
	}
	serverPort := startServer(t, true)
	s := &server{}
	ctx := context.Background()
	started, _ := s.Start(ctx, &gen.LoadConfigReq{CoreConfig: guiConfig(serverPort, freePort(t), true, false), StatsOutbounds: []string{"proxy", "bypass"}})
	if started.Error != "" {
		t.Fatal("Start with TUN:", started.Error)
	}
	defer s.Stop(ctx, &gen.EmptyReq{})
	time.Sleep(3 * time.Second)

	client := &http.Client{Transport: &http.Transport{Proxy: nil}, Timeout: 15 * time.Second}
	var lastErr error
	ok := false
	for attempt := 0; attempt < 4 && !ok; attempt++ {
		response, err := client.Get("http://cp.cloudflare.com/")
		if err != nil {
			lastErr = err
			time.Sleep(2 * time.Second)
			continue
		}
		io.Copy(io.Discard, response.Body)
		response.Body.Close()
		if response.StatusCode == http.StatusNoContent {
			ok = true
		} else {
			lastErr = fmt.Errorf("status %d", response.StatusCode)
		}
	}
	if !ok {
		t.Fatal("no internet through TUN:", lastErr)
	}
	time.Sleep(300 * time.Millisecond)
	up, _ := s.QueryStats(ctx, &gen.QueryStatsReq{Tag: "proxy", Direct: "uplink"})
	if up.Traffic <= 0 {
		t.Fatal("the request did not go through the TUN adapter and the proxy")
	}
	t.Logf("TUN works: %d bytes went up through the proxy", up.Traffic)

	stopped, _ := s.Stop(ctx, &gen.EmptyReq{})
	if stopped.Error != "" {
		t.Fatal("Stop:", stopped.Error)
	}
	// the internet must work again without the TUN adapter
	time.Sleep(2 * time.Second)
	response, err := client.Get("http://cp.cloudflare.com/")
	if err != nil {
		t.Fatal("no internet after the TUN adapter was closed:", err)
	}
	response.Body.Close()
}
