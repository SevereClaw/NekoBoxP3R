package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/sagernet/sing-box/common/srs"
	C "github.com/sagernet/sing-box/constant"
	"github.com/sagernet/sing-box/option"
)

func writeRuleSet(t *testing.T, path string) {
	t.Helper()
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	rule := option.HeadlessRule{Type: C.RuleTypeDefault, DefaultOptions: option.DefaultHeadlessRule{DomainSuffix: []string{"ru"}}}
	if err := srs.Write(f, option.PlainRuleSet{Rules: []option.HeadlessRule{rule}}, C.RuleSetVersion1); err != nil {
		t.Fatal(err)
	}
}

func ruleSetConfig(path string) []byte {
	out, _ := json.Marshal(map[string]any{"route": map[string]any{"rule_set": []any{
		map[string]any{"tag": "geosite-test", "type": "local", "format": "binary", "path": path},
	}}})
	return out
}

func ruleSetPathOf(t *testing.T, config []byte) string {
	t.Helper()
	var root map[string]any
	json.Unmarshal(config, &root)
	return root["route"].(map[string]any)["rule_set"].([]any)[0].(map[string]any)["path"].(string)
}

func TestDamagedDownloadedRuleSetFallsBack(t *testing.T) {
	bundled := t.TempDir()
	downloaded := t.TempDir()
	writeRuleSet(t, filepath.Join(bundled, "geosite-test.srs"))
	bad := filepath.Join(downloaded, "geosite-test.srs")
	os.WriteFile(bad, []byte("SRS\x01not zlib at all"), 0o644)

	out := useReadableRuleSets(ruleSetConfig(bad), []string{bundled})
	if got := ruleSetPathOf(t, out); got != filepath.Join(bundled, "geosite-test.srs") {
		t.Fatalf("a damaged list was not replaced: %s", got)
	}
}

func TestGoodRuleSetIsKept(t *testing.T) {
	downloaded := t.TempDir()
	good := filepath.Join(downloaded, "geosite-test.srs")
	writeRuleSet(t, good)
	bundled := t.TempDir()
	writeRuleSet(t, filepath.Join(bundled, "geosite-test.srs"))
	in := ruleSetConfig(good)
	if out := useReadableRuleSets(in, []string{bundled}); string(out) != string(in) {
		t.Fatalf("a good list was changed: %s", out)
	}
}
