#!/usr/bin/env python3
import argparse
import pathlib
import struct
import sys

MAGIC = 0x50414B31
VERSION = 1
FLAG_UNCOMPRESSED = 1
MAX_ASSET_NAME = 256
HEADER_SIZE = 24
TOC_ENTRY_SIZE = 280


def normalize(name):
    return name.replace("\\", "/")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("entries", nargs="+")
    args = parser.parse_args()

    parsed = []
    for entry in args.entries:
        if "=" not in entry:
            raise SystemExit(f"entry must be name=path: {entry}")
        name, path = entry.split("=", 1)
        name = normalize(name)
        encoded = name.encode("utf-8")
        if len(encoded) >= MAX_ASSET_NAME:
            raise SystemExit(f"asset name too long: {name}")
        data = pathlib.Path(path).read_bytes()
        parsed.append((encoded, data))

    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)

    offset = HEADER_SIZE + TOC_ENTRY_SIZE * len(parsed)
    toc = bytearray()
    data_chunks = []
    for name, data in parsed:
        name_buf = name + b"\0" * (MAX_ASSET_NAME - len(name))
        toc += struct.pack("<256sQQQ", name_buf, offset, len(data), len(data))
        data_chunks.append(data)
        offset += len(data)

    with output.open("wb") as f:
        f.write(struct.pack("<IIIIQ", MAGIC, VERSION, len(parsed),
                            FLAG_UNCOMPRESSED, HEADER_SIZE))
        f.write(toc)
        for data in data_chunks:
            f.write(data)

    print(f"Packed {len(parsed)} uncompressed assets into {output}")


if __name__ == "__main__":
    sys.exit(main())
