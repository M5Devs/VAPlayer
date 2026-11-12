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
