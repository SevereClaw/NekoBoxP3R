package main

import (
	"bufio"
	stdjson "encoding/json"
	"log"
	"os"
	"path/filepath"

	"github.com/sagernet/sing-box/common/srs"
)

// NekoBoxP3R: the GUI may point a rule-set at a copy it downloaded itself (config/rule-set). If that file
// cannot be read (broken download, disk error), use the copy shipped with the program instead, so a bad
// list never stops the VPN from starting.
func useReadableRuleSets(config []byte, dirs []string) []byte {
	var root map[string]any
	if err := stdjson.Unmarshal(config, &root); err != nil {
		return config
	}
	route, _ := root["route"].(map[string]any)
	sets, _ := route["rule_set"].([]any)
	changed := false
	for _, item := range sets {
		set, ok := item.(map[string]any)
		if !ok || set["type"] != "local" {
			continue
		}
		format, _ := set["format"].(string)
		path, _ := set["path"].(string)
		if path == "" || (format != "" && format != "binary") || filepath.Ext(path) != ".srs" {
			continue
		}
		if ruleSetReadable(path) {
			continue
		}
		replacement := ""
		for _, dir := range dirs {
			candidate := filepath.Join(dir, filepath.Base(path))
			if sameFile(candidate, path) {
				continue
			}
			if ruleSetReadable(candidate) {
				replacement = candidate
				break
			}
		}
		if replacement == "" {
			log.Println("[rule-set] cannot read", path, "and there is no other copy")
			continue
		}
		log.Println("[rule-set] cannot read", path, "- using", replacement)
		set["path"] = replacement
		changed = true
	}
	if !changed {
		return config
	}
	out, err := stdjson.Marshal(root)
	if err != nil {
		return config
	}
	return out
}

func ruleSetReadable(path string) bool {
	f, err := os.Open(path)
	if err != nil {
		return false
	}
	defer f.Close()
	_, err = srs.Read(bufio.NewReader(f), false)
	return err == nil
}

func sameFile(a, b string) bool {
	ia, err1 := os.Stat(a)
	ib, err2 := os.Stat(b)
	return err1 == nil && err2 == nil && os.SameFile(ia, ib)
}
