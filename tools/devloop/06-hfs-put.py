#!/usr/bin/env python3
"""UNTESTED ON A PHONE. The raw-image path has host checks (check-06-hfs-put.sh, tests/check-hfs-journal.sh); the adb path
has never run. This copy adds to plans/boot/06-hfs-put.py: --state, an undo guard, --undo-dir, coalesced writes.

What: replaces existing files inside the iOS root in place, with no reflash. It writes only blocks that differ, and grows a
file by allocating one new extent. It works with the phone in recovery (root reads and writes userdata) or on a raw image.
It cannot create files: a new path still needs `renegade update` and a flash. Refused: dirty or inconsistent volumes, a
journal with unreplayed transactions, compressed files, growth of a file that already uses overflow extents.

Apply: nothing to apply. Shut iOS down from Settings first, or the volume is dirty and this refuses.
  06-hfs-put.py --image ROOT.img  /usr/local/lib/bgmtl/bgmtl-inject.dylib=NEW.dylib [PATH=FILE ...]
  06-hfs-put.py --adb SERIAL      /usr/local/lib/bgmtl/bgmtl-inject.dylib=NEW.dylib      recovery, adb root
  add --dry-run to list changes. Every run saves undo-TIME.bin; `--undo undo-TIME.bin` puts the old bytes back, and refuses
  once the volume header has changed: a boot since the put makes old blocks wrong, so put the old file back instead.
  --state prints clean or why not, exit 0 or 3, writes nothing. A reader of a volume that is not clean can get a stale catalog.
"""
import argparse
import hashlib
import math
import os
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'ios-asteroids', 'tools', 'renegade'))
import hfsx  # noqa: E402

BLK = 4096
HFS_EPOCH = 2082844800
A_UNMOUNTED, A_INCONSISTENT, A_JOURNALED = 0x100, 0x800, 0x2000
UF_COMPRESSED = 0x20
FORK, ALLOC_FORK = 88, 112      # data fork in a catalog file record, allocation file in the volume header


class Raw:
    def __init__(s, path, ro):
        s.f = open(path, 'rb' if ro else 'r+b')

    def read(s, off, n):
        s.f.seek(off)
        return s.f.read(n)

    def write(s, off, data):
        s.f.seek(off)
        s.f.write(data)
        s.f.flush()
        os.fsync(s.f.fileno())


