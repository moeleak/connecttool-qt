# Release package layout

The release jobs treat deployment output as an explicit runtime dependency
closure. Deployment tools may discover the closure, but their optional tooling,
translations, drivers, and alternate control styles are removed before an
artifact is accepted.

## Audited baseline

The `refactor` run `29691613070` was used as the pre-cleanup baseline.

| Platform | Uploaded artifact | Expanded payload | Regular files |
| --- | ---: | ---: | ---: |
| Windows x86_64 | 89,076,147 bytes | 153 MiB | 107 |
| macOS x86_64 | 95,802,478 bytes | 203 MiB app bundle | 1,840 |

The largest avoidable items were a second embedded CJK font, the Visual C++
installer, QML debugger plugins, every Qt translation, SQL drivers, and all Qt
Quick Controls styles for platforms the application does not use.

## Windows ZIP

Windows resolves an executable's directly imported DLLs before application code
can change search paths. Those DLLs must remain beside `connecttool-qt.exe`.

### Root files

| File | Reason |
| --- | --- |
| `connecttool-qt.exe` | Application, static ConnectTool QML, QmlMaterial, shaders, icons, sound, and the regular CJK font |
| `steam_api64.dll`, `steamwebrtc64.dll`, `steam_appid.txt` | Steam API and P2P/ICE runtime for public test AppID 480 |
| `wintun.dll` | Windows TUN mode |
| `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Network.dll` | Qt foundation, rendering, and update networking |
| `Qt6Qml.dll`, `Qt6QmlMeta.dll`, `Qt6QmlModels.dll`, `Qt6QmlWorkerScript.dll` | QML engine dependency closure |
| `Qt6Quick.dll`, `Qt6QuickControls2.dll`, `Qt6QuickLayouts.dll`, `Qt6QuickTemplates2.dll` | Qt Quick and QmlMaterial controls |
| `Qt6OpenGL.dll`, `D3Dcompiler_47.dll`, `opengl32sw.dll` | D3D shader compiler plus OpenGL/software compatibility fallback |
| `Qt6LabsPlatform.dll`, `Qt6Widgets.dll` | Native update save dialog |
| `Qt6Svg.dll` | About-page SVG artwork |
| `MSVCP140*.dll`, `VCRUNTIME140*.dll`, `CONCRT140.dll` | Application-local MSVC runtime; makes the ZIP portable without running an installer |
| `qt.conf` | Relocatable plugin and QML paths |
| `README.txt` | Launch, layout, log, and Steam AppID notes |

`runtime/plugins` contains only the Windows platform plugin, SVG image/icon
plugins, the native widget style, and the Schannel TLS backend. `runtime/qml`
contains only modules found by `qmlimportscanner`; `*.qmltypes` tooling metadata
is removed. `licenses` contains the four redistributed license/notice files.

The package rejects QML debugger plugins, generic touch input, network discovery,
OpenSSL/certificate-only TLS backends, GIF/ICO/JPEG plugins, translations, a
Visual C++ installer, or a Unix-style `share` tree.

## macOS DMG

The disk image root intentionally contains only `connecttool-qt.app` and the
`Applications` symlink. The app bundle follows the standard `Contents` layout.

The framework allowlist is:

`QtCore`, `QtDBus`, `QtGui`, `QtLabsPlatform`, `QtNetwork`, `QtOpenGL`, `QtQml`,
`QtQmlMeta`, `QtQmlModels`, `QtQmlWorkerScript`, `QtQuick`, `QtQuickControls2`,
`QtQuickEffects`, `QtQuickLayouts`, `QtQuickShapes`, `QtQuickTemplates2`,
`QtSvg`, and `QtWidgets`.

The plugin allowlist is:

- platform: `libqcocoa.dylib`
- native widget style: `libqmacstyle.dylib`
- image/icon: `libqsvg.dylib`, `libqsvgicon.dylib`
- TLS: `libqsecuretransportbackend.dylib`
- QML: the QtQml, QtQuick, Layouts, Templates, Shapes, Effects, Window,
  WorkerScript, and `Qt.labs.platform` plugins

SQL, LocalStorage, XML list models, Particles, Dialogs, VectorImage, design
helpers, Quick tooling, and every Qt Quick Controls platform/style module are
excluded. A dependency audit checks every remaining Mach-O file, the pruned app
is rendered once, and the complete bundle is re-signed before the DMG is made.

## Runtime diagnostics

Windows portable builds create `logs/connecttool.log` beside the executable.
When that directory is not writable, logging falls back to Qt's per-user local
application-data directory. Steam initialization starts only after the first Qt
Quick frame, so Steam client or relay initialization cannot prevent the main
window from being exposed. On Windows, the executable supervises its own first
frame: it keeps the platform-default renderer when startup succeeds, and
automatically relaunches the same executable with Qt's software scene graph if
the renderer exits or times out before that frame. No secondary launcher is
packaged.
