#!/usr/bin/env python3
"""Split an Android DTBO image (dtbo_a) into the overlays it carries.

The header layout measured on this board is:
  magic 0xd7b7ab1e, total_size, header_size=32, entry_size=32,
  entry_count, entries_offset=32, page_size
and every entry is (size, offset, id, rev, custom[4]).
"""
import argparse
import pathlib
import struct

MAGIC = 0xD7B7AB1E
FDT_MAGIC = 0xD00DFEED


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=pathlib.Path)
    parser.add_argument("-o", "--out", type=pathlib.Path, required=True)
    args = parser.parse_args()

    data = args.image.read_bytes()
    magic, total, header, entry_size, count, entries_off, page = struct.unpack_from(">7I", data, 0)
    if magic != MAGIC:
        raise SystemExit(f"not a DTBO image: magic 0x{magic:08x}")
    print(f"total={total} header={header} entry_size={entry_size} count={count} "
          f"entries_off={entries_off} page={page}")

    args.out.mkdir(parents=True, exist_ok=True)
    kept = 0
    for index in range(count):
        size, offset, ident, rev = struct.unpack_from(">4I", data, entries_off + index * entry_size)
        if not size or offset + size > len(data):
            print(f"[{index}] invalid entry size={size} offset={offset}")
            continue
        blob = data[offset:offset + size]
        if struct.unpack_from(">I", blob, 0)[0] != FDT_MAGIC:
            print(f"[{index}] not an FDT")
            continue
        target = args.out / f"overlay-{index:02d}-id{ident}-rev{rev}.dtb"
        target.write_bytes(blob)
        kept += 1
        print(f"[{index}] {size} bytes offset=0x{offset:x} id={ident} rev={rev}")
    print(f"wrote {kept} overlays to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
