# VAPlayer provisioning tools — tests
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

"""End-to-end tests that drive the command-line entry points.

These invoke the real CLIs in a subprocess, so they exercise argument parsing,
exit codes, and stdout formatting exactly as a user would see them.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys

import pytest

from conftest import TOOLS_DIR, requires_e2fsprogs, requires_qemu_img


def run_cli(script: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, os.path.join(TOOLS_DIR, script), *args],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=TOOLS_DIR)


pytestmark = requires_e2fsprogs


# ---------------------------------------------------------------------------
# vflash
# ---------------------------------------------------------------------------

def test_vflash_help():
    proc = run_cli("vflash/cli.py", "--help")
    assert proc.returncode == 0
    assert b"inspect" in proc.stdout
    assert b"flash" in proc.stdout


def test_vflash_inspect(android_like_ext4):
    proc = run_cli("vflash/cli.py", "--image", android_like_ext4, "--inspect")
    assert proc.returncode == 0, proc.stderr.decode()
    out = proc.stdout.decode()
    assert "label:" in out
    assert "system" in out
    assert "ro.build.version.release" in out


def test_vflash_inspect_json(android_like_ext4):
    proc = run_cli("vflash/cli.py", "--image", android_like_ext4, "--json")
    assert proc.returncode == 0
    data = json.loads(proc.stdout.decode())
    assert data["filesystem"]["label"] == "system"
    assert data["container"] == "raw"


def test_vflash_check(android_like_ext4):
    proc = run_cli("vflash/cli.py", "--image", android_like_ext4, "--check")
    assert proc.returncode == 0
    assert proc.stdout.decode().startswith("OK:")


def test_vflash_tree(android_like_ext4):
    proc = run_cli("vflash/cli.py", "--image", android_like_ext4,
                   "--tree", "/", "--depth", "1")
    assert proc.returncode == 0
    assert "lib64" in proc.stdout.decode()


def test_vflash_cat(android_like_ext4):
    proc = run_cli("vflash/cli.py", "--image", android_like_ext4,
                   "--cat", "/build.prop")
    assert proc.returncode == 0
    assert b"ro.build.version.release=9" in proc.stdout


def test_vflash_extract(android_like_ext4, tmp_path):
    dest = str(tmp_path / "extracted.prop")
    proc = run_cli("vflash/cli.py", "--image", android_like_ext4,
                   "--extract", "/build.prop", dest)
    assert proc.returncode == 0
    assert os.path.exists(dest)


def test_vflash_flash(android_like_ext4, edify_zip):
    proc = run_cli("vflash/cli.py", "--image", android_like_ext4,
                   "--flash", edify_zip)
    assert proc.returncode == 0, proc.stderr.decode()
    assert "files:" in proc.stdout.decode()


def test_vflash_set_prop(android_like_ext4):
    proc = run_cli("vflash/cli.py", "--image", android_like_ext4,
                   "--set-prop", "ro.test.cli=works")
    assert proc.returncode == 0
    from vaplayer_tools.ext4 import Ext4Image
    assert b"ro.test.cli=works" in Ext4Image(android_like_ext4).read_file("/build.prop")


def test_vflash_missing_image_errors():
    proc = run_cli("vflash/cli.py", "--image", "/no/such.img", "--inspect")
    assert proc.returncode == 1
    assert b"error" in proc.stderr.lower()


def test_vflash_requires_a_mode(android_like_ext4):
    proc = run_cli("vflash/cli.py", "--image", android_like_ext4)
    assert proc.returncode == 2  # argparse: a mode is required


def test_vflash_set_prop_bad_format(android_like_ext4):
    proc = run_cli("vflash/cli.py", "--image", android_like_ext4,
                   "--set-prop", "no-equals-sign")
    assert proc.returncode == 2


def test_vflash_shell_wrapper(android_like_ext4):
    """The bin/vflash shell wrapper must dispatch correctly."""
    wrapper = os.path.join(TOOLS_DIR, "bin", "vflash")
    if not os.access(wrapper, os.X_OK):
        pytest.skip("bin/vflash is not executable")
    proc = subprocess.run([wrapper, "--image", android_like_ext4, "--check"],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert proc.returncode == 0
    assert b"OK:" in proc.stdout


# ---------------------------------------------------------------------------
# fetch_image
# ---------------------------------------------------------------------------

def test_fetch_image_help():
    proc = run_cli("fetch_image.py", "--help")
    assert proc.returncode == 0
    out = proc.stdout.decode()
    assert "--list" in out
    assert "--image" in out


def test_fetch_image_list_sources():
    proc = run_cli("fetch_image.py", "--list")
    assert proc.returncode == 0
    out = proc.stdout.decode()
    assert "android-x86" in out
    assert "bliss" in out.lower()


def test_fetch_image_unknown_source():
    proc = run_cli("fetch_image.py", "--image", "not-a-real-source",
                   "--out", "/tmp/nope.qcow2")
    assert proc.returncode != 0


def test_fetch_image_missing_iso_errors(tmp_path):
    """A nonexistent local ISO must fail rather than silently succeed."""
    proc = run_cli("fetch_image.py", "--iso", "/no/such.iso",
                   "--out", str(tmp_path / "x.qcow2"))
    assert proc.returncode != 0


# ---------------------------------------------------------------------------
# inject_arm
# ---------------------------------------------------------------------------

def test_inject_arm_help():
    proc = run_cli("inject_arm.py", "--help")
    assert proc.returncode == 0
    assert b"--profile" in proc.stdout


def test_inject_arm_verify_only(android_like_ext4):
    proc = run_cli("inject_arm.py", "--image", android_like_ext4, "--verify")
    assert proc.returncode == 0
    assert b"bridge installed: False" in proc.stdout


def test_inject_arm_full(android_like_ext4, ndk_source_dir):
    proc = run_cli("inject_arm.py", "--image", android_like_ext4,
                   "--source-dir", ndk_source_dir, "--profile", "rog8")
    assert proc.returncode == 0, proc.stderr.decode()

    verify = run_cli("inject_arm.py", "--image", android_like_ext4, "--verify")
    assert verify.returncode == 0
    assert b"bridge installed: True" in verify.stdout
    assert b"x86_64" in verify.stdout


def test_inject_arm_missing_image():
    proc = run_cli("inject_arm.py", "--image", "/no/such.img", "--verify")
    assert proc.returncode == 1


def test_inject_arm_list_profiles():
    proc = run_cli("inject_arm.py", "--list-profiles")
    assert proc.returncode == 0
    assert b"rog8" in proc.stdout.lower()


def test_inject_arm_no_spoof(android_like_ext4, ndk_source_dir):
    proc = run_cli("inject_arm.py", "--image", android_like_ext4,
                   "--source-dir", ndk_source_dir, "--no-spoof")
    assert proc.returncode == 0
    from vaplayer_tools.ext4 import Ext4Image
    bp = Ext4Image(android_like_ext4).read_file("/build.prop")
    assert b"ro.dalvik.vm.native.bridge=libndk_translation.so" in bp
    assert b"ro.product.brand=Android-x86" in bp


@requires_qemu_img
def test_inject_arm_qcow2_writes_back_in_place(android_like_ext4,
                                               ndk_source_dir, tmp_path):
    """Injecting into a qcow2 must patch that file, not a side raw file.

    Regression: the flattening step wrote to ``<image>.raw`` and never
    repacked, so a qcow2 target was silently left unmodified.
    """
    from vaplayer_tools import qcow2

    target = str(tmp_path / "system.qcow2")
    qcow2.convert(android_like_ext4, target, fmt="qcow2", compress=False)

    proc = run_cli("inject_arm.py", "--image", target,
                   "--source-dir", ndk_source_dir, "--profile", "rog8")
    assert proc.returncode == 0, proc.stderr.decode()

    # No leftover raw side file.
    assert not os.path.exists(target + ".raw")

    # The qcow2 itself must now report the bridge as installed.
    verify = run_cli("inject_arm.py", "--image", target, "--verify")
    assert verify.returncode == 0, verify.stderr.decode()
    assert b"bridge installed: True" in verify.stdout
    assert b"x86_64" in verify.stdout

