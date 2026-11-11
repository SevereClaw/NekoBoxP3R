package grpc_server

import (
	"context"
	"encoding/json"
	"grpc_server/gen"
	"html"
	"io"
	"net/http"
	"net/url"
	"os"
	"regexp"
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

// The unauthenticated GitHub API allows only 60 requests per hour per IP, and behind a VPN or a
// mobile carrier thousands of people share one IP ("rate limit exceeded"). So the newest version
// is taken from the redirect of github.com/<repo>/releases/latest, release notes from the
// releases.atom feed and the zip from its direct link. The API is only a fallback.

var (
	reTag     = regexp.MustCompile(`/releases/tag/([^"/?#<]+)`)
	reEntry   = regexp.MustCompile(`(?s)<entry>(.*?)</entry>`)
	reContent = regexp.MustCompile(`(?s)<content[^>]*>(.*?)</content>`)
	reLi      = regexp.MustCompile(`(?i)<li[^>]*>`)
	reBreak   = regexp.MustCompile(`(?i)<br\s*/?>|</p>|</li>|</h\d>`)
	reTagAny  = regexp.MustCompile(`<[^>]+>`)
	reBlank   = regexp.MustCompile(`\n{2,}`)
)

func htmlToText(h string) string {
	h = reLi.ReplaceAllString(h, "\n- ")
	h = reBreak.ReplaceAllString(h, "\n")
	h = reTagAny.ReplaceAllString(h, "")
	return strings.TrimSpace(reBlank.ReplaceAllString(html.UnescapeString(h), "\n"))
}

type feedRelease struct{ Tag, Notes string }

func p3rGet(ctx context.Context, client *http.Client, method, u string) (*http.Response, error) {
	req, _ := http.NewRequestWithContext(ctx, method, u, nil)
	req.Header.Set("User-Agent", "NekoBoxP3R-updater")
	return client.Do(req)
}

// Newest stable tag from the github.com redirect (no API, no rate limit).
func p3rLatestTag(ctx context.Context, client *http.Client) (string, error) {
	c := *client
	c.CheckRedirect = func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }
	resp, err := p3rGet(ctx, &c, "HEAD", "https://github.com/"+P3RRepo+"/releases/latest")
	if err != nil {
		return "", err
	}
	resp.Body.Close()
	m := reTag.FindStringSubmatch(resp.Header.Get("Location"))
	if m == nil {
		return "", errStr("GitHub: " + resp.Status)
	}
	return url.PathUnescape(m[1])
}

// Releases from the atom feed, newest first (includes pre-releases).
func p3rFeed(ctx context.Context, client *http.Client) ([]feedRelease, error) {
	resp, err := p3rGet(ctx, client, "GET", "https://github.com/"+P3RRepo+"/releases.atom")
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, errStr("GitHub: " + resp.Status)
	}
	b, err := io.ReadAll(io.LimitReader(resp.Body, 4<<20))
	if err != nil {
		return nil, err
	}
	var out []feedRelease
	for _, e := range reEntry.FindAllStringSubmatch(string(b), -1) {
		m := reTag.FindStringSubmatch(e[1])
		if m == nil {
			continue
		}
		tag, _ := url.PathUnescape(m[1])
		notes := ""
		if c := reContent.FindStringSubmatch(e[1]); c != nil {
			notes = htmlToText(html.UnescapeString(c[1]))
		}
		out = append(out, feedRelease{tag, notes})
	}
	return out, nil
}

type errStr string

func (e errStr) Error() string { return string(e) }

// Check without the API. ok=false means "could not tell" (then the API is tried).
func p3rCheckNoAPI(ctx context.Context, client *http.Client, pre bool, ret *gen.UpdateResp) (ok bool) {
	stable, err := p3rLatestTag(ctx, client)
	tag := stable
	feed, ferr := p3rFeed(ctx, client)
	if pre && ferr == nil && len(feed) > 0 {
		tag = feed[0].Tag // newest of all, pre-releases included
	} else if err != nil || tag == "" {
		return false
	}
	if compareVersion(tag, P3RVersion) <= 0 {
		return true // no update
	}
	name := "NekoBoxP3R-" + tag + "-windows64.zip"
	dl := "https://github.com/" + P3RRepo + "/releases/download/" + url.PathEscape(tag) + "/" + name
	// make sure the zip is really there (a release may still be uploading)
	if resp, err := p3rGet(ctx, client, "HEAD", dl); err != nil || resp.StatusCode != http.StatusOK {
		if resp != nil {
			resp.Body.Close()
		}
		return false
	} else {
		resp.Body.Close()
	}
	update_download_url = dl
	ret.AssetsName = name
	ret.DownloadUrl = dl
	ret.ReleaseUrl = "https://github.com/" + P3RRepo + "/releases/tag/" + url.PathEscape(tag)
	for _, f := range feed {
		if f.Tag == tag {
			ret.ReleaseNote = f.Notes
			break
		}
	}
	ret.IsPreRelease = tag != stable && (stable == "" || compareVersion(tag, stable) > 0)
	return true
}

func (s *BaseServer) Update(ctx context.Context, in *gen.UpdateReq) (*gen.UpdateResp, error) {
	ret := &gen.UpdateResp{}

	client := neko_common.CreateProxyHttpClient(neko_common.GetCurrentInstance())

	if in.Action == gen.UpdateAction_Check { // Check update (NekoBoxP3R releases)
		ctx, cancel := context.WithTimeout(ctx, time.Second*15)
		defer cancel()

		if runtime.GOOS != "windows" || runtime.GOARCH != "amd64" {
			ret.Error = "NekoBoxP3R updates are only built for Windows x64"
			return ret, nil
		}
		if p3rCheckNoAPI(ctx, client, in.CheckPreRelease, ret) {
			return ret, nil
		}

		// fallback: GitHub API
		req, _ := http.NewRequestWithContext(ctx, "GET", "https://api.github.com/repos/"+P3RRepo+"/releases", nil)
		req.Header.Set("Accept", "application/vnd.github+json")
		req.Header.Set("User-Agent", "NekoBoxP3R-updater")
		resp, err := client.Do(req)
		if err != nil {
			ret.Error = err.Error()
			return ret, nil
		}
		defer resp.Body.Close()
		if resp.StatusCode == http.StatusForbidden || resp.StatusCode == http.StatusTooManyRequests {
			ret.Error = "GitHub временно ограничил проверки с твоего адреса (часто бывает через VPN). Попробуй позже."
			return ret, nil
		}
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
