# VAPlayer UI

A lightweight, cross-platform Qt front-end for `vaplayer-core`, the pruned
x86_64 Android emulator in `../core`.

The interface is a single dark-themed window with an emulator viewport and a
control sidebar (power, device, and ADB controls). All emulator control is
driven through QEMU's QMP protocol over a local socket, so the UI never links
against the emulator itself.

## Requirements

- CMake 3.16 or newer
- A C++17 compiler
- Qt 6 (Core, Gui, Widgets, Network) or Qt 5.15 as a fallback
- Qt Test (optional) for the test suite

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The build produces `build/vaplayer-ui`. Set `-DVAPLAYER_BUILD_TESTS=OFF` to
skip the tests, or `-DCMAKE_PREFIX_PATH=/path/to/qt` to select a specific Qt
installation.

## Running

The UI locates `vaplayer-core` next to the build tree, or from an explicit path
configured in Settings. To point it at a specific binary:

```sh
VAPLAYER_CORE=/path/to/vaplayer-core ./build/vaplayer-ui
```

## Layout

| Path | Purpose |
| --- | --- |
| `main.cpp` | Application entry point and global setup |
| `core_bridge/QmpClient.{h,cpp}` | Asynchronous QMP client (handshake, commands, events) |
| `process/EmulatorRunner.{h,cpp}` | `QProcess` wrapper that launches and supervises the core |
| `ui/MainWindow.{h,cpp,ui}` | Main window: viewport, sidebar, ADB status |
| `ui/SettingsDialog.{h,cpp,ui}` | CPU, memory, and device settings |
| `resources/` | Qt resource manifest and application icon |
| `tests/` | QtTest unit and integration tests |

## Architecture

`EmulatorRunner` owns the emulator process and builds its command line from an
`EmulatorConfig`. It starts the core with a QMP socket (`-qmp
unix:…,server=on,wait=off`, or a named pipe on Windows) and exposes the
endpoint to the rest of the application.

`QmpClient` connects to that endpoint, performs the QMP handshake
(`greeting` → `qmp_capabilities`), and then provides typed helpers
(`stop`, `cont`, `reset`, `systemPowerdown`, `sendKey`) plus a generic
`sendCommand`. Replies are correlated by request id, and asynchronous events
are surfaced through signals. Every call is non-blocking; nothing in the UI
waits on the emulator.

The window reacts to runner state changes, connecting the QMP client once the
guest is running and disconnecting it when the process stops. The ADB indicator
probes the forwarded port rather than shelling out to the `adb` binary.

## Testing

```sh
cd build && ctest --output-on-failure
```

Three suites run:

- `emulator_runner` — command-line construction for the configuration matrix.
- `qmp_client` — the client against an in-process QMP server speaking the real
  wire protocol (handshake, replies, events, errors, key encoding).
- `core_integration` — the runner and client against a real `vaplayer-core`
  binary. It skips cleanly when the binary is not built; set `VAPLAYER_CORE`
  to run it against a specific build.

## Notes

- The pruned core does not include the user-mode (`slirp`) network backend, so
  `EmulatorRunner` probes for it and falls back to a socket netdev. Restoring
  `libslirp` re-enables ADB port forwarding.
- Sound requires an explicit `-audiodev` backend; the UI uses the `none`
  driver so the guest device works without touching host audio.

## License

GPLv3. See `../COPYING`.
