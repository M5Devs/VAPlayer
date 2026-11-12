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

"""Unit tests for the ext4 layer (real images, no mocks)."""

from __future__ import annotations


import pytest

from conftest import requires_e2fsprogs
from vaplayer_tools.errors import ImageError
from vaplayer_tools.ext4 import Ext4Image, create_filesystem

pytestmark = requires_e2fsprogs


def test_open_and_read_metadata(android_like_ext4):
    img = Ext4Image(android_like_ext4)
    info = img.info()
    assert info.block_size == 4096
    assert info.label == "system"
    assert info.size_bytes > 0
    assert info.inode_count > 0


def test_rejects_non_ext4(tmp_path):
    path = tmp_path / "notfs.img"
    path.write_bytes(b"\x00" * 4096)
    with pytest.raises(ImageError):
        Ext4Image(str(path))


def test_missing_file_raises(android_like_ext4):
    img = Ext4Image(android_like_ext4)
    with pytest.raises(ImageError):
        img.read_file("/does/not/exist")


def test_exists_and_types(android_like_ext4):
    img = Ext4Image(android_like_ext4)
    assert img.exists("/build.prop")
    assert img.file_type("/build.prop") == "regular"
    assert img.is_dir("/lib64")
    assert img.file_type("/lib64") == "directory"
    assert not img.exists("/nope")
    assert img.file_type("/nope") == "missing"


def test_read_file_roundtrip(android_like_ext4):
    img = Ext4Image(android_like_ext4)
    data = img.read_file("/build.prop")
    assert b"ro.build.version.release=9" in data


def test_listdir_and_walk(android_like_ext4):
    img = Ext4Image(android_like_ext4)
    root = img.listdir("/")
    assert "build.prop" in root
    assert "lib64" in root

    seen_dirs: set[str] = set()
    for dirpath, dirs, _files in img.walk("/"):
        seen_dirs.add(dirpath)
        assert isinstance(dirs, list)
    assert "/lib64" in seen_dirs
    assert "/etc" in seen_dirs


def test_write_file_creates_and_replaces(empty_ext4):
    with Ext4Image(empty_ext4, readonly=False) as img:
        img.write_file("/data/test.txt", b"first", mode=0o644)
        assert img.read_file("/data/test.txt") == b"first"
        img.write_file("/data/test.txt", b"second", mode=0o644)
        assert img.read_file("/data/test.txt") == b"second"
        img.flush()

    reopened = Ext4Image(empty_ext4)
    assert reopened.read_file("/data/test.txt") == b"second"


def test_write_file_preserves_type_bits(empty_ext4):
    """Regression: chmod must not clobber the inode file-type bits.

    ``debugfs set_inode_field mode`` writes the whole st_mode; setting only the
    permission bits turned regular files into "bad type" inodes, which e2fsck
    then cleared.
    """
    with Ext4Image(empty_ext4, readonly=False) as img:
        img.write_file("/bin/tool", b"#!/bin/sh\n", mode=0o755)
        img.flush()

    reopened = Ext4Image(empty_ext4)
    assert reopened.file_type("/bin/tool") == "regular"
    assert reopened.exists("/bin/tool")
    assert reopened.read_file("/bin/tool") == b"#!/bin/sh\n"


def test_directory_creation(empty_ext4):
    with Ext4Image(empty_ext4, readonly=False) as img:
        img.mkdir_p("/a/b/c")
        img.flush()
    reopened = Ext4Image(empty_ext4)
    assert reopened.is_dir("/a")
    assert reopened.is_dir("/a/b")
    assert reopened.is_dir("/a/b/c")


def test_copy_in_and_remove(tmp_path, empty_ext4):
    host = tmp_path / "payload.bin"
    host.write_bytes(b"\x01\x02\x03\x04" * 1024)
    with Ext4Image(empty_ext4, readonly=False) as img:
        img.copy_in(str(host), "/lib64/payload.bin", mode=0o644)
        assert img.read_file("/lib64/payload.bin") == host.read_bytes()
        img.rm("/lib64/payload.bin")
        assert not img.exists("/lib64/payload.bin")
        img.flush()


def test_symlink(empty_ext4):
    with Ext4Image(empty_ext4, readonly=False) as img:
        img.write_file("/bin/real", b"x", mode=0o755)
        img.symlink("/bin/real", "/bin/link")
        img.flush()
    reopened = Ext4Image(empty_ext4)
    assert reopened.file_type("/bin/link") == "symlink"


def test_readonly_rejects_writes(empty_ext4):
    img = Ext4Image(empty_ext4, readonly=True)
    with pytest.raises(ImageError):
        img.write_file("/x", b"y")


def test_write_survives_fsck(empty_ext4):
    """Edits must leave a filesystem that e2fsck considers clean."""
    with Ext4Image(empty_ext4, readonly=False) as img:
        img.write_file("/lib64/libfoo.so", b"\x7fELF" + b"\x00" * 100, mode=0o644)
        img.mkdir_p("/etc/init")
        img.write_file("/etc/init/foo.rc", b"on boot\n", mode=0o644)
        img.flush()

    # A second check should report no errors (non-interactive, exit 0).
    import subprocess
    proc = subprocess.run(["e2fsck", "-fn", empty_ext4],
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert proc.returncode == 0, proc.stdout.decode()


def test_create_filesystem_and_resize(tmp_path):
    path = str(tmp_path / "new.img")
    create_filesystem(path, 32 * 1024 * 1024, label="data")
    img = Ext4Image(path)
    assert img.info().label == "data"

    from vaplayer_tools.ext4 import resize
    resize(path, 64 * 1024 * 1024)
    assert Ext4Image(path).info().size_bytes >= 64 * 1024 * 1024


def test_create_filesystem_rejects_tiny(tmp_path):
    with pytest.raises(ImageError):
        create_filesystem(str(tmp_path / "tiny.img"), 1024)


def test_extract_filesystem_carve(tmp_path, gpt_disk):
    """Carving the system partition out of a GPT disk yields a valid fs."""
    from vaplayer_tools.ext4 import extract_filesystem
    from vaplayer_tools.partition import probe

    layout = probe(gpt_disk)
    assert layout.ext4_offset != 0
    dest = str(tmp_path / "carved.img")
    extract_filesystem(gpt_disk, layout.ext4_offset, dest=dest)
    carved = Ext4Image(dest)
    assert carved.exists("/build.prop")
    assert b"ro.build.version.release=9" in carved.read_file("/build.prop")
