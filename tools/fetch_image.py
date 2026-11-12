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

"""Download and unpack a lean Android-x86 image into a qcow2 for vaplayer-core.

Examples
--------
List the tested base images::

    tools/fetch_image.sh --list

Provision the default image into ``images/``::

    tools/fetch_image.sh --image android-x86-9 --out images/android.qcow2

Reuse an already-downloaded ISO and also emit a writable data disk::

    tools/fetch_image.sh --iso ~/android-x86_64-9.0-r2.iso \
        --out images/android.qcow2 --data-disk images/data.qcow2 --data-size 8G
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vaplayer_tools import image as image_mod  # noqa: E402
from vaplayer_tools.errors import ToolsError  # noqa: E402

DEFAULT_CACHE = os.path.expanduser("~/.cache/vaplayer")


def _parse_size(text: str) -> int:
    """Parse ``8G`` / ``512M`` / ``1073741824`` into bytes."""
    text = text.strip().upper()
    units = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30, "T": 1 << 40}
    if text and text[-1] in units:
        try:
            return int(float(text[:-1]) * units[text[-1]])
        except ValueError:
            raise argparse.ArgumentTypeError(f"invalid size: {text}") from None
    try:
        return int(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"invalid size: {text}") from None


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="fetch_image",
        description="Download and unpack an Android-x86 base image into qcow2.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--list", action="store_true",
                   help="list the known base images and exit")
    p.add_argument("--image", default=image_mod.DEFAULT_IMAGE,
                   help=f"known image key or URL (default: {image_mod.DEFAULT_IMAGE})")
    p.add_argument("--iso", metavar="PATH",
                   help="use an already-downloaded ISO instead of fetching one")
    p.add_argument("--out", "-o", default="android.qcow2",
                   help="output qcow2 path (default: android.qcow2)")
    p.add_argument("--workdir",
                   help="working directory (default: alongside --out)")
    p.add_argument("--cache", default=DEFAULT_CACHE,
                   help=f"download cache (default: {DEFAULT_CACHE})")
    p.add_argument("--keep-work", action="store_true",
                   help="keep intermediate unpacked files")
    p.add_argument("--compress", action="store_true",
                   help="produce a compressed (read-only) qcow2")
    p.add_argument("--data-disk", metavar="PATH",
                   help="also create a writable guest /data disk at PATH")
    p.add_argument("--data-size", type=_parse_size, default=8 << 30,
                   help="size of the data disk (default: 8G)")
    p.add_argument("--no-verify", action="store_true",
                   help="skip the final qemu-img check")
    p.add_argument("--quiet", "-q", action="store_true", help="less output")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    if args.list:
        print("Known base images:\n")
        for key, meta in sorted(image_mod.KNOWN_IMAGES.items()):
            default = " (default)" if key == image_mod.DEFAULT_IMAGE else ""
            print(f"  {key:18} Android {meta['android']:>4}  {meta['label']}{default}")
            print(f"  {'':18} {meta['url']}")
        return 0

    def log(msg: str) -> None:
        if not args.quiet:
            print(f"[fetch_image] {msg}", flush=True)

    out = os.path.abspath(args.out)
    workdir = os.path.abspath(args.workdir) if args.workdir else out + ".work"
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)

    try:
        # 1. Obtain the ISO.
        if args.iso:
            iso = os.path.abspath(args.iso)
            if not os.path.exists(iso):
                raise ToolsError(f"no such ISO: {iso}")
            log(f"using local ISO {iso}")
        else:
            url, meta = image_mod.resolve_image(args.image)
            log(f"image: {meta['label']}")
            os.makedirs(args.cache, exist_ok=True)
            iso = os.path.join(args.cache, os.path.basename(url.split("?")[0]) or "base.iso")
            if os.path.exists(iso) and os.path.getsize(iso) > 0:
                log(f"reusing cached {iso}")
            else:
                log(f"downloading {url}")
                image_mod.download(url, iso, log=log)

        # 2. Unpack ISO -> system.img (+ kernel/ramdisk).
        log(f"unpacking into {workdir}")
        unpacked = image_mod.extract_from_iso(iso, workdir, log=log)
        raw = image_mod.prepare_system_image(unpacked, workdir, log=log)
        log(unpacked.describe())

        # 3. Convert to qcow2.
        image_mod.make_qcow2(raw, out, compress=args.compress, log=log)

        # 4. Optional data disk.
        if args.data_disk:
            image_mod.make_data_disk(os.path.abspath(args.data_disk),
                                     args.data_size, log=log)

        # 5. Verify.
        if not args.no_verify:
            info = image_mod.verify_qcow2(out)
            log(f"verified {out}: {info.format}, "
                f"virtual={info.virtual_size / 2**30:.2f} GiB, "
                f"disk={info.disk_size / 2**30:.2f} GiB, sparse={info.is_sparse}")
        print(f"\nProvisioned image: {out}")
        return 0
    except ToolsError as exc:
        print(f"error: {exc}", file=sys.stderr)
        if "not found" in str(exc):
            missing = str(exc).split(":")[1].split()[0] if ":" in str(exc) else ""
            if missing:
                print(f"hint: install '{missing}' (see tools/README.md)", file=sys.stderr)
        return 1
    finally:
        if not args.keep_work and os.path.isdir(workdir):
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
