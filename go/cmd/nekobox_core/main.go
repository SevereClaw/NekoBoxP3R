package main

import (
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"grpc_server"

	"github.com/matsuridayo/libneko/neko_common"
	"github.com/sagernet/sing-box/constant"
)

func main() {
	fmt.Println("sing-box:", constant.Version, "NekoBox:", neko_common.Version_neko)
	fmt.Println()

	// nekobox_core
	if len(os.Args) > 1 && os.Args[1] == "nekobox" {
		neko_common.RunMode = neko_common.RunMode_NekoBox_Core
		grpc_server.RunCore(setupCore, &server{})
		return
	}

	// standalone: nekobox_core [--disable-color] [-D dir] run -c config.json
	os.Exit(runStandalone(os.Args[1:]))
}

func runStandalone(args []string) int {
	configPath := ""
	command := ""
	for i := 0; i < len(args); i++ {
		switch args[i] {
		case "--disable-color":
		case "-c", "--config":
			if i+1 < len(args) {
				i++
				configPath = args[i]
			}
		case "-D", "--directory":
			if i+1 < len(args) {
				i++
				if err := os.Chdir(args[i]); err != nil {
					fmt.Fprintln(os.Stderr, "FATAL:", err)
					return 1
				}
			}
		case "run", "check", "version":
			command = args[i]
		}
	}
	switch command {
	case "version":
		return 0
	case "run", "check":
	default:
		fmt.Fprintln(os.Stderr, "usage: nekobox_core [--disable-color] [-D dir] run|check -c config.json")
		return 1
	}
	if configPath == "" {
		configPath = "config.json"
	}
	content, err := os.ReadFile(configPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "FATAL:", err)
		return 1
	}
	created, cancel, err := createBox(content, nil, nil)
	if err != nil {
		fmt.Fprintln(os.Stderr, "FATAL:", err)
		return 1
	}
	if command == "run" {
		signals := make(chan os.Signal, 1)
		signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
		<-signals
	}
	cancel()
	created.Close()
	return 0
}
