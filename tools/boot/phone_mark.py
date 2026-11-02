#!/usr/bin/env python3
"""ONE-TIME, owner-approved: mark boot_a successful in both LUN 4 GPT copies, from OrangeFox (adb root).
Replaces phone_mark.sh after analysis/gptwrite-audit.md (stdin swallowed by adb, masked dd failures, iflag=direct).
Usage: phone_mark.py SERIAL OUTDIR   (OUTDIR must not exist; everything read is saved there). Add --write to write;
without it the script only checks and plans."""
import hashlib, os, subprocess, sys
sys.dont_write_bytecode = True
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import mark_successful as m

DEV, BOOT_A_NODE = '/dev/block/sde', '/dev/block/sde11'
WANT_A, WANT_B = 0x37, 0x3b         # reviewed live state 2026-11-02 18:45: a priority 3 active retry 6, b inactive retry 7


def die(msg):
    sys.exit('STOP: ' + msg)


def adb(serial, *args, binary=False):
    r = subprocess.run(['adb', '-s', serial, *args], stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
    if r.returncode != 0:
        die('adb %s failed (%d): %s' % (' '.join(args)[:80], r.returncode, r.stderr.decode(errors='replace')[:200]))
    return r.stdout if binary else r.stdout.decode().strip()


def read_blocks(serial, lba, count):
    data = adb(serial, 'exec-out', 'dd if=%s bs=4096 skip=%d count=%d 2>/dev/null' % (DEV, lba, count), binary=True)
    if len(data) != count * m.BLOCK:
        die('read of LBA %#x x%d returned %d bytes' % (lba, count, len(data)))
    return data


def sha(b):
    return hashlib.sha256(b).hexdigest()


def main():
    if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and sys.argv[3] != '--write'):
        sys.exit(__doc__)
    serial, out, write = sys.argv[1], sys.argv[2], len(sys.argv) == 4
    os.makedirs(out)                                            # fails if it exists: no stale plan files
    if adb(serial, 'get-state') != 'recovery':
        die('not in recovery')
    if adb(serial, 'shell', 'id -u') != '0':
        die('adbd is not root')
    if adb(serial, 'shell', 'test -b %s && echo blk' % DEV) != 'blk':
        die(DEV + ' is not a block device')
    if adb(serial, 'shell', 'cat /sys/block/sde/queue/logical_block_size') != '4096':
        die('logical block size is not 4096')
    if adb(serial, 'shell', 'cat /sys/block/sde/size') != '8388608':
        die('sde is not the 4 GiB LUN 4')
    if adb(serial, 'shell', 'readlink -f /dev/block/by-name/boot_a') != BOOT_A_NODE:
        die('boot_a is not ' + BOOT_A_NODE)
    primary, backup = read_blocks(serial, 1, 5), read_blocks(serial, m.LAST_LBA - 4, 5)
    for n, d in (('primary.bin', primary), ('backup.bin', backup)):
        open(os.path.join(out, n), 'wb').write(d)
    stock = open(os.path.join(here, '../../backup-asteroids/gpt-sde.bin'), 'rb').read()
    try:
        writes = m.plan(primary, backup, stock)
    except m.Refuse as e:
        die('plan refused: %s' % e)
    a, b = primary[m.BLOCK + m.ATTR_OFFSET], primary[m.BLOCK + m.BOOT_B * m.ENTRY_SIZE + m.ATTR_TOP]
    if (a, b) != (WANT_A, WANT_B):
        die('attributes are a=%#x b=%#x, reviewed state is a=%#x b=%#x' % (a, b, WANT_A, WANT_B))
    for n, (lba, old, new) in enumerate(writes):
        open(os.path.join(out, 'old%d.bin' % n), 'wb').write(old)
        open(os.path.join(out, 'new%d.bin' % n), 'wb').write(new)
        print('write %d: LBA %#x, %d bytes change' % (n, lba, sum(x != y for x, y in zip(old, new))))
    if not write:
        print('plan ok, nothing written (add --write)')
        return
    tag = 'ms%d' % os.getpid()
    for n, (lba, old, new) in enumerate(writes):
        remote = '/tmp/%s-%d.bin' % (tag, n)
        adb(serial, 'push', os.path.join(out, 'new%d.bin' % n), remote)
        if adb(serial, 'shell', "stat -c %%s %s" % remote) != '4096' or \
           adb(serial, 'shell', 'sha256sum %s' % remote).split()[0] != sha(new):
            die('write %d: staged block is not the planned one' % n)
        if read_blocks(serial, lba, 1) != old:
            die('write %d: LBA %#x changed since the plan' % (n, lba))
        ok = adb(serial, 'shell', 'dd if=%s of=%s bs=4096 seek=%d count=1 conv=notrunc,fsync 2>/dev/null && sync && echo WROTE'
                 % (remote, DEV, lba))
        if not ok.endswith('WROTE'):
            die('write %d: dd did not complete' % n)
        adb(serial, 'shell', 'sync; echo 3 > /proc/sys/vm/drop_caches')
        if read_blocks(serial, lba, 1) != new:
            die('write %d: read-back of LBA %#x differs' % (n, lba))
        print('write %d at LBA %#x verified' % (n, lba))
        if n == 1:
            bk = read_blocks(serial, m.LAST_LBA - 4, 5)
            if not m.copy_valid(bk[-m.BLOCK:], bk[:m.ARRAY_BYTES], m.LAST_LBA):
                die('backup copy invalid after writing it; primary untouched')
    p2, b2 = read_blocks(serial, 1, 5), read_blocks(serial, m.LAST_LBA - 4, 5)
    pa, ba = p2[m.BLOCK:m.BLOCK + m.ARRAY_BYTES], b2[:m.ARRAY_BYTES]
    if not (m.copy_valid(p2[:m.BLOCK], pa, 1) and m.copy_valid(b2[-m.BLOCK:], ba, m.LAST_LBA) and pa == ba):
        die('final tables do not validate')
    if (pa[m.ATTR_OFFSET], pa[m.BOOT_B * m.ENTRY_SIZE + m.ATTR_TOP]) != (WANT_A | m.SUCCESSFUL, WANT_B):
        die('final attributes unexpected')
    print('DONE: boot_a marked successful in both copies')


if __name__ == '__main__':
    main()
