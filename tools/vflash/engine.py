# VAPlayer V-Flash core
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

"""The V-Flash engine: open, inspect, and modify Android disk images offline.

Supported inputs
----------------
* a bare ext4 ``system.img`` (filesystem at offset 0),
* a raw disk image carrying an MBR/GPT partition table,
* a ``qcow2`` image (flattened to raw for editing, then repacked).

Editing is performed with ``debugfs`` against the raw filesystem, so no loop
device, ``mount``, or root privilege is required — which is what makes V-Flash
usable on a host that is simultaneously running the emulator.
"""

from __future__ import annotations

import os
import shutil
import tempfile
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Iterator

from vaplayer_tools import android as android_mod
from vaplayer_tools import qcow2
from vaplayer_tools.errors import FlashError, ImageError
from vaplayer_tools.ext4 import Ext4Image, extract_filesystem
from vaplayer_tools.flashzip import ZipFlasher, FlashReport, is_flashable
from vaplayer_tools.partition import DiskLayout, probe


@dataclass
class ImageHandle:
    """An open image and the filesystem layer V-Flash works on."""

    path: str                     # the image the user named
    layout: DiskLayout
    fs_path: str                  # raw filesystem file (may be a temp copy)
    fs: Ext4Image
    container: str = "raw"        # "raw" or "qcow2"
    partition: str = ""           # partition name used, if any
    raw_path: str = ""            # flattened raw container (qcow2 inputs)
    _tempdir: str | None = None

    @property
    def writable(self) -> bool:
        return not self.fs.readonly


