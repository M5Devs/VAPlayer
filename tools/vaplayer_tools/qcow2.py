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

"""qcow2 conversion helpers built on ``qemu-img``."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass

from .errors import ImageError
from .shell import require_tool, run, run_text


@dataclass
class ImageInfo:
    """The subset of ``qemu-img info`` output the tools care about."""

    path: str
    format: str
    virtual_size: int
    disk_size: int
    cluster_size: int | None = None
    backing_file: str | None = None

    @property
    def is_sparse(self) -> bool:
        """True when the on-disk file is meaningfully smaller than the virtual size."""
        return self.virtual_size > 0 and self.disk_size < self.virtual_size

    @property
    def compression_ratio(self) -> float:
        if self.virtual_size == 0:
            return 0.0
        return 1.0 - (self.disk_size / self.virtual_size)


def info(path: str) -> ImageInfo:
    """Return :class:`ImageInfo` for *path*."""
    require_tool("qemu-img")
    out = run_text(["qemu-img", "info", "--output=json", path])
    try:
        data = json.loads(out)
    except json.JSONDecodeError as exc:
        raise ImageError(f"could not parse qemu-img output for {path}: {exc}") from exc
    return ImageInfo(
        path=path,
        format=data.get("format", "unknown"),
        virtual_size=int(data.get("virtual-size", 0)),
        disk_size=int(data.get("actual-size", data.get("disk-size", 0))),
        cluster_size=data.get("cluster-size"),
        backing_file=data.get("backing-filename"),
    )


def convert(
    src: str,
    dest: str,
    *,
    fmt: str = "qcow2",
    compress: bool = True,
    sparse: bool = True,
    cluster_size: int | None = None,
    progress: bool = False,
) -> str:
    """Convert *src* into *dest* as ``fmt`` (qcow2 by default).

    ``compress`` produces a read-only compressed image; ``sparse`` (default)
    keeps unallocated regions unallocated so the result stays small. Pass
    ``compress=False`` when the image must be written to afterwards.
    """
    require_tool("qemu-img")
    cmd = ["qemu-img", "convert", "-f", "raw", "-O", fmt]
    if compress:
        cmd.append("-c")
    if sparse:
        cmd.append("-S")
        cmd.append("4k")
    if cluster_size:
        cmd += ["-o", f"cluster_size={cluster_size}"]
    cmd += [src, dest]
    if progress:
        cmd.insert(2, "-p")
    run(cmd)
    return dest


def create(
    path: str,
    size_bytes: int,
    *,
    fmt: str = "qcow2",
    backing: str | None = None,
    backing_fmt: str | None = None,
) -> str:
    """Create an empty image of *size_bytes*."""
    require_tool("qemu-img")
    cmd = ["qemu-img", "create", "-f", fmt]
    if backing:
        cmd += ["-b", backing]
        if backing_fmt:
            cmd += ["-F", backing_fmt]
    cmd += [path, str(size_bytes)]
    run(cmd)
    return path


def to_raw(qcow2_path: str, raw_path: str | None = None) -> str:
    """Flatten *qcow2_path* into a raw image, returning the raw path."""
    require_tool("qemu-img")
    if raw_path is None:
        raw_path = os.path.splitext(qcow2_path)[0] + ".raw"
    run(["qemu-img", "convert", "-f", "qcow2", "-O", "raw", qcow2_path, raw_path])
    return raw_path


def resize(path: str, size_bytes: int) -> None:
    """Resize an image to *size_bytes* (qcow2 only)."""
    require_tool("qemu-img")
    run(["qemu-img", "resize", path, str(size_bytes)])


def check(path: str) -> None:
    """Validate the image structure, raising :class:`ImageError` on corruption."""
    require_tool("qemu-img")
    proc = run(["qemu-img", "check", path], check=False)
    if proc.returncode != 0:
        err = (proc.stderr or b"").decode("utf-8", "replace").strip()
        raise ImageError(f"image check failed for {path}:\n{err}")
