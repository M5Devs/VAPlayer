#!/usr/bin/env python3
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

"""vflash — inspect and modify an Android image offline, without booting it.

Examples
--------
Show what is inside an image::

    vflash --image android.qcow2 --inspect

Print a directory tree of ``/etc``::

    vflash --image system.img --tree /etc --depth 2

Pull a file out::

    vflash --image android.qcow2 --extract /build.prop ./build.prop

Flash Magisk (or any flashable ZIP) into the image::

    vflash --image android.qcow2 --flash magisk.zip --backup android.orig.qcow2

Set a single property::

    vflash --image system.img --set-prop ro.product.model=Pixel 8 Pro
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from vflash.engine import VFlashEngine  # noqa: E402
from vaplayer_tools.errors import ToolsError  # noqa: E402
from vaplayer_tools.flashzip import is_flashable  # noqa: E402


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="vflash",
        description="Offline Android image manipulation (inspect, extract, flash).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--image", "-i", required=True,
                   help="image to operate on (raw ext4, raw disk, or qcow2)")

    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--inspect", action="store_true",
                      help="show filesystem, Android, and bridge details")
    mode.add_argument("--tree", nargs="?", const="/", metavar="PATH",
                      help="print a directory tree (default: /)")
    mode.add_argument("--extract", nargs=2, metavar=("PATH", "DEST"),
                      help="copy PATH out of the image to DEST")
    mode.add_argument("--cat", metavar="PATH",
                      help="print the contents of a file to stdout")
    mode.add_argument("--flash", metavar="ZIP",
                      help="flash a flashable ZIP package into the image")
    mode.add_argument("--set-prop", metavar="KEY=VALUE",
                      help="set a build.prop property")
    mode.add_argument("--check", action="store_true",
                      help="verify the image can be opened and its fs is sane")
    mode.add_argument("--json", action="store_true",
                      help="emit --inspect output as JSON")

    p.add_argument("--depth", type=int, default=2,
                   help="tree depth for --tree (default: 2)")
    p.add_argument("--backup", metavar="PATH",
                   help="write a backup copy before modifying (--flash/--set-prop)")
    p.add_argument("--keep-temp", action="store_true",
                   help="keep intermediate flattened/carved files")
    p.add_argument("--quiet", "-q", action="store_true")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    def log(msg: str) -> None:
        if not args.quiet:
            print(f"[vflash] {msg}", file=sys.stderr, flush=True)

    engine = VFlashEngine(keep_temp=args.keep_temp, log=log)

    try:
        if args.inspect or args.json:
            result = engine.inspect(args.image)
            if args.json:
                print(json.dumps(result, indent=2, sort_keys=True))
            else:
                _print_inspect(result)
            return 0

        if args.check:
            result = engine.inspect(args.image)
            fs = result["filesystem"]
            print(f"OK: {args.image} — {result['container']} image, "
                  f"ext4 '{fs['label']}' at offset {result['ext4_offset']}, "
                  f"{fs['size_bytes'] / 2**20:.1f} MiB")
            return 0

        if args.tree is not None:
            print(engine.tree(args.image, args.tree, max_depth=args.depth))
            return 0

        if args.extract:
            src, dest = args.extract
            out = engine.extract(args.image, src, dest)
            log(f"extracted {src} -> {out}")
            return 0

        if args.cat:
            data = engine.cat(args.image, args.cat)
            sys.stdout.buffer.write(data)
            return 0

        if args.flash:
            if not is_flashable(args.flash):
                log(f"warning: {args.flash} is not a standard flashable ZIP")
            report = engine.flash(args.image, args.flash, backup=args.backup)
            print(report.summary())
            return 0

        if args.set_prop:
            if "=" not in args.set_prop:
                print("error: --set-prop expects KEY=VALUE", file=sys.stderr)
                return 2
            key, value = args.set_prop.split("=", 1)
            engine.set_property(args.image, key.strip(), value)
            log(f"set {key.strip()}={value}")
            return 0

        return 2
    except ToolsError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        engine.close_all()


def _print_inspect(result: dict) -> None:
    fs = result["filesystem"]
    print(f"image:            {result['image']}")
    print(f"container:        {result['container']}")
    print(f"partition scheme: {result['partition_scheme']}")
    print(f"partition used:   {result['partition_used']}")
    print(f"ext4 offset:      {result['ext4_offset']}")
    print("filesystem:")
    print(f"  label:          {fs['label'] or '-'}")
    print(f"  uuid:           {fs['uuid'] or '-'}")
    print(f"  block size:     {fs['block_size']}")
    print(f"  size:           {fs['size_bytes'] / 2**20:.1f} MiB "
          f"({fs['size_bytes']} bytes)")
    print(f"  inodes:         {fs['inode_count']}")
    print(f"  free blocks:    {fs['free_blocks']}")
    if result.get("android"):
        print("android:")
        for key, value in result["android"].items():
            print(f"  {key:32} {value}")
    bridge = result.get("native_bridge", {})
    print("native bridge:")
    print(f"  installed:      {bridge.get('installed')}")
    if bridge.get("library"):
        print(f"  library:        {bridge['library']} ({bridge.get('size')} bytes)")
    print(f"  proxy libs:     {bridge.get('proxy_count', 0)}")
    print(f"root entries:     {', '.join(result.get('root_entries', [])[:12])}"
          f"{' ...' if len(result.get('root_entries', [])) > 12 else ''}")


if __name__ == "__main__":
    raise SystemExit(main())
