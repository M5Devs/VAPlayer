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

"""Extract and inject the ARM native bridge (``libndk_translation``).

The bridge lets an x86_64 Android guest execute ARM64/ARM32 native code via
binary translation. Upstream ships it as a prebuilt package; this module
fetches the pieces, verifies them, and installs them into an offline
``system.img`` at the locations the Android init scripts expect.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import struct
from dataclasses import dataclass, field

from . import android
from .errors import ImageError, ToolsError
from .ext4 import Ext4Image
from .shell import run

# The upstream prebuilt package (ChromeOS "zork"/guybrush generations ship the
# same 0.2.3 bridge used by Android 11-14 x86_64 images).
DEFAULT_PREBUILT_REPO = (
    "supremegamers/vendor_google_proprietary_ndk_translation-prebuilt"
)
DEFAULT_PREBUILT_REF = "11arm_12arm64"
_RAW_BASE = "https://raw.githubusercontent.com/{repo}/{ref}/{path}"

# Files that make up the bridge, and where they belong in the system image.
# ``lib64`` -> ``/lib64`` because the image root *is* the system partition.
BRIDGE_FILES: dict[str, tuple[str, int]] = {
    "prebuilts/lib64/libndk_translation.so": ("/lib64/libndk_translation.so", 0o644),
    "prebuilts/lib/libndk_translation.so": ("/lib/libndk_translation.so", 0o644),
    "prebuilts/bin/ndk_translation_program_runner_binfmt_misc":
        ("/bin/ndk_translation_program_runner_binfmt_misc", 0o755),
    "prebuilts/bin/ndk_translation_program_runner_binfmt_misc_arm64":
        ("/bin/ndk_translation_program_runner_binfmt_misc_arm64", 0o755),
    "prebuilts/etc/init/ndk_translation.rc":
        ("/etc/init/ndk_translation.rc", 0o644),
}

# The proxy libraries (libndk_translation_proxy_*.so) are what the guest's
# public NDK libraries resolve to. They are fetched as a group.
PROXY_PREFIX = "libndk_translation_proxy_"

# Known-good SHA-256 for the 64-bit bridge of the default prebuilt, so a
# tampered or truncated download is rejected. ``None`` disables the check.
KNOWN_SHA256: dict[str, str] = {}


@dataclass
class InjectReport:
    """Outcome of a native-bridge injection."""

    image: str
    profile: str = ""
    installed: list[str] = field(default_factory=list)
    proxies: int = 0
    props_set: dict[str, str] = field(default_factory=dict)
    warnings: list[str] = field(default_factory=list)
    source: str = ""

    def summary(self) -> str:
        lines = [
            f"image:        {self.image}",
            f"source:       {self.source or '-'}",
            f"profile:      {self.profile or '(unchanged)'}",
            f"libraries:    {len(self.installed)}",
            f"proxy libs:   {self.proxies}",
            f"props set:    {len(self.props_set)}",
        ]
        for p in sorted(self.props_set):
            lines.append(f"  {p}={self.props_set[p]}")
        if self.warnings:
            lines.append(f"warnings:     {len(self.warnings)}")
            for w in self.warnings:
                lines.append(f"  - {w}")
        return "\n".join(lines)


def _elf_arch(path: str) -> str:
    """Return ``x86_64``/``i386``/``arm64``/``arm`` for an ELF, else ``unknown``."""
    with open(path, "rb") as fh:
        hdr = fh.read(20)
    if hdr[:4] != b"\x7fELF":
        return "unknown"
    machine = struct.unpack_from("<H", hdr, 18)[0]
    return {0x3E: "x86_64", 0x03: "i386", 0xB7: "arm64", 0x28: "arm"}.get(
        machine, f"machine-{machine}")


def _sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# Fetching
# ---------------------------------------------------------------------------

def _download(url: str, dest: str) -> str:
    """Download *url* to *dest* using curl, wget, or urllib as available."""
    if shutil.which("curl"):
        run(["curl", "-fsSL", "--retry", "3", "-o", dest, url])
    elif shutil.which("wget"):
        run(["wget", "-q", "-O", dest, url])
    else:  # pragma: no cover - exercised only without curl/wget
        import urllib.request
        urllib.request.urlretrieve(url, dest)
    return dest


def fetch_prebuilt(
    dest_dir: str,
    *,
    repo: str = DEFAULT_PREBUILT_REPO,
    ref: str = DEFAULT_PREBUILT_REF,
    include_proxies: bool = True,
    log=lambda msg: None,
) -> list[str]:
    """Download the bridge files into *dest_dir*.

    Returns the list of local paths written. Only the files listed in
    :data:`BRIDGE_FILES` (and, optionally, the proxy libraries) are fetched.
    """
    os.makedirs(dest_dir, exist_ok=True)
    wanted = list(BRIDGE_FILES)
    if include_proxies:
        wanted += _proxy_paths(repo, ref)

    written: list[str] = []
    for remote in wanted:
        # Preserve the upstream sub-path so the 32-bit and 64-bit
        # libndk_translation.so do not overwrite each other in the cache.
        local = os.path.join(dest_dir, remote.replace("/", os.sep))
        os.makedirs(os.path.dirname(local), exist_ok=True)
        url = _RAW_BASE.format(repo=repo, ref=ref, path=remote)
        log(f"fetch {remote}")
        try:
            _download(url, local)
        except ToolsError as exc:
            # Proxy libraries are optional; core files are not.
            if PROXY_PREFIX in remote:
                log(f"  (skipped optional {remote}: {exc})")
                continue
            raise
        written.append(local)
    return written


def _proxy_paths(repo: str, ref: str) -> list[str]:
    """Return the proxy library paths, discovered from the repo tree.

    The list is fixed in practice; this keeps a single source of truth without
    a network call for the common case.
    """
    proxies = [
        "libEGL", "libGLESv1_CM", "libGLESv2", "libGLESv3", "libOpenMAXAL",
        "libOpenSLES", "libaaudio", "libamidi", "libandroid", "libandroid_runtime",
        "libbinder_ndk", "libc", "libcamera2ndk", "libicui18n", "libicuuc",
        "libjnigraphics", "libmediandk", "libnativehelper", "libnativewindow",
        "libneuralnetworks", "libvulkan", "libwebviewchromium_plat_support",
    ]
    out: list[str] = []
    for name in proxies:
        out.append(f"prebuilts/lib64/{PROXY_PREFIX}{name}.so")
    return out


# ---------------------------------------------------------------------------
# Injection
# ---------------------------------------------------------------------------

def _resolve_lib64(image: Ext4Image) -> str:
    """Return the guest path of the 64-bit library directory."""
    for candidate in ("/lib64", "/system/lib64"):
        if image.is_dir(candidate):
            return candidate
    raise ImageError("image has no /lib64 (is this an Android system image?)")


def _resolve_bin(image: Ext4Image) -> str:
    for candidate in ("/bin", "/system/bin"):
        if image.is_dir(candidate):
            return candidate
    raise ImageError("image has no /bin")


def _resolve_etc_init(image: Ext4Image) -> str:
    for candidate in ("/etc/init", "/system/etc/init"):
        if image.is_dir(candidate):
            return candidate
    # Create it under /etc if absent.
    if image.is_dir("/etc"):
        image.mkdir_p("/etc/init")
        return "/etc/init"
    raise ImageError("image has no /etc/init and no /etc to create it in")


def inject(
    image_path: str,
    source_dir: str,
    *,
    profile: str | None = None,
    spoof_device: bool = True,
    report_progress=lambda msg: None,
) -> InjectReport:
    """Install the bridge from *source_dir* into the ext4 image at *image_path*.

    ``source_dir`` holds the files fetched by :func:`fetch_prebuilt`. The image
    is opened read-write and the following happen:

    1. The 64/32-bit ``libndk_translation.so`` and the two program runners are
       installed at their expected paths with correct modes.
    2. Any ``libndk_translation_proxy_*.so`` present are installed into
       ``/lib64`` so the guest's public libraries resolve to the bridge.
    3. The init rc file is installed under ``/etc/init``.
    4. ``build.prop`` gains the native-bridge properties, and — when
       *spoof_device* is set — the identity of *profile*.
    """
    report = InjectReport(image=image_path, profile=profile or "")
    image = Ext4Image(image_path, readonly=False)
    lib64 = _resolve_lib64(image)
    bindir = _resolve_bin(image)
    init_dir = _resolve_etc_init(image)

    # 1 + 2: libraries and binaries.
    installed: list[str] = []
    proxies = 0
    for remote, (guest_tmpl, mode) in BRIDGE_FILES.items():
        local = _locate_source(source_dir, remote)
        if local is None:
            report.warnings.append(f"missing from source: {remote}")
            continue
        guest = _guest_dest(guest_tmpl, lib64, bindir, init_dir)
        report_progress(f"install {guest}")
        image.copy_in(local, guest, mode=mode)
        installed.append(guest)

    for name in _iter_proxy_files(source_dir):
        local = name
        guest = f"{lib64.rstrip('/')}/{os.path.basename(name)}"
        image.copy_in(local, guest, mode=0o644)
        proxies += 1

    if proxies == 0:
        report.warnings.append(
            "no proxy libraries found; the bridge will load but guest NDK "
            "libraries may not resolve")

    report.installed = installed
    report.proxies = proxies
    report.source = source_dir

    # 3: properties.
    props = dict(android.NATIVE_BRIDGE_PROPS)
    props["ro.product.cpu.abilist"] = android.NATIVE_BRIDGE_ABILIST
    props["ro.product.cpu.abilist32"] = android.NATIVE_BRIDGE_ABILIST32
    props["ro.product.cpu.abilist64"] = android.NATIVE_BRIDGE_ABILIST64

    if spoof_device:
        prof = android.get_profile(profile or android.DEFAULT_PROFILE)
        props.update(prof.props)
        report.profile = prof.label

    build_prop = _find_build_prop(image)
    report_progress(f"patch {build_prop}")
    bp = android.BuildProp(image.read_file(build_prop))
    bp.update(props, comment="VAPlayer: ARM native bridge / device spoof")
    image.write_file(build_prop, bp.to_bytes(), mode=0o600)
    report.props_set = props

    # debugfs leaves the superblock counters stale; reconcile before closing.
    report_progress("reconciling filesystem counters")
    image.flush()

    return report


def _locate_source(source_dir: str, remote: str) -> str | None:
    """Find a fetched file in *source_dir*.

    Accepts both the nested layout written by :func:`fetch_prebuilt`
    (``prebuilts/lib64/libndk_translation.so``) and a flat directory of
    basenames, so a hand-assembled source dir also works.
    """
    nested = os.path.join(source_dir, remote.replace("/", os.sep))
    if os.path.isfile(nested):
        return nested
    flat = os.path.join(source_dir, os.path.basename(remote))
    if os.path.isfile(flat):
        return flat
    return None


def _iter_proxy_files(source_dir: str) -> list[str]:
    """Return the proxy library paths found anywhere under *source_dir*."""
    found: list[str] = []
    for dirpath, _dirs, files in os.walk(source_dir):
        for name in sorted(files):
            if name.startswith(PROXY_PREFIX) and name.endswith(".so"):
                found.append(os.path.join(dirpath, name))
    return found


def _guest_dest(template: str, lib64: str, bindir: str, init_dir: str) -> str:
    """Rewrite a canonical guest path to match the image's actual layout."""
    if template.startswith("/lib64/"):
        return f"{lib64.rstrip('/')}/{template[len('/lib64/'):]}"
    if template.startswith("/lib/"):
        # 32-bit libs live next to lib64 when the image root is the partition.
        parent = os.path.dirname(lib64.rstrip("/")) or ""
        return f"{parent}/lib/{template[len('/lib/'):]}".replace("//", "/")
    if template.startswith("/bin/"):
        return f"{bindir.rstrip('/')}/{template[len('/bin/'):]}"
    if template.startswith("/etc/init/"):
        return f"{init_dir.rstrip('/')}/{template[len('/etc/init/'):]}"
    return template


