# VAPlayer provisioning tools — tests
#
# Copyright (C) 2026 VAPlayer contributors
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program.  If not, see <https://www.gnu.org/licenses/>.

"""Shared pytest fixtures.

The fixtures build *real* ext4 images, real GPT tables, real qcow2 images, and
real flashable ZIPs on disk with the same tools the production code uses. No
mocks: every assertion exercises the code paths that run in production.
"""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import sys
import zipfile

import pytest

TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)


def _have(tool: str) -> bool:
    return shutil.which(tool) is not None


requires_e2fsprogs = pytest.mark.skipif(
    not (_have("mke2fs") and _have("debugfs") and _have("dumpe2fs")),
    reason="e2fsprogs (mke2fs/debugfs/dumpe2fs) is required")
requires_qemu_img = pytest.mark.skipif(
    not _have("qemu-img"), reason="qemu-img is required")
requires_zip = pytest.mark.skipif(
    not _have("zip"), reason="zip is required")


@pytest.fixture(scope="session")
def tools_dir() -> str:
    return TOOLS_DIR


def _run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def make_ext4(path: str, *, size_mb: int = 32, label: str = "system",
              tree: dict[str, str] | None = None) -> str:
    """Create an ext4 image at *path* containing *tree* (guest path -> text)."""
    with open(path, "wb") as fh:
        fh.truncate(size_mb * 1024 * 1024)
    _run(["mke2fs", "-q", "-t", "ext4", "-F", "-b", "4096", "-L", label,
          "-O", "^has_journal", path])
    if tree:
        from vaplayer_tools.ext4 import Ext4Image
        with Ext4Image(path, readonly=False) as img:
            for guest, content in tree.items():
                img.write_file(guest, content.encode(), mode=0o644)
            img.flush()
    return path


@pytest.fixture
def empty_ext4(tmp_path):
    """A bare 32 MiB ext4 image with a ``/lib64`` and ``/bin``."""
    path = str(tmp_path / "system.img")
    return make_ext4(path, tree={
        "/lib64/.keep": "",
        "/bin/.keep": "",
        "/etc/.keep": "",
    })


@pytest.fixture
def android_like_ext4(tmp_path):
    """An ext4 image that resembles an Android ``system`` partition."""
    build_prop = (
        "ro.build.version.release=9\n"
        "ro.build.version.sdk=28\n"
        "ro.product.brand=Android-x86\n"
        "ro.product.device=x86_64\n"
        "ro.product.cpu.abi=x86_64\n"
        "ro.product.cpu.abilist=x86_64,x86,armeabi-v7a,armeabi\n"
        "ro.dalvik.vm.native.bridge=0\n"
    )
    path = str(tmp_path / "system.img")
    return make_ext4(path, size_mb=64, tree={
        "/build.prop": build_prop,
        "/lib64/.keep": "",
        "/lib/.keep": "",
        "/bin/.keep": "",
        "/etc/init/.keep": "",
        "/system/bin/.keep": "",
    })


