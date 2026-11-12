# Project memory (VAPlayer)

Repo: `https://github.com/M5Devs/VAPlayer.git` — x86_64 Android-emulation
emulator. `core/` is a **pruned QEMU 11.1.90 tree** that builds the
`vaplayer-core` binary. Initial commit `840a554` on `main` (4806 files, ~8.6 MB
of source; `core/build/` and `core/pyvenv/` are gitignored).

## Build
From `core/build`:
```
../configure --target-list=x86_64-softmmu --disable-docs --disable-guest-agent \
  --disable-tools --disable-sdl --disable-gtk --disable-vnc --disable-opengl \
  --disable-spice --disable-curl --disable-libssh --disable-usb-redir \
  --disable-libusb --disable-brlapi --disable-curses --disable-capstone \
  --disable-numa --disable-replication
ninja          # -> build/vaplayer-core (~76 MB)
```
`meson`/`ninja` come from `pip install --user meson ninja` → `~/.local/bin`.

## Verified after build
- `-accel help` → `tcg`, `mshv`, `kvm`. WHPX/HVF dirs are kept
  (`accel/whpx`, `accel/hvf`) and selected on Windows/macOS hosts.
- `-M help` → `pc` (i440fx), `pc-q35-*`, `microvm`.
- Required VirtIO present: `virtio-gpu-pci`, `virtio-blk-pci`,
  `virtio-net-pci`, `virtio-sound-pci`, `virtio-keyboard/mouse/tablet-pci`.
  **The sound device is `virtio-sound-pci`, not `virtio-snd`**
  (`TYPE_VIRTIO_SND == "virtio-sound-device"`).
- Legacy NICs pruned: the default `e1000e` NIC is **not** built; use
  `virtio-net`.

## Pruning traps (each cost a build cycle)
- A *dangling* meson variable after deleting a dir: `hw/intc/meson.build`
  referenced `arm_common_ss`; had to define an empty `ss.source_set()`.
- `docs/meson.build` removed but `build_docs` still read → set
  `build_docs = false` in `meson.build`.
- Keep trace dirs in sync with deleted ones: dropping `hw/scsi/` requires
  removing `'hw/scsi'` from `trace_events_subdirs` (else missing
  `trace/trace-hw_scsi.h`).
- Pruning `hw/ide`/`hw/block` meson lines breaks FDC/IDE_ISA: `hw/i386` ISAPC
  `select IDE_ISA`, PC `imply FDC_ISA`; `hw/isa` ISA_SUPERIO selects
  FDC_ISA/IDE_ISA. Restored `fdc.c`, `fdc-isa.c`, `fdc-internal.h`,
  `hw/block/dataplane/`, `ide/{cf,isa,ioport}.c`, plus `subdir('dataplane')`.
- Removing QAPI `rocker` leaves `hmp_rocker*` referenced by
  `hmp-commands-info.hx` → delete those 4 command blocks **and** the prototypes
  in `include/monitor/hmp.h`.
- `accel/tcg` references `qemu_plugin_vcpu_mem_cb`: keep `plugins/` +
  `semihosting/` and their `subdir()` calls, or the link fails.
- `accel/meson.build` has `subdir('nitro')`; drop the line if `accel/nitro` is
  removed.
- `pc-bios`: prune `blobs`/`fds` lists **and** `descriptors/meson.build` in step
  with the `.bz2`/`.json` files, or meson errors on missing files.
- `.gitmodules` listed `tests/lcitool/libvirt-ci` after `tests/` was removed →
  delete the stale entry (removed the whole file; submodules are empty).
- Prefer many small `python3 - <<'PY'` patches over large heredocs (a big
  heredoc silently kept stale content once).

## Git
- `origin` is `https://github.com/M5Devs/VAPlayer.git`, branch `main`.
- Push with the token in the URL:
  `git push "https://${GITHUB_TOKEN}@github.com/M5Devs/VAPlayer.git" main`
  (the embedded credential in the remote is stale and hangs on a prompt).
