#!/usr/bin/env bash

set -euo pipefail

app="${1:?usage: verify_macos_bundle.sh <application.app>}"
contents="$app/Contents"
frameworks="$contents/Frameworks"

required=(
  "$contents/MacOS/connecttool-qt"
  "$frameworks/QtCore.framework/Versions/A/QtCore"
  "$frameworks/QtQuick.framework/Versions/A/QtQuick"
  "$frameworks/libsteam_api.dylib"
  "$contents/PlugIns/platforms/libqcocoa.dylib"
  "$contents/PlugIns/quick/liblabsplatformplugin.dylib"
  "$contents/PlugIns/imageformats/libqsvg.dylib"
  "$contents/PlugIns/tls/libqsecuretransportbackend.dylib"
  "$contents/Resources/qml/QtQuick/qmldir"
)
for path in "${required[@]}"; do
  if [[ ! -e "$path" ]]; then
    echo "Required macOS runtime file is missing: $path" >&2
    exit 1
  fi
done

forbidden=(
  "$frameworks/QtSql.framework"
  "$frameworks/QtQuickControls2IOSStyleImpl.framework"
  "$frameworks/QtQuickControls2FluentWinUI3StyleImpl.framework"
  "$contents/PlugIns/sqldrivers"
  "$contents/Resources/qml/QtQuick/Controls"
)
for path in "${forbidden[@]}"; do
  if [[ -e "$path" ]]; then
    echo "Unneeded macOS runtime content was packaged: $path" >&2
    exit 1
  fi
done

while IFS= read -r -d '' binary; do
  if ! file "$binary" | grep -q 'Mach-O'; then
    continue
  fi
  while IFS= read -r dependency; do
    case "$dependency" in
      @rpath/*.framework/*)
        framework="${dependency#@rpath/}"
        framework="${framework%%.framework/*}.framework"
        if [[ ! -d "$frameworks/$framework" ]]; then
          echo "Missing framework $framework required by $binary" >&2
          exit 1
        fi
        ;;
      @loader_path/../Frameworks/*.dylib)
        library="${dependency##*/}"
        if [[ ! -f "$frameworks/$library" ]]; then
          echo "Missing library $library required by $binary" >&2
          exit 1
        fi
        ;;
      /System/* | /usr/lib/* | @rpath/*.dylib | @loader_path/* | @executable_path/*)
        ;;
      *)
        echo "Unexpected non-system dependency $dependency in $binary" >&2
        exit 1
        ;;
    esac
  done < <(otool -L "$binary" | tail -n +2 | awk '{print $1}')
done < <(find "$contents/MacOS" "$frameworks" "$contents/PlugIns" -type f -print0)

if find "$contents/Resources/qml" -type f -name '*.qmltypes' -print -quit | grep -q .; then
  echo "QML tooling metadata was packaged in the macOS bundle" >&2
  exit 1
fi

codesign --verify --deep --strict "$app"
