package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
	"testing"
	"time"

	"nekobox_core/legacy"

	"grpc_server/gen"

	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/experimental/deprecated"
	"github.com/sagernet/sing-box/include"
	sblog "github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	sjson "github.com/sagernet/sing/common/json"
	"github.com/sagernet/sing/service"
)

// These tests use the configs written by the real GUI code (tests/config_builder) for many settings.
// P3R_GUI_CONFIGS is the folder with them; the CI job generates it before running the tests.

func guiConfigDir(t *testing.T) string {
	dir := os.Getenv("P3R_GUI_CONFIGS")
	if dir == "" {
		t.Skip("set P3R_GUI_CONFIGS to the output folder of tests/config_builder")
	}
	return dir
}

func guiConfigFiles(t *testing.T) []string {
	files, _ := filepath.Glob(filepath.Join(guiConfigDir(t), "*.json"))
	sort.Strings(files)
	if len(files) < 20 {
		t.Fatalf("expected the generated configs, found %d files", len(files))
	}
	return files
}

// Every generated config is already in the current format (the converter has nothing to change)
// and the core accepts it.
func TestGuiConfigsAreCurrentFormat(t *testing.T) {
	for _, file := range guiConfigFiles(t) {
		name := filepath.Base(file)
		t.Run(name, func(t *testing.T) {
			content, err := os.ReadFile(file)
			if err != nil {
				t.Fatal(err)
			}
			migrated, err := legacy.Migrate(content, legacy.Options{RuleSetDirs: ruleSetDirs()})
			if err != nil {
				t.Fatal(err)
			}
			var before, after any
			json.Unmarshal(stripComments(content), &before)
			json.Unmarshal(migrated.Config, &after)
			if !reflect.DeepEqual(before, after) {
				a, _ := json.Marshal(before)
				b, _ := json.Marshal(after)
				t.Errorf("the GUI still writes old fields, the converter changed the config:\nGUI:  %s\nCORE: %s", a, b)
			}
			if len(migrated.Warnings) > 0 {
				t.Errorf("converter warnings: %v", migrated.Warnings)
			}

			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			ctx = service.ContextWith[deprecated.Manager](ctx, deprecated.NewStderrManager(sblog.StdLogger()))
			ctx = include.Context(ctx)
			options, err := sjson.UnmarshalExtendedContext[option.Options](ctx, stripComments(content))
			if err != nil {
				t.Fatal("decode:", err)
			}
			instance, err := box.New(box.Options{Context: ctx, Options: options})
			if err != nil {
				t.Fatal("create:", err)
			}
			instance.Close()
		})
	}
}

func stripComments(content []byte) []byte {
	var lines []string
	for _, line := range strings.Split(string(content), "\n") {
		if strings.HasPrefix(strings.TrimSpace(line), "//") {
			continue
		}
		lines = append(lines, line)
	}
	return []byte(strings.Join(lines, "\n"))
}

// pointToLocalServer makes a generated config use the local test Trojan server and free ports.
func pointToLocalServer(t *testing.T, file string, serverPort, mixedPort int) string {
	t.Helper()
	content, err := os.ReadFile(file)
	if err != nil {
		t.Fatal(err)
	}
	var root map[string]any
	if err := json.Unmarshal(content, &root); err != nil {
		t.Fatal(err)
	}
	found := false
	for _, item := range root["outbounds"].([]any) {
		outbound := item.(map[string]any)
		if outbound["tag"] != "proxy" {
			continue
		}
		found = true
		outbound["server"] = "127.0.0.1"
		outbound["server_port"] = serverPort
		tls := outbound["tls"].(map[string]any)
		tls["insecure"] = true
		tls["server_name"] = "de.test.local"
		if transport, ok := outbound["transport"].(map[string]any); ok {
			transport["path"] = "/ws"
			transport["headers"] = map[string]any{"Host": "de.test.local"}
		}
	}
	if !found {
		t.Fatal("no proxy outbound in", file)
	}
	for _, item := range root["inbounds"].([]any) {
		inbound := item.(map[string]any)
		if inbound["type"] == "mixed" {
			inbound["listen_port"] = mixedPort
		}
	}
	delete(root, "experimental")
	out, _ := json.Marshal(root)
	return string(out)
}

// Real traffic through a config written by the GUI ("browser and apps" mode, Trojan over WebSocket).
func TestGuiConfigTraffic(t *testing.T) {
	file := filepath.Join(guiConfigDir(t), "system_proxy_only.json")
	target := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(20 * time.Millisecond)
		w.WriteHeader(http.StatusNoContent)
	}))
	defer target.Close()
	serverPort := startServer(t, false)
	mixedPort := freePort(t)
	s := &server{}
	ctx := context.Background()
	started, _ := s.Start(ctx, &gen.LoadConfigReq{CoreConfig: pointToLocalServer(t, file, serverPort, mixedPort), StatsOutbounds: []string{"proxy", "bypass"}})
	if started.Error != "" {
		t.Fatal("Start:", started.Error)
	}
	defer s.Stop(ctx, &gen.EmptyReq{})

	proxyURL, _ := url.Parse(fmt.Sprintf("http://127.0.0.1:%d", mixedPort))
	client := &http.Client{Transport: &http.Transport{Proxy: http.ProxyURL(proxyURL)}, Timeout: 10 * time.Second}
	response, err := client.Get(target.URL)
	if err != nil {
		t.Fatal("request through the GUI config:", err)
	}
	io.Copy(io.Discard, response.Body)
	response.Body.Close()
	client.CloseIdleConnections()
	time.Sleep(300 * time.Millisecond)
	up, _ := s.QueryStats(ctx, &gen.QueryStatsReq{Tag: "proxy", Direct: "uplink"})
	if up.Traffic <= 0 {
		t.Fatal("the request did not go through the proxy outbound")
	}
}

// The default "VPN for all apps" config of the GUI with a real TUN adapter (root / administrator, internet).
func TestGuiConfigTun(t *testing.T) {
	if os.Getenv("P3R_TEST_TUN") != "1" {
		t.Skip("set P3R_TEST_TUN=1 to run")
	}
	file := filepath.Join(guiConfigDir(t), "vpn_default.json")
	serverPort := startServer(t, true)
	s := &server{}
	ctx := context.Background()
	started, _ := s.Start(ctx, &gen.LoadConfigReq{CoreConfig: pointToLocalServer(t, file, serverPort, freePort(t)), StatsOutbounds: []string{"proxy", "bypass"}})
	if started.Error != "" {
		t.Fatal("Start with TUN:", started.Error)
	}
	defer s.Stop(ctx, &gen.EmptyReq{})
	time.Sleep(3 * time.Second)

	// a name, so DNS goes through the TUN adapter, the hijack rule and the remote DNS over the proxy
	client := &http.Client{Transport: &http.Transport{Proxy: nil, DisableKeepAlives: true}, Timeout: 15 * time.Second}
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
		ok = response.StatusCode == http.StatusNoContent
		if !ok {
			lastErr = fmt.Errorf("status %d", response.StatusCode)
		}
	}
	if !ok {
		t.Fatal("no internet through the GUI TUN config:", lastErr)
	}
	time.Sleep(300 * time.Millisecond)
	up, _ := s.QueryStats(ctx, &gen.QueryStatsReq{Tag: "proxy", Direct: "uplink"})
	if up.Traffic <= 0 {
		t.Fatal("the request did not go through the proxy")
	}
}
