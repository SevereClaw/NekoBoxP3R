#!/bin/bash
# Qt SDK for the Windows build.
#   DL_QT_VER=5.15  -> the old prebuilt Qt 5 runtime of the upstream project
#   DL_QT_VER=6.12  -> the newest official Qt 6.12.x for MSVC (aqtinstall, download.qt.io)
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

python -m pip install --quiet --upgrade aqtinstall
QT_VER=$(python -m aqt list-qt windows desktop --spec "$DL_QT_VER" --latest-version)
QT_ARCH=$(python -m aqt list-qt windows desktop --arch "$QT_VER" | tr ' ' '\n' | grep -E '^win64_msvc20[0-9]+_64$' | sort | tail -1)
echo "Qt $QT_VER ($QT_ARCH)"
# qtsvg: SVG icons; qtimageformats: extra image plugins; qttools (LinguistTools, windeployqt) comes with the base
python -m aqt install-qt windows desktop "$QT_VER" "$QT_ARCH" -m qtimageformats --outputdir aqt
QT_DIR=$(ls -d aqt/"$QT_VER"/msvc*_64 | head -1)
mv "$QT_DIR" Qt
rm -rf aqt
ls Qt/bin | grep -E "^Qt6(Core|Svg|Widgets|Network)\.dll$"
test -x Qt/bin/windeployqt.exe || test -f Qt/bin/windeployqt.exe
echo "$QT_VER" > Qt/QT_VERSION
