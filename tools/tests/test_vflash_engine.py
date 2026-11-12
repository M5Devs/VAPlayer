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

"""Tests for the V-Flash engine (inspect, tree, extract, flash, writeback)."""

from __future__ import annotations

import os

import pytest

from conftest import requires_e2fsprogs, requires_qemu_img
from vaplayer_tools.errors import ImageError
from vaplayer_tools.ext4 import Ext4Image
from vflash.engine import VFlashEngine

pytestmark = requires_e2fsprogs


@pytest.fixture
def engine():
    e = VFlashEngine(log=lambda _m: None)
    yield e
    e.close_all()


# ---------------------------------------------------------------------------
# Opening
# ---------------------------------------------------------------------------

def test_open_bare_ext4(engine, android_like_ext4):
    handle = engine.open(android_like_ext4)
    assert handle.container == "raw"
    assert handle.layout.ext4_offset == 0
    assert handle.fs.exists("/build.prop")
    engine.close(handle)


def test_open_missing_image(engine):
    with pytest.raises(ImageError):
        engine.open("/no/such/image.img")


def test_session_closes(engine, android_like_ext4):
    with engine.session(android_like_ext4) as handle:
        assert handle.fs.exists("/build.prop")
    assert engine._handles == []


def test_open_partitioned_disk(engine, gpt_disk):
    handle = engine.open(gpt_disk)
    assert handle.layout.ext4_offset != 0
    assert handle.partition == "system"
    assert handle.fs.exists("/build.prop")
    engine.close(handle)


@requires_qemu_img
def test_open_qcow2(engine, qcow2_image):
    handle = engine.open(qcow2_image)
    assert handle.container == "qcow2"
    assert handle.fs.exists("/build.prop")
    engine.close(handle)


# ---------------------------------------------------------------------------
# Inspect
# ---------------------------------------------------------------------------

def test_inspect_reports_filesystem(engine, android_like_ext4):
    result = engine.inspect(android_like_ext4)
    assert result["container"] == "raw"
    assert result["ext4_offset"] == 0
    fs = result["filesystem"]
    assert fs["label"] == "system"
    assert fs["block_size"] == 4096
    assert fs["size_bytes"] > 0
    assert "build.prop" in result["root_entries"]


def test_inspect_reports_android_props(engine, android_like_ext4):
    result = engine.inspect(android_like_ext4)
    assert result["android"]["ro.build.version.release"] == "9"
    assert result["android"]["ro.product.cpu.abi"] == "x86_64"


def test_inspect_reports_bridge_status(engine, android_like_ext4, ndk_source_dir):
    from vaplayer_tools import nativebridge
    nativebridge.inject(android_like_ext4, ndk_source_dir, profile="rog8")
    result = engine.inspect(android_like_ext4)
    bridge = result["native_bridge"]
    assert bridge["installed"] is True
    assert bridge["proxy_count"] == 4
    assert bridge["size"] > 0


def test_inspect_qcow2(engine, qcow2_image):
    result = engine.inspect(qcow2_image)
    assert result["container"] == "qcow2"


# ---------------------------------------------------------------------------
# Tree / extract / cat
# ---------------------------------------------------------------------------

def test_tree_renders_directories(engine, android_like_ext4):
    text = engine.tree(android_like_ext4, "/", max_depth=1)
    assert "lib64" in text
    assert "bin" in text
    assert "build.prop" in text


def test_extract_file(engine, android_like_ext4, tmp_path):
    dest = str(tmp_path / "out.prop")
    engine.extract(android_like_ext4, "/build.prop", dest)
    assert os.path.exists(dest)
    with open(dest, "rb") as fh:
        assert b"ro.build.version.release=9" in fh.read()


def test_extract_directory(engine, android_like_ext4, tmp_path):
    dest = str(tmp_path / "lib64")
    engine.extract(android_like_ext4, "/lib64", dest)
    assert os.path.isdir(dest)


def test_cat_returns_bytes(engine, android_like_ext4):
    data = engine.cat(android_like_ext4, "/build.prop")
    assert isinstance(data, bytes)
    assert b"ro.product.device=x86_64" in data


# ---------------------------------------------------------------------------
# Flashing
# ---------------------------------------------------------------------------

def test_flash_raw_image(engine, android_like_ext4, edify_zip):
    report = engine.flash(android_like_ext4, edify_zip)
    assert report.files_written >= 3
    assert Ext4Image(android_like_ext4).exists("/bin/hello.txt")


def test_flash_with_backup(engine, android_like_ext4, edify_zip, tmp_path):
    backup = str(tmp_path / "backup.img")
    engine.flash(android_like_ext4, edify_zip, backup=backup)
    assert os.path.exists(backup)
    # The backup predates the flash and must not contain the new file.
    assert not Ext4Image(backup).exists("/bin/hello.txt")
    assert Ext4Image(android_like_ext4).exists("/bin/hello.txt")


@requires_qemu_img
def test_flash_qcow2_persists(engine, qcow2_image, edify_zip):
    report = engine.flash(qcow2_image, edify_zip)
    assert report.files_written >= 3
    # Reopen: the change must be visible through the qcow2.
    reopened = engine.open(qcow2_image)
    assert reopened.fs.exists("/bin/hello.txt")
    engine.close(reopened)


@requires_qemu_img
def test_flash_qcow2_preserves_other_content(engine, qcow2_image, edify_zip):
    engine.flash(qcow2_image, edify_zip)
    reopened = engine.open(qcow2_image)
    assert reopened.fs.exists("/build.prop")
    assert b"ro.product.device=x86_64" in reopened.fs.read_file("/build.prop")
    engine.close(reopened)


def test_flash_partitioned_disk(engine, gpt_disk, edify_zip):
    report = engine.flash(gpt_disk, edify_zip)
    assert report.files_written >= 3
    # The partition table must survive the splice-back.
    reopened = engine.open(gpt_disk)
    assert reopened.layout.scheme == "gpt"
    assert reopened.fs.exists("/bin/hello.txt")
    engine.close(reopened)


# ---------------------------------------------------------------------------
# set_property
# ---------------------------------------------------------------------------

def test_set_property(engine, android_like_ext4):
    engine.set_property(android_like_ext4, "ro.test.key", "hello")
    bp = Ext4Image(android_like_ext4).read_file("/build.prop")
    assert b"ro.test.key=hello" in bp


def test_set_property_updates_existing(engine, android_like_ext4):
    engine.set_property(android_like_ext4, "ro.build.version.release", "14")
    bp = Ext4Image(android_like_ext4).read_file("/build.prop")
    assert bp.count(b"ro.build.version.release=") == 1
    assert b"ro.build.version.release=14" in bp


def test_flash_missing_package(engine, android_like_ext4):
    from vaplayer_tools.errors import FlashError
    with pytest.raises(FlashError):
        engine.flash(android_like_ext4, "/no/such.zip")


def test_to_qcow2(engine, android_like_ext4, tmp_path):
    if not __import__("shutil").which("qemu-img"):
        pytest.skip("qemu-img required")
    dest = str(tmp_path / "out.qcow2")
    engine.to_qcow2(android_like_ext4, dest)
    assert os.path.exists(dest)
