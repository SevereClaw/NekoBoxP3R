#!/bin/bash
set -e

source libs/env_deploy.sh
DEST=$DEPLOYMENT/windows64
rm -rf $DEST
mkdir -p $DEST

#### copy exe ####
cp $BUILD/nekobox.exe $DEST

#### deploy qt & DLL runtime ####
pushd $DEST
windeployqt nekobox.exe --no-compiler-runtime --no-system-d3d-compiler --no-opengl-sw --verbose 2
rm -rf translations
rm -rf libEGL.dll libGLESv2.dll Qt6Pdf.dll

# OpenSSL: the old prebuilt Qt had it; official Qt 6 uses the Windows TLS (Schannel) plugin instead
for dll in libcrypto-3-x64.dll libssl-3-x64.dll; do
  if [ -f "$SRC_ROOT/qtsdk/Qt/bin/$dll" ]; then cp "$SRC_ROOT/qtsdk/Qt/bin/$dll" .; fi
done
ls tls 2>/dev/null || true

popd

#### prepare deployment ####
cp $BUILD/*.pdb $DEPLOYMENT
