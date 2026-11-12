#!/usr/bin/env python3
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

"""Inject the ARM native bridge (libndk_translation) into an Android image.

Examples
--------
Inspect an image and report whether the bridge is installed::

    tools/inject_arm.sh --image android.qcow2 --verify

Fetch the bridge from upstream and inject it, spoofing a ROG Phone 8::

    tools/inject_arm.sh --image system.img --profile rog8

Inject from a local prebuilt directory without touching the network::

    tools/inject_arm.sh --image system.img --source-dir ./ndk-prebuilt
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vaplayer_tools import android, nativebridge  # noqa: E402
from vaplayer_tools.errors import ToolsError  # noqa: E402
from vaplayer_tools import qcow2  # noqa: E402

DEFAULT_CACHE = os.path.expanduser("~/.cache/vaplayer/ndk")


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="inject_arm",
        description="Inject libndk_translation ARM translation into an Android image.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--image", "-i", required=False,
                   help="target image (raw ext4, or qcow2 with --extract)")
    p.add_argument("--profile", default=android.DEFAULT_PROFILE,
                   help="device profile to spoof (default: %(default)s)")
    p.add_argument("--no-spoof", action="store_true",
                   help="install the bridge without changing device identity")
    p.add_argument("--source-dir",
                   help="directory of prebuilt bridge files (skips download)")
    p.add_argument("--cache", default=DEFAULT_CACHE,
                   help=f"bridge download cache (default: {DEFAULT_CACHE})")
    p.add_argument("--ref", default=nativebridge.DEFAULT_PREBUILT_REF,
                   help="upstream prebuilt ref/branch (default: %(default)s)")
    p.add_argument("--no-proxies", action="store_true",
                   help="skip the optional proxy libraries")
    p.add_argument("--extract", metavar="RAW",
                   help="flatten a qcow2 --image into this raw file first")
    p.add_argument("--verify", action="store_true",
                   help="only inspect the image and report bridge status")
    p.add_argument("--list-profiles", action="store_true",
                   help="list device profiles and exit")
    p.add_argument("--quiet", "-q", action="store_true")
    return p


def _log(args):
    def log(msg: str) -> None:
        if not args.quiet:
            print(f"[inject_arm] {msg}", flush=True)
    return log


def _flatten_if_needed(path: str, extract_to: str | None) -> tuple[str, str | None]:
    """Return (working raw path, temp path to clean up).

    A raw ext4 image is returned unchanged; a qcow2 is flattened to a raw file
    because the offline ext4 editor cannot address a qcow2 directly.
    """
    layout_ok = False
    try:
        with open(path, "rb") as fh:
            fh.seek(0x438)
            layout_ok = fh.read(2) == b"\x53\xef"
    except OSError:
        pass
    if layout_ok:
        return path, None

    # Not a bare ext4 at offset 0 — try to flatten via qemu-img.
    raw = extract_to or (path + ".raw")
    qcow2.to_raw(path, raw)
    return raw, None if extract_to else raw


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    log = _log(args)

    if args.list_profiles:
        print("Device profiles:\n")
        for key, prof in sorted(android.DEVICE_PROFILES.items()):
            print(f"  {key:12} {prof.label}")
            print(f"  {'':12} {prof.props['ro.product.model']}")
        return 0

    if not args.image:
        build_parser().error("--image is required (unless using --list-profiles)")

    image_path = os.path.abspath(args.image)
    if not os.path.exists(image_path):
        print(f"error: no such image: {image_path}", file=sys.stderr)
        return 1

    try:
        if args.verify:
            _print_verify(image_path)
            return 0

        raw, cleanup = _flatten_if_needed(image_path, args.extract)
        if raw != image_path:
            log(f"flattened {image_path} -> {raw}")

        # Resolve the bridge source directory.
        if args.source_dir:
            source = os.path.abspath(args.source_dir)
            log(f"using local bridge source {source}")
        else:
            source = args.cache
            log(f"fetching bridge from {nativebridge.DEFAULT_PREBUILT_REPO}"
                f"@{args.ref}")
            nativebridge.fetch_prebuilt(
                source, ref=args.ref,
                include_proxies=not args.no_proxies, log=log)

        report = nativebridge.inject(
            raw, source,
            profile=args.profile,
            spoof_device=not args.no_spoof,
            report_progress=log,
        )
        print("\n" + report.summary())

        if args.extract:
            log(f"injected raw image written to {raw}")

        print("\nBridge installed. Re-verify with:")
        print(f"  tools/inject_arm.sh --image {image_path} --verify")
        return 0
    except ToolsError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


def _print_verify(image_path: str) -> None:
    # If the target is a qcow2 we cannot read ext4 from it directly.
    with open(image_path, "rb") as fh:
        magic = fh.read(4)
    if magic == b"QFI\xfb":
        print(f"{image_path}: qcow2 image; flatten it first "
              "(inject_arm --image X --extract X.raw) then --verify the raw file")
        return
    result = nativebridge.verify(image_path)
    print(f"image:            {result['image']}")
    print(f"bridge installed: {result.get('bridge_installed')}")
    if "error" in result:
        print(f"error:            {result['error']}")
        return
    print(f"library:          {result.get('library')} "
          f"({'present' if result.get('library_present') else 'missing'}, "
          f"{result.get('library_arch', '?')}, {result.get('library_size', 0)} bytes)")
    print(f"proxy libs:       {result.get('proxy_count')}")
    print(f"program runners:  {len(result.get('runners', []))}")
    props = result.get("props", {})
    print("native bridge properties:")
    for key, value in props.items():
        print(f"  {key}={value}")
    device = result.get("device", {})
    print(f"device identity:  {device.get('brand')} / {device.get('model')}")


if __name__ == "__main__":
    raise SystemExit(main())
