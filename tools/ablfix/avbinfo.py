#!/usr/bin/env python3
"""Print vbmeta descriptors and AVB footer of an image. Read-only. Usage: avbinfo.py IMG..."""
import struct, sys
def vbmeta(d, off, name):
    if d[off:off+4] != b'AVB0':
        print(f'  {name}: no AVB0 at {off:#x}'); return
    h = d[off:off+256]
    auth, aux = struct.unpack_from('>QQ', h, 12)
    alg, = struct.unpack_from('>I', h, 28)
    doff, dsz = struct.unpack_from('>QQ', h, 96)
    ri, = struct.unpack_from('>Q', h, 112)
    flags, ril = struct.unpack_from('>II', h, 120)
    print(f'  {name}: alg {alg} rollback_index {ri} location {ril} flags {flags} release {h[128:176].split(b"\0")[0].decode()}')
    p = off + 256 + auth + doff; end = p + dsz
    while p < end:
        tag, n = struct.unpack_from('>QQ', d, p)
        b = d[p+16:p+16+n]
        if tag == 2:  # hash
            isz, = struct.unpack_from('>Q', b, 0); alg_ = b[8:40].split(b'\0')[0]
            pnl, sl, dl, fl = struct.unpack_from('>IIII', b, 40)
            pn = b[116:116+pnl].decode()
            print(f'    hash {pn} image_size {isz} {alg_.decode()} flags {fl}')
        elif tag == 4:  # chain
            ril_, pnl, pkl = struct.unpack_from('>III', b, 0)
            pn = b[76:76+pnl].decode() if len(b) > 76 else b[12+64:12+64+pnl].decode()
            print(f'    chain {pn} rollback_location {ril_}')
        elif tag == 1:
            ksz, vsz = struct.unpack_from('>QQ', b, 0)
            print(f'    prop {b[16:16+ksz].decode()}={b[17+ksz:17+ksz+vsz][:60]!r}')
        elif tag == 3:
            print('    cmdline', b[8:].split(b'\0')[0][:80])
        elif tag == 0:
            print('    hashtree')
        p += 16 + n
for f in sys.argv[1:]:
    d = open(f, 'rb').read()
    print(f, len(d))
    if d[:4] == b'AVB0': vbmeta(d, 0, 'vbmeta')
    if d[-64:-60] == b'AVBf':
        osz, voff, vsz = struct.unpack_from('>QQQ', d, len(d) - 64 + 12)
        print(f'  footer original_size {osz} vbmeta_offset {voff} size {vsz}')
        vbmeta(d, voff, 'footer vbmeta')
    elif d[:4] != b'AVB0':
        print('  no footer')
