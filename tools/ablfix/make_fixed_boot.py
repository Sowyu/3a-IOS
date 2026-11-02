#!/usr/bin/env python3
"""Build a full-partition boot image: our boot image at 0, zeros, then the stock
boot_a tail (signed vbmeta at the footer's offset, and the AVB footer at the end).
ABL follows boot_a's footer even for `fastboot boot`, and dies on a bad vbmeta header.
Usage: make_fixed_boot.py STOCK_BOOT OUR_BOOT OUT"""
import struct, sys
stock, ours, out = sys.argv[1:]
s = open(stock, 'rb').read()
o = open(ours, 'rb').read()
assert s[-64:-60] == b'AVBf', 'stock has no footer'
voff, vsz = struct.unpack_from('>QQ', s, len(s) - 64 + 20)
assert s[voff:voff + 4] == b'AVB0', 'stock vbmeta missing'
assert o[:8] == b'ANDROID!', 'not a boot image'
assert len(o) <= voff, f'our image {len(o)} reaches the vbmeta at {voff}'
img = o + bytes(voff - len(o)) + s[voff:]
assert len(img) == len(s)
assert img[voff:voff + vsz] == s[voff:voff + vsz] and img[-64:] == s[-64:]
assert img[:len(o)] == o
open(out, 'wb').write(img)
print(f'ok {out}: {len(img)} bytes, ours {len(o)}, stock vbmeta {vsz} at {voff}, footer kept')
