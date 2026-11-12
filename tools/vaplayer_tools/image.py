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

"""End-to-end image provisioning: download, unpack, assemble, convert.

An Android-x86 ISO is a bootable image whose system partition lives in a
SquashFS blob (``system.sfs``) that itself contains a raw ext4 ``system.img``.
Provisioning therefore runs: ISO -> ``system.sfs`` -> ``system.img`` ->
[injection] -> ``qcow2``.
"""

from __future__ import annotations

import os
import shutil
from dataclasses import dataclass, field

from . import qcow2
from .errors import ImageError, ToolsError
from .shell import require_tool, run, which

# Default, tested base images. All are x86_64 Android 9-11 builds.
KNOWN_IMAGES: dict[str, dict[str, str]] = {
    "android-x86-9": {
        "label": "Android-x86 9.0-r2 (x86_64, headless-friendly)",
        "url": "https://sourceforge.net/projects/android-x86/files/"
               "Release%209.0/android-x86_64-9.0-r2.iso/download",
        "kind": "iso",
        "android": "9",
    },
    "android-x86-8.1": {
        "label": "Android-x86 8.1-r6 (x86_64)",
        "url": "https://sourceforge.net/projects/android-x86/files/"
               "Release%208.1/android-x86_64-8.1-r6.iso/download",
        "kind": "iso",
        "android": "8.1",
    },
    "blissos-15": {
        "label": "BlissOS 15.8.6 vanilla (x86_64, Android 12L base)",
        "url": "https://sourceforge.net/projects/blissos-dev/files/Beta/"
               "Bliss-v15.8.6-x86_64-OFFICIAL-vanilla-20230628.iso/download",
        "kind": "iso",
        "android": "12",
    },
}

DEFAULT_IMAGE = "android-x86-9"


@dataclass
class UnpackedImage:
    """The parts recovered from a base image."""

    system_img: str
    kernel: str | None = None
    ramdisk: str | None = None
    initrd: str | None = None
    source: str = ""
    notes: list[str] = field(default_factory=list)

    def describe(self) -> str:
        size = os.path.getsize(self.system_img) if os.path.exists(self.system_img) else 0
        lines = [
            f"system.img:   {self.system_img} ({size} bytes, {size / 2**20:.1f} MiB)",
            f"kernel:       {self.kernel or '-'}",
            f"ramdisk:      {self.ramdisk or '-'}",
            f"initrd:       {self.initrd or '-'}",
        ]
        for note in self.notes:
            lines.append(f"note:         {note}")
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# Download
# ---------------------------------------------------------------------------

def resolve_image(name_or_url: str) -> tuple[str, dict[str, str]]:
    """Resolve a known image key or a raw URL to (url, metadata)."""
    if name_or_url in KNOWN_IMAGES:
        return KNOWN_IMAGES[name_or_url]["url"], KNOWN_IMAGES[name_or_url]
    if name_or_url.startswith(("http://", "https://")):
        return name_or_url, {"label": name_or_url, "url": name_or_url,
                             "kind": "iso", "android": "unknown"}
    known = ", ".join(sorted(KNOWN_IMAGES))
    raise ImageError(f"unknown image '{name_or_url}' (known: {known}, or pass a URL)")


def download(url: str, dest: str, *, log=lambda msg: None) -> str:
    """Download *url* to *dest*, resuming a partial file when possible."""
    os.makedirs(os.path.dirname(os.path.abspath(dest)) or ".", exist_ok=True)
    if shutil.which("curl"):
        # ``-C -`` resumes; SourceForge redirects are followed by ``-L``.
        run(["curl", "-fL", "-C", "-", "--retry", "3", "-o", dest, url])
    elif shutil.which("wget"):
        run(["wget", "-q", "-c", "-O", dest, url])
    else:
        raise ToolsError("neither curl nor wget is available to download images")
    log(f"downloaded {url} -> {dest}")
    return dest


# ---------------------------------------------------------------------------
# Unpack
# ---------------------------------------------------------------------------

