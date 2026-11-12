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

"""Android-specific knowledge: ``build.prop``, device profiles, sparse images.

The property set used for the ARM native-bridge injection is the one the
Android build system itself applies (see the vendor makefile
``libndk_translation.mk`` in the prebuilt package): ``ro.dalvik.vm.isa.arm``,
``ro.vendor.enable.native.bridge.exec`` and friends.
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field

from .errors import ImageError
from .shell import require_tool, run

# ---------------------------------------------------------------------------
# build.prop
# ---------------------------------------------------------------------------

_PROP_RE = re.compile(r"^\s*([A-Za-z0-9_.\-]+)\s*=\s*(.*?)\s*$")


class BuildProp:
    """A parsed ``build.prop`` that preserves order and comments on rewrite."""

    def __init__(self, text: str | bytes) -> None:
        if isinstance(text, (bytes, bytearray)):
            text = bytes(text).decode("utf-8", "replace")
        self._lines: list[str] = text.splitlines()
        self._props: dict[str, str] = {}
        for line in self._lines:
            m = _PROP_RE.match(line)
            if m:
                self._props[m.group(1)] = m.group(2)

    @classmethod
    def from_bytes(cls, data: bytes) -> "BuildProp":
        return cls(data.decode("utf-8", "replace"))

    # -- reads ------------------------------------------------------------
    def get(self, key: str, default: str | None = None) -> str | None:
        return self._props.get(key, default)

    def __contains__(self, key: str) -> bool:
        return key in self._props

    def as_dict(self) -> dict[str, str]:
        return dict(self._props)

    def keys(self):
        return self._props.keys()

    def items(self):
        return self._props.items()

    # -- writes -----------------------------------------------------------
    def set(self, key: str, value: str, *, comment: str = "") -> None:
        """Set *key* to *value*, rewriting the line in place if it exists."""
        if comment:
            self._insert_comment_once(comment)
        for i, line in enumerate(self._lines):
            m = _PROP_RE.match(line)
            if m and m.group(1) == key:
                self._lines[i] = f"{key}={value}"
                self._props[key] = value
                return
        self._lines.append(f"{key}={value}")
        self._props[key] = value

    def update(self, values: dict[str, str], *, comment: str = "") -> None:
        if comment:
            self._insert_comment_once(comment)
        for key, value in values.items():
            self.set(key, value)

    def _insert_comment_once(self, comment: str) -> None:
        marker = f"# {comment}"
        if marker not in self._lines:
            self._lines.append(marker)

    def to_bytes(self) -> bytes:
        return ("\n".join(self._lines) + "\n").encode("utf-8")


# ---------------------------------------------------------------------------
# Device spoofing profiles
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class DeviceProfile:
    """A device identity used to spoof ``build.prop`` for high-FPS unlocking.

    Games commonly gate high-refresh modes on ``ro.product.model`` /
    ``ro.product.brand``. These profiles carry the full set of properties a
    real device reports so the spoof is internally consistent.
    """

    key: str
    label: str
    props: dict[str, str] = field(default_factory=dict)


DEVICE_PROFILES: dict[str, DeviceProfile] = {
    "rog8": DeviceProfile(
        key="rog8",
        label="ASUS ROG Phone 8 (165 Hz)",
        props={
            "ro.product.brand": "asus",
            "ro.product.manufacturer": "asus",
            "ro.product.model": "ASUS_AI2401",
            "ro.product.device": "AI2401",
            "ro.product.name": "WW_AI2401",
            "ro.product.board": "kalama",
            "ro.product.cpu.abi": "arm64-v8a",
            "ro.product.cpu.abilist": "arm64-v8a,armeabi-v7a,armeabi",
            "ro.product.cpu.abilist32": "armeabi-v7a,armeabi",
            "ro.product.cpu.abilist64": "arm64-v8a",
            "ro.build.characteristics": "default",
            "ro.build.version.release": "14",
            "ro.build.version.sdk": "34",
            "ro.sf.lcd_density": "440",
            "ro.max_fps": "165",
            "persist.sys.display.max_refresh_rate": "165",
        },
    ),
    "s24ultra": DeviceProfile(
        key="s24ultra",
        label="Samsung Galaxy S24 Ultra (120 Hz)",
        props={
            "ro.product.brand": "samsung",
            "ro.product.manufacturer": "samsung",
            "ro.product.model": "SM-S928B",
            "ro.product.device": "e3q",
            "ro.product.name": "e3qxxx",
            "ro.product.board": "kalama",
            "ro.product.cpu.abi": "arm64-v8a",
            "ro.product.cpu.abilist": "arm64-v8a,armeabi-v7a,armeabi",
            "ro.product.cpu.abilist32": "armeabi-v7a,armeabi",
            "ro.product.cpu.abilist64": "arm64-v8a",
            "ro.build.characteristics": "default",
            "ro.build.version.release": "14",
            "ro.build.version.sdk": "34",
            "ro.sf.lcd_density": "480",
            "ro.max_fps": "120",
            "persist.sys.display.max_refresh_rate": "120",
        },
    ),
    "pixel8pro": DeviceProfile(
        key="pixel8pro",
        label="Google Pixel 8 Pro (120 Hz)",
        props={
            "ro.product.brand": "google",
            "ro.product.manufacturer": "Google",
            "ro.product.model": "Pixel 8 Pro",
            "ro.product.device": "husky",
            "ro.product.name": "husky",
            "ro.product.board": "zuma",
            "ro.product.cpu.abi": "arm64-v8a",
            "ro.product.cpu.abilist": "arm64-v8a,armeabi-v7a,armeabi",
            "ro.product.cpu.abilist32": "armeabi-v7a,armeabi",
            "ro.product.cpu.abilist64": "arm64-v8a",
            "ro.build.characteristics": "nosdcard",
            "ro.build.version.release": "14",
            "ro.build.version.sdk": "34",
            "ro.sf.lcd_density": "420",
            "ro.max_fps": "120",
            "persist.sys.display.max_refresh_rate": "120",
        },
    ),
}

DEFAULT_PROFILE = "rog8"


def get_profile(key: str) -> DeviceProfile:
    """Return the profile named *key*, raising :class:`ImageError` if unknown."""
    try:
        return DEVICE_PROFILES[key.lower()]
    except KeyError:
        known = ", ".join(sorted(DEVICE_PROFILES))
        raise ImageError(f"unknown device profile '{key}' (known: {known})") from None


# ---------------------------------------------------------------------------
# Native bridge properties
# ---------------------------------------------------------------------------

# Mirrors the Android build system's own native-bridge configuration so the
# guest advertises ARM support consistently.
NATIVE_BRIDGE_PROPS: dict[str, str] = {
    "ro.dalvik.vm.native.bridge": "libndk_translation.so",
    "ro.dalvik.vm.isa.arm": "x86",
    "ro.dalvik.vm.isa.arm64": "x86_64",
    "ro.vendor.enable.native.bridge.exec": "1",
    "ro.vendor.enable.native.bridge.exec64": "1",
    "ro.enable.native.bridge.exec": "1",
    "ro.ndk_translation.version": "0.2.3",
}

# ABI entries the guest must advertise once the bridge is installed.
NATIVE_BRIDGE_ABILIST = "arm64-v8a,armeabi-v7a,armeabi"
NATIVE_BRIDGE_ABILIST32 = "armeabi-v7a,armeabi"
NATIVE_BRIDGE_ABILIST64 = "arm64-v8a"


# ---------------------------------------------------------------------------
# Android sparse images
# ---------------------------------------------------------------------------

_SPARSE_MAGIC = 0xED26FF3A


def is_sparse(path: str) -> bool:
    """True if *path* is an Android sparse image."""
    with open(path, "rb") as fh:
        return struct.unpack("<I", fh.read(4))[0] == _SPARSE_MAGIC


def unsparse(path: str, dest: str | None = None) -> str:
    """Convert an Android sparse image to a raw image, returning the raw path."""
    require_tool("simg2img")
    if dest is None:
        dest = path + ".raw"
    run(["simg2img", path, dest])
    return dest


def resparse(raw_path: str, dest: str | None = None) -> str:
    """Convert a raw image back to Android sparse format."""
    require_tool("img2simg")
    if dest is None:
        dest = raw_path + ".simg"
    run(["img2simg", raw_path, dest])
    return dest


# ---------------------------------------------------------------------------
# Property inspection helpers
# ---------------------------------------------------------------------------

def summary(props: BuildProp) -> dict[str, str]:
    """Return a human-readable subset of the interesting properties."""
    interesting = [
        "ro.build.version.release",
        "ro.build.version.sdk",
        "ro.product.brand",
        "ro.product.model",
        "ro.product.device",
        "ro.product.cpu.abi",
        "ro.product.cpu.abilist",
        "ro.dalvik.vm.native.bridge",
        "ro.dalvik.vm.isa.arm64",
    ]
    return {k: props.get(k, "-") or "-" for k in interesting}
