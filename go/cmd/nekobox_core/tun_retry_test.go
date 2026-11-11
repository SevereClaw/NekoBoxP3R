package main

import (
	"encoding/json"
	"errors"
	"testing"
)

func TestTunAdapterBusyError(t *testing.T) {
	busy := errors.New("start service: start inbound: start inbound/tun[tun-in]: configure tun interface: (create adapter: Cannot create a file when that file already exists. | open existing adapter: Element not found.)")
	if !isTunAdapterBusy(busy) {
		t.Error("the error from the problem report was not recognised")
	}
	if isTunAdapterBusy(errors.New("decode config: unknown field")) {
		t.Error("an unrelated error was treated as a busy adapter")
	}
}

func TestRenameTun(t *testing.T) {
	config := []byte(`{"inbounds":[{"type":"mixed","tag":"m"},{"type":"tun","tag":"tun-in","interface_name":"neko-tun"}]}`)
	out, name, ok := renameTun(config, 1)
	if !ok || name != "neko-tun-2" {
		t.Fatalf("got %q %v", name, ok)
	}
	var root map[string]any
	json.Unmarshal(out, &root)
	if root["inbounds"].([]any)[1].(map[string]any)["interface_name"] != "neko-tun-2" {
		t.Errorf("not renamed: %s", out)
	}
	if _, _, ok := renameTun([]byte(`{"inbounds":[{"type":"mixed"}]}`), 0); ok {
		t.Error("renamed a config without TUN")
	}
}
