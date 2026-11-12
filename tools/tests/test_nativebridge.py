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

"""Tests for the ARM native-bridge injector and its verification."""

from __future__ import annotations

import os
import struct

import pytest

from conftest import requires_e2fsprogs
from vaplayer_tools import android, nativebridge
from vaplayer_tools.errors import ImageError
from vaplayer_tools.ext4 import Ext4Image

pytestmark = requires_e2fsprogs


def _elf(machine: int, size: int = 64) -> bytes:
    hdr = bytearray(size)
    hdr[0:4] = b"\x7fELF"
    hdr[4] = 2
    hdr[5] = 1
    struct.pack_into("<H", hdr, 16, 2)
    struct.pack_into("<H", hdr, 18, machine)
    return bytes(hdr)


# ---------------------------------------------------------------------------
# ELF arch detection
# ---------------------------------------------------------------------------

def test_elf_arch_detection(tmp_path):
    cases = {"x86_64": 0x3E, "i386": 0x03, "arm64": 0xB7, "arm": 0x28}
    for name, machine in cases.items():
        path = tmp_path / f"{name}.so"
        path.write_bytes(_elf(machine))
        assert nativebridge._elf_arch(str(path)) == name


def test_elf_arch_rejects_non_elf(tmp_path):
    path = tmp_path / "text.txt"
    path.write_text("not an elf")
    assert nativebridge._elf_arch(str(path)) == "unknown"


def test_elf_arch_from_bytes():
    assert nativebridge._elf_arch_from_bytes(_elf(0x3E)) == "x86_64"
    assert nativebridge._elf_arch_from_bytes(b"nope") == "not-elf"


# ---------------------------------------------------------------------------
# Source layout resolution
# ---------------------------------------------------------------------------

def test_locate_source_nested_and_flat(tmp_path):
    nested = tmp_path / "prebuilts" / "lib64"
    nested.mkdir(parents=True)
    (nested / "libndk_translation.so").write_bytes(_elf(0x3E))
    remote = "prebuilts/lib64/libndk_translation.so"
    assert nativebridge._locate_source(str(tmp_path), remote) is not None

    flat = tmp_path / "flat"
    flat.mkdir()
    (flat / "libndk_translation.so").write_bytes(_elf(0x3E))
    assert nativebridge._locate_source(str(flat), remote) is not None


def test_locate_source_missing(tmp_path):
    assert nativebridge._locate_source(str(tmp_path), "prebuilts/lib64/x.so") is None


def test_iter_proxy_files(ndk_source_dir):
    found = nativebridge._iter_proxy_files(ndk_source_dir)
    assert len(found) == 4
    assert all(os.path.basename(p).startswith("libndk_translation_proxy_") for p in found)


# ---------------------------------------------------------------------------
# Injection
# ---------------------------------------------------------------------------

def test_inject_installs_library_and_props(android_like_ext4, ndk_source_dir):
    report = nativebridge.inject(android_like_ext4, ndk_source_dir,
                                 profile="rog8")
    assert len(report.installed) == 5
    assert report.proxies == 4
    assert report.props_set["ro.dalvik.vm.native.bridge"] == "libndk_translation.so"
    assert report.props_set["ro.product.model"] == "ASUS_AI2401"
    assert "ROG" in report.profile or "ASUS" in report.profile

    img = Ext4Image(android_like_ext4)
    assert img.exists("/lib64/libndk_translation.so")
    assert img.exists("/lib/libndk_translation.so")
    assert img.exists("/bin/ndk_translation_program_runner_binfmt_misc")
    assert img.exists("/bin/ndk_translation_program_runner_binfmt_misc_arm64")
    assert img.exists("/etc/init/ndk_translation.rc")
    # The 64-bit and 32-bit libraries must be distinct files.
    assert img.read_file("/lib64/libndk_translation.so") != \
        img.read_file("/lib/libndk_translation.so")


def test_inject_library_arch_is_x86_64(android_like_ext4, ndk_source_dir):
    nativebridge.inject(android_like_ext4, ndk_source_dir, profile="rog8")
    img = Ext4Image(android_like_ext4)
    data = img.read_file("/lib64/libndk_translation.so")
    assert nativebridge._elf_arch_from_bytes(data) == "x86_64"


def test_inject_sets_abi_list(android_like_ext4, ndk_source_dir):
    nativebridge.inject(android_like_ext4, ndk_source_dir, profile="s24ultra")
    bp = android.BuildProp(Ext4Image(android_like_ext4).read_file("/build.prop"))
    assert bp.get("ro.product.cpu.abilist") == android.NATIVE_BRIDGE_ABILIST
    assert "arm64-v8a" in bp.get("ro.product.cpu.abilist")
    assert bp.get("ro.product.model") == "SM-S928B"


