#!/usr/bin/env bash

set -euo pipefail

app="${1:?usage: prune_macos_bundle.sh <application.app>}"
contents="$app/Contents"
frameworks="$contents/Frameworks"
plugins="$contents/PlugIns"
qml="$contents/Resources/qml"

if [[ ! -d "$frameworks" || ! -d "$plugins" || ! -d "$qml" ]]; then
  echo "Invalid deployed application bundle: $app" >&2
  exit 1
fi

# macdeployqt deploys every available Qt Quick Controls style and several
# optional QML modules. Keep the exact framework closure used by ConnectTool,
# QmlMaterial, the native save dialog, and SVG rendering.
shopt -s nullglob
for framework in "$frameworks"/*.framework; do
  name="$(basename "$framework" .framework)"
  case "$name" in
    QtCore | QtDBus | QtGui | QtLabsPlatform | QtNetwork | QtOpenGL | \
      QtQml | QtQmlMeta | QtQmlModels | QtQmlWorkerScript | QtQuick | \
      QtQuickControls2 | QtQuickEffects | QtQuickLayouts | QtQuickShapes | \
      QtQuickTemplates2 | QtSvg | QtWidgets)
      ;;
    *)
      rm -rf "$framework"
      ;;
  esac
done

rm -rf \
  "$plugins/networkinformation" \
  "$plugins/sqldrivers"

keep_only() {
  local directory="$1"
  shift
  local file base keep candidate
  [[ -d "$directory" ]] || return 0
  for file in "$directory"/*.dylib; do
    base="$(basename "$file")"
    keep=false
    for candidate in "$@"; do
      if [[ "$base" == "$candidate" ]]; then
        keep=true
        break
      fi
    done
    if [[ "$keep" == false ]]; then
      rm -f "$file"
    fi
  done
}

keep_only "$plugins/platforms" libqcocoa.dylib
keep_only "$plugins/styles" libqmacstyle.dylib
keep_only "$plugins/imageformats" libqsvg.dylib
keep_only "$plugins/iconengines" libqsvgicon.dylib
keep_only "$plugins/tls" libqsecuretransportbackend.dylib
keep_only "$plugins/quick" \
  libeffectsplugin.dylib \
  liblabsplatformplugin.dylib \
  libmodelsplugin.dylib \
  libqmlplugin.dylib \
  libqmlshapesplugin.dylib \
  libqquicklayoutsplugin.dylib \
  libqtquick2plugin.dylib \
  libqtquicktemplates2plugin.dylib \
  libquickwindowplugin.dylib \
  libworkerscriptplugin.dylib

rm -rf \
  "$qml/QtQml/LocalStorage" \
  "$qml/QtQml/XmlListModel" \
  "$qml/QtQuick/Controls" \
  "$qml/QtQuick/Dialogs" \
  "$qml/QtQuick/LocalStorage" \
  "$qml/QtQuick/NativeStyle" \
  "$qml/QtQuick/Particles" \
  "$qml/QtQuick/Shapes/DesignHelpers" \
  "$qml/QtQuick/VectorImage" \
  "$qml/QtQuick/tooling"

# qmltypes files describe modules to IDEs and qmllint; the runtime uses qmldir
# and the plugin binaries instead.
find "$qml" -type f -name '*.qmltypes' -delete