- `git config` user: `openhands` / `openhands@all-hands.dev`.
- The remote moves independently: `git push` can be rejected with "fetch
  first". Fetch, `git rebase FETCH_HEAD`, rebuild, then push.

## Qt UI (`ui/`, commit `05d7b7d`)
- Qt6 (6.8.2) is installed; Qt5.15 via `sudo apt-get install qtbase5-dev
  qtbase5-dev-tools`. Build: `cmake -S ui -B ui/build -G Ninja && cmake
  --build ui/build`. Verify the Qt5 path with
  `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON` in a separate build dir.
- `ui/` owns the `VAPlayer` CMake project; it is **not** wired into the core
  build. `CMAKE_AUTOUIC_SEARCH_PATHS` is set to `ui/` so `#include
  "ui/ui_MainWindow.h"` resolves.
- Qt5 compatibility: guard renamed signal overloads with
  `#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)` — `QProcess::errorOccurred` /
  `QProcess::finished`, and socket `errorOccurred` (Qt5 uses `error` via
  `QOverload<...>::of(...)`). Qt5 emits deprecation warnings for these; that
  is expected.
- QMP `send-key` takes `keys` as an array of **KeyValue union objects**:
  `[{"type":"qcode","data":"volumeup"}]`, not bare strings. Bare strings fail
  with "Invalid parameter type for 'keys[0]', expected: object". Valid qcodes
  for the sidebar: `volumeup`, `volumedown`, `power`, `sleep`, `wake`.
- `QmpClient::replyReceived(id, result)` emits the **inner value of the
  `return` member**, so an accepted `stop`/`cont` yields an empty object.
  Rejections arrive via `errorOccurred`, not as a reply.
- QMP socket path: `-qmp unix:<path>,server=on,wait=off` (Linux/macOS) or
  `pipe:<name>,server=on,wait=off` (Windows). The socket file appears a moment
  after the process starts — retry the connect.
- The pruned core has no `user` (slirp) netdev; `EmulatorRunner` probes
  `-netdev help` and falls back to `socket,id=n0,listen=:0`. Default NIC
  `e1000e` is absent, so always name `virtio-net-pci` explicitly.
- Tests: `cd ui/build && QT_QPA_PLATFORM=offscreen ctest`. The
  `core_integration` suite drives a real `vaplayer-core` (found via
  `VAPLAYER_CORE`, else `../core/build/vaplayer-core`) and skips cleanly when
  the binary is absent — a fresh clone shows it passing in ~0.02 s.
- GUI render check: run under `Xvfb :N -screen 0 1280x800x24` with
  `QT_QPA_PLATFORM=xcb` (NOT `offscreen`, which renders nothing) and capture
  with `import -window root`. A leftover exported `QT_QPA_PLATFORM=offscreen`
  in the shell silently produces blank screenshots.
- Segfault class to avoid: creating a per-poll `QTcpSocket` and capturing the
  raw pointer in a `QTimer::singleShot` while also `deleteLater()`-ing it. Use
  one persistent member socket instead.
- Qt stylesheet: a `QLabel` child paints its own background over a parent
  frame; set `background: transparent` on the label or the frame colour (e.g.
  the black screen area) is hidden.

## tools/ (Task C) — provisioning, ARM bridge, V-Flash  [commit c033cb8]
- Run tests: `/usr/bin/python3 -m pytest tools/tests -q` (137 tests, real
  artifacts, no mocks). `/usr/local/bin/python3` has NO pytest.
- `tools/vaplayer_tools/`: shell, partition (MBR/GPT), ext4 (debugfs engine),
  qcow2, android (build.prop/profiles/sparse), image (ISO→system.img→qcow2),
  flashzip (edify + Magisk), nativebridge (libndk_translation injector).
