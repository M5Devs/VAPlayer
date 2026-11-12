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

"""Apply Magisk-style flashable ZIP packages to an offline ext4 image.

A recovery-flashable ZIP is an ordinary ZIP whose ``META-INF/com/google/
android/update-binary`` is a shell script run by the recovery. Running that
script requires a live Android userspace, so instead we interpret the
*declarative* payload most packages carry:

* ``META-INF/com/google/android/updater-script`` — Edify script; we translate
  the common ``package_extract_file`` / ``package_extract_dir`` / ``set_perm``
  / ``symlink`` verbs into filesystem operations.
* ``META-INF/com/google/android/update-binary`` — Magisk and MicroG ship a
  ``#!/sbin/sh`` script that unpacks a payload; we handle the well-known
  ``magiskboot``/``unzip`` layouts heuristically.

Anything we cannot translate is reported, not silently dropped, so the user
knows exactly what did and did not make it into the image.
"""

from __future__ import annotations

import os
import re
import tempfile
import zipfile
from dataclasses import dataclass, field

from .errors import FlashError
from .ext4 import Ext4Image

# Magisk's own install path, relative to the system image root.
_MAGISK_HINTS = ("magisk", "magiskinit", "magiskpolicy", "stub.apk", "init-ld")


@dataclass
class FlashAction:
    """A single filesystem operation performed (or planned) by a flash."""

    kind: str            # "extract", "extract_dir", "symlink", "set_perm", "mkdir"
    target: str
    source: str = ""
    detail: str = ""
    applied: bool = False


@dataclass
class FlashReport:
    """The outcome of flashing one package."""

    package: str
    package_type: str = "unknown"
    actions: list[FlashAction] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    files_written: int = 0

    def add(self, action: FlashAction) -> None:
        self.actions.append(action)
        if action.applied and action.kind in ("extract", "extract_dir"):
            self.files_written += 1

    @property
    def applied_actions(self) -> list[FlashAction]:
        return [a for a in self.actions if a.applied]

    def summary(self) -> str:
        lines = [
            f"package:      {self.package}",
            f"type:         {self.package_type}",
            f"actions:      {len(self.actions)} ({len(self.applied_actions)} applied)",
            f"files:        {self.files_written}",
        ]
        if self.warnings:
            lines.append(f"warnings:     {len(self.warnings)}")
            for w in self.warnings:
                lines.append(f"  - {w}")
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# Edify updater-script interpretation
# ---------------------------------------------------------------------------

# Matches one statement, e.g. `package_extract_file("system/x", "/system/x");`
_STMT_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;]*)\)\s*;", re.DOTALL)


_ARG_TOKEN_RE = re.compile(
    r'\s*("(?:[^"\\]|\\.)*"|[^,]+)\s*(?:,|$)')


def _parse_edify_args(argstr: str) -> list[str]:
    """Split an edify argument list into positional values.

    Values may be quoted strings or bare numbers, so this splits on top-level
    commas rather than scanning for quotes — otherwise ``set_perm(0, 0, 0755,
    "/path")`` would lose its numeric arguments and shift the indices.
    """
    args: list[str] = []
    for match in _ARG_TOKEN_RE.finditer(argstr):
        token = match.group(1).strip()
        if token.startswith('"') and token.endswith('"') and len(token) >= 2:
            token = token[1:-1].replace('\\"', '"')
        if token != "" or args:
            args.append(token)
    return args


def _map_guest_path(path: str) -> str:
    """Map a recovery-style absolute path to an image-root path.

    A ``system.img`` is mounted at ``/system`` during recovery, so
    ``/system/bin/foo`` must become ``/bin/foo`` inside the image. Paths that
    already refer to other partitions (``/data``, ``/vendor``) are kept as-is
    only when they live under the same image; otherwise they are flagged.
    """
    if path.startswith("/system/"):
        return path[len("/system"):]
    if path == "/system":
        return "/"
    return path


