#!/usr/bin/env python3
"""Synthetic malformed MDT checks. No phone or firmware fixtures."""
import struct
import unittest
from inspect_mdt import inspect, GSI_BASE


def image(rows):
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    header = struct.pack('<16sHHIIIIIHHHHHH', ident, 2, 40, 1, 0, 52, 0, 0, 52, 32, len(rows), 0, 0, 0)
    return header + b''.join(struct.pack('<8I', *p) for p in rows)


def load(pa=0, filesz=16, memsz=4096, flags=0x08000000):
    return (1, 0, pa, pa, filesz, memsz, flags, 4096)


class Check(unittest.TestCase):
    def test_relocation_and_hash_exclusion(self):
        result = inspect(image([load(0x1000), load(0x2000, 0, 4096), load(0, 32, 32, 0x02000000)]))
        self.assertEqual(result['span'], 8192)
        self.assertEqual(result['file_bytes'], 16)
        self.assertEqual(result['loads'][0]['destination'], GSI_BASE)
        self.assertFalse(result['authenticated'])
        self.assertFalse(result['split_files_checked'])

    def test_invalid(self):
        bad = [b'', image([load()])[:-1], image([load(filesz=4097)]),
               image([load(0xfffff000, memsz=8192)]), image([load(memsz=0xb000)]),
               image([load(), load(0x800)]), image([load(flags=0)]),
               image([load(filesz=0, memsz=0)]), image([load(flags=0x02000000)])]
        for data in bad:
            with self.subTest(data=data.hex()[:24]):
                with self.assertRaises(ValueError):
                    inspect(data)

    def test_exact_limit(self):
        self.assertEqual(inspect(image([load(memsz=0xa000)]))['span'], 0xa000)


if __name__ == '__main__':
    unittest.main()
