package grpc_server

import (
	"context"
	"encoding/json"
	"grpc_server/gen"
	"io"
	"net/http"
	"os"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
)

var update_download_url string

// NekoBoxP3R: set at build time via -ldflags "-X grpc_server.P3RVersion=..."
var P3RVersion = "0.0.0"

const P3RRepo = "SevereClaw/NekoBoxP3R"

// compareVersion compares "v1.2.3" style versions: -1 if a<b, 0 if equal, 1 if a>b
func compareVersion(a, b string) int {
	pa := strings.Split(strings.TrimPrefix(strings.TrimSpace(a), "v"), ".")
	pb := strings.Split(strings.TrimPrefix(strings.TrimSpace(b), "v"), ".")
	for i := 0; i < len(pa) || i < len(pb); i++ {
		var x, y int
		if i < len(pa) {
			x, _ = strconv.Atoi(strings.SplitN(pa[i], "-", 2)[0])
		}
		if i < len(pb) {
			y, _ = strconv.Atoi(strings.SplitN(pb[i], "-", 2)[0])
		}
		if x != y {
			if x < y {
				return -1
			}
			return 1
		}
	}
	return 0
}

func (s *BaseServer) Update(ctx context.Context, in *gen.UpdateReq) (*gen.UpdateResp, error) {
	ret := &gen.UpdateResp{}

	client := neko_common.CreateProxyHttpClient(neko_common.GetCurrentInstance())

	if in.Action == gen.UpdateAction_Check { // Check update (NekoBoxP3R releases)
		ctx, cancel := context.WithTimeout(ctx, time.Second*15)
		defer cancel()

		req, _ := http.NewRequestWithContext(ctx, "GET", "https://api.github.com/repos/"+P3RRepo+"/releases", nil)
		req.Header.Set("Accept", "application/vnd.github+json")
		req.Header.Set("User-Agent", "NekoBoxP3R-updater")
		resp, err := client.Do(req)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer resp.Body.Close()
		if resp.StatusCode != http.StatusOK {
			ret.Error = "GitHub: " + resp.Status
			return ret, nil
		}

		v := []struct {
			TagName string `json:"tag_name"`
			HtmlUrl string `json:"html_url"`
			Assets  []struct {
				Name               string `json:"name"`
				BrowserDownloadUrl string `json:"browser_download_url"`
			} `json:"assets"`
			Prerelease bool   `json:"prerelease"`
			Draft      bool   `json:"draft"`
			Body       string `json:"body"`
		}{}
		err = json.NewDecoder(resp.Body).Decode(&v)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}

		if runtime.GOOS != "windows" || runtime.GOARCH != "amd64" {
			ret.Error = "NekoBoxP3R updates are only built for Windows x64"
			return ret, nil
		}

		// newest suitable release
		for _, release := range v {
			if release.Draft || (release.Prerelease && !in.CheckPreRelease) {
				continue
			}
			for _, asset := range release.Assets {
				if !strings.Contains(asset.Name, "windows64") || !strings.HasSuffix(asset.Name, ".zip") {
					continue
				}
				if compareVersion(release.TagName, P3RVersion) <= 0 {
					return ret, nil // No update
				}
				update_download_url = asset.BrowserDownloadUrl
				ret.AssetsName = asset.Name
				ret.DownloadUrl = asset.BrowserDownloadUrl
				ret.ReleaseUrl = release.HtmlUrl
				ret.ReleaseNote = release.Body
				ret.IsPreRelease = release.Prerelease
				return ret, nil // update
			}
		}
	} else { // Download update
		if update_download_url == "" {
			ret.Error = "?"
			return ret, nil
		}

		req, _ := http.NewRequestWithContext(ctx, "GET", update_download_url, nil)
		resp, err := client.Do(req)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer resp.Body.Close()

		f, err := os.OpenFile("../nekoray.zip", os.O_TRUNC|os.O_CREATE|os.O_RDWR, 0644)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer f.Close()

		_, err = io.Copy(f, resp.Body)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		f.Sync()
	}

	return ret, nil
}
