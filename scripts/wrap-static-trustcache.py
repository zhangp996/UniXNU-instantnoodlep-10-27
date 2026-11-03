#!/usr/bin/env python3
"""Wrap a static trust cache IM4P in the module bundle DarwinBoot expects.

The payload sidecar is not the raw .trustcache IM4P. It is a bundle with a two-word
header (count = 1, first offset = 8) followed by the original version-2 static trust
module. board.toml pins that wrapped bundle's SHA-256 in [payload] trust_cache_sha256.
"""
import hashlib
import struct
import sys
from pathlib import Path

STATIC_SHA256 = 'da88460e6e513438d5307e5c526f76497a74754476a24da29f239f0c90120fc2'
BUNDLE_SHA256 = 'c9e50d678793ecaf8329ce022856c23c27775333181becb130c73ea93f376d17'


def der(data, at):
    if at + 2 > len(data):
        raise SystemExit('truncated DER')
    tag, size = data[at], data[at + 1]
    at += 2
    if size & 0x80:
        count = size & 0x7f
        if not 0 < count <= 4 or at + count > len(data):
            raise SystemExit('invalid DER length')
        size = int.from_bytes(data[at:at + count], 'big')
        at += count
    if at + size > len(data):
        raise SystemExit('DER value exceeds the input')
    return tag, data[at:at + size], at + size


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: wrap-static-trustcache.py <static.trustcache> <out.bundle>')
    raw = Path(sys.argv[1]).read_bytes()
    tag, sequence, end = der(raw, 0)
    if tag != 0x30 or end != len(raw):
        raise SystemExit('not one complete DER sequence')
    module = None
    at = 0
    while at < len(sequence):
        kind, value, at = der(sequence, at)
        if kind == 0x04:
            module = value
    if module is None:
        raise SystemExit('the IM4P carries no octet-string module')
    if hashlib.sha256(module).hexdigest() != STATIC_SHA256:
        raise SystemExit('the static trust module is not the 21H461 module this board pins')
    if struct.unpack_from('<I', module)[0] != 2 or len(module) != 24 + struct.unpack_from('<I', module, 20)[0] * 24:
        raise SystemExit('the static trust module is not a complete version-2 module')
    bundle = struct.pack('<2I', 1, 8) + module
    digest = hashlib.sha256(bundle).hexdigest()
    if digest != BUNDLE_SHA256:
        raise SystemExit('the wrapped bundle differs from the board trust_cache_sha256')
    out = Path(sys.argv[2])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bundle)
    print(f'PASS: static trust bundle {len(bundle)} bytes sha256 {digest}')


if __name__ == '__main__':
    main()
