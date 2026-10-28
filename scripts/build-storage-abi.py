#!/usr/bin/env python3
"""Compile the public IOStorageFamily sources into a kernel archive.

The board's UFS driver subclasses IOBlockStorageDevice, and IOStorageFamily ships as a
kext of the original kernelcache, so an archive-form driver leaves that vtable undefined
at the kernel link. UniXNU's own device records describe the same failure
(docs/.vitepress/device-records/sirius.toml line 191). This builds the public sources
into build/storage-abi-kernel/libIOStorageFamilyKernel.a so the archive form links;
docs/bring-up.md records option B.

The compile flags come from an XNU compile record, exactly like build-arm-driver.py, so
the objects match the kernel ABI (SDK, target, ABI flags) without duplicating them here.
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path.cwd()
REFER = ROOT / 'refer/IOStorageFamily'
OUT = ROOT / 'build/storage-abi-kernel'
RECORD_DIR = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT'

# Everything IOBlockStorageDevice pulls in, plus the media and partition schemes the
# storage driver publishes. Kept explicit so a source that fails on a board is visible.
SOURCES = [
    'IOBlockStorageDevice.cpp',
    'IOBlockStorageDriver.cpp',
    'IOMedia.cpp',
    'IOGUIDPartitionScheme.cpp',
    'IOApplePartitionScheme.cpp',
    'IOFDiskPartitionScheme.cpp',
    'IORequest.cpp',
    'IORequestsPool.cpp',
    'IOFilterScheme.cpp',
]


def record_arguments():
    records = sorted(RECORD_DIR.glob('*.o.json'))
    if not records:
        raise SystemExit(f'no compile records in {RECORD_DIR}; build the kernel records first')
    for path in records:
        record = json.loads(path.read_text())
        arguments = record.get('arguments') or []
        if '-c' in arguments:
            return record, arguments
    raise SystemExit('no compile record carries "-c"')


def main():
    if not REFER.is_dir():
        raise SystemExit('missing refer/IOStorageFamily; run "unixnu fetch" first')
    OUT.mkdir(parents=True, exist_ok=True)
    record, arguments = record_arguments()
    objects = []
    for name in SOURCES:
        source = REFER / name
        if not source.is_file():
            print(f'skip {name}: not in the public source')
            continue
        destination = OUT / (Path(name).stem + '.o')
        command, index = [], 0
        while index < len(arguments):
            value = arguments[index]
            if value == record['file']:
                command.append(str(source))
            elif value == '-o':
                command.extend(['-o', str(destination)])
                index += 1
            elif value == '-Xclang' and index + 1 < len(arguments) and arguments[index + 1].startswith('-W'):
                index += 1
            elif not value.startswith('-W'):
                command.append(value)
            index += 1
        command += ['-I', str(REFER), '-I', str(ROOT / 'build/storage-abi/include'),
                    '-I', str(ROOT / '.cache/xnu-work/osfmk'),
                    '-Wno-unused-parameter', '-Wno-c99-designator', '-Wno-reorder-init-list',
                    '-Wno-deprecated-enum-enum-conversion', '-Wno-unnecessary-virtual-specifier',
                    '-Wno-error']
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stdout[-4000:])
            print(result.stderr[-4000:], file=sys.stderr)
            raise SystemExit(f'compile failed: {name}')
        objects.append(destination)
        print(f'compiled {name}')
    if not objects:
        raise SystemExit('no storage source compiled')
    archive = OUT / 'libIOStorageFamilyKernel.a'
    archive.unlink(missing_ok=True)
    subprocess.run(['ar', 'rcs', str(archive), *map(str, objects)], check=True)
    print(f'archive: {archive} ({archive.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
