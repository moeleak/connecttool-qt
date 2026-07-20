# ConnectTool architecture

The refactored code keeps the 1.5.x Steam/TCP/TUN behavior and wire formats,
but separates policy from transport and presentation.

```text
modules/ConnectTool (QML module + C++ backing plugin)
  └─ App (typed singleton)
       ├─ SessionController / LobbyController / SocialController
       ├─ ChatController / NetworkController / UpdaterController
       └─ ApplicationRuntime
            ├─ integrations/steam (SDK adapters)
            ├─ network (protocol, TCP multiplexing, VPN negotiation)
            ├─ platform (audio, TUN, OS services)
            └─ services/UpdateController
```

## Repository layout

```text
modules/ConnectTool/       versioned QML URI, backing types, pages, assets
src/application/           process orchestration and application services
src/domain/                SDK- and framework-independent value types
src/network/               protocol, transport, and VPN core
src/integrations/steam/    the only Steamworks adapter layer
src/platform/              TUN, audio, and operating-system implementations
deps/cmake/                dependency discovery and pinned dependency fixes
deps/vendor/               vendored source dependencies
tests/                     protocol, compatibility, and boundary tests
```

Every first-party layer is a separate CMake target. `connecttool_ui` is a
static `qt_add_qml_module()` target with its own `ConnectToolPlugin`, matching
the way `Qcm.Material` is consumed. The executable contains startup and
diagnostics only; it imports and links the module instead of compiling QML and
controllers directly.

## Boundaries

- `src/domain/` contains strongly typed identifiers. Different identifier kinds
  cannot be mixed accidentally.
- `src/network/protocol/` and `src/network/vpn/` are Steam-independent core
  boundaries. They use `std::span`, `std::expected`, concepts, constrained
  templates, and explicit byte order instead of packed-pointer casts.
- `src/network/tcp/ReliableChannel` is the transport port. `MultiplexSession`
  depends on that interface, while `SteamReliableChannel` lives in
  `src/integrations/steam/`; no Steamworks type crosses into the core target.
- `src/integrations/steam/` adapts the core to Steam Networking. Ownership is
  explicit (`unique_ptr` for owned services, raw pointers only for non-owning
  SDK handles).
- `modules/ConnectTool/controllers/application_controller.*` is the narrow,
  typed API consumed by QML. QML does not reach into `ApplicationRuntime`
  directly.
- `src/application/services/update_controller.*` owns update checking and
  downloads independently of room/network state.
- `src/platform/system/platform_environment.*` isolates Windows, Linux, and
  macOS privilege and Steam discovery workarounds from orchestration.
- Windows TUN configuration is isolated in `src/platform/tun/windows_network_config.*` and
  uses IP Helper APIs for addresses, routes, MTU, and interface state. Firewall
  rules use the Windows Firewall COM API. Joining a room therefore launches no
  command shell, PowerShell process, `route.exe`, or `netsh.exe`.
- `modules/ConnectTool/` uses the pinned `Qcm.Material` module for Material Design
  3 tokens, controls, icons, motion, and shaders. `Main.qml` owns only the
  application shell, theme bootstrap, navigation, dialogs, and snackbars.
- The room surface is split into `ConnectionPanel`, `ChatPanel`, and
  `PeoplePanel`; message/member/friend delegates are separate types. Page code
  binds only to the typed `App` façade and contains no transport policy.

## Compatibility and safety

The binary packet layouts remain compatible with ConnectTool 1.5.x. Golden-byte
tests lock those layouts in place. Decoders reject truncated, oversized, or
unknown frames before exposing payload spans, and payload structs are copied
with alignment-safe operations. Packet-level IP conflict resolution now emits
the defined `ForcedReleasePayload`; 1.5.x incorrectly relabeled an entire IP
packet as a forced-release message.

Long-running workers use `std::jthread` and stop tokens. TCP client lifetime is
held in a shared registry so asynchronous close callbacks never dereference a
destroyed server. Each client also owns a serialized write queue, so asynchronous
writes never overlap. The server has a single read owner; this removes the
previous double-read path between `TcpTunnelServer` and `MultiplexSession`.

## Verification

Use the CMake presets from the repository root:

```sh
cmake --preset core-tests
cmake --build --preset core-tests
ctest --preset core-tests
```

The core preset compiles `connecttool_domain`, `connecttool_protocol`, and
`connecttool_network` without locating Steamworks. The
`architecture-boundaries` test rejects Qt/Steam imports in these directories
and rejects parent-relative first-party includes.

With a Steamworks SDK available, `dev` also builds the application and runs the
offscreen QML smoke test. `connecttool_ui_qmllint` statically checks every
application QML binding without treating warnings in the pinned dependency as
first-party failures. The three platform CI jobs run both checks before packaging.

For visual regression checks, an application build can render any primary page
offscreen without changing normal startup behavior:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ./connecttool-qt --qml-page=0 --qml-screenshot=/tmp/connecttool-room.png
```

The `release` preset builds `connecttool_protocol_benchmark`. Run it on the same
machine before and after transport changes; it reports encode/decode operations
per second for a 1,200-byte payload. End-to-end throughput should still be
measured with the existing `iperf3` procedure because Steam relay selection and
peer latency dominate real sessions.
