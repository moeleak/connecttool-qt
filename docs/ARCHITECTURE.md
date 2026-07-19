# ConnectTool architecture

The refactored code keeps the 1.5.x Steam/TCP/TUN behavior and wire formats,
but separates policy from transport and presentation.

```text
QML pages
  └─ App (typed singleton)
       ├─ SessionController
       ├─ LobbyController
       ├─ SocialController
       ├─ ChatController
       ├─ NetworkController
       └─ UpdaterController
            └─ Backend orchestration
                 ├─ Steam room/network services
                 ├─ TCP multiplex transport
                 ├─ TUN bridge and route negotiation
                 └─ UpdateController
```

## Boundaries

- `domain/` contains strongly typed identifiers. Different identifier kinds
  cannot be mixed accidentally.
- `net/*_protocol.*` and `net/wire_codec.h` are Steam-independent protocol
  boundaries. They use `std::span`, `std::expected`, concepts, constrained
  templates, and explicit byte order instead of packed-pointer casts.
- `steam/` adapts the protocol layer to Steam Networking. Ownership is explicit
  (`unique_ptr` for owned services, raw pointers only for non-owning adapters).
- `src/application_controller.*` is the narrow, typed API consumed by QML.
  QML no longer reaches into the legacy orchestration object directly.
- `src/update_controller.*` owns update checking and downloads independently of
  room/network state.
- `src/platform_environment.*` isolates Windows, Linux, and macOS privilege and
  Steam discovery workarounds from application orchestration.
- `qml/ConnectTool/` uses the pinned `Qcm.Material` module for Material Design
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
previous double-read path between `TCPServer` and `MultiplexManager`.

## Verification

Use the CMake presets from the repository root:

```sh
cmake --preset core-tests
cmake --build --preset core-tests
ctest --preset core-tests
```

With a Steamworks SDK available, `dev` also builds the application and runs the
offscreen QML smoke test. `all_qmllint` statically checks every QML binding. The
three platform CI jobs run both checks before packaging.

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
