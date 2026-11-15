package grpc_server

import (
	"archive/zip"
	"bufio"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
)

// NekoBoxP3R: an update is written to "<dst>.part", checked, and only then renamed to <dst>.
// The updater never sees a broken or foreign archive: if anything does not match, nothing changes.
//
// Checks: HTTP 200; the received size equals Content-Length and the size from the GitHub API;
// SHA-256 equals "<file>.sha256" published next to the release file (or the digest from the API);
// the zip opens, every file in it reads back with a correct CRC, and it contains nekoray/nekobox.exe.

const maxUpdateSize = 400 << 20 // a release is ~50 MB; anything this large is not ours

func p3rExpectedSha256(ctx context.Context, client *http.Client, downloadURL string) (string, error) {
	req, _ := http.NewRequestWithContext(ctx, "GET", downloadURL+".sha256", nil)
	req.Header.Set("User-Agent", "NekoBoxP3R-updater")
	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusNotFound {
		return "", nil // releases made before v0.20.8 have no checksum file
	}
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("checksum file: %s", resp.Status)
	}
	return parseSha256File(io.LimitReader(resp.Body, 4096))
}

// parseSha256File reads "<hex>" or "<hex>  <name>" (sha256sum format).
func parseSha256File(r io.Reader) (string, error) {
	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		fields := strings.Fields(scanner.Text())
		if len(fields) == 0 {
			continue
		}
		sum := strings.ToLower(strings.TrimPrefix(fields[0], "sha256:"))
		if len(sum) == 64 {
			if _, err := hex.DecodeString(sum); err == nil {
				return sum, nil
			}
		}
		return "", errors.New("the checksum file is damaged")
	}
	return "", errors.New("the checksum file is empty")
}

func p3rDownloadVerified(ctx context.Context, client *http.Client, downloadURL, dst string) error {
	// an archive left from an earlier attempt must never be installed instead of this one
	os.Remove(dst)
	expected, err := p3rExpectedSha256(ctx, client, downloadURL)
	if err != nil {
		return fmt.Errorf("could not get the checksum of the update: %w", err)
	}
	if expected == "" {
		expected = update_api_digest
	}

	req, _ := http.NewRequestWithContext(ctx, "GET", downloadURL, nil)
	req.Header.Set("User-Agent", "NekoBoxP3R-updater")
	resp, err := client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("download: GitHub answered %s", resp.Status)
	}
	if resp.ContentLength > maxUpdateSize {
		return fmt.Errorf("download: the file is too large (%d bytes)", resp.ContentLength)
	}

	part := dst + ".part"
	os.Remove(part)
	f, err := os.OpenFile(part, os.O_TRUNC|os.O_CREATE|os.O_RDWR, 0o644)
	if err != nil {
		return err
	}
	keep := false
	defer func() {
		f.Close()
		if !keep {
			os.Remove(part)
		}
	}()

	hash := sha256.New()
	written, err := io.Copy(io.MultiWriter(f, hash), io.LimitReader(resp.Body, maxUpdateSize+1))
	if err != nil {
		return fmt.Errorf("download was interrupted: %w", err)
	}
	if err := verifyDownload(written, resp.ContentLength, update_api_size, hex.EncodeToString(hash.Sum(nil)), expected); err != nil {
		return err
	}
	if err := f.Sync(); err != nil {
		return err
	}
	if err := verifyUpdateZip(part); err != nil {
		return err
	}
	f.Close()

	if err := os.Rename(part, dst); err != nil {
		return err
	}
	keep = true
	return nil
}

func verifyDownload(written, contentLength, apiSize int64, got, expected string) error {
	if written > maxUpdateSize {
		return errors.New("download: the file is too large")
	}
	if contentLength >= 0 && written != contentLength {
		return fmt.Errorf("download was cut off: got %d of %d bytes", written, contentLength)
	}
	if apiSize > 0 && written != apiSize {
		return fmt.Errorf("download has the wrong size: %d bytes instead of %d", written, apiSize)
	}
	if expected != "" && !strings.EqualFold(got, expected) {
		return fmt.Errorf("the update file is damaged or was replaced: SHA-256 %s, expected %s", got, expected)
	}
	return nil
}

// verifyUpdateZip reads every file of the archive (zip checks each CRC-32) and looks for the program.
func verifyUpdateZip(path string) error {
	archive, err := zip.OpenReader(path)
	if err != nil {
		return fmt.Errorf("the update is not a valid zip: %w", err)
	}
	defer archive.Close()
	hasProgram, hasCore := false, false
	for _, file := range archive.File {
		name := strings.ReplaceAll(file.Name, "\\", "/")
		if strings.Contains(name, "..") || strings.HasPrefix(name, "/") {
			return fmt.Errorf("the update contains an unsafe path: %s", file.Name)
		}
		switch name {
		case "nekoray/nekobox.exe":
			hasProgram = true
		case "nekoray/nekobox_core.exe":
			hasCore = true
		}
		if file.FileInfo().IsDir() {
			continue
		}
		r, err := file.Open()
		if err != nil {
			return fmt.Errorf("the update archive is damaged (%s): %w", file.Name, err)
		}
		_, err = io.Copy(io.Discard, r)
		r.Close()
		if err != nil {
			return fmt.Errorf("the update archive is damaged (%s): %w", file.Name, err)
		}
	}
	if !hasProgram || !hasCore {
		return errors.New("the update archive does not contain the program")
	}
	return nil
}
