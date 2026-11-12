# VAPlayer provisioning tools
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

"""Offline ext4 read/write via ``debugfs``.

``debugfs -w`` edits a filesystem directly, so no loop device or ``mount``
privilege is needed. That matters for CI containers (where loop mounts are
often unavailable) and for the host-side ``vflash`` engine, which must never
touch the running guest.

Every operation takes a ``suboffset``: the byte offset at which the ext4
filesystem begins inside the disk image. ``debugfs`` has no concept of a
partition table, so we present it a "slice" of the image through an on-demand
``/dev/fd``-style temporary view. To keep memory bounded on multi-gigabyte
images we use a small ``debugfs``-visible copy-on-write wrapper: the caller
supplies a path to a *filesystem-only* image produced by
:func:`extract_filesystem`.
"""

from __future__ import annotations

import os
import re
import tempfile
from dataclasses import dataclass, field
from typing import Iterable

from .errors import ImageError
from .shell import require_tool, run, run_text

_EXT4_MAGIC = 0xEF53

# st_mode file-type bits, keyed by the ``Type:`` word debugfs prints.
_TYPE_BITS = {
    "regular": 0o100000,
    "directory": 0o040000,
    "symlink": 0o120000,
    "character": 0o020000,
    "block": 0o060000,
    "fifo": 0o010000,
    "socket": 0o140000,
}


@dataclass
class FsInfo:
    """Summary of an ext4 filesystem, from its superblock."""

    block_size: int
    block_count: int
    inode_count: int
    free_blocks: int
    label: str = ""
    uuid: str = ""
    features: list[str] = field(default_factory=list)

    @property
    def size_bytes(self) -> int:
        return self.block_size * self.block_count


def _parse_dumpe2fs(text: str) -> FsInfo:
    def grab(pattern: str) -> str:
        m = re.search(pattern, text, re.MULTILINE | re.IGNORECASE)
        return m.group(1).strip() if m else ""

    block_size = int(grab(r"^Block size:\s+(\d+)") or 0)
    block_count = int(grab(r"^Block count:\s+(\d+)") or 0)
    inode_count = int(grab(r"^Inode count:\s+(\d+)") or 0)
    free_blocks = int(grab(r"^Free blocks:\s+(\d+)") or 0)
    features_raw = grab(r"^Filesystem features:\s+(.*)$")
    return FsInfo(
        block_size=block_size,
        block_count=block_count,
        inode_count=inode_count,
        free_blocks=free_blocks,
        label=grab(r"^Filesystem volume name:\s+(.*)$"),
        uuid=grab(r"^Filesystem UUID:\s+(\S+)"),
        features=features_raw.split() if features_raw else [],
    )


def fsck(path: str) -> None:
    """Run a filesystem check, repairing what it can (non-interactive)."""
    require_tool("e2fsck")
    run(["e2fsck", "-fy", path], check=False)


