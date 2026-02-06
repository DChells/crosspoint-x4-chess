#!/usr/bin/env python3
# pyright: basic
"""Inspect CPZ1 puzzle pack headers and first record."""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys


HEADER_SIZE = 18


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Inspect a CPZ1 file")
    parser.add_argument("path", help="Path to .cpz file")
    return parser.parse_args()


def decode_text_field(blob: bytes) -> str:
    return blob.split(b"\x00", 1)[0].decode("ascii", errors="ignore")


def main() -> None:
    args = parse_args()
    path = pathlib.Path(args.path)
    data = path.read_bytes()

    if len(data) < HEADER_SIZE:
        raise SystemExit("File too small for CPZ header")
    if data[0:4] != b"CPZ1":
        raise SystemExit("Not a CPZ1 file (magic mismatch)")

    record_size = struct.unpack_from("<H", data, 4)[0]
    puzzle_count = struct.unpack_from("<I", data, 6)[0]
    rating_min = struct.unpack_from("<H", data, 10)[0]
    rating_max = struct.unpack_from("<H", data, 12)[0]
    reserved = data[14:18]

    print(f"Path: {path}")
    print(f"Record size: {record_size}")
    print(f"Puzzle count: {puzzle_count}")
    print(f"Rating min/max: {rating_min}/{rating_max}")
    print(f"Reserved bytes: {reserved.hex()}")

    expected = HEADER_SIZE + puzzle_count * record_size
    print(f"File size: {len(data)} (expected {expected})")
    if len(data) != expected:
        raise SystemExit("Size mismatch vs header")

    if puzzle_count == 0:
        return

    first = data[HEADER_SIZE : HEADER_SIZE + record_size]
    rating = struct.unpack_from("<H", first, 0)[0]
    move_count = first[3]
    print(f"First record rating: {rating}")
    print(f"First record move count: {move_count}")

    if record_size >= 128:
        print(f"First record themes: {decode_text_field(first[84:116])}")
        print(f"First record opening: {decode_text_field(first[116:128])}")


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        sys.exit(0)
