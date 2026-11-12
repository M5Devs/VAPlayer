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

"""Tests for the image-provisioning pipeline.

The provisioning path that matters is "unpack a real Android-x86 ISO and turn
its system filesystem into a bootable qcow2". Building a full Android-x86 ISO
here would be wasteful, so the fixture assembles a *structurally faithful* ISO
with ``xorriso``: a kernel, a ramdisk, and a squashfs ``system.sfs`` holding a
real ext4 ``system.img``. The pipeline code under test is identical either way.
"""

from __future__ import annotations

import os
import shutil
import subprocess

import pytest

from conftest import TOOLS_DIR, make_ext4, requires_e2fsprogs, requires_qemu_img

from vaplayer_tools import image
from vaplayer_tools.errors import ImageError

pytestmark = [requires_e2fsprogs, requires_qemu_img]


def _have(tool: str) -> bool:
    return shutil.which(tool) is not None


requires_iso_tools = pytest.mark.skipif(
    not (_have("xorriso") and _have("mksquashfs") and _have("unsquashfs")),
    reason="xorriso, mksquashfs and unsquashfs are required")


@pytest.fixture
def fake_android_iso(tmp_path):
    """A minimal but structurally real Android-x86-style installer ISO."""
    work = tmp_path / "iso-build"
    work.mkdir(parents=True)

    (work / "kernel").write_bytes(b"\x1f\x8b" + b"kernel" * 512)
    (work / "initrd.img").write_bytes(b"\x1f\x8b" + b"ramdisk" * 512)

    system_img = str(work / "system.img")
    make_ext4(system_img, size_mb=32, tree={
        "/build.prop": (
            "ro.build.version.release=9\n"
            "ro.build.version.sdk=28\n"
            "ro.product.cpu.abi=x86_64\n"
            "ro.dalvik.vm.native.bridge=0\n"
        ),
        "/lib64/.keep": "",
        "/bin/.keep": "",
        "/etc/.keep": "",
    })

    sfs_stage = work / "sfs-stage"
    sfs_stage.mkdir()
    shutil.copy2(system_img, sfs_stage / "system.img")
    os.unlink(system_img)  # the ISO ships only system.sfs, like the real one
    subprocess.run(["mksquashfs", str(sfs_stage), str(work / "system.sfs"),
                    "-noappend", "-no-progress", "-quiet"], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    shutil.rmtree(sfs_stage)

    iso = tmp_path / "android-x86-test.iso"
    subprocess.run(["xorriso", "-as", "mkisofs", "-o", str(iso),
                    "-V", "Android-x86", str(work)],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return str(iso)


# ---------------------------------------------------------------------------
# Source resolution
# ---------------------------------------------------------------------------

def test_known_images_have_real_urls():
    assert image.KNOWN_IMAGES
    for key, meta in image.KNOWN_IMAGES.items():
        assert meta["url"].startswith("http"), key
        assert meta["label"]
        assert meta["android"]
        assert meta["kind"] == "iso"


def test_resolve_image_by_key():
    url, meta = image.resolve_image("android-x86-9")
    assert url == meta["url"]
    assert url.startswith("http")
    assert meta["android"] == "9"


def test_resolve_image_by_url():
    url = "https://example.com/custom.iso"
    resolved, meta = image.resolve_image(url)
    assert resolved == url
    assert meta["kind"] == "iso"


def test_resolve_image_unknown():
    with pytest.raises(ImageError):
        image.resolve_image("not-a-known-image")


def test_default_image_is_known():
    assert image.DEFAULT_IMAGE in image.KNOWN_IMAGES


# ---------------------------------------------------------------------------
# ISO extraction
# ---------------------------------------------------------------------------

@requires_iso_tools
def test_extract_from_iso(fake_android_iso, tmp_path):
    workdir = str(tmp_path / "work")
    unpacked = image.extract_from_iso(fake_android_iso, workdir)
    assert isinstance(unpacked, image.UnpackedImage)
    assert os.path.exists(unpacked.system_img)
    assert unpacked.kernel is not None
    assert unpacked.initrd is not None
    assert any("SquashFS" in n for n in unpacked.notes)


@requires_iso_tools
def test_extracted_system_is_valid_ext4(fake_android_iso, tmp_path):
    unpacked = image.extract_from_iso(fake_android_iso, str(tmp_path / "work"))
    from vaplayer_tools.ext4 import Ext4Image
    img = Ext4Image(unpacked.system_img)
    assert img.exists("/build.prop")
    assert b"ro.build.version.release=9" in img.read_file("/build.prop")


@requires_iso_tools
def test_extract_rejects_non_iso(tmp_path):
    bad = tmp_path / "bad.iso"
    bad.write_bytes(b"not an iso at all")
    with pytest.raises(Exception):
        image.extract_from_iso(str(bad), str(tmp_path / "work"))


def test_extract_missing_iso(tmp_path):
    with pytest.raises(Exception):
        image.extract_from_iso("/no/such.iso", str(tmp_path / "work"))


# ---------------------------------------------------------------------------
# SquashFS
# ---------------------------------------------------------------------------

@requires_iso_tools
def test_unpack_squashfs_direct(tmp_path, android_like_ext4):
    stage = tmp_path / "stage"
    stage.mkdir()
    shutil.copy2(android_like_ext4, stage / "system.img")
    sfs = str(tmp_path / "system.sfs")
    subprocess.run(["mksquashfs", str(stage), sfs, "-noappend",
                    "-no-progress", "-quiet"], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    found = image.unpack_squashfs(sfs, str(tmp_path / "out"))
    assert os.path.exists(found)
    assert os.path.basename(found) == "system.img"


@requires_iso_tools
def test_unpack_squashfs_without_system_img(tmp_path):
    stage = tmp_path / "stage"
    (stage / "system").mkdir(parents=True)
    (stage / "system" / "build.prop").write_text("ro.x=1\n")
    sfs = str(tmp_path / "tree.sfs")
    subprocess.run(["mksquashfs", str(stage), sfs, "-noappend",
                    "-no-progress", "-quiet"], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    with pytest.raises(ImageError):
        image.unpack_squashfs(sfs, str(tmp_path / "out"))


# ---------------------------------------------------------------------------
# Assemble
# ---------------------------------------------------------------------------

@requires_iso_tools
def test_prepare_system_image_passthrough(fake_android_iso, tmp_path):
    unpacked = image.extract_from_iso(fake_android_iso, str(tmp_path / "work"))
    raw = image.prepare_system_image(unpacked, str(tmp_path / "work"))
    assert raw == unpacked.system_img


def test_prepare_system_image_unsparses(tmp_path):
    """An Android sparse image must be converted to raw by prepare_system_image."""
    if not _have("img2simg"):
        pytest.skip("img2simg is required")
    raw_src = str(tmp_path / "system.img")
    make_ext4(raw_src, size_mb=16, tree={"/build.prop": "ro.x=1\n"})
    sparse = str(tmp_path / "system.sparse.img")
    subprocess.run(["img2simg", raw_src, sparse], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    unpacked = image.UnpackedImage(system_img=sparse)
    out = image.prepare_system_image(unpacked, str(tmp_path))
    assert out != sparse
    with open(out, "rb") as fh:
        fh.seek(0x438)
        assert fh.read(2) == b"\x53\xef"  # ext4 magic
    assert any("sparse" in n.lower() for n in unpacked.notes)


@requires_iso_tools
def test_make_qcow2_and_verify(fake_android_iso, tmp_path):
    unpacked = image.extract_from_iso(fake_android_iso, str(tmp_path / "work"))
    raw = image.prepare_system_image(unpacked, str(tmp_path / "work"))
    out = str(tmp_path / "android.qcow2")
    image.make_qcow2(raw, out)

    info = image.verify_qcow2(out)
    assert info.format == "qcow2"
    assert info.is_sparse

    from vflash.engine import VFlashEngine
    engine = VFlashEngine(log=lambda _m: None)
    try:
        handle = engine.open(out)
        assert handle.fs.exists("/build.prop")
        assert b"ro.build.version.release=9" in handle.fs.read_file("/build.prop")
        engine.close(handle)
    finally:
        engine.close_all()


@requires_iso_tools
def test_make_qcow2_compressed(fake_android_iso, tmp_path):
    unpacked = image.extract_from_iso(fake_android_iso, str(tmp_path / "work"))
    raw = image.prepare_system_image(unpacked, str(tmp_path / "work"))
    plain = str(tmp_path / "plain.qcow2")
    comp = str(tmp_path / "comp.qcow2")
    image.make_qcow2(raw, plain, compress=False)
    image.make_qcow2(raw, comp, compress=True)
    assert os.path.getsize(comp) <= os.path.getsize(plain)


def test_make_data_disk(tmp_path):
    dest = str(tmp_path / "data.qcow2")
    image.make_data_disk(dest, 64 * 1024 * 1024)
    assert os.path.exists(dest)
    info = image.verify_qcow2(dest)
    assert info.virtual_size == 64 * 1024 * 1024


# ---------------------------------------------------------------------------
# End-to-end via the CLI
# ---------------------------------------------------------------------------

@requires_iso_tools
def test_fetch_image_cli_end_to_end(fake_android_iso, tmp_path):
    import sys
    out = str(tmp_path / "images" / "android.qcow2")
    proc = subprocess.run(
        [sys.executable, os.path.join(TOOLS_DIR, "fetch_image.py"),
         "--iso", fake_android_iso, "--out", out,
         "--workdir", str(tmp_path / "work"), "--keep-work"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=TOOLS_DIR)
    assert proc.returncode == 0, proc.stderr.decode()
    assert os.path.exists(out)

    from vaplayer_tools import qcow2
    assert qcow2.info(out).format == "qcow2"


@requires_iso_tools
def test_fetch_image_cli_with_data_disk(fake_android_iso, tmp_path):
    import sys
    out = str(tmp_path / "android.qcow2")
    data = str(tmp_path / "data.qcow2")
    proc = subprocess.run(
        [sys.executable, os.path.join(TOOLS_DIR, "fetch_image.py"),
         "--iso", fake_android_iso, "--out", out,
         "--data-disk", data, "--data-size", "64M",
         "--workdir", str(tmp_path / "work")],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=TOOLS_DIR)
    assert proc.returncode == 0, proc.stderr.decode()
    assert os.path.exists(out)
    assert os.path.exists(data)