class Ext4Image:
    """An ext4 filesystem opened for offline inspection and editing.

    The filesystem must live at the start of *path* (use
    :func:`extract_filesystem` to carve one out of a partitioned disk).

    ``debugfs`` edits inodes and block bitmaps directly but does not keep the
    superblock's free-block/free-inode counters in step, so a writable image is
    reconciled with ``e2fsck -fy`` by :meth:`flush` (called automatically on
    context-manager exit).
    """

    def __init__(self, path: str, *, readonly: bool = True) -> None:
        self.path = path
        self.readonly = readonly
        self._dirty = False
        if not os.path.exists(path):
            raise ImageError(f"no such filesystem image: {path}")
        require_tool("debugfs")
        require_tool("dumpe2fs")
        self._check_superblock()

    def __enter__(self) -> "Ext4Image":
        return self

    def __exit__(self, *_exc) -> None:
        self.flush()

    def flush(self) -> None:
        """Reconcile the superblock counters after writes (no-op if clean)."""
        if self.readonly or not self._dirty:
            return
        fsck(self.path)
        self._dirty = False

    def _check_superblock(self) -> None:
        with open(self.path, "rb") as fh:
            fh.seek(0x438)
            magic = fh.read(2)
        if magic != b"\x53\xef":
            raise ImageError(
                f"{self.path} does not start with an ext4 superblock "
                "(carve the partition out first)")

    # -- metadata ---------------------------------------------------------
    def info(self) -> FsInfo:
        text = run_text(["dumpe2fs", "-h", self.path], check=False)
        info = _parse_dumpe2fs(text)
        if info.block_size == 0:
            raise ImageError(f"could not read superblock of {self.path}")
        return info

    # -- debugfs primitives ----------------------------------------------
    def _debugfs(self, command: str, *, write: bool = False) -> str:
        if write and self.readonly:
            raise ImageError(f"{self.path} is opened read-only")
        if write:
            self._dirty = True
        cmd = ["debugfs"]
        if write:
            cmd.append("-w")
        cmd += ["-R", command, self.path]
        return run_text(cmd)

    def exists(self, guest_path: str) -> bool:
        """True if *guest_path* exists inside the filesystem."""
        out = self._debugfs(f"stat {_q(guest_path)}")
        return "File not found" not in out and "Inode:" in out

    def is_dir(self, guest_path: str) -> bool:
        """True if *guest_path* is a directory."""
        out = self._debugfs(f"stat {_q(guest_path)}")
        if "File not found" in out or "Inode:" not in out:
            return False
        m = re.search(r"Type:\s+(\S+)", out)
        if m:
            return m.group(1).lower() == "directory"
        # Older debugfs versions omit Type: and print a typed mode instead.
        m = re.search(r"Mode:\s+(\S+)", out)
        if m:
            try:
                return bool(int(m.group(1), 8) & 0o170000 == 0o040000)
            except ValueError:
                return False
        return False

    def file_type(self, guest_path: str) -> str:
        """Return ``directory``, ``regular``, ``symlink``, or ``unknown``."""
        out = self._debugfs(f"stat {_q(guest_path)}")
        if "File not found" in out or "Inode:" not in out:
            return "missing"
        m = re.search(r"Type:\s+(\S+)", out)
        return m.group(1).lower() if m else "unknown"

    def listdir(self, guest_path: str) -> list[str]:
        """Return the names inside directory *guest_path*."""
        out = self._debugfs(f"ls -l {_q(guest_path)}")
        names: list[str] = []
        for line in out.splitlines():
            line = line.strip()
            # debugfs prints "<inode> <mode> <links> <uid> <gid> <size> <date...> <name>"
            if not line or line.startswith("debugfs") or line.startswith("Inode"):
                continue
            parts = line.split()
            if len(parts) >= 8:
                names.append(parts[-1])
        return [n for n in names if n not in (".", "..")]

    def read_file(self, guest_path: str) -> bytes:
        """Return the contents of a regular file."""
        if not self.exists(guest_path):
            raise ImageError(f"no such file in image: {guest_path}")
        # ``dump`` writes to a host file, which is binary-safe.
        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            tmp_path = tmp.name
        try:
            self._debugfs(f"dump -p {_q(guest_path)} {_q(tmp_path)}")
            with open(tmp_path, "rb") as fh:
                return fh.read()
        finally:
            os.unlink(tmp_path)

    # -- mutation ---------------------------------------------------------
    def mkdir(self, guest_path: str, mode: int = 0o755) -> None:
        """Create directory *guest_path* (parents are not created)."""
        if self.exists(guest_path):
            return
        parent = os.path.dirname(guest_path) or "/"
        if not self.is_dir(parent):
            raise ImageError(f"parent directory does not exist: {parent}")
        self._debugfs(f"mkdir {_q(guest_path)}", write=True)
        self.chmod(guest_path, mode)

    def mkdir_p(self, guest_path: str, mode: int = 0o755) -> None:
        """Create *guest_path* and any missing parents."""
        parts = [p for p in guest_path.split("/") if p]
        current = ""
        for part in parts:
            current += "/" + part
            if not self.exists(current):
                self.mkdir(current, mode)

    def chmod(self, guest_path: str, mode: int) -> None:
        """Set the permission bits of *guest_path*.

        ``debugfs``'s ``mode`` field holds the *whole* ``st_mode``, so the file
        type bits must be preserved or the inode becomes "bad type" and gets
        cleared by the next ``e2fsck``.
        """
        type_bits = _TYPE_BITS.get(self.file_type(guest_path), 0o100000)
        self._debugfs(
            f"set_inode_field {_q(guest_path)} mode 0{type_bits | (mode & 0o7777):o}",
            write=True)

    def chown(self, guest_path: str, uid: int, gid: int) -> None:
        self._debugfs(f"set_inode_field {_q(guest_path)} uid {uid}", write=True)
        self._debugfs(f"set_inode_field {_q(guest_path)} gid {gid}", write=True)

    def symlink(self, target: str, link_path: str) -> None:
        """Create a symbolic link at *link_path* pointing to *target*."""
        if self.exists(link_path):
            self.rm(link_path)
        self._debugfs(f"symlink {_q(link_path)} {_q(target)}", write=True)

    def write_file(self, guest_path: str, data: bytes, mode: int = 0o644) -> None:
        """Write *data* to *guest_path*, creating or replacing it."""
        if self.is_dir(guest_path):
            raise ImageError(f"cannot overwrite directory: {guest_path}")
        self.mkdir_p(os.path.dirname(guest_path) or "/")
        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            tmp.write(data)
            tmp_path = tmp.name
        try:
            if self.exists(guest_path):
                self.rm(guest_path)
            self._debugfs(f"write {_q(tmp_path)} {_q(guest_path)}", write=True)
        finally:
            os.unlink(tmp_path)
        self.chmod(guest_path, mode)

    def copy_in(self, host_path: str, guest_path: str, mode: int = 0o644) -> None:
        """Copy a host file into the image."""
        self.mkdir_p(os.path.dirname(guest_path) or "/")
        if self.exists(guest_path):
            self.rm(guest_path)
        self._debugfs(f"write {_q(host_path)} {_q(guest_path)}", write=True)
        self.chmod(guest_path, mode)

    def rm(self, guest_path: str) -> None:
        if not self.exists(guest_path):
            return
        self._debugfs(f"rm {_q(guest_path)}", write=True)

    def rm_rf(self, guest_path: str) -> None:
        if self.is_dir(guest_path):
            for name in self.listdir(guest_path):
                self.rm_rf(f"{guest_path}/{name}")
            self._debugfs(f"rmdir {_q(guest_path)}", write=True)
        else:
            self.rm(guest_path)

    def walk(self, root: str = "/") -> Iterable[tuple[str, list[str], list[str]]]:
        """Yield ``(dirpath, dirnames, filenames)`` like :func:`os.walk`."""
        dirs = [d for d in self.listdir(root) if self.is_dir(f"{root}/{d}".replace("//", "/"))]
        files = [f for f in self.listdir(root) if not self.is_dir(f"{root}/{f}".replace("//", "/"))]
        yield root, dirs, files
        for d in dirs:
            child = f"{root}/{d}".replace("//", "/")
            yield from self.walk(child)


