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

"""Tests for the flashable-ZIP engine (edify and shell payloads)."""

from __future__ import annotations

import zipfile

import pytest

from conftest import make_flashable_zip, requires_e2fsprogs
from vaplayer_tools.errors import FlashError
from vaplayer_tools.ext4 import Ext4Image
from vaplayer_tools.flashzip import ZipFlasher, is_flashable

pytestmark = requires_e2fsprogs


# ---------------------------------------------------------------------------
# Detection
# ---------------------------------------------------------------------------

def test_is_flashable_true(edify_zip):
    assert is_flashable(edify_zip)


def test_is_flashable_false(tmp_path):
    plain = tmp_path / "plain.zip"
    with zipfile.ZipFile(plain, "w") as zf:
        zf.writestr("readme.txt", "not flashable")
    assert not is_flashable(str(plain))


def test_is_flashable_rejects_non_zip(tmp_path):
    path = tmp_path / "not.zip"
    path.write_text("hello")
    assert not is_flashable(str(path))


def test_flash_rejects_missing_package(empty_ext4):
    with Ext4Image(empty_ext4, readonly=False) as img:
        with pytest.raises(FlashError):
            ZipFlasher(img).flash("/nonexistent.zip")


def test_flash_rejects_non_zip(tmp_path, empty_ext4):
    bad = tmp_path / "bad.zip"
    bad.write_text("not a zip")
    with Ext4Image(empty_ext4, readonly=False) as img:
        with pytest.raises(FlashError):
            ZipFlasher(img).flash(str(bad))


def test_flash_requires_writable_image(empty_ext4, edify_zip):
    img = Ext4Image(empty_ext4, readonly=True)
    with pytest.raises(FlashError):
        ZipFlasher(img).flash(edify_zip)


# ---------------------------------------------------------------------------
# Edify packages
# ---------------------------------------------------------------------------

def test_edify_extract_file(android_like_ext4, edify_zip):
    with Ext4Image(android_like_ext4, readonly=False) as img:
        report = ZipFlasher(img).flash(edify_zip)
    assert report.package_type == "edify"
    assert report.files_written >= 3
    reopened = Ext4Image(android_like_ext4)
    assert reopened.exists("/bin/hello.txt")
    assert reopened.read_file("/bin/hello.txt") == b"hello from edify\n"
    # /system/bin/tool must have landed at /bin/tool in the image.
    assert reopened.exists("/bin/tool")
    assert reopened.read_file("/bin/tool").startswith(b"#!/bin/sh")


def test_edify_extract_dir(android_like_ext4, edify_zip):
    with Ext4Image(android_like_ext4, readonly=False) as img:
        ZipFlasher(img).flash(edify_zip)
    reopened = Ext4Image(android_like_ext4)
    assert reopened.exists("/etc/conf.d/extra.conf")
    assert reopened.read_file("/etc/conf.d/extra.conf") == b"key=value\n"


def test_edify_symlink(android_like_ext4, edify_zip):
    with Ext4Image(android_like_ext4, readonly=False) as img:
        ZipFlasher(img).flash(edify_zip)
    reopened = Ext4Image(android_like_ext4)
    assert reopened.file_type("/bin/tool-link") == "symlink"


def test_edify_set_perm(android_like_ext4, edify_zip):
    with Ext4Image(android_like_ext4, readonly=False) as img:
        ZipFlasher(img).flash(edify_zip)
    reopened = Ext4Image(android_like_ext4)
    # Permissions were requested as 0755; confirm they were applied.
    out = reopened._debugfs("stat /bin/tool")
    assert "0755" in out


def test_edify_reports_actions(android_like_ext4, edify_zip):
    with Ext4Image(android_like_ext4, readonly=False) as img:
        report = ZipFlasher(img).flash(edify_zip)
    kinds = {a.kind for a in report.applied_actions}
    assert "extract" in kinds
    assert "extract_dir" in kinds
    assert "symlink" in kinds
    assert "set_perm" in kinds


