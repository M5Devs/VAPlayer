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

"""MBR/GPT partition table parsing and raw ext4 discovery.

Android disk images reach us in several shapes: a bare ext4 ``system.img``, a
GPT "super"/disk image containing a ``system`` partition, or an image with an
Android sparse header. This module answers the only question the higher layers
care about: *where does the ext4 filesystem start?*
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import BinaryIO

from .errors import ImageError

# Linux filesystem type GUIDs (mixed endianness as stored on disk).
_GUID_EXT4 = bytes.fromhex("af3dc60f838472478e793d69d8477de4")
_GUID_LINUX_FS = bytes.fromhex("0fc63daf848347478e793d69d8477de4")
_GUID_ANDROID_SYSTEM = bytes.fromhex("387d4d3d1d53464a8a1e8e5c9e6b8c9f")

_EXT4_MAGIC = 0xEF53
_EXT4_OFFSET = 0x438  # superblock magic lives 1080 bytes into the filesystem


@dataclass
class Partition:
    """One entry from a partition table."""

    index: int
    name: str
    start: int          # byte offset within the disk image
    size: int           # bytes
    type_guid: bytes = b""
    partition_type: str = "unknown"

    @property
    def end(self) -> int:
        return self.start + self.size

    def contains(self, offset: int) -> bool:
        return self.start <= offset < self.end


@dataclass
class DiskLayout:
    """The result of probing a disk image."""

    path: str
    total_size: int
    scheme: str = "none"                 # "gpt", "mbr", or "none"
    partitions: list[Partition] = field(default_factory=list)
    ext4_offset: int = 0                 # byte offset of the ext4 superblock
    notes: list[str] = field(default_factory=list)

    def find_partition(self, name: str) -> Partition | None:
        """Return the partition whose name matches *name* (case-insensitive)."""
        wanted = name.lower()
        for part in self.partitions:
            if part.name.lower() == wanted:
                return part
        return None

    def system_partition(self) -> Partition | None:
        """Return the most likely ``system`` partition, if any."""
        for candidate in ("system", "system_a", "super"):
            part = self.find_partition(candidate)
            if part is not None:
                return part
        return None


def _read_at(fh: BinaryIO, offset: int, length: int) -> bytes:
    fh.seek(offset)
    return fh.read(length)


def find_ext4_offset(fh: BinaryIO, size: int, hint: int = 0) -> int | None:
    """Scan for an ext4 superblock.

    Checks *hint* first, then the well-known offsets (1024, and the 2048-sector
    alignment used by Android), then walks the first few MiB. Returns the byte
    offset of the filesystem start or ``None``.
    """
    candidates = [hint, 1024, 512 * 2048, 0, 2048, 4096]
    seen: set[int] = set()
    for off in candidates:
        if off in seen or off < 0 or off + 0x438 + 2 > size:
            continue
        seen.add(off)
        sb = _read_at(fh, off + 0x400, 0x400)
        if len(sb) >= 0x38 + 2 and struct.unpack_from("<H", sb, 0x38)[0] == _EXT4_MAGIC:
            return off

    # Fallback: scan sector-aligned offsets across the first 8 MiB.
    limit = min(size, 8 * 1024 * 1024)
    for off in range(0, limit, 512):
        if off in seen:
            continue
        fh.seek(off + 0x438)
        if fh.read(2) == b"\x53\xef":
            return off
    return None


def parse_gpt(fh: BinaryIO, size: int) -> tuple[list[Partition], list[str]]:
    """Parse a GPT partition table at LBA 1. Returns (partitions, notes)."""
    header = _read_at(fh, 512, 92)
    if len(header) < 92 or header[:8] != b"EFI PART":
        return [], []
    notes: list[str] = []
    entry_lba, num_entries, entry_size = struct.unpack_from("<QII", header, 72)
    if entry_size == 0 or entry_size > 4096 or num_entries > 1024:
        raise ImageError(f"implausible GPT header (entries={num_entries}, size={entry_size})")

    parts: list[Partition] = []
    table = _read_at(fh, entry_lba * 512, num_entries * entry_size)
    for i in range(num_entries):
        entry = table[i * entry_size:(i + 1) * entry_size]
        if len(entry) < 128:
            break
        type_guid = entry[:16]
        if type_guid == b"\x00" * 16:
            continue
        first_lba, last_lba = struct.unpack_from("<QQ", entry, 32)
        name = entry[56:128].decode("utf-16-le", "replace").rstrip("\x00")
        ptype = "unknown"
        if type_guid in (_GUID_EXT4, _GUID_LINUX_FS):
            ptype = "ext4"
        elif type_guid == _GUID_ANDROID_SYSTEM:
            ptype = "system"
        parts.append(Partition(
            index=i + 1,
            name=name or f"part{i + 1}",
            start=first_lba * 512,
            size=(last_lba - first_lba + 1) * 512,
            type_guid=type_guid,
            partition_type=ptype,
        ))
    return parts, notes


def parse_mbr(fh: BinaryIO, size: int) -> list[Partition]:
    """Parse a classic DOS/MBR partition table. Returns the primary entries."""
    mbr = _read_at(fh, 0, 512)
    if len(mbr) < 512 or mbr[510:512] != b"\x55\xaa":
        return []
    parts: list[Partition] = []
    for i in range(4):
        base = 446 + i * 16
        ptype = mbr[base + 4]
        if ptype == 0:
            continue
        lba, count = struct.unpack_from("<II", mbr, base + 8)
        parts.append(Partition(
            index=i + 1,
            name=f"part{i + 1}",
            start=lba * 512,
            size=count * 512,
            partition_type="ext4" if ptype in (0x83, 0xEF) else "unknown",
        ))
    return parts


def probe(path: str) -> DiskLayout:
    """Probe *path* and describe its partition layout and ext4 location."""
    import os

    size = os.path.getsize(path)
    layout = DiskLayout(path=path, total_size=size)
    with open(path, "rb") as fh:
        gpt_parts, gpt_notes = parse_gpt(fh, size)
        layout.notes.extend(gpt_notes)
        if gpt_parts:
            layout.scheme = "gpt"
            layout.partitions = gpt_parts
        else:
            mbr_parts = parse_mbr(fh, size)
            if mbr_parts:
                layout.scheme = "mbr"
                layout.partitions = mbr_parts

        # Prefer an explicit system/ext4 partition; otherwise treat the image
        # as a bare filesystem.
        hint = 0
        syspart = layout.system_partition()
        if syspart is not None:
            hint = syspart.start
            layout.notes.append(
                f"using partition '{syspart.name}' at offset {syspart.start}")
        else:
            for part in layout.partitions:
                if part.partition_type == "ext4":
                    hint = part.start
                    layout.notes.append(
                        f"using ext4 partition {part.index} at offset {part.start}")
                    break

        offset = find_ext4_offset(fh, size, hint=hint)
        if offset is None:
            raise ImageError(f"no ext4 superblock found in {path}")
        layout.ext4_offset = offset
        if offset == 0:
            layout.notes.append("bare ext4 filesystem (no partition table)")
    return layout
