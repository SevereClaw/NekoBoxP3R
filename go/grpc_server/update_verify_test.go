package grpc_server

import (
	"archive/zip"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func makeZip(t *testing.T, files map[string]string) []byte {
	t.Helper()
	path := filepath.Join(t.TempDir(), "a.zip")
	f, _ := os.Create(path)
	w := zip.NewWriter(f)
	for name, body := range files {
		e, _ := w.CreateHeader(&zip.FileHeader{Name: name, Method: zip.Store}) // stored: test bytes stay findable
		e.Write([]byte(body))
	}
	w.Close()
	f.Close()
	data, _ := os.ReadFile(path)
	return data
}

func goodZip(t *testing.T) []byte {
	return makeZip(t, map[string]string{"nekoray/nekobox.exe": "gui", "nekoray/nekobox_core.exe": "core", "nekoray/rule-set/geoip-ru.srs": "x"})
}

func sum(b []byte) string {
	h := sha256.Sum256(b)
	return hex.EncodeToString(h[:])
}

type fakeRelease struct {
	zip       []byte
	shaFile   string // "" = 404
	status    int
	cutAt     int // send only this many bytes but announce the full length
	shaStatus int
}

func (r *fakeRelease) server() *httptest.Server {
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		if strings.HasSuffix(req.URL.Path, ".sha256") {
			if r.shaStatus != 0 {
				w.WriteHeader(r.shaStatus)
				return
			}
			if r.shaFile == "" {
				http.NotFound(w, req)
				return
			}
			w.Write([]byte(r.shaFile))
			return
		}
		if r.status != 0 && r.status != 200 {
			w.WriteHeader(r.status)
			w.Write([]byte("<html>error</html>"))
			return
		}
		body := r.zip
		if r.cutAt > 0 {
			w.Header().Set("Content-Length", "999999")
			w.WriteHeader(200)
			w.Write(body[:r.cutAt])
			if hj, ok := w.(http.Hijacker); ok {
				conn, _, _ := hj.Hijack()
				conn.Close()
			}
			return
		}
		w.Write(body)
	}))
}

func run(t *testing.T, r *fakeRelease) (string, error) {
	t.Helper()
	srv := r.server()
	defer srv.Close()
	update_api_digest, update_api_size = "", 0
	dst := filepath.Join(t.TempDir(), "nekoray.zip")
	os.WriteFile(dst, []byte("previous download"), 0o644)
	err := p3rDownloadVerified(context.Background(), srv.Client(), srv.URL+"/NekoBoxP3R-v9.9.9-windows64.zip", dst)
	if _, statErr := os.Stat(dst + ".part"); statErr == nil {
		t.Error("the .part file was left behind")
	}
	return dst, err
}

func TestGoodUpdate(t *testing.T) {
	z := goodZip(t)
	dst, err := run(t, &fakeRelease{zip: z, shaFile: sum(z) + "  NekoBoxP3R-v9.9.9-windows64.zip\n"})
	if err != nil {
		t.Fatal(err)
	}
	got, _ := os.ReadFile(dst)
	if sum(got) != sum(z) {
		t.Fatal("the saved file differs")
	}
}

func TestOldReleaseWithoutChecksumStillChecksZip(t *testing.T) {
	if _, err := run(t, &fakeRelease{zip: goodZip(t)}); err != nil {
		t.Fatal(err)
	}
	if _, err := run(t, &fakeRelease{zip: []byte("not a zip")}); err == nil {
		t.Fatal("a non-zip passed")
	}
}

func TestBadUpdatesAreRejected(t *testing.T) {
	z := goodZip(t)
	cases := map[string]*fakeRelease{
		"wrong checksum":     {zip: z, shaFile: strings.Repeat("ab", 32)},
		"error page":         {zip: z, status: 503},
		"cut off":            {zip: z, cutAt: len(z) / 2},
		"checksum not found": {zip: z, shaStatus: 500},
		"no program inside":  {zip: makeZip(t, map[string]string{"readme.txt": "x"})},
		"unsafe path":        {zip: makeZip(t, map[string]string{"nekoray/nekobox.exe": "a", "nekoray/nekobox_core.exe": "b", "../evil.exe": "c"})},
	}
	for name, release := range cases {
		dst, err := run(t, release)
		if err == nil {
			t.Errorf("%s: accepted", name)
			continue
		}
		if _, statErr := os.Stat(dst); statErr == nil {
			t.Errorf("%s: an old or broken archive was left for the updater", name)
		}
		t.Logf("%s: %v", name, err)
	}
}

func TestDamagedArchiveIsRejected(t *testing.T) {
	z := makeZip(t, map[string]string{"nekoray/nekobox.exe": "UNIQUE-PROGRAM-BYTES", "nekoray/nekobox_core.exe": "core"})
	broken := append([]byte{}, z...)
	at := bytes.Index(broken, []byte("UNIQUE-PROGRAM-BYTES"))
	if at < 0 {
		t.Fatal("test data not found")
	}
	broken[at+3] ^= 0xff // inside the file data: only the CRC can notice
	if _, err := run(t, &fakeRelease{zip: broken, shaFile: sum(broken)}); err == nil {
		t.Fatal("a damaged archive with a matching checksum passed")
	}
}

func TestApiDigestAndSize(t *testing.T) {
	if err := verifyDownload(10, 10, 11, "aa", ""); err == nil {
		t.Error("wrong API size passed")
	}
	if err := verifyDownload(10, -1, 0, "aa", "AA"); err != nil {
		t.Error("case of the digest must not matter:", err)
	}
}