def _q(path: str) -> str:
    """Quote a path for a debugfs command line."""
    if not path.startswith("/"):
        path = "/" + path
    if any(c in path for c in " \t"):
        return '"' + path.replace('"', '\\"') + '"'
    return path


def extract_filesystem(
    disk_path: str,
    offset: int,
    *,
    length: int | None = None,
    dest: str | None = None,
) -> str:
    """Carve the filesystem at *offset* out of *disk_path* into a new file.

    Returns the path to the extracted filesystem. When *offset* is 0 the
    original path is returned unchanged (no copy needed).
    """
    if offset == 0:
        return disk_path
    size = os.path.getsize(disk_path)
    if length is None:
        length = size - offset
    if dest is None:
        fd, dest = tempfile.mkstemp(prefix="vaplayer-fs-", suffix=".img")
        os.close(fd)
    # Copy in 8 MiB chunks so a 3 GB system partition never lands in memory.
    with open(disk_path, "rb") as src, open(dest, "wb") as out:
        src.seek(offset)
        remaining = length
        while remaining > 0:
            chunk = src.read(min(8 * 1024 * 1024, remaining))
            if not chunk:
                break
            out.write(chunk)
            remaining -= len(chunk)
    return dest


def create_filesystem(
    path: str,
    size_bytes: int,
    *,
    label: str = "system",
    block_size: int = 4096,
) -> str:
    """Create an empty ext4 filesystem at *path* of *size_bytes*.

    Used by the test fixtures and by ``vflash`` when it must grow an image.
    """
    require_tool("mke2fs")
    if size_bytes < 16 * 1024 * 1024:
        raise ImageError("ext4 filesystem must be at least 16 MiB")
    with open(path, "wb") as fh:
        fh.truncate(size_bytes)
    run([
        "mke2fs", "-q", "-t", "ext4", "-F",
        "-b", str(block_size),
        "-L", label,
        "-O", "^has_journal",
        path,
    ])
    return path


def resize(path: str, size_bytes: int) -> None:
    """Grow an ext4 filesystem to *size_bytes*."""
    require_tool("resize2fs")
    with open(path, "r+b") as fh:
        fh.truncate(size_bytes)
    run(["resize2fs", path])