def test_inject_without_spoof_keeps_identity(android_like_ext4, ndk_source_dir):
    before = android.BuildProp(Ext4Image(android_like_ext4).read_file("/build.prop"))
    nativebridge.inject(android_like_ext4, ndk_source_dir, spoof_device=False)
    after = android.BuildProp(Ext4Image(android_like_ext4).read_file("/build.prop"))
    assert after.get("ro.product.brand") == before.get("ro.product.brand")
    assert after.get("ro.product.model") == before.get("ro.product.model")
    # But the bridge property is still set.
    assert after.get("ro.dalvik.vm.native.bridge") == "libndk_translation.so"


def test_inject_preserves_existing_props(android_like_ext4, ndk_source_dir):
    nativebridge.inject(android_like_ext4, ndk_source_dir, profile="rog8")
    bp = android.BuildProp(Ext4Image(android_like_ext4).read_file("/build.prop"))
    assert bp.get("ro.build.version.release") == "14"  # profile overrides
    assert bp.get("ro.build.version.sdk") == "34"


def test_inject_leaves_clean_filesystem(android_like_ext4, ndk_source_dir):
    """Regression: the injected image must pass e2fsck with no errors."""
    import subprocess
    nativebridge.inject(android_like_ext4, ndk_source_dir, profile="rog8")
    proc = subprocess.run(["e2fsck", "-fn", android_like_ext4],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert proc.returncode == 0, proc.stdout.decode()


def test_inject_warns_on_missing_files(android_like_ext4, tmp_path):
    empty = tmp_path / "empty-ndk"
    empty.mkdir()
    report = nativebridge.inject(android_like_ext4, str(empty))
    assert len(report.warnings) >= 5
    assert report.installed == []


def test_inject_requires_build_prop(tmp_path):
    from conftest import make_ext4
    path = make_ext4(str(tmp_path / "no-prop.img"), tree={"/lib64/.keep": ""})
    with pytest.raises(ImageError):
        nativebridge.inject(path, str(tmp_path))


# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

def test_verify_before_injection(android_like_ext4):
    result = nativebridge.verify(android_like_ext4)
    assert result["bridge_installed"] is False
    assert result["library_present"] is False
    assert result["proxy_count"] == 0


def test_verify_after_injection(android_like_ext4, ndk_source_dir):
    nativebridge.inject(android_like_ext4, ndk_source_dir, profile="rog8")
    result = nativebridge.verify(android_like_ext4)
    assert result["bridge_installed"] is True
    assert result["library_present"] is True
    assert result["library_arch"] == "x86_64"
    assert result["library_size"] > 0
    assert result["proxy_count"] == 4
    assert len(result["runners"]) == 2
    assert result["props"]["ro.dalvik.vm.native.bridge"] == "libndk_translation.so"
    assert result["device"]["brand"] == "asus"


def test_verify_reports_missing_lib64(tmp_path):
    from conftest import make_ext4
    path = make_ext4(str(tmp_path / "x.img"), tree={"/build.prop": "ro.x=1\n"})
    result = nativebridge.verify(path)
    assert result["bridge_installed"] is False
    assert "error" in result


def test_inject_idempotent(android_like_ext4, ndk_source_dir):
    nativebridge.inject(android_like_ext4, ndk_source_dir, profile="rog8")
    nativebridge.inject(android_like_ext4, ndk_source_dir, profile="rog8")
    img = Ext4Image(android_like_ext4)
    bp = android.BuildProp(img.read_file("/build.prop"))
    # No duplicated properties.
    text = img.read_file("/build.prop").decode()
    assert text.count("ro.dalvik.vm.native.bridge=") == 1
    assert bp.get("ro.dalvik.vm.native.bridge") == "libndk_translation.so"


# ---------------------------------------------------------------------------
# fetch_prebuilt (local layout only; network is not exercised)
# ---------------------------------------------------------------------------

def test_fetch_prebuilt_writes_nested_layout(tmp_path, monkeypatch):
    """fetch_prebuilt must not collide the 32- and 64-bit libraries."""
    calls: list[str] = []

    def fake_download(url: str, dest: str) -> str:
        calls.append(url)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as fh:
            fh.write(_elf(0x3E))
        return dest

    monkeypatch.setattr(nativebridge, "_download", fake_download)
    written = nativebridge.fetch_prebuilt(str(tmp_path), include_proxies=False)
    names = [os.path.relpath(w, tmp_path) for w in written]
    assert os.path.join("prebuilts", "lib64", "libndk_translation.so") in names
    assert os.path.join("prebuilts", "lib", "libndk_translation.so") in names
    assert len(set(names)) == len(names)


def test_fetch_prebuilt_skips_optional_proxy_failures(tmp_path, monkeypatch):
    def flaky(url: str, dest: str) -> str:
        if "proxy" in url:
            from vaplayer_tools.errors import ToolsError
            raise ToolsError("404")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as fh:
            fh.write(_elf(0x3E))
        return dest

    monkeypatch.setattr(nativebridge, "_download", flaky)
    written = nativebridge.fetch_prebuilt(str(tmp_path), include_proxies=True)
    assert len(written) == 5  # only the core files
