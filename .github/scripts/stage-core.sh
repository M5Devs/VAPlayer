#!/bin/sh
#
# Stage vaplayer-core together with the firmware it loads at runtime.
#
# A relocated QEMU looks for its data files under
# "$exec_dir/qemu-bundle/<prefix>/share/qemu", so the bundle has to sit beside
# the binary.  Without it the core exits immediately with
# "could not load PC BIOS 'bios-256k.bin'" and never opens its QMP socket.
#
# Usage: stage-core.sh [core-dir] <dest-bin-dir>

set -eu

core_dir="${1:-core}"
dest="${2:?usage: stage-core.sh [core-dir] <dest-bin-dir>}"

if [ ! -x "$core_dir/build/vaplayer-core" ]; then
    echo "stage-core: $core_dir/build/vaplayer-core is missing or not executable" >&2
    exit 1
fi

mkdir -p "$dest/qemu-bundle/usr/local/share"
install -m 0755 "$core_dir/build/vaplayer-core" "$dest/vaplayer-core"
cp -r "$core_dir/pc-bios" "$dest/qemu-bundle/usr/local/share/qemu"
