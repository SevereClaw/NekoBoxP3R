package main

import (
	"context"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"reflect"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"nekobox_core/legacy"

	"github.com/matsuridayo/libneko/neko_common"
	"github.com/matsuridayo/libneko/neko_log"
	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/experimental/deprecated"
	"github.com/sagernet/sing-box/include"
	sblog "github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	tun "github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/common/bufio"
	E "github.com/sagernet/sing/common/exceptions"
	"github.com/sagernet/sing/common/json"
	M "github.com/sagernet/sing/common/metadata"
	N "github.com/sagernet/sing/common/network"
	"github.com/sagernet/sing/service"
)

var instance *box.Box
var instance_cancel context.CancelFunc
var instanceStats *statsTracker

// ---------- create ----------

// ruleSetDirs lists the folders with geosite-*.srs / geoip-*.srs (next to the core and in the working directory).
func ruleSetDirs() []string {
	var dirs []string
	if exe, err := os.Executable(); err == nil {
		dir := filepath.Dir(exe)
		dirs = append(dirs, filepath.Join(dir, "rule-set"), dir)
	}
	if wd, err := os.Getwd(); err == nil {
		dirs = append(dirs, filepath.Join(wd, "rule-set"), wd)
	}
	if extra := os.Getenv("NEKO_RULE_SET_DIR"); extra != "" {
		dirs = append([]string{extra}, dirs...)
	}
	return dirs
}

// createBox converts a (possibly old-format) configuration, creates the instance and starts it.
// logWriter may be nil: then the instance logs to stderr.
func createBox(configContent []byte, logWriter io.Writer, tracker adapter.ConnectionTracker) (*box.Box, context.CancelFunc, error) {
	migrated, err := legacy.Migrate(configContent, legacy.Options{RuleSetDirs: ruleSetDirs()})
	if err != nil {
		return nil, nil, err
	}
	for _, warning := range migrated.Warnings {
		log.Println("[config]", warning)
	}
	if neko_common.Debug {
		log.Println("Converted config:", string(migrated.Config))
	}

	ctx, cancel := context.WithCancel(context.Background())
	ctx = service.ContextWith[deprecated.Manager](ctx, deprecated.NewStderrManager(sblog.StdLogger()))
	ctx = include.Context(ctx)

	options, err := json.UnmarshalExtendedContext[option.Options](ctx, migrated.Config)
	if err != nil {
		cancel()
		return nil, nil, E.Cause(err, "decode config")
	}
	if options.Log == nil {
		options.Log = &option.LogOptions{}
	}
	options.Log.DisableColor = true

	created, err := box.New(box.Options{
		Context: ctx,
		Options: options,
	})
	if err != nil {
		cancel()
		return nil, nil, E.Cause(err, "create service")
	}
	if logWriter != nil {
		setLogWriter(created, logWriter)
	}
	if tracker != nil {
		created.Router().AppendTracker(tracker)
	}
	err = created.Start()
	if err != nil {
		created.Close()
		cancel()
		return nil, nil, E.Cause(err, "start service")
	}
	return created, cancel, nil
}

// setLogWriter redirects the instance log into the GUI log (the log factory has no public option
// for a plain writer that does not also enable the cache file).
func setLogWriter(created *box.Box, writer io.Writer) {
	defer func() {
		if r := recover(); r != nil {
			log.Println("[core] cannot redirect the log:", r)
		}
	}()
	factory := reflect.ValueOf(created.LogFactory())
	if factory.Kind() != reflect.Ptr {
		return
	}
	field := reflect.Indirect(factory).FieldByName("writer")
	if !field.IsValid() {
		return
	}
	field = reflect.NewAt(field.Type(), unsafe.Pointer(field.UnsafeAddr())).Elem()
	field.Set(reflect.ValueOf(&writer).Elem())
}

// ---------- traffic statistics ----------

type statsTracker struct {
	outbounds map[string]bool
	access    sync.Mutex
	counters  map[string]*atomic.Int64
}

func newStatsTracker(outbounds []string) *statsTracker {
	tracker := &statsTracker{
		outbounds: make(map[string]bool),
		counters:  make(map[string]*atomic.Int64),
	}
	for _, tag := range outbounds {
		tracker.outbounds[tag] = true
	}
	return tracker
}

func (s *statsTracker) counter(tag string, direct string) *atomic.Int64 {
	name := tag + ">>>" + direct
	s.access.Lock()
	defer s.access.Unlock()
	counter, loaded := s.counters[name]
	if !loaded {
		counter = &atomic.Int64{}
		s.counters[name] = counter
	}
	return counter
}