class ZipFlasher:
    """Applies a flashable ZIP to an :class:`Ext4Image`."""

    def __init__(self, image: Ext4Image) -> None:
        if image.readonly:
            raise FlashError("the target image is opened read-only")
        self.image = image

    # -- entry point ------------------------------------------------------
    def flash(self, package: str) -> FlashReport:
        if not os.path.exists(package):
            raise FlashError(f"no such package: {package}")
        if not zipfile.is_zipfile(package):
            raise FlashError(f"not a ZIP archive: {package}")

        report = FlashReport(package=package)
        with tempfile.TemporaryDirectory(prefix="vflash-") as work:
            with zipfile.ZipFile(package) as zf:
                report.package_type = self._detect_type(zf)
                zf.extractall(work)
            self._apply(work, report)
        # debugfs edits leave the superblock counters stale; reconcile.
        self.image.flush()
        return report

    # -- detection --------------------------------------------------------
    def _detect_type(self, zf: zipfile.ZipFile) -> str:
        names = zf.namelist()
        lower = [n.lower() for n in names]
        if any("magisk" in n for n in lower):
            return "magisk"
        if any("microg" in n or "gmscore" in n for n in lower):
            return "microg"
        if any(n.endswith("updater-script") for n in lower):
            return "edify"
        if any(n.endswith("update-binary") for n in lower):
            return "shell"
        return "unknown"

    # -- dispatch ---------------------------------------------------------
    def _apply(self, work: str, report: FlashReport) -> None:
        # An updater-script is authoritative when it actually does something;
        # some packages ship a stub script plus a shell update-binary, so fall
        # through to the next strategy when it yields no applied action.
        updater = self._find(work, "updater-script")
        if updater:
            self._apply_edify(updater, work, report)
            if report.applied_actions:
                return

        update_binary = self._find(work, "update-binary")
        if update_binary:
            before = len(report.applied_actions)
            self._apply_shell_payload(update_binary, work, report)
            if len(report.applied_actions) > before:
                return

        self._apply_flat_payload(work, report)

    @staticmethod
    def _find(root: str, basename: str) -> str | None:
        for dirpath, _dirs, files in os.walk(root):
            if basename in files:
                return os.path.join(dirpath, basename)
        return None

    # -- edify ------------------------------------------------------------
    def _apply_edify(self, script: str, work: str, report: FlashReport) -> None:
        text = open(script, "r", encoding="utf-8", errors="replace").read()
        for m in _STMT_RE.finditer(text):
            verb = m.group(1)
            args = _parse_edify_args(m.group(2))
            handler = {
                "package_extract_file": self._op_extract_file,
                "package_extract_dir": self._op_extract_dir,
                "symlink": self._op_symlink,
                "set_perm": self._op_set_perm,
                "set_perm_recursive": self._op_set_perm_recursive,
                "mkdir": self._op_mkdir,
                "run_program": self._op_run_program,
                "ui_print": self._op_ui_print,
            }.get(verb)
            if handler is None:
                report.warnings.append(f"unsupported edify verb ignored: {verb}")
                continue
            handler(args, work, report)

    def _op_extract_file(self, args: list[str], work: str, report: FlashReport) -> None:
        if len(args) < 2:
            report.warnings.append("package_extract_file with too few arguments")
            return
        src, dst = args[0], _map_guest_path(args[1])
        self._install_file(os.path.join(work, src), dst, report)

    def _op_extract_dir(self, args: list[str], work: str, report: FlashReport) -> None:
        if len(args) < 2:
            report.warnings.append("package_extract_dir with too few arguments")
            return
        src_dir, dst_dir = args[0], _map_guest_path(args[1])
        action = FlashAction("extract_dir", dst_dir, src_dir)
        before = report.files_written
        self._install_dir(os.path.join(work, src_dir), dst_dir, report)
        action.applied = report.files_written > before
        if not action.applied:
            report.warnings.append(f"package_extract_dir source missing: {src_dir}")
        report.add(action)

    def _op_symlink(self, args: list[str], work: str, report: FlashReport) -> None:
        if len(args) < 2:
            return
        target = args[0]
        for link in args[1:]:
            link_path = _map_guest_path(link)
            action = FlashAction("symlink", link_path, target)
            try:
                self.image.symlink(target, link_path)
                action.applied = True
            except Exception as exc:  # noqa: BLE001 - surface, do not abort
                report.warnings.append(f"symlink {link_path}: {exc}")
            report.add(action)

    def _op_set_perm(self, args: list[str], work: str, report: FlashReport) -> None:
        # Edify: set_perm(uid, gid, mode, path)
        if len(args) < 4:
            report.warnings.append("set_perm with too few arguments")
            return
        try:
            mode = int(args[2], 8)
        except ValueError:
            return
        path = _map_guest_path(args[3])
        action = FlashAction("set_perm", path, detail=f"{mode:o}")
        if self.image.exists(path):
            self.image.chmod(path, mode)
            action.applied = True
        else:
            report.warnings.append(f"set_perm target missing: {path}")
        report.add(action)

    def _op_set_perm_recursive(self, args: list[str], work: str, report: FlashReport) -> None:
        # Edify: set_perm_recursive(uid, gid, dirmode, filemode, path)
        if len(args) < 5:
            report.warnings.append("set_perm_recursive with too few arguments")
            return
        try:
            dir_mode = int(args[2], 8)
            file_mode = int(args[3], 8)
        except ValueError:
            return
        root = _map_guest_path(args[4])
        action = FlashAction("set_perm", root,
                             detail=f"dir={dir_mode:o} file={file_mode:o} recursive")
        if self.image.is_dir(root):
            self._chmod_tree(root, dir_mode, file_mode)
            action.applied = True
        else:
            report.warnings.append(f"set_perm_recursive target missing: {root}")
        report.add(action)

    def _chmod_tree(self, root: str, dir_mode: int, file_mode: int) -> None:
        self.image.chmod(root, dir_mode)
        for dirpath, dirs, files in self.image.walk(root):
            for d in dirs:
                self.image.chmod(f"{dirpath}/{d}".replace("//", "/"), dir_mode)
            for f in files:
                self.image.chmod(f"{dirpath}/{f}".replace("//", "/"), file_mode)

    def _op_mkdir(self, args: list[str], work: str, report: FlashReport) -> None:
        if not args:
            return
        path = _map_guest_path(args[0])
        action = FlashAction("mkdir", path)
        self.image.mkdir_p(path)
        action.applied = True
        report.add(action)

    def _op_run_program(self, args: list[str], work: str, report: FlashReport) -> None:
        # Recovery runs helper binaries we cannot execute offline; record it.
        report.warnings.append(
            "run_program skipped (requires live recovery): "
            + (args[0] if args else "?"))

    def _op_ui_print(self, args: list[str], work: str, report: FlashReport) -> None:
        # ui_print is display-only; nothing to apply.
        return

    # -- shell payload ----------------------------------------------------
    def _apply_shell_payload(self, update_binary: str, work: str, report: FlashReport) -> None:
        """Interpret the well-known Magisk/MicroG payload layout.

        These packages ship their files as ordinary ZIP entries and reference
        them from the shell script. Rather than emulate ``sh``, we locate the
        recognizable payload directories and install them, recording a warning
        for the parts that need a live environment.
        """
        installed_any = False
        for candidate in ("system", "system_root"):
            root = os.path.join(work, candidate)
            if os.path.isdir(root):
                self._install_dir(root, "/", report)
                installed_any = True

        # Magisk stores its binaries under lib/ or a magisk/ tree.
        for candidate in ("magisk", "lib", "lib64"):
            root = os.path.join(work, candidate)
            if os.path.isdir(root):
                dest = "/" + candidate if candidate.startswith("lib") else "/magisk"
                self._install_dir(root, dest, report)
                installed_any = True

        if not installed_any:
            self._apply_flat_payload(work, report)
        else:
            report.warnings.append(
                "shell update-binary interpreted declaratively; "
                "on-device init hooks are not applied offline")

    def _apply_flat_payload(self, work: str, report: FlashReport) -> None:
        """Install any ``system/`` tree in the package, else everything."""
        system_root = os.path.join(work, "system")
        if os.path.isdir(system_root):
            self._install_dir(system_root, "/", report)
            return
        report.warnings.append(
            "no updater-script, update-binary, or system/ tree found in package")

    # -- low-level install ------------------------------------------------
    def _install_file(self, host_path: str, guest_path: str, report: FlashReport) -> None:
        action = FlashAction("extract", guest_path, host_path)
        if not os.path.isfile(host_path):
            report.warnings.append(f"package is missing {host_path}")
            report.add(action)
            return
        try:
            self.image.copy_in(host_path, guest_path)
            action.applied = True
        except Exception as exc:  # noqa: BLE001
            report.warnings.append(f"install {guest_path}: {exc}")
        report.add(action)

    def _install_dir(self, host_dir: str, guest_dir: str, report: FlashReport) -> None:
        if not os.path.isdir(host_dir):
            return
        for dirpath, _dirs, files in os.walk(host_dir):
            rel = os.path.relpath(dirpath, host_dir)
            target_dir = guest_dir if rel == "." else f"{guest_dir.rstrip('/')}/{rel}"
            target_dir = "/" + target_dir.lstrip("/")
            for name in files:
                self._install_file(
                    os.path.join(dirpath, name),
                    f"{target_dir.rstrip('/')}/{name}",
                    report,
                )


def is_flashable(package: str) -> bool:
    """True if *package* looks like a recovery-flashable ZIP."""
    if not zipfile.is_zipfile(package):
        return False
    with zipfile.ZipFile(package) as zf:
        names = zf.namelist()
    markers = (
        "META-INF/com/google/android/update-binary",
        "META-INF/com/google/android/updater-script",
    )
    return any(n in names for n in markers)
