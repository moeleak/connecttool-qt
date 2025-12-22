#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-appimage}"
APPDIR="${APPDIR:-$ROOT_DIR/packaging/appimage/AppDir}"
OUTPUT_DIR="${OUTPUT_DIR:-$ROOT_DIR/packaging/appimage}"

LINUXDEPLOY="${LINUXDEPLOY:-linuxdeploy}"
LINUXDEPLOY_PLUGIN_QT="${LINUXDEPLOY_PLUGIN_QT:-linuxdeploy-plugin-qt}"

if ! command -v "$LINUXDEPLOY" >/dev/null 2>&1; then
  echo "linuxdeploy not found in PATH (set LINUXDEPLOY or install it)." >&2
  exit 1
fi
if ! command -v "$LINUXDEPLOY_PLUGIN_QT" >/dev/null 2>&1; then
  echo "linuxdeploy-plugin-qt not found in PATH (set LINUXDEPLOY_PLUGIN_QT or install it)." >&2
  exit 1
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" --config Release

rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

export QML_SOURCES_PATHS="${QML_SOURCES_PATHS:-$ROOT_DIR/qml}"

pushd "$OUTPUT_DIR" >/dev/null
"$LINUXDEPLOY" --appdir "$APPDIR" --plugin qt --output appimage
popd >/dev/null

echo "AppImage generated under $OUTPUT_DIR"