- CLIs: `tools/fetch_image.{sh,py}`, `tools/inject_arm.{sh,py}`,
  `tools/bin/vflash` (+ `tools/vflash/{engine,cli}.py`). Docs: `tools/README.md`.
- **debugfs pitfalls (both fixed, both silent-fatal):** (1) `set_inode_field
  mode` writes the WHOLE st_mode — chmod must OR in the type bits or e2fsck
  deletes the file as "bad type"; (2) debugfs never fixes superblock free-block
  counters, so every write batch ends with `e2fsck -fy` (`Ext4Image.flush()`).
- `unsquashfs -d DIR` does not create missing parents. Nested prebuilt paths
  must be preserved or 32/64-bit `libndk_translation.so` collide. Edify args
  split on top-level commas, not quote-scanning. A stub `updater-script` must
  fall through to the shell/flat payload path.
- Loop/mount still unusable ⇒ offline debugfs editing is the only route.

## ui/ provisioning (Task D) — V-Flash + ROM Manager  [see 2026-11-12.md]
- New: `ui/tools/ToolRunner.{h,cpp}` (async QProcess wrapper + static output
  parsers), `ui/ui/VFlashDialog.{h,cpp}`, `ui/ui/RomManagerDialog.{h,cpp}`,
  `ui/tests/test_provisioning.cpp` (16 cases, incl. a real flash).
- `MainWindow.ui` sets `objectName` on every sidebar button (shared
  `secondaryButton`/`ghostButton` QSS ids), so `findChild("vflashButton")` is
  null — look buttons up by `text()`. `ui->vflashButton` still works.
- `vflash` maps the image root onto `/system`: edify `/system/bin/x` → `/bin/x`.
- Presets in `RomManagerDialog::presets()` mirror `tools` known images:
  `android-x86-9` (default), `android-x86-7.1`, `blissos-15`, local ISO.
- Tests: `cd ui/build && ctest` = 4/4; `tools` pytest = 138 passed.

## ui/ Task E — Quick Controls, Drag&Drop, Shared Folder, Keymapper
- New modules: `ui/keymapper/{Keymap,KeymapperEngine,KeymapperOverlay}`,
  `ui/tools/AdbClient`, `ui/ui/{DropHelper,ViewportWidget,SharedFolderDialog,
  KeymapperDialog}`, `ui/profiles/default.json` (copied next to the binary by a
  POST_BUILD step so `Keymap::defaultProfilePath()` resolves).
- `ViewportWidget` **replaces** the old `QFrame screenFrame` in `MainWindow.ui`
  (registered under `<customwidgets>`). There is **no `ui->screenLabel`
  anymore** — use `m_viewport->setPlaceholderText()`. `qobject_cast<ViewportWidget*>(
  ui->screenFrame)` gives the instance.
- The keymapper editor is **modeless**: tap points are placed by clicking the
  overlay while the dialog is open, so `exec()` would deadlock the placement
  flow. Toggle button = active/editing; closing the dialog returns the overlay
  to click-through play.
- Mouse input is forwarded **only** when an engine is attached
  (`ViewportWidget::setKeymapperEngine`) and only while a button is held.
- 9p shared folder is **gated on `supportsDevice("virtio-9p-pci")`**; the pruned
  core does not build 9p yet, so the export is silently skipped. The probe runs
  `<core> -device help`; test it with a stub script (see
  `test_emulator_runner.cpp::writeStubEmulator`).
- Tests: `ui/tests/test_keymapper.cpp` (14) + `ui/tests/test_task_e.cpp` (13),
  registered with `QT_QPA_PLATFORM=offscreen`. Suite = 6/6. `QmpClient` test
  servers must send the greeting **on connect** (QMP greeting is unsolicited).
- `Qt::SingleShotConnection` needs Qt >= 6.5; guard with
  `QT_VERSION_CHECK(6,5,0)` (constexpr `kSingleShot` in MainWindow.cpp).