@pytest.fixture
def gpt_disk(tmp_path, android_like_ext4):
    """A raw disk with a GPT table whose ``system`` partition is the ext4 image."""
    from vaplayer_tools.ext4 import Ext4Image

    fs_size = os.path.getsize(android_like_ext4)
    sector = 512
    start_lba = 2048
    # Round the partition up to a whole number of sectors.
    sectors = (fs_size + sector - 1) // sector
    disk_size = (start_lba + sectors + 2048) * sector
    path = str(tmp_path / "disk.img")

    with open(path, "wb") as fh:
        fh.truncate(disk_size)
        # Protective MBR.
        mbr = bytearray(512)
        mbr[446 + 4] = 0xEE
        struct.pack_into("<II", mbr, 446 + 8, 1, min(sectors + 33, 0xFFFFFFFF))
        mbr[510:512] = b"\x55\xaa"
        fh.seek(0)
        fh.write(mbr)

        # GPT header at LBA 1 (offsets per UEFI spec).
        header = bytearray(512)
        header[0:8] = b"EFI PART"
        struct.pack_into("<I", header, 8, 0x00010000)   # revision
        struct.pack_into("<I", header, 12, 92)          # header size
        struct.pack_into("<Q", header, 24, 1)           # current LBA
        struct.pack_into("<Q", header, 32, disk_size // sector - 1)  # backup LBA
        struct.pack_into("<Q", header, 40, 34)          # first usable LBA
        struct.pack_into("<Q", header, 48, disk_size // sector - 34)  # last usable
        struct.pack_into("<Q", header, 72, 34)          # partition entries LBA
        struct.pack_into("<I", header, 80, 128)         # number of entries
        struct.pack_into("<I", header, 84, 128)         # entry size
        fh.seek(sector)
        fh.write(header)

        # One partition entry: Linux filesystem GUID.
        entry = bytearray(128)
        entry[0:16] = bytes.fromhex("af3dc60f838472478e793d69d8477de4")
        struct.pack_into("<Q", entry, 32, start_lba)
        struct.pack_into("<Q", entry, 40, start_lba + sectors - 1)
        name = "system".encode("utf-16-le")
        entry[56:56 + len(name)] = name
        fh.seek(34 * sector)
        fh.write(entry)

        # Copy the ext4 filesystem into the partition.
        with open(android_like_ext4, "rb") as src:
            fh.seek(start_lba * sector)
            shutil.copyfileobj(src, fh, length=8 * 1024 * 1024)
    return path


@pytest.fixture
def qcow2_image(tmp_path, android_like_ext4):
    """A qcow2 image wrapping the Android-like ext4 filesystem."""
    if not _have("qemu-img"):
        pytest.skip("qemu-img is required")
    path = str(tmp_path / "android.qcow2")
    _run(["qemu-img", "convert", "-f", "raw", "-O", "qcow2",
          android_like_ext4, path])
    return path


@pytest.fixture
def ndk_source_dir(tmp_path):
    """A directory of fake prebuilt bridge files laid out like upstream."""
    root = tmp_path / "ndk" / "prebuilts"
    (root / "lib64").mkdir(parents=True)
    (root / "lib").mkdir(parents=True)
    (root / "bin").mkdir(parents=True)
    (root / "etc" / "init").mkdir(parents=True)

    # Minimal ELF headers so arch detection has something real to read.
    def elf(machine: int, size: int = 64) -> bytes:
        hdr = bytearray(64)
        hdr[0:4] = b"\x7fELF"
        hdr[4] = 2          # 64-bit
        hdr[5] = 1          # little-endian
        struct.pack_into("<H", hdr, 16, 2)       # e_type = ET_EXEC
        struct.pack_into("<H", hdr, 18, machine)  # e_machine
        return bytes(hdr) + b"\x00" * (size - 64)

    (root / "lib64" / "libndk_translation.so").write_bytes(elf(0x3E))
    (root / "lib" / "libndk_translation.so").write_bytes(elf(0x03))
    (root / "bin" / "ndk_translation_program_runner_binfmt_misc").write_bytes(
        elf(0x3E, 32))
    (root / "bin" / "ndk_translation_program_runner_binfmt_misc_arm64").write_bytes(
        elf(0x3E, 32))
    (root / "etc" / "init" / "ndk_translation.rc").write_text(
        "on early-init\n    mount binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc\n")

    for name in ("libEGL", "libGLESv2", "libc", "libvulkan"):
        (root / "lib64" / f"libndk_translation_proxy_{name}.so").write_bytes(elf(0x3E, 48))
    return str(tmp_path / "ndk")


def make_flashable_zip(path: str, files: dict[str, str], *,
                       edify: str | None = None,
                       update_binary: str | None = None) -> str:
    """Build a flashable ZIP at *path*.

    *files* maps archive-relative names to text content. When *edify* is given
    an ``updater-script`` is written; when *update_binary* is given a shell
    ``update-binary`` is written.
    """
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        for name, content in files.items():
            zf.writestr(name, content)
        if edify is not None:
            zf.writestr("META-INF/com/google/android/updater-script", edify)
        if update_binary is not None:
            zf.writestr("META-INF/com/google/android/update-binary", update_binary)
    return path


@pytest.fixture
def edify_zip(tmp_path):
    """A package that installs files via a declarative edify script."""
    return make_flashable_zip(
        str(tmp_path / "edify.zip"),
        {
            "payload/hello.txt": "hello from edify\n",
            "payload/tool": "#!/bin/sh\necho tool\n",
            "payload/etc/conf.d/extra.conf": "key=value\n",
        },
        edify=(
            'ui_print("Installing test package");\n'
            'package_extract_file("payload/hello.txt", "/system/bin/hello.txt");\n'
            'package_extract_file("payload/tool", "/system/bin/tool");\n'
            'package_extract_dir("payload/etc", "/system/etc");\n'
            'set_perm(0, 0, 0755, "/system/bin/tool");\n'
            'symlink("/system/bin/tool", "/system/bin/tool-link");\n'
        ),
    )


@pytest.fixture
def magisk_zip(tmp_path):
    """A package that resembles Magisk's shell-driven payload."""
    return make_flashable_zip(
        str(tmp_path / "magisk.zip"),
        {
            "system/bin/magisk": "magisk binary\n",
            "system/lib64/magisk_stub.apk": "stub apk\n",
            "lib64/magiskinit": "magiskinit binary\n",
        },
        update_binary="#!/sbin/sh\n# Magisk installer (declarative interpretation)\n",
    )
