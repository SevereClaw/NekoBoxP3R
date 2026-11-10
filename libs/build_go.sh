#!/bin/bash
set -e

source libs/env_deploy.sh
[ "$GOOS" == "windows" ] && [ "$GOARCH" == "amd64" ] && DEST=$DEPLOYMENT/windows64 || true
[ "$GOOS" == "windows" ] && [ "$GOARCH" == "arm64" ] && DEST=$DEPLOYMENT/windows-arm64 || true
[ "$GOOS" == "linux" ] && [ "$GOARCH" == "amd64" ] && DEST=$DEPLOYMENT/linux64 || true
[ "$GOOS" == "linux" ] && [ "$GOARCH" == "arm64" ] && DEST=$DEPLOYMENT/linux-arm64 || true
if [ -z $DEST ]; then
  echo "Please set GOOS GOARCH"
  exit 1
fi
rm -rf $DEST
mkdir -p $DEST

export CGO_ENABLED=0

#### Go: updater ####
pushd go/cmd/updater
[ "$GOOS" == "darwin" ] || go build -o $DEST -trimpath -ldflags "-w -s"
[ "$GOOS" == "linux" ] && mv $DEST/updater $DEST/launcher || true
popd

#### Go: nekobox_core ####
source libs/get_source_env.sh
# same tags and linker flags as the official sing-box build, without the parts NekoBoxP3R does not use
CORE_TAGS="with_gvisor,with_quic,with_dhcp,with_wireguard,with_utls,with_clash_api,badlinkname,tfogo_checklinkname0"
pushd go/cmd/nekobox_core
go mod tidy -e
go build -v -o $DEST -trimpath -ldflags "-w -s -checklinkname=0 -X runtime.godebugDefault=multipathtcp=0,tlssha1=1 -X github.com/sagernet/sing-box/constant.Version=$SING_BOX_VERSION -X github.com/matsuridayo/libneko/neko_common.Version_neko=$version_standalone -X grpc_server.P3RVersion=$(tr -d ' \r\n' < $SRC_ROOT/p3r_version.txt)" -tags "$CORE_TAGS"
popd
