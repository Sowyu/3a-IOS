#!/usr/bin/env python3
"""A full boot partition for an image longer than the stock vbmeta offset (14118912): the image, then the stock signed vbmeta
blob moved to just after it, then a footer pointing there. The footer is not signed and the vbmeta keeps its signature; only
the boot hash stops matching, which an unlocked ABL accepts (analysis/ablfix.md). Inferred, the first phone run decides.
Usage: make_relocated.py IMAGE STOCK_PARTITION OUT"""
import struct, sys
PART = 100663296
img, stock, out = sys.argv[1:4]
d = open(img, 'rb').read()
s = open(stock, 'rb').read()
assert len(s) == PART, 'stock partition must be the full 100 MiB backup'
f = s[-64:]
assert f[:4] == b'AVBf', 'no AVB footer in the stock partition'
orig, voff, vsz = struct.unpack_from('>QQQ', f, 12)
blob = s[voff:voff + vsz]
assert blob[:4] == b'AVB0', 'stock vbmeta missing'
new = (len(d) + 4095) // 4096 * 4096
assert new + vsz <= PART - 4096, 'image too long for the partition'
p = bytearray(PART)
p[:len(d)] = d
p[new:new + vsz] = blob
nf = bytearray(f)
struct.pack_into('>QQQ', nf, 12, new, new, vsz)
p[-64:] = nf
open(out, 'wb').write(p)
print('image %d bytes, vbmeta %d bytes moved from %d to %d' % (len(d), vsz, voff, new))
