#!/usr/bin/env python3
"""Globalise a Mach-O symbol in place (the Linux replacement for llvm-objcopy --globalize-symbol)."""
import struct
import sys
from pathlib import Path

path = Path(sys.argv[1]); wanted = sys.argv[2].encode()
data = bytearray(path.read_bytes())
magic, cpu, sub, ft, ncmds, sz, flags, _ = struct.unpack_from('<IiiIIIII', data, 0)
off = 32; sym = None
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from('<II', data, off)
    if cmd == 2:
        sym = struct.unpack_from('<IIII', data, off + 8)
    off += cmdsize
symoff, nsyms, stroff, strsize = sym
patched = 0
for i in range(nsyms):
    at = symoff + i * 16
    strx, ntype, nsect, ndesc, nval = struct.unpack_from('<IBBHQ', data, at)
    name = data[stroff + strx:data.index(b'\0', stroff + strx)]
    if name == wanted and not (ntype & 0x01):
        data[at + 4] = ntype | 0x01
        patched += 1
path.write_bytes(bytes(data))
print(f'globalised {sys.argv[2]}: {patched} site(s)')
