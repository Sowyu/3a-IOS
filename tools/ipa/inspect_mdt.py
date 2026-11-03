#!/usr/bin/env python3
"""Read an extracted ipa_fws.mdt on the host. Never authenticates or loads it."""
import argparse
import json
from pathlib import Path
import struct

GSI_BASE = 0x9EA90000
GSI_SIZE = 0xA000


def inspect(data):
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('expected ELF32 little-endian MDT')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if h[3] != 1 or h[8] != 52 or h[9] != 32 or not 1 <= h[10] <= 64:
        raise ValueError('invalid ELF header or program count')
    off, count = h[5], h[10]
    if off < 52 or off + count * 32 > len(data):
        raise ValueError('truncated program headers')
    loads = []
    for i in range(count):
        typ, fileoff, va, pa, filesz, memsz, flags, align = struct.unpack_from('<8I', data, off + i * 32)
        if typ != 1 or (flags & 0x07000000) == 0x02000000:
            continue  # Qualcomm authentication metadata is not executable memory.
        if filesz > memsz or pa + memsz > 0x100000000:
            raise ValueError('invalid load size or address overflow')
        if not memsz:
            continue
        if not flags & 0x08000000:
            raise ValueError('expected relocatable IPA load')
        if align and (align & (align - 1)):
            raise ValueError('invalid segment alignment')
        loads.append(dict(index=i, address=pa, file_bytes=filesz, memory_bytes=memsz))
    if not loads:
        raise ValueError('no load segments')
    loads.sort(key=lambda p: p['address'])
    low = loads[0]['address'] & ~4095
    end = low
    for p in loads:
        if p['address'] < end:
            raise ValueError('overlapping loads')
        end = p['address'] + p['memory_bytes']
        p['destination'] = GSI_BASE + p['address'] - low
    span = ((end + 4095) & ~4095) - low
    if span > GSI_SIZE:
        raise ValueError('load exceeds GSI reservation')
    return dict(pas_id=15, reservation_base=GSI_BASE, reservation_size=GSI_SIZE,
                span=span, file_bytes=sum(p['file_bytes'] for p in loads), loads=loads,
                authenticated=False, split_files_checked=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mdt', type=Path)
    args = parser.parse_args()
    if not args.mdt.is_file():
        parser.error('input must be a regular extracted file')
    with args.mdt.open('rb') as stream:
        data = stream.read(1024 * 1024 + 1)
    if len(data) > 1024 * 1024:
        parser.error('MDT exceeds 1 MiB inspection bound')
    try:
        print(json.dumps(inspect(data), indent=2))
    except ValueError as error:
        parser.error(str(error))