// Query returns the bytes counted since the previous call.
func (s *statsTracker) Query(tag string, direct string) int64 {
	if s == nil {
		return 0
	}
	return s.counter(tag, direct).Swap(0)
}

func (s *statsTracker) RoutedConnection(ctx context.Context, conn net.Conn, metadata adapter.InboundContext, matchedRule adapter.Rule, matchOutbound adapter.Outbound) net.Conn {
	tag := matchOutbound.Tag()
	if !s.outbounds[tag] {
		return conn
	}
	return bufio.NewInt64CounterConn(conn, []*atomic.Int64{s.counter(tag, "uplink")}, []*atomic.Int64{s.counter(tag, "downlink")})
}

func (s *statsTracker) RoutedPacketConnection(ctx context.Context, conn N.PacketConn, metadata adapter.InboundContext, matchedRule adapter.Rule, matchOutbound adapter.Outbound) N.PacketConn {
	tag := matchOutbound.Tag()
	if !s.outbounds[tag] {
		return conn
	}
	return bufio.NewInt64CounterPacketConn(conn, []*atomic.Int64{s.counter(tag, "uplink")}, nil, []*atomic.Int64{s.counter(tag, "downlink")}, nil)
}

func (s *statsTracker) RoutedFlow(ctx context.Context, metadata adapter.InboundContext, matchedRule adapter.Rule, matchOutbound adapter.Outbound) tun.FlowTracker {
	tag := matchOutbound.Tag()
	if !s.outbounds[tag] {
		return nil
	}
	return &statsFlow{uplink: s.counter(tag, "uplink"), downlink: s.counter(tag, "downlink")}
}

type statsFlow struct {
	uplink   *atomic.Int64
	downlink *atomic.Int64
}

func (t *statsFlow) AttachFlow(handle tun.FlowHandle) {}
func (t *statsFlow) CountForward(n int)               { t.uplink.Add(int64(n)) }
func (t *statsFlow) CountReverse(n int)               { t.downlink.Add(int64(n)) }
func (t *statsFlow) FlowEstablished()                 {}

// ---------- dialing through an instance ----------

// proxyOutbound is the outbound the tests go through: the profile itself ("proxy"), else the default one.
func proxyOutbound(i *box.Box) adapter.Outbound {
	if outbound, loaded := i.Outbound().Outbound("proxy"); loaded {
		return outbound
	}
	return i.Outbound().Default()
}

func dialContext(ctx context.Context, i *box.Box, network, addr string) (net.Conn, error) {
	return proxyOutbound(i).DialContext(ctx, network, M.ParseSocksaddr(addr))
}

func dialUDP(ctx context.Context, i *box.Box) (net.PacketConn, error) {
	return proxyOutbound(i).ListenPacket(ctx, M.ParseSocksaddr("8.8.8.8:53"))
}

func createProxyHttpClient(i *box.Box) *http.Client {
	transport := &http.Transport{
		TLSHandshakeTimeout:   time.Second * 3,
		ResponseHeaderTimeout: time.Second * 3,
	}
	if i != nil {
		transport.DialContext = func(ctx context.Context, network, addr string) (net.Conn, error) {
			return dialContext(ctx, i, network, addr)
		}
	}
	return &http.Client{Transport: transport}
}

func setupCore() {
	neko_log.SetupLog(50*1024, "./neko.log")
	//
	neko_common.GetCurrentInstance = func() interface{} {
		return instance
	}
	neko_common.DialContext = func(ctx context.Context, specifiedInstance interface{}, network, addr string) (net.Conn, error) {
		if i, ok := specifiedInstance.(*box.Box); ok && i != nil {
			return dialContext(ctx, i, network, addr)
		}
		if instance != nil {
			return dialContext(ctx, instance, network, addr)
		}
		return neko_common.DialContextSystem(ctx, network, addr)
	}
	neko_common.DialUDP = func(ctx context.Context, specifiedInstance interface{}) (net.PacketConn, error) {
		if i, ok := specifiedInstance.(*box.Box); ok && i != nil {
			return dialUDP(ctx, i)
		}
		if instance != nil {
			return dialUDP(ctx, instance)
		}
		return neko_common.DialUDPSystem(ctx)
	}
	neko_common.CreateProxyHttpClient = func(specifiedInstance interface{}) *http.Client {
		if i, ok := specifiedInstance.(*box.Box); ok && i != nil {
			return createProxyHttpClient(i)
		}
		return createProxyHttpClient(instance)
	}
}
