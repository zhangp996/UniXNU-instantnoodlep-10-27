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

# The kernel runs every __DATA,__mod_init_func entry of the objects linked into it, which would register
# this class before the prelinked family that owns its superclass.  Clear the entries so the initialiser only
# runs when the probe calls it (the reference builder removes this section with llvm-objcopy).
off = 32
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from('<II', data, off)
    if cmd == 0x19:
        nsects = struct.unpack_from('<I', data, off + 64)[0]
        for i in range(nsects):
            so = off + 72 + i * 80
            sect = bytes(data[so:so + 16]).rstrip(b'\0')
            if sect == b'__mod_init_func':
                size = struct.unpack_from('<Q', data, so + 40)[0]
                foff = struct.unpack_from('<I', data, so + 48)[0]
                data[foff:foff + size] = b'\0' * size
                print(f'cleared __mod_init_func ({size} bytes)')
    off += cmdsize

path.write_bytes(bytes(data))
print(f'globalised {sys.argv[2]}: {patched} site(s)')