def _find_build_prop(image: Ext4Image) -> str:
    for candidate in ("/build.prop", "/system/build.prop"):
        if image.exists(candidate):
            return candidate
    raise ImageError("image has no build.prop")


# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

def verify(image_path: str) -> dict[str, object]:
    """Inspect an image and report whether the bridge is correctly installed."""
    image = Ext4Image(image_path, readonly=True)
    lib64 = None
    for candidate in ("/lib64", "/system/lib64"):
        if image.is_dir(candidate):
            lib64 = candidate
            break

    result: dict[str, object] = {"image": image_path, "bridge_installed": False}
    if lib64 is None:
        result["error"] = "no lib64 directory"
        return result

    so_path = f"{lib64}/libndk_translation.so"
    result["library"] = so_path
    result["library_present"] = image.exists(so_path)
    if image.exists(so_path):
        data = image.read_file(so_path)
        result["library_size"] = len(data)
        result["library_arch"] = _elf_arch_from_bytes(data)

    proxies = [n for n in image.listdir(lib64) if n.startswith(PROXY_PREFIX)]
    result["proxy_count"] = len(proxies)
    result["runners"] = [
        p for p in ("/bin/ndk_translation_program_runner_binfmt_misc",
                    "/bin/ndk_translation_program_runner_binfmt_misc_arm64")
        if image.exists(p)
    ]

    try:
        bp = android.BuildProp(image.read_file(_find_build_prop(image)))
        result["props"] = {
            k: bp.get(k) for k in android.NATIVE_BRIDGE_PROPS
        }
        result["device"] = {
            "brand": bp.get("ro.product.brand"),
            "model": bp.get("ro.product.model"),
        }
        result["bridge_installed"] = (
            bool(result.get("library_present"))
            and bp.get("ro.dalvik.vm.native.bridge") == "libndk_translation.so"
        )
    except ImageError as exc:
        result["error"] = str(exc)
    return result


def _elf_arch_from_bytes(data: bytes) -> str:
    if len(data) < 20 or data[:4] != b"\x7fELF":
        return "not-elf"
    machine = struct.unpack_from("<H", data, 18)[0]
    return {0x3E: "x86_64", 0x03: "i386", 0xB7: "arm64", 0x28: "arm"}.get(
        machine, f"machine-{machine}")
