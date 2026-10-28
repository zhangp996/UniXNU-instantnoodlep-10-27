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
RECORD = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'

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
    # build-arm-driver.py reads this exact record: one JSON object with a trailing comma.
    if not RECORD.is_file():
        raise SystemExit(f'missing {RECORD}; build the kernel compile records first')
    record = json.loads(RECORD.read_text().strip().rstrip(','))
    arguments = record.get('arguments') or []
    if '-c' not in arguments:
        raise SystemExit(f'{RECORD} carries no "-c" compile command')
    return record, arguments


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
        # The record describes a kernel object compile (XNU's own iokit object). Reuse its
        # flags, but rebuild the argument list defensively: the source is ours, the object
        # is ours, and any relative include path is resolved against the object directory
        # the record came from so "meta_features.h" and friends still resolve.
        command, index, placed = [], 0, False
        while index < len(arguments):
            value = arguments[index]
            if value in ('-o',):
                index += 2
                continue
            if value == record.get('file') or (not placed and value.endswith(('.cpp', '.c', '.cc'))):
                command.append(str(source))
                placed = True
            elif value == '-Xclang' and index + 1 < len(arguments) and arguments[index + 1].startswith('-W'):
                index += 1
            elif value.startswith('-W'):
                pass
            elif value in ('-I', '-include') and index + 1 < len(arguments):
                path = arguments[index + 1]
                candidate = Path(path)
                if not candidate.is_absolute():
                    for base in (RECORD.parent, ROOT / '.cache/xnu-work', REFER):
                        if (base / path).exists():
                            candidate = base / path
                            break
                if candidate.exists() or value == '-I':
                    command.extend([value, str(candidate)])
                index += 1
            else:
                command.append(value)
            index += 1
        if not placed:
            command.append(str(source))
        command.extend(['-o', str(destination)])
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
