#!/bin/bash
# Qt SDK for the Windows build.
#   DL_QT_VER=5.15  -> the old prebuilt Qt 5 runtime of the upstream project
#   DL_QT_VER=6.12  -> the newest official Qt 6.12.x for MSVC (aqtinstall, download.qt.io and its mirrors)
set -e
mkdir qtsdk
cd qtsdk
if [ "$DL_QT_VER" == "5.15" ]; then
  curl -LSO https://github.com/MatsuriDayo/nekoray_qt_runtime/releases/download/20220503/Qt5.15.7-Windows-x86_64-VS2019-16.11.20-20221103.7z
  7z x *.7z
  rm *.7z
  mv Qt* Qt
  exit 0
fi

# the newest aqtinstall from its repository: the PyPI release can lag behind a new Qt (different repository layout)
python -m pip install --quiet --upgrade "git+https://github.com/miurahr/aqtinstall.git" || python -m pip install --quiet --upgrade aqtinstall
python -m aqt version
# download.qt.io often times out from CI: allow its mirrors as checksum sources too, and retry more
cat > aqt.ini <<'INI'
[requests]
connection_timeout: 10
response_timeout: 60
max_retries_on_connection_error: 8
max_retries_on_checksum_error: 8
max_retries_to_retrieve_hash: 8

[mirrors]
trusted_mirrors:
    https://download.qt.io
    https://qt.mirror.constant.com
    https://mirrors.ocf.berkeley.edu/qt
    https://ftp.fau.de/qtproject
fallbacks:
    https://qt.mirror.constant.com
    https://mirrors.ocf.berkeley.edu/qt
    https://ftp.fau.de/qtproject
    https://mirror.netcologne.de/qtproject
INI
aqt() { local cmd="$1"; shift; python -m aqt -c aqt.ini "$cmd" "$@"; }

retry() {
  local n
  for n in 1 2 3 4; do
    if "$@"; then return 0; fi
    echo "attempt $n failed, retrying in $((n * 15)) s"
    sleep $((n * 15))
  done
  return 1
}

QT_VER=""
for n in 1 2 3 4; do
  if aqt list-qt windows desktop --spec "$DL_QT_VER" --latest-version >qtver.txt 2>qterr.txt; then
    QT_VER=$(tr -d '\r' < qtver.txt | tail -1)
  fi
  [ -n "$QT_VER" ] && break
  cat qterr.txt
  sleep $((n * 15))
done
[ -n "$QT_VER" ] || { echo "cannot list Qt versions"; exit 1; }
aqt list-qt windows desktop --arch "$QT_VER" >qtarch.txt 2>&1 || true
cat qtarch.txt
QT_ARCH=$(tr -d '\r' < qtarch.txt | tr ' ' '\n' | grep -E '^win64_msvc20[0-9]+_64$' | sort | tail -1 || true)
[ -n "$QT_ARCH" ] || QT_ARCH=win64_msvc2022_64
echo "Qt $QT_VER ($QT_ARCH)"
# qtsvg (SVG icons) and qttools (LinguistTools, windeployqt) come with the base package
retry aqt install-qt windows desktop "$QT_VER" "$QT_ARCH" -m qtimageformats --outputdir aqt
QT_DIR=$(ls -d aqt/"$QT_VER"/msvc*_64 | head -1)
mv "$QT_DIR" Qt
rm -rf aqt aqt.ini qtver.txt qterr.txt qtarch.txt
ls Qt/bin | grep -E "^Qt6(Core|Svg|Widgets|Network)\.dll$"
test -f Qt/bin/windeployqt.exe
echo "$QT_VER" > Qt/QT_VERSION
