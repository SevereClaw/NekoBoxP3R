package main

import (
	"context"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/codeclysm/extract"
)

func Updater() {
	pre_cleanup := func() {
		if runtime.GOOS == "linux" {
			os.RemoveAll("./usr")
		}
		os.RemoveAll("./nekoray_update")
	}

	// find update package
	var updatePackagePath string
	if len(os.Args) == 2 && Exist(os.Args[1]) {
		updatePackagePath = os.Args[1]
	} else if Exist("./nekoray.zip") {
		updatePackagePath = "./nekoray.zip"
	} else if Exist("./nekoray.tar.gz") {
		updatePackagePath = "./nekoray.tar.gz"
	} else {
		log.Fatalln("no update")
	}
	log.Println("updating from", updatePackagePath)

	// extract update package
	if strings.HasSuffix(updatePackagePath, ".zip") {
		pre_cleanup()
		f, err := os.Open(updatePackagePath)
		if err != nil {
			log.Fatalln(err.Error())
		}
		err = extract.Zip(context.Background(), f, "./nekoray_update", nil)
		if err != nil {
			log.Fatalln(err.Error())
		}
		f.Close()
	} else if strings.HasSuffix(updatePackagePath, ".tar.gz") {
		pre_cleanup()
		f, err := os.Open(updatePackagePath)
		if err != nil {
			log.Fatalln(err.Error())
		}
		err = extract.Gz(context.Background(), f, "./nekoray_update", nil)
		if err != nil {
			log.Fatalln(err.Error())
		}
		f.Close()
	}

	// NekoBoxP3R: wait until the program and the core have really exited (stopping the VPN adapter can take
	// several seconds), and keep the program from being started again while files are being replaced.
	waitUnlocked("./nekobox.exe", 20*time.Second)
	waitUnlocked("./nekobox_core.exe", 10*time.Second)
	cleanAside(".")
	parked := false
	if runtime.GOOS == "windows" && Exist("./nekobox.exe") {
		os.Remove("./nekobox.exe.updating")
		parked = os.Rename("./nekobox.exe", "./nekobox.exe.updating") == nil
	}

	// remove old file
	removeAll("./*.dll")
	removeAll("./*.dmp")

	// update move
	err := Mv("./nekoray_update/nekoray", "./")
	if err != nil {
		if parked && !Exist("./nekobox.exe") {
			os.Rename("./nekobox.exe.updating", "./nekobox.exe")
		}
		MessageBoxPlain("NekoGui Updater", "Update failed. Please close the running instance and run the updater again.\n\n"+err.Error())
		log.Fatalln(err.Error())
	}
	os.Remove("./nekobox.exe.updating")

	os.RemoveAll("./nekoray_update")
	os.RemoveAll("./nekoray.zip")
	os.RemoveAll("./nekoray.tar.gz")

	// nekoray -> nekobox
	os.Remove("./nekoray.exe")
	os.Remove("./nekoray.png")
	os.Remove("./nekoray_core.exe")
}

func Exist(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func FindExist(paths []string) string {
	for _, path := range paths {
		if Exist(path) {
			return path
		}
	}
	return ""
}

func Mv(src, dst string) error {
	s, err := os.Stat(src)
	if err != nil {
		return err
	}
	if s.IsDir() {
		es, err := os.ReadDir(src)
		if err != nil {
			return err
		}
		for _, e := range es {
			err = Mv(filepath.Join(src, e.Name()), filepath.Join(dst, e.Name()))
			if err != nil {
				return err
			}
		}
	} else {
		err = os.MkdirAll(filepath.Dir(dst), 0755)
		if err != nil {
			return err
		}
		err = replaceFile(src, dst)
		if err != nil {
			return err
		}
	}
	return nil
}

// replaceFile moves src over dst. A file that is still open in another process (a loaded DLL)
// cannot be overwritten on Windows, but it can be renamed: move it aside and put the new one in place.
func replaceFile(src, dst string) error {
	var err error
	for attempt := 0; attempt < 6; attempt++ {
		err = os.Rename(src, dst)
		if err == nil {
			return nil
		}
		aside := dst + asideSuffix
		os.Remove(aside)
		if os.Rename(dst, aside) == nil {
			if err = os.Rename(src, dst); err == nil {
				return nil
			}
			os.Rename(aside, dst) // put the old file back
		}
		time.Sleep(500 * time.Millisecond)
	}
	return err
}

const asideSuffix = ".old-update"

// cleanAside deletes the files moved aside by an earlier update.
func cleanAside(root string) {
	filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return nil
		}
		if info.IsDir() && (info.Name() == "config" || info.Name() == "nekoray_update") {
			return filepath.SkipDir
		}
		if !info.IsDir() && strings.HasSuffix(info.Name(), asideSuffix) {
			os.Remove(path)
		}
		return nil
	})
}

// waitUnlocked waits until a file is no longer in use (a running .exe cannot be opened for writing).
func waitUnlocked(path string, timeout time.Duration) {
	if runtime.GOOS != "windows" || !Exist(path) {
		return
	}
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		f, err := os.OpenFile(path, os.O_RDWR, 0)
		if err == nil {
			f.Close()
			return
		}
		time.Sleep(300 * time.Millisecond)
	}
}

func removeAll(glob string) {
	files, _ := filepath.Glob(glob)
	for _, f := range files {
		os.Remove(f)
	}
}
