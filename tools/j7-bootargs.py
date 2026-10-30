#!/usr/bin/env python3
"""Edit boot arguments embedded in a J7 Android boot image without changing its payload.

The Samsung boot header's cmdline is not used by the J7 shim. XNU's command line
is the 608-byte field in the SWEETXNU payload wrapped at kernel offset 0x2000.
This helper changes that field and recalculates Android's component SHA-1.
"""
import argparse
import hashlib
import struct
from pathlib import Path

PAGE = 2048
WRAPPER_OFFSET = 0x2000
PAYLOAD_OFFSET = WRAPPER_OFFSET + 16
CMDLINE_OFFSET = 0x48
CMDLINE_SIZE = 608


def page_up(n):
    return (n + PAGE - 1) // PAGE * PAGE


def edit(src, dst, append, remove):
    image = bytearray(Path(src).read_bytes())
    if image[:8] != b"ANDROID!":
        raise ValueError("not an Android boot image")
    kernel_size = struct.unpack_from("<I", image, 8)[0]
    ramdisk_size = struct.unpack_from("<I", image, 16)[0]
    dt_size = struct.unpack_from("<I", image, 40)[0]
    kernel_off = PAGE
    ramdisk_off = kernel_off + page_up(kernel_size)
    dt_off = ramdisk_off + page_up(ramdisk_size)
    if dt_off + dt_size > len(image):
        raise ValueError("boot image components exceed file size")
    if image[kernel_off + WRAPPER_OFFSET:kernel_off + WRAPPER_OFFSET + 8] != b"J7XNPAY1":
        raise ValueError("J7 embedded payload marker not found")
    payload_len = struct.unpack_from("<Q", image, kernel_off + WRAPPER_OFFSET + 8)[0]
    payload_off = kernel_off + PAYLOAD_OFFSET
    if payload_len < CMDLINE_OFFSET + CMDLINE_SIZE or image[payload_off:payload_off + 8] != b"SWEETXNU":
        raise ValueError("invalid embedded SWEETXNU payload")

    field = payload_off + CMDLINE_OFFSET
    current = image[field:field + CMDLINE_SIZE].split(b"\0", 1)[0].decode("ascii")
    args = current.split()
    remove = set(remove)
    args = [arg for arg in args if arg not in remove]
    for arg in append:
        if arg not in args:
            args.append(arg)
    updated = " ".join(args).encode("ascii")
    if len(updated) >= CMDLINE_SIZE:
        raise ValueError("updated boot arguments do not fit")
    image[field:field + CMDLINE_SIZE] = updated + b"\0" * (CMDLINE_SIZE - len(updated))

    digest = hashlib.sha1()
    for offset, size in ((kernel_off, kernel_size), (ramdisk_off, ramdisk_size), (dt_off, dt_size)):
        digest.update(image[offset:offset + size])
        digest.update(struct.pack("<I", size))
    image[576:608] = digest.digest() + b"\0" * 12
    Path(dst).write_bytes(image)
    print(f"{dst}: {len(image)} bytes")
    print(f"XNU boot args: {updated.decode()}")


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("image", help="input Samsung Android boot image")
parser.add_argument("output", help="output boot image")
parser.add_argument("--append", action="append", default=[], metavar="ARG")
parser.add_argument("--remove", action="append", default=[], metavar="ARG")
args = parser.parse_args()
edit(args.image, args.output, args.append, args.remove)
