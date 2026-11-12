# VAPlayer

VAPlayer is an x86_64 system emulator for Android virtualisation, built on a
pruned QEMU source tree.

## Layout

- `core/` — trimmed QEMU tree (upstream `qemu.git`) used to build the
  `vaplayer-core` binary.
- `COPYING`, `COPYING.LIB` — GPLv3 / LGPLv2 licenses inherited from QEMU.

## Pruning

The `core/` tree is reduced to what x86_64 system emulation needs:

- Only the `x86_64-softmmu` target (`target/i386`) is kept.
- Legacy guest architectures (arm, mips, riscv, ppc, sparc, s390x, hppa,
  loongarch, hexagon, ...) and their devices are removed.
- `docs/` and `tests/` are removed; unused subprojects and legacy devices
  (floppy, IDE, SCSI, legacy NICs) are pruned.
- Firmware blobs in `pc-bios/` are limited to the x86_64 set.

VirtIO drivers retained: `virtio-gpu`, `virtio-blk`, `virtio-net`,
`virtio-snd`, `virtio-input`.

## Building

```sh
cd core
mkdir -p build && cd build
../configure --target-list=x86_64-softmmu \
    --disable-docs --disable-guest-agent --disable-tools \
    --disable-sdl --disable-gtk --disable-vnc --disable-opengl \
    --disable-spice --disable-curl --disable-libssh --disable-usb-redir \
    --disable-libusb --disable-brlapi --disable-curses --disable-capstone \
    --disable-numa --disable-replication
ninja
```

The result is `build/vaplayer-core`. Hardware acceleration is provided by
WHPX (Windows), KVM (Linux) and HVF (macOS), with TCG as the portable
fallback.