class VFlashEngine:
    """Opens images and applies flashable packages to them."""

    def __init__(self, *, keep_temp: bool = False,
                 log=lambda msg: None) -> None:
        self.keep_temp = keep_temp
        self.log = log
        self._handles: list[ImageHandle] = []

    # -- open / close -----------------------------------------------------
    def open(self, image: str, *, readonly: bool = True,
             partition: str | None = None) -> ImageHandle:
        """Open *image* and return a handle to its system filesystem."""
        if not os.path.exists(image):
            raise ImageError(f"no such image: {image}")

        container = self._detect_container(image)
        tempdir: str | None = None
        raw = image

        if container == "qcow2":
            tempdir = tempfile.mkdtemp(prefix="vflash-")
            raw = os.path.join(tempdir, "flattened.raw")
            self.log(f"flattening qcow2 {image} -> {raw}")
            qcow2.to_raw(image, raw)

        # A bare ext4 image has its superblock at offset 0; probe() reports it.
        try:
            layout = probe(raw)
        except ImageError:
            if tempdir:
                shutil.rmtree(tempdir, ignore_errors=True)
            raise

        fs_path = raw
        part_name = ""
        if layout.ext4_offset != 0:
            fs_path = os.path.join(
                tempdir or tempfile.mkdtemp(prefix="vflash-fs-"),
                "partition.img")
            if tempdir is None:
                tempdir = os.path.dirname(fs_path)
            length = layout.total_size - layout.ext4_offset
            syspart = layout.system_partition()
            if syspart is not None:
                length = syspart.size
                part_name = syspart.name
            self.log(f"carving filesystem at offset {layout.ext4_offset}")
            extract_filesystem(raw, layout.ext4_offset, length=length, dest=fs_path)

        fs = Ext4Image(fs_path, readonly=readonly)
        handle = ImageHandle(
            path=image, layout=layout, fs_path=fs_path, fs=fs,
            container=container, partition=part_name,
            raw_path=raw, _tempdir=tempdir,
        )
        self._handles.append(handle)
        return handle

    @staticmethod
    def _detect_container(image: str) -> str:
        with open(image, "rb") as fh:
            magic = fh.read(4)
        if magic == b"QFI\xfb":
            return "qcow2"
        if magic == b"KDMV":
            return "vmdk"
        return "raw"

    def close(self, handle: ImageHandle) -> None:
        """Close *handle*, optionally cleaning up temporary files."""
        if handle in self._handles:
            self._handles.remove(handle)
        if handle._tempdir and not self.keep_temp:
            shutil.rmtree(handle._tempdir, ignore_errors=True)
            handle._tempdir = None

    def close_all(self) -> None:
        for handle in list(self._handles):
            self.close(handle)

    @contextmanager
    def session(self, image: str, *, readonly: bool = True) -> Iterator[ImageHandle]:
        """Context manager that opens *image* and always closes it."""
        handle = self.open(image, readonly=readonly)
        try:
            yield handle
        finally:
            self.close(handle)

    # -- inspection -------------------------------------------------------
    def inspect(self, image: str) -> dict[str, object]:
        """Return a structured description of *image*."""
        with self.session(image) as handle:
            fs = handle.fs
            info = fs.info()
            root_entries = fs.listdir("/")
            result: dict[str, object] = {
                "image": image,
                "container": handle.container,
                "partition_scheme": handle.layout.scheme,
                "partition_used": handle.partition or "(bare filesystem)",
                "ext4_offset": handle.layout.ext4_offset,
                "filesystem": {
                    "label": info.label,
                    "uuid": info.uuid,
                    "block_size": info.block_size,
                    "size_bytes": info.size_bytes,
                    "inode_count": info.inode_count,
                    "free_blocks": info.free_blocks,
                    "features": info.features,
                },
                "root_entries": sorted(root_entries),
            }
            build_prop = "/build.prop" if fs.exists("/build.prop") else None
            if build_prop:
                bp = android_mod.BuildProp(fs.read_file(build_prop))
                result["android"] = android_mod.summary(bp)
            bridge = self._bridge_status(fs)
            result["native_bridge"] = bridge
            return result

    @staticmethod
    def _bridge_status(fs: Ext4Image) -> dict[str, object]:
        for lib64 in ("/lib64", "/system/lib64"):
            if fs.is_dir(lib64):
                so = f"{lib64}/libndk_translation.so"
                present = fs.exists(so)
                proxies = [n for n in fs.listdir(lib64)
                           if n.startswith("libndk_translation_proxy_")]
                return {
                    "installed": present,
                    "library": so if present else None,
                    "size": len(fs.read_file(so)) if present else 0,
                    "proxy_count": len(proxies),
                }
        return {"installed": False, "library": None, "size": 0, "proxy_count": 0}

    def tree(self, image: str, guest_path: str = "/", *, max_depth: int = 2) -> str:
        """Render a directory tree of *guest_path* as text."""
        with self.session(image) as handle:
            fs = handle.fs
            lines: list[str] = [guest_path]

            def walk(path: str, prefix: str, depth: int) -> None:
                if depth > max_depth:
                    return
                try:
                    names = sorted(fs.listdir(path))
                except ImageError:
                    return
                dirs = [n for n in names if fs.is_dir(f"{path.rstrip('/')}/{n}")]
                files = [n for n in names if n not in dirs]
                entries = [(d, True) for d in dirs] + [(f, False) for f in files]
                for i, (name, is_dir) in enumerate(entries):
                    last = i == len(entries) - 1
                    branch = "└── " if last else "├── "
                    lines.append(prefix + branch + name + ("/" if is_dir else ""))
                    if is_dir:
                        walk(f"{path.rstrip('/')}/{name}",
                             prefix + ("    " if last else "│   "), depth + 1)

            walk(guest_path, "", 1)
            return "\n".join(lines)

    def extract(self, image: str, guest_path: str, dest: str) -> str:
        """Copy *guest_path* out of the image to host path *dest*."""
        with self.session(image) as handle:
            fs = handle.fs
            if fs.is_dir(guest_path):
                os.makedirs(dest, exist_ok=True)
                for dirpath, dirs, files in fs.walk(guest_path):
                    rel = os.path.relpath(dirpath, guest_path)
                    out_dir = dest if rel == "." else os.path.join(dest, rel)
                    os.makedirs(out_dir, exist_ok=True)
                    for name in files:
                        data = fs.read_file(f"{dirpath.rstrip('/')}/{name}")
                        with open(os.path.join(out_dir, name), "wb") as fh:
                            fh.write(data)
            else:
                data = fs.read_file(guest_path)
                os.makedirs(os.path.dirname(os.path.abspath(dest)) or ".", exist_ok=True)
                with open(dest, "wb") as fh:
                    fh.write(data)
            return dest

    def cat(self, image: str, guest_path: str) -> bytes:
        """Return the bytes of a file inside the image."""
        with self.session(image) as handle:
            return handle.fs.read_file(guest_path)

    # -- flashing ---------------------------------------------------------
    def flash(self, image: str, package: str, *,
              backup: str | None = None) -> FlashReport:
        """Flash *package* into the writable system filesystem of *image*.

        The image is opened read-write. For qcow2 inputs the edited filesystem
        is written back into the qcow2. When *backup* is given the original
        image is copied there first, before any modification.
        """
        if not os.path.exists(package):
            raise FlashError(f"no such package: {package}")
        if not is_flashable(package):
            self.log(f"warning: {package} is not a standard flashable ZIP; "
                     "attempting a directory-payload install")

        # Copy the pristine image first: raw filesystem edits land in place.
        if backup:
            shutil.copy2(image, backup)
            self.log(f"backup written to {backup}")

        handle = self.open(image, readonly=False)
        try:
            flasher = ZipFlasher(handle.fs)
            report = flasher.flash(package)
            self._writeback(handle, image)
            return report
        finally:
            self.close(handle)

    def _writeback(self, handle: ImageHandle, original: str) -> None:
        """Persist edits made to ``handle.fs_path`` back into *original*.

        Three cases:

        * bare raw filesystem — edits landed in the file itself, nothing to do;
        * partitioned raw image — splice the edited filesystem back over its
          partition so the table and sibling partitions survive;
        * qcow2 — splice into the flattened raw container, then reconvert.
        """
        needs_splice = (handle.layout.ext4_offset != 0
                        and handle.fs_path != handle.raw_path)
        if not needs_splice and handle.container != "qcow2":
            return

        if needs_splice:
            self.log(f"splicing filesystem back at offset "
                     f"{handle.layout.ext4_offset}")
            with open(handle.fs_path, "rb") as src, \
                    open(handle.raw_path, "r+b") as dst:
                dst.seek(handle.layout.ext4_offset)
                shutil.copyfileobj(src, dst, length=8 * 1024 * 1024)

        if handle.container == "qcow2":
            self.log(f"repacking {handle.raw_path} -> {original} (qcow2)")
            qcow2.convert(handle.raw_path, original, fmt="qcow2",
                          compress=False, sparse=True)

    # -- provisioning passthroughs ---------------------------------------
    def set_property(self, image: str, key: str, value: str) -> None:
        """Set a single ``build.prop`` property in the image."""
        handle = self.open(image, readonly=False)
        try:
            bp = android_mod.BuildProp(handle.fs.read_file("/build.prop"))
            bp.set(key, value)
            handle.fs.write_file("/build.prop", bp.to_bytes(), mode=0o600)
            self._writeback(handle, image)
        finally:
            self.close(handle)

    def to_qcow2(self, raw_image: str, dest: str) -> str:
        """Convert a raw image to qcow2."""
        return qcow2.convert(raw_image, dest, fmt="qcow2")
