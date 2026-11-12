# VAPlayer provisioning tools

Everything needed to get a bootable Android guest onto disk for
`vaplayer-core`, to give that guest ARM application support, and to modify it
afterwards — all **offline**, with no loop device, no `mount`, and no root.

```
tools/
├── fetch_image.py / .sh      download + unpack a base image -> qcow2
├── inject_arm.py  / .sh      inject Google's libndk_translation ARM bridge
├── vflash/                   V-Flash core: inspect, edit, and flash images
│   ├── engine.py             image session, tree/extract, flash, write-back
│   └── cli.py                the `vflash` command
├── bin/vflash                shell entry point for vflash
├── vaplayer_tools/           shared library
│   ├── shell.py              subprocess helpers, tool discovery
│   ├── partition.py          MBR/GPT parsing, ext4 offset discovery
│   ├── ext4.py               debugfs-backed ext4 read/write engine
│   ├── qcow2.py              qemu-img wrapper (convert, create, check)
│   ├── android.py            build.prop, device profiles, sparse images
│   ├── image.py              ISO -> system.img -> qcow2 pipeline
│   ├── flashzip.py           flashable-ZIP engine (edify + shell payloads)
│   └── nativebridge.py       libndk_translation injector and verifier
└── tests/                    137 tests; real images, no mocks
```

## Why offline editing

VAPlayer runs the emulator on the same host that prepares the images. A loop
device or `mount` would need root and would conflict with the running guest, so
the tools edit ext4 filesystems directly through `debugfs`. Writing to a
filesystem this way updates inodes and block bitmaps but leaves the superblock's
free-block counters stale, so the engine runs `e2fsck -fy` after a batch of
writes (`Ext4Image.flush()`). Every write path in the tools ends in a clean
filesystem — this is covered by tests.

## Requirements

| Purpose | Tools |
| --- | --- |
| ext4 editing | `mke2fs`, `debugfs`, `dumpe2fs`, `e2fsck` (e2fsprogs) |
| images | `qemu-img` (qemu-utils) |
| ISO unpacking | `xorriso`, `unsquashfs`, `mksquashfs` (squashfs-tools) |
| Android sparse images | `simg2img` / `img2simg` (android-sdk-libsparse-utils) |
| downloads | `curl` or `wget` |
| tests | `pytest` |

Python 3.9+ and the standard library only — there is nothing to `pip install`
for the tools themselves.

## 1. Provisioning an image

```sh
tools/fetch_image.sh --list
tools/fetch_image.sh --image android-x86-9 --out images/android.qcow2
tools/fetch_image.sh --iso ~/android-x86_64-9.0-r2.iso \
                     --out images/android.qcow2 --data-disk images/data.qcow2
```

`fetch_image` downloads an Android-x86 or BlissOS ISO, extracts `system.sfs`,
unpacks the raw ext4 `system.img` from the SquashFS, normalises Android sparse
images to raw, converts to a sparse `qcow2`, and verifies the result with
`qemu-img check`.

Known sources are in `vaplayer_tools/image.py`; pass any URL to `--image` to use
another build.

## 2. ARM application support

Android-x86 guests are x86_64 and cannot run ARM-only APKs. Google's native
bridge, `libndk_translation`, solves this by translating ARM code at runtime.

```sh
# Fetch the bridge from upstream and inject it, spoofing a ROG Phone 8:
tools/inject_arm.sh --image images/android.qcow2 --extract images/android.raw \
                    --profile rog8

# Or from a local prebuilt directory, with no network:
tools/inject_arm.sh --image system.img --source-dir ./ndk-prebuilt

# Inspect what is installed:
tools/inject_arm.sh --image images/android.raw --verify
```

The injector:

- installs `libndk_translation.so` into `/lib64` (x86_64) and `/lib` (x86),
  keeping the two architectures distinct;
- installs the `ndk_translation_program_runner_binfmt_misc{,_arm64}` runners
  and the `ndk_translation.rc` init script;
- copies the `libndk_translation_proxy_*.so` libraries;
- sets the bridge properties (`ro.dalvik.vm.native.bridge`,
  `ro.dalvik.vm.isa.arm{,64}`, `ro.vendor.enable.native.bridge.exec{,64}`);
- widens `ro.product.cpu.abilist` to `arm64-v8a,armeabi-v7a,armeabi` so the
  package manager accepts ARM APKs;
- optionally spoofs a device profile (`--profile`, see `--list-profiles`).

Source: [`supremegamers/vendor_google_proprietary_ndk_translation-prebuilt`](https://github.com/supremegamers/vendor_google_proprietary_ndk_translation-prebuilt).

## 3. V-Flash

`vflash` inspects and modifies an image without booting it. It understands a
bare ext4 filesystem, a raw disk with an MBR/GPT table, and `qcow2`.

```sh
bin/vflash --image images/android.qcow2 --inspect
bin/vflash --image images/android.qcow2 --json > image.json
bin/vflash --image images/android.qcow2 --tree /lib64 --depth 1
bin/vflash --image images/android.qcow2 --extract /build.prop ./build.prop
bin/vflash --image images/android.qcow2 --cat /build.prop
bin/vflash --image images/android.qcow2 --set-prop ro.product.model=Pixel
bin/vflash --image images/android.qcow2 --flash magisk.zip --backup before.qcow2
```

Flashable packages are applied in this order:

1. **edify** — an `updater-script` is interpreted. `package_extract_file`,
   `package_extract_dir`, `set_perm`, `set_perm_recursive`, `symlink`,
   `mkdir`, `run_program`, `ui_print`, `mount`, and `unmount` are handled.
   `format` is recognised but refused, since it would destroy the guest.
2. **shell payload** — a Magisk-style package whose files live in a `system/`
   tree is installed directly. On-device `init` hooks are not applied offline;
   the report says so.
3. **flat payload** — a `system/`-rooted tree with no installer script.

For a `qcow2` input the edited filesystem is written back into the original
qcow2; for a partitioned raw image it is spliced back over its partition so the
table and sibling partitions survive.

## Tests

```sh
cd tools
python3 -m pytest tests/ -q
```

The suite builds **real** artifacts — real ext4 images via `mke2fs`, a real GPT
table, real `qcow2` files via `qemu-img`, real squashfs and ISO images via
`mksquashfs`/`xorriso`, and real flashable ZIPs — and asserts on the results.
Nothing is mocked, so a passing suite means the same code paths a user runs
were exercised. Tests whose tools are missing skip rather than fail.

Note: if your `python3` lacks `pytest`, use a `python3` that has it (for
example `/usr/bin/python3` on Debian/Ubuntu).

## License

GPL-3.0-or-later, matching the rest of VAPlayer. See `../COPYING`.