class Adb:
    MB = 1 << 20

    def __init__(s, serial, dev):
        s.serial, s.dev, s.cache = serial, dev, {}

    def _block(s, i):
        if i not in s.cache:
            out = subprocess.run(['adb', '-s', s.serial, 'exec-out', f'dd if={s.dev} bs={s.MB} skip={i} count=1 2>/dev/null'],
                                 capture_output=True, check=True).stdout
            if len(out) != s.MB:
                sys.exit(f'short read at MiB {i}: {len(out)} bytes')
            s.cache[i] = out
        return s.cache[i]

    def read(s, off, n):
        out = bytearray()
        while n > 0:
            i, o = divmod(off, s.MB)
            k = min(n, s.MB - o)
            out += s._block(i)[o:o + k]
            off += k
            n -= k
        return bytes(out)

    def write(s, off, data):
        assert off % BLK == 0 and len(data) % BLK == 0
        subprocess.run(['adb', '-s', s.serial, 'exec-in', f'dd of={s.dev} bs={BLK} seek={off // BLK} conv=notrunc,fsync 2>/dev/null'],
                       input=data, check=True)
        for i in range(off // s.MB, (off + len(data) - 1) // s.MB + 1):
            s.cache.pop(i, None)
        if s.read(off, len(data)) != data:
            sys.exit(f'read-back mismatch at {off:#x}: restore with --undo')


class Writer:
    """Block-aligned read-modify-write, with every old block saved first."""

    def __init__(s, dev, undo_path):
        s.dev, s.undo = dev, open(undo_path, 'ab') if undo_path else None

    def put(s, off, data):
        start = off - off % BLK
        end = math.ceil((off + len(data)) / BLK) * BLK
        old = s.dev.read(start, end - start)
        new = old[:off - start] + data + old[off - start + len(data):]
        if new == old:
            return
        s.undo.write(struct.pack('<QI', start, len(old)) + old)
        s.undo.flush()
        os.fsync(s.undo.fileno())
        s.dev.write(start, new)


def not_clean(dev, vh, bs):
    """None if the on-disk blocks are the whole truth (clean unmount and an empty journal), else why not."""
    attrs = struct.unpack_from('>I', vh, 4)[0]
    if not attrs & A_UNMOUNTED:
        return 'volume was not cleanly unmounted: boot iOS and use Settings > Shut Down, or fsck it first'
    if attrs & A_INCONSISTENT:
        return 'volume is marked inconsistent: fsck it first'
    if attrs & A_JOURNALED:
        ji = dev.read(struct.unpack_from('>I', vh, 12)[0] * bs, 52)
        flags = struct.unpack_from('>I', ji, 0)[0]
        if not flags & 0x4:      # need-init: nothing was ever journaled
            offset = struct.unpack_from('>Q', ji, 36)[0]
            jh = dev.read(offset, 44)
            magic = struct.unpack_from('<I', jh, 0)[0]
            order = '<' if magic == 0x4a4e4c78 else '>'
            if struct.unpack_from(order + 'I', jh, 0)[0] != 0x4a4e4c78:
                return 'journal header not recognised'
            start, end, size = struct.unpack_from(order + 'QQQ', jh, 8)
            hsize = struct.unpack_from(order + 'I', jh, 40)[0]
            # replay_journal: a pointer at the very end wraps to the first byte after the header
            if (hsize if start == size else start) != (hsize if end == size else end):
                return 'journal has unreplayed transactions: boot iOS once, shut down cleanly'
    return None


def check_clean(dev, vh, bs):
    why = not_clean(dev, vh, bs)
    if why:
        sys.exit(why)


def catalog_pos(vol, off):
    """offset in the device of byte `off` of the catalog file"""
    _, ext = vol.fork(272)
    for st, c in vol.all_extents(4, 0, ext):
        if off < c * vol.bsize:
            return st * vol.bsize + off
        off -= c * vol.bsize
    sys.exit('catalog offset out of range')


def catalog_put(vol, w, off, data):
    """A catalog node can cross two extents, and Writer writes contiguous bytes."""
    pos = catalog_pos(vol, off)
    if catalog_pos(vol, off + len(data) - 1) != pos + len(data) - 1:
        sys.exit('catalog record crosses an extent boundary: not handled, restore with --undo')
    w.put(pos, data)


def allocate(dev, vh, bs, count):
    """First free contiguous run of `count` blocks, from the bitmap only; returns its start and the bitmap image."""
    total = struct.unpack_from('>I', vh, 44)[0]
    logical = struct.unpack_from('>Q', vh, ALLOC_FORK)[0]
    exts = [struct.unpack_from('>II', vh, ALLOC_FORK + 16 + 8 * i) for i in range(8)]
    bits = bytearray()
    for st, c in exts:
        if c:
            bits += dev.read(st * bs, c * bs)
    if len(bits) < min(logical, math.ceil(total / 8)):
        sys.exit('allocation file has overflow extents: not handled')
    run, i = 0, 0
    while i < total:
        if run == 0 and i % 8 == 0 and bits[i // 8] == 0xff:
            i += 8
            continue
        if bits[i // 8] & (0x80 >> (i % 8)):
            run = 0
        else:
            run += 1
            if run == count:
                return i - count + 1, bits, exts
        i += 1
    sys.exit(f'no free run of {count} blocks')


def bitmap_offset(exts, bs, byte):
    for st, c in exts:
        if byte < c * bs:
            return st * bs + byte
        byte -= c * bs
    sys.exit('bitmap offset out of range')


def plan(vol, path, data):
    e = vol.lookup(path)
    if not e or e[0] != 2:
        sys.exit(f'{path}: no such file in the root (this tool cannot create files)')
    _, cnid, d = e
    if vol.cat[d + 41] & UF_COMPRESSED:
        sys.exit(f'{path}: compressed on disk, not handled')
    return cnid, d


def put_file(vol, dev, w, vh, path, data, dry):
    bs = vol.bsize
    cnid, d = plan(vol, path, data)
    logical, ext8 = vol.fork_of(d)
    have = struct.unpack_from('>I', vol.cat, d + FORK + 12)[0]
    in8 = sum(c for _, c in ext8)
    need = math.ceil(len(data) / bs)
    newext = list(ext8)
    grow = max(0, need - have)
    if grow:
        if in8 != have:
            sys.exit(f'{path}: uses overflow extents and has to grow: not handled')
        start, bits, bm_ext = allocate(dev, vh, bs, grow)
        used = [i for i, (_, c) in enumerate(newext) if c]
        last = used[-1] if used else -1
        if last >= 0 and sum(newext[last]) == start:
            newext[last] = (newext[last][0], newext[last][1] + grow)
        elif last + 1 < 8:
            newext[last + 1] = (start, grow)
        else:
            sys.exit(f'{path}: all 8 extents used and has to grow: not handled')
    _, all_ext = vol.file_extents(cnid, d)
    if grow:
        all_ext = [e for e in newext if e[1]]
    runs = []
    pos = 0
    for st, c in all_ext:
        runs.append((pos, st, c))
        pos += c * bs
    changed = 0
    padded = data + bytes(need * bs - len(data))
    pending = None      # [device offset, bytes]: contiguous changed blocks go in one write, over adb a write costs a dd
    for pos, st, c in runs:
        for b in range(c):
            off = pos + b * bs
            if off >= len(padded):
                break
            chunk = padded[off:off + bs]
            if dev.read((st + b) * bs, bs) != chunk:
                changed += 1
                if not dry:
                    if pending and pending[0] + len(pending[1]) == (st + b) * bs:
                        pending[1] += chunk
                    else:
                        if pending:
                            w.put(pending[0], bytes(pending[1]))
                        pending = [(st + b) * bs, bytearray(chunk)]
    if pending:
        w.put(pending[0], bytes(pending[1]))
    print(f'{path}: {logical} -> {len(data)} bytes, {changed} blocks differ, {grow} blocks allocated')
    if dry:
        return
    if grow:
        for b in range(start, start + grow):
            bits[b // 8] |= 0x80 >> (b % 8)
        for byte in range(start // 8, (start + grow - 1) // 8 + 1):
            w.put(bitmap_offset(bm_ext, bs, byte), bytes([bits[byte]]))
        free = struct.unpack_from('>I', vh, 48)[0] - grow
        struct.pack_into('>I', vh, 48, free)
    fork = struct.pack('>QII', len(data), struct.unpack_from('>I', vol.cat, d + FORK + 8)[0], have + grow)
    fork += b''.join(struct.pack('>II', *e) for e in newext)
    catalog_put(vol, w, d + FORK, fork)
    catalog_put(vol, w, d + 16, struct.pack('>I', int(time.time()) + HFS_EPOCH))
    vol.cat = bytes(vol.cat[:d + FORK]) + fork + bytes(vol.cat[d + FORK + len(fork):])


def write_headers(w, vh, bs, attrs):
    struct.pack_into('>I', vh, 4, attrs)
    total = struct.unpack_from('>I', vh, 44)[0]
    w.put(1024, bytes(vh))
    w.put(total * bs - 1024, bytes(vh))


def vh_hash(dev):
    return hashlib.sha256(dev.read(1024, 512)).hexdigest()


def undo(dev, path):
    """Only over the exact header this tool left: any mount rewrites it, and old catalog blocks would then corrupt the volume."""
    try:
        ok = open(path + '.state').read().split()
    except OSError:
        sys.exit(f'{path}.state missing: cannot prove the volume is unchanged, refusing')
    if vh_hash(dev) not in ok:
        sys.exit('volume header changed since this put (iOS booted?): undo refused, put the old file back with a new run')
    recs = []
    with open(path, 'rb') as f:
        while h := f.read(12):
            off, n = struct.unpack('<QI', h)
            recs.append((off, f.read(n)))
    for off, old in reversed(recs):
        dev.write(off, old)
    print(f'{len(recs)} writes undone')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--image')
    g.add_argument('--adb')
    ap.add_argument('--dev', default='/dev/block/by-name/userdata')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--undo')
    ap.add_argument('--undo-dir', default='.')
    ap.add_argument('--state', action='store_true')
    ap.add_argument('files', nargs='*', help='ROOTPATH=LOCALFILE')
    a = ap.parse_args()
    dev = Raw(a.image, a.dry_run or a.state) if a.image else Adb(a.adb, a.dev)
    if a.state:
        vh = dev.read(1024, 512)
        why = not_clean(dev, vh, struct.unpack_from('>I', vh, 40)[0])
        print(why or 'clean')
        sys.exit(3 if why else 0)
    if a.undo:
        return undo(dev, a.undo)
    pairs = [x.split('=', 1) for x in a.files]
    if not pairs or any(len(p) != 2 for p in pairs):
        sys.exit('give ROOTPATH=LOCALFILE')
    blobs = [(p, open(l, 'rb').read()) for p, l in pairs]
    vol = hfsx.Vol(dev)
    vh = bytearray(vol.vh)
    check_clean(dev, vh, vol.bsize)
    for p, _ in blobs:      # refuse before the first write
        plan(vol, p, b'')
    if a.dry_run:
        for p, data in blobs:
            put_file(vol, dev, None, vh, p, data, True)
        return
    upath = os.path.join(a.undo_dir, f'undo-{time.strftime("%Y%m%d-%H%M%S")}.bin')
    w = Writer(dev, upath)
    state = open(upath + '.state', 'a')

    def note_header():
        state.write(vh_hash(dev) + '\n')
        state.flush()
        os.fsync(state.fileno())

    attrs = struct.unpack_from('>I', vh, 4)[0]
    write_headers(w, vh, vol.bsize, attrs & ~A_UNMOUNTED)      # a cut from here on reads as dirty, not as clean and wrong
    note_header()
    for p, data in blobs:
        put_file(vol, dev, w, vh, p, data, False)
    write_headers(w, vh, vol.bsize, attrs)
    note_header()
    print(f'done, undo file {upath}')


if __name__ == '__main__':
    main()