def test_edify_unsupported_verb_warns(android_like_ext4, tmp_path):
    pkg = make_flashable_zip(
        str(tmp_path / "weird.zip"),
        {"payload/a.txt": "a\n"},
        edify=(
            'package_extract_file("payload/a.txt", "/system/bin/a.txt");\n'
            'format("ext4", "EMMC", "/dev/block/x", "0", "/system");\n'
        ),
    )
    with Ext4Image(android_like_ext4, readonly=False) as img:
        report = ZipFlasher(img).flash(pkg)
    assert any("format" in w for w in report.warnings)
    assert Ext4Image(android_like_ext4).exists("/bin/a.txt")


def test_edify_missing_payload_warns(android_like_ext4, tmp_path):
    pkg = make_flashable_zip(
        str(tmp_path / "incomplete.zip"),
        {},
        edify='package_extract_file("nope.txt", "/system/bin/nope.txt");\n',
    )
    with Ext4Image(android_like_ext4, readonly=False) as img:
        report = ZipFlasher(img).flash(pkg)
    assert report.warnings


# ---------------------------------------------------------------------------
# Magisk / shell packages
# ---------------------------------------------------------------------------

def test_magisk_payload_installs(android_like_ext4, magisk_zip):
    with Ext4Image(android_like_ext4, readonly=False) as img:
        report = ZipFlasher(img).flash(magisk_zip)
    assert report.package_type == "magisk"
    assert report.files_written == 3
    reopened = Ext4Image(android_like_ext4)
    assert reopened.read_file("/bin/magisk") == b"magisk binary\n"
    assert reopened.read_file("/lib64/magiskinit") == b"magiskinit binary\n"
    assert reopened.exists("/lib64/magisk_stub.apk")


def test_magisk_warns_about_init_hooks(android_like_ext4, magisk_zip):
    with Ext4Image(android_like_ext4, readonly=False) as img:
        report = ZipFlasher(img).flash(magisk_zip)
    assert any("init hooks" in w for w in report.warnings)


def test_stub_edify_falls_through_to_shell(android_like_ext4, tmp_path):
    """A package with a no-op edify script but a real payload still installs."""
    pkg = make_flashable_zip(
        str(tmp_path / "stub.zip"),
        {"system/bin/real": "payload\n"},
        edify='ui_print("stub");\n',  # does nothing
        update_binary="#!/sbin/sh\n",
    )
    with Ext4Image(android_like_ext4, readonly=False) as img:
        report = ZipFlasher(img).flash(pkg)
    assert report.files_written >= 1
    assert Ext4Image(android_like_ext4).exists("/bin/real")


def test_flat_payload_fallback(android_like_ext4, tmp_path):
    """A ZIP with only a system/ tree installs it at the image root."""
    pkg = make_flashable_zip(
        str(tmp_path / "flat.zip"),
        {"system/bin/plain": "plain\n"},
    )
    with Ext4Image(android_like_ext4, readonly=False) as img:
        report = ZipFlasher(img).flash(pkg)
    assert report.files_written == 1
    assert Ext4Image(android_like_ext4).exists("/bin/plain")


def test_flash_leaves_clean_filesystem(android_like_ext4, edify_zip):
    """Regression: flashing must leave an e2fsck-clean filesystem."""
    import subprocess
    with Ext4Image(android_like_ext4, readonly=False) as img:
        ZipFlasher(img).flash(edify_zip)
    proc = subprocess.run(["e2fsck", "-fn", android_like_ext4],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert proc.returncode == 0, proc.stdout.decode()


def test_flash_binary_payload_intact(android_like_ext4, tmp_path):
    """Binary content must survive the flash byte-for-byte."""
    blob = bytes(range(256)) * 40
    pkg_path = str(tmp_path / "binary.zip")
    with zipfile.ZipFile(pkg_path, "w") as zf:
        zf.writestr("system/lib64/libblob.so", blob)
        zf.writestr("META-INF/com/google/android/update-binary", "#!/sbin/sh\n")
    with Ext4Image(android_like_ext4, readonly=False) as img:
        ZipFlasher(img).flash(pkg_path)
    assert Ext4Image(android_like_ext4).read_file("/lib64/libblob.so") == blob


def test_flash_report_summary(android_like_ext4, edify_zip):
    with Ext4Image(android_like_ext4, readonly=False) as img:
        report = ZipFlasher(img).flash(edify_zip)
    text = report.summary()
    assert "package:" in text
    assert "type:" in text
    assert "files:" in text