def extract_from_iso(iso_path: str, workdir: str, *,
                     log=lambda msg: None) -> UnpackedImage:
    """Extract ``system.sfs`` (or ``system.img``) and kernel/ramdisk from an ISO."""
    os.makedirs(workdir, exist_ok=True)
    require_tool("xorriso")

    listing = _iso_listing(iso_path)
    wanted = [p for p in ("system.sfs", "system.img", "kernel", "ramdisk.img",
                          "initrd.img") if p in listing]
    if not wanted:
        raise ImageError(
            f"{iso_path} contains no system.sfs/system.img — not an Android-x86 ISO?")

    log(f"extracting from ISO: {', '.join(wanted)}")
    for name in wanted:
        run(["xorriso", "-osirrox", "on", "-indev", iso_path,
             "-extract", f"/{name}", os.path.join(workdir, name)])

    system_img = os.path.join(workdir, "system.img")
    notes: list[str] = []
    if "system.sfs" in wanted:
        sfs = os.path.join(workdir, "system.sfs")
        system_img = unpack_squashfs(sfs, workdir, log=log)
        notes.append("system extracted from SquashFS (system.sfs)")

    return UnpackedImage(
        system_img=system_img,
        kernel=_maybe(os.path.join(workdir, "kernel")),
        ramdisk=_maybe(os.path.join(workdir, "ramdisk.img")),
        initrd=_maybe(os.path.join(workdir, "initrd.img")),
        source=iso_path,
        notes=notes,
    )


def _iso_listing(iso_path: str) -> set[str]:
    """Return the top-level file names in an ISO image."""
    proc = run(["xorriso", "-indev", iso_path, "-find", "/", "-maxdepth", "1",
                "-type", "f"], check=False)
    text = (proc.stdout or b"").decode("utf-8", "replace")
    names = set()
    for line in text.splitlines():
        line = line.strip().strip("'")
        if line.startswith("/") and "/" not in line[1:]:
            names.add(line.lstrip("/"))
    return names


def unpack_squashfs(sfs_path: str, workdir: str, *,
                    log=lambda msg: None) -> str:
    """Extract the ``system.img`` held inside a SquashFS blob."""
    require_tool("unsquashfs")
    if not os.path.exists(sfs_path):
        raise ImageError(f"no such SquashFS image: {sfs_path}")
    os.makedirs(workdir, exist_ok=True)
    out = os.path.join(workdir, "sfs-root")
    log(f"unsquashfs {sfs_path}")
    run(["unsquashfs", "-f", "-d", out, sfs_path])

    # The archive holds either ``system.img`` at the root or under a folder.
    for dirpath, _dirs, files in os.walk(out):
        for name in files:
            if name in ("system.img", "system.raw.img", "system_a.img"):
                return os.path.join(dirpath, name)
    # A squashfs may instead hold a full system *tree*; repack it.
    if os.path.isdir(os.path.join(out, "system")):
        raise ImageError(
            "SquashFS holds a directory tree, not system.img; "
            "repack with mksquashfs after editing instead")
    raise ImageError(f"no system.img found inside {sfs_path}")


def _maybe(path: str) -> str | None:
    return path if os.path.exists(path) else None


# ---------------------------------------------------------------------------
# Assemble
# ---------------------------------------------------------------------------

def prepare_system_image(unpacked: UnpackedImage, workdir: str, *,
                         log=lambda msg: None) -> str:
    """Normalise the extracted system image to a raw, writable ext4 file.

    Android sparse images are converted to raw here so the ext4 layer can edit
    them; a plain ext4 image is passed through untouched.
    """
    from . import android as _android

    path = unpacked.system_img
    if _android.is_sparse(path):
        log("converting Android sparse image to raw")
        raw = os.path.join(workdir, "system.raw.img")
        path = _android.unsparse(path, raw)
        unpacked.notes.append("system image was Android-sparse; converted to raw")
    return path


def make_qcow2(raw_path: str, dest: str, *,
               compress: bool = False,
               log=lambda msg: None) -> str:
    """Convert a raw system image into a sparse qcow2 ready for vaplayer-core.

    ``compress`` produces a smaller, read-only image; leave it off for a base
    image that will be overlaid with a writable data disk at run time.
    """
    log(f"qemu-img convert -> {dest}")
    qcow2.convert(raw_path, dest, fmt="qcow2", compress=compress, sparse=True)
    return dest


def make_data_disk(dest: str, size_bytes: int, *,
                   log=lambda msg: None) -> str:
    """Create an empty writable qcow2 to serve as the guest ``/data`` disk."""
    log(f"create data disk {dest} ({size_bytes / 2**30:.1f} GiB)")
    return qcow2.create(dest, size_bytes, fmt="qcow2")


def verify_qcow2(path: str) -> qcow2.ImageInfo:
    """Check a qcow2 image and return its info."""
    qcow2.check(path)
    return qcow2.info(path)
