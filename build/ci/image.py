#!/usr/bin/env python3
"""Assemble and verify a regular-file OC200 eMMC image at the Beta 2 offsets."""

import argparse
import json
import os
import struct
import subprocess
from pathlib import Path

from upstream import digest

SECTOR = 512
SECTORS = 7_631_744
DTB_LBA = 0x380
DTB_SECTORS = 0x19
KERNEL_LBA = 0x400
P1_LBA = 0x20000
P2_LBA = 0x40000


def copy_payload(image, path, lba):
    image.seek(lba * SECTOR)
    zeros = bytes(1024 * 1024)
    with path.open("rb") as source:
        while chunk := source.read(len(zeros)):
            if chunk == zeros[:len(chunk)]:
                image.seek(len(chunk), os.SEEK_CUR)
            else:
                image.write(chunk)


def verify_mbr(image):
    image.seek(0)
    mbr = image.read(SECTOR)
    expected = [(0x83, P1_LBA, P2_LBA - P1_LBA), (0x83, P2_LBA, SECTORS - P2_LBA),
                (0, 0, 0), (0, 0, 0)]
    entries = [struct.unpack_from("<B3xII", mbr, 450 + 16 * n) for n in range(4)]
    if mbr[510:] != b"\x55\xaa" or entries != expected:
        raise ValueError("assembled image has an unexpected MBR layout")


def verify_payload(image, path, lba):
    image.seek(lba * SECTOR)
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            if image.read(len(chunk)) != chunk:
                raise ValueError(f"image readback mismatch: {path.name}")


def assemble(args):
    parts = (
        ("dtb", args.dtb, DTB_LBA, DTB_SECTORS, False),
        ("kernel", args.kernel, KERNEL_LBA, P1_LBA - KERNEL_LBA, False),
        ("rootfs", args.rootfs, P1_LBA, P2_LBA - P1_LBA, False),
        ("userdata", args.userdata, P2_LBA, SECTORS - P2_LBA, True),
    )
    components = {}
    for name, path, start, sectors, exact in parts:
        if not path.is_file():
            raise ValueError(f"not a regular file: {path}")
        size = path.stat().st_size
        limit = sectors * SECTOR
        if not 0 < size <= limit or (exact and size != limit):
            raise ValueError(f"{path.name}: {size} bytes does not fit its {limit}-byte region")
        components[name] = {"start_lba": start, "sectors": sectors, "bytes": size,
                            "sha256": digest(path)}

    dtb = args.dtb.read_bytes()
    if len(dtb) < 40 or struct.unpack_from(">II", dtb) != (0xD00DFEED, len(dtb)):
        raise ValueError("invalid DTB header or DTB size")
    with args.kernel.open("rb") as kernel:
        if kernel.read(64)[56:60] != b"ARM\x64":
            raise ValueError("expected an uncompressed ARM64 Linux Image")

    with args.image.open("xb") as image:
        image.truncate(SECTORS * SECTOR)
    subprocess.run(
        ["sfdisk", "--no-reread", "--no-tell-kernel", str(args.image)],
        input=f"label: dos\nunit: sectors\n\n"
              f"start={P1_LBA}, size={P2_LBA - P1_LBA}, type=83\n"
              f"start={P2_LBA}, size={SECTORS - P2_LBA}, type=83\n",
        text=True, check=True,
    )
    with args.image.open("r+b") as image:
        verify_mbr(image)
        for _, path, start, _, _ in parts:
            copy_payload(image, path, start)
        for _, path, start, _, _ in parts:
            verify_payload(image, path, start)
    if args.image.stat().st_size != SECTORS * SECTOR:
        raise ValueError("assembled image has the wrong size")

    manifest = {
        "build_commit": args.commit,
        "upstream": json.loads(args.upstream.read_text()),
        "sector_bytes": SECTOR,
        "image": {"file": args.image.name, "bytes": SECTORS * SECTOR,
                  "sha256": digest(args.image)},
        "components": components,
    }
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    for option in ("dtb", "kernel", "rootfs", "userdata", "upstream", "image", "manifest"):
        parser.add_argument(f"--{option}", type=Path, required=True)
    assemble(parser.parse_args())
