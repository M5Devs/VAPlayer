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

"""Unit tests for partition probing, qcow2, and Android metadata parsing."""

from __future__ import annotations

import pytest

from conftest import requires_e2fsprogs, requires_qemu_img
from vaplayer_tools import android, qcow2
from vaplayer_tools.errors import ImageError
from vaplayer_tools.partition import probe


# ---------------------------------------------------------------------------
# Partition / disk layout
# ---------------------------------------------------------------------------

@requires_e2fsprogs
def test_probe_bare_ext4(android_like_ext4):
    layout = probe(android_like_ext4)
    assert layout.scheme == "none"
    assert layout.ext4_offset == 0
    assert layout.partitions == []
    assert layout.system_partition() is None


@requires_e2fsprogs
def test_probe_gpt_finds_system(android_like_ext4, gpt_disk):
    layout = probe(gpt_disk)
    assert layout.scheme == "gpt"
    assert layout.ext4_offset != 0
    part = layout.system_partition()
    assert part is not None
    assert part.name == "system"
    assert part.start == layout.ext4_offset
    assert part.size >= 64 * 1024 * 1024 - 4096
    assert part.contains(layout.ext4_offset)


@requires_e2fsprogs
def test_probe_mbr(tmp_path, android_like_ext4):
    import struct

    fs_size = __import__("os").path.getsize(android_like_ext4)
    start_lba = 2048
    sectors = (fs_size + 511) // 512
    path = str(tmp_path / "mbr.img")
    with open(path, "wb") as fh:
        fh.truncate((start_lba + sectors) * 512)
        mbr = bytearray(512)
        mbr[446 + 4] = 0x83  # Linux
        struct.pack_into("<II", mbr, 446 + 8, start_lba, sectors)
        mbr[510:512] = b"\x55\xaa"
        fh.seek(0)
        fh.write(mbr)
        with open(android_like_ext4, "rb") as src:
            fh.seek(start_lba * 512)
            fh.write(src.read())

    layout = probe(path)
    assert layout.scheme == "mbr"
    assert layout.ext4_offset == start_lba * 512
    assert layout.partitions[0].partition_type == "ext4"


def test_probe_no_filesystem(tmp_path):
    path = tmp_path / "blank.img"
    path.write_bytes(b"\x00" * (1024 * 1024))
    with pytest.raises(ImageError):
        probe(str(path))


# ---------------------------------------------------------------------------
# qcow2
# ---------------------------------------------------------------------------

@requires_qemu_img
def test_qcow2_info_and_sparse(android_like_ext4, tmp_path):
    dest = str(tmp_path / "out.qcow2")
    qcow2.convert(android_like_ext4, dest, compress=False)
    info = qcow2.info(dest)
    assert info.format == "qcow2"
    assert info.virtual_size == __import__("os").path.getsize(android_like_ext4)
    assert info.is_sparse


@requires_qemu_img
def test_qcow2_compressed_smaller(android_like_ext4, tmp_path):
    plain = str(tmp_path / "plain.qcow2")
    compressed = str(tmp_path / "compressed.qcow2")
    qcow2.convert(android_like_ext4, plain, compress=False)
    qcow2.convert(android_like_ext4, compressed, compress=True)
    assert __import__("os").path.getsize(compressed) <= __import__("os").path.getsize(plain)


@requires_qemu_img
def test_qcow2_check_and_create(tmp_path):
    path = str(tmp_path / "blank.qcow2")
    qcow2.create(path, 16 * 1024 * 1024)
    qcow2.check(path)
    assert qcow2.info(path).virtual_size == 16 * 1024 * 1024


@requires_qemu_img
def test_qcow2_roundtrip_to_raw(tmp_path, android_like_ext4):
    import os
    qc = str(tmp_path / "x.qcow2")
    raw = str(tmp_path / "x.raw")
    qcow2.convert(android_like_ext4, qc, compress=False)
    qcow2.to_raw(qc, raw)
    assert os.path.getsize(raw) == os.path.getsize(android_like_ext4)
    with open(raw, "rb") as fh:
        fh.seek(0x438)
        assert fh.read(2) == b"\x53\xef"


@requires_qemu_img
def test_qcow2_check_detects_corruption(tmp_path):
    path = str(tmp_path / "bad.qcow2")
    with open(path, "wb") as fh:
        fh.write(b"QFI\xfb" + b"\x00" * 100)
    with pytest.raises(ImageError):
        qcow2.check(path)


# ---------------------------------------------------------------------------
# build.prop / device profiles
# ---------------------------------------------------------------------------

def test_buildprop_parse_and_preserve():
    text = "# comment\nro.a=1\nro.b=two\n\nro.c = spaced \n"
    bp = android.BuildProp(text)
    assert bp.get("ro.a") == "1"
    assert bp.get("ro.b") == "two"
    assert bp.get("ro.c") == "spaced"
    assert bp.get("ro.missing") is None
    assert "ro.a" in bp


def test_buildprop_set_updates_in_place():
    bp = android.BuildProp("ro.a=1\nro.b=2\n")
    bp.set("ro.a", "9")
    out = bp.to_bytes().decode()
    assert "ro.a=9" in out
    assert out.count("ro.a=") == 1
    assert "ro.b=2" in out


def test_buildprop_set_appends_new():
    bp = android.BuildProp("ro.a=1\n")
    bp.set("ro.new", "value")
    out = bp.to_bytes().decode()
    assert "ro.a=1" in out
    assert "ro.new=value" in out


def test_buildprop_roundtrip_from_bytes():
    bp = android.BuildProp(b"ro.x=1\nro.y=2\n")
    assert bp.get("ro.x") == "1"
    assert b"ro.x=1" in bp.to_bytes()


def test_buildprop_update_with_comment():
    bp = android.BuildProp("ro.a=1\n")
    bp.update({"ro.b": "2"}, comment="VAPlayer")
    out = bp.to_bytes().decode()
    assert "# VAPlayer" in out
    assert "ro.b=2" in out


def test_device_profiles_are_consistent():
    for key, prof in android.DEVICE_PROFILES.items():
        assert prof.props["ro.product.cpu.abi"] == "arm64-v8a"
        assert prof.props["ro.product.cpu.abilist"].startswith("arm64-v8a")
        assert prof.props["ro.product.model"]
        assert prof.props["ro.product.brand"]
        assert int(prof.props["ro.max_fps"]) >= 120, key


def test_get_profile_and_unknown():
    assert android.get_profile("rog8").key == "rog8"
    assert android.get_profile("S24ULTRA").key == "s24ultra"
    with pytest.raises(ImageError):
        android.get_profile("nonexistent")


def test_native_bridge_props_shape():
    assert android.NATIVE_BRIDGE_PROPS["ro.dalvik.vm.native.bridge"] == "libndk_translation.so"
    assert android.NATIVE_BRIDGE_PROPS["ro.dalvik.vm.isa.arm64"] == "x86_64"
    assert "arm64-v8a" in android.NATIVE_BRIDGE_ABILIST


def test_summary_of_buildprop():
    bp = android.BuildProp("ro.build.version.release=9\nro.product.cpu.abi=x86_64\n")
    s = android.summary(bp)
    assert s["ro.build.version.release"] == "9"
    assert s["ro.product.cpu.abi"] == "x86_64"
    assert s["ro.dalvik.vm.native.bridge"] == "-"
