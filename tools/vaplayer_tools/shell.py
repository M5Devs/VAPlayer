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

"""Thin subprocess helpers with consistent error handling and logging."""

from __future__ import annotations

import logging
import shutil
import subprocess
from typing import Sequence

from .errors import ToolNotFound, ToolsError

log = logging.getLogger("vaplayer")

# Hints printed when a tool is missing, keyed by executable name.
_TOOL_HINTS = {
    "qemu-img": "install qemu-utils (Debian/Ubuntu) or qemu (Arch/Fedora)",
    "debugfs": "install e2fsprogs",
    "mke2fs": "install e2fsprogs",
    "dumpe2fs": "install e2fsprogs",
    "resize2fs": "install e2fsprogs",
    "e2fsck": "install e2fsprogs",
    "unsquashfs": "install squashfs-tools",
    "mksquashfs": "install squashfs-tools",
    "simg2img": "install android-sdk-libsparse-utils",
    "img2simg": "install android-sdk-libsparse-utils",
    "xorriso": "install xorriso",
    "7z": "install p7zip-full",
    "unzip": "install unzip",
    "zip": "install zip",
    "cpio": "install cpio",
    "curl": "install curl",
    "wget": "install wget",
}


def which(tool: str) -> str | None:
    """Return the absolute path of *tool*, or ``None`` if it is not installed."""
    return shutil.which(tool)


def require_tool(tool: str) -> str:
    """Return the path of *tool*, raising :class:`ToolNotFound` if absent."""
    path = shutil.which(tool)
    if path is None:
        raise ToolNotFound(tool, _TOOL_HINTS.get(tool, ""))
    return path


def run(
    cmd: Sequence[str],
    *,
    check: bool = True,
    capture: bool = True,
    input_bytes: bytes | None = None,
    timeout: float | None = None,
    cwd: str | None = None,
) -> subprocess.CompletedProcess:
    """Run *cmd*, returning the completed process.

    ``check`` raises :class:`ToolsError` on a non-zero exit and folds the tail
    of stderr into the message so callers get an actionable error.
    """
    log.debug("run: %s", " ".join(str(c) for c in cmd))
    proc = subprocess.run(
        [str(c) for c in cmd],
        input=input_bytes,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
        timeout=timeout,
        cwd=cwd,
    )
    if check and proc.returncode != 0:
        err = (proc.stderr or b"").decode("utf-8", "replace").strip()
        out = (proc.stdout or b"").decode("utf-8", "replace").strip()
        detail = err or out or f"exit status {proc.returncode}"
        tail = "\n".join(detail.splitlines()[-8:])
        raise ToolsError(f"command failed: {' '.join(str(c) for c in cmd)}\n{tail}")
    return proc


def run_text(cmd: Sequence[str], **kwargs) -> str:
    """Run *cmd* and return stdout decoded as text."""
    proc = run(cmd, **kwargs)
    return (proc.stdout or b"").decode("utf-8", "replace")
