#!/usr/bin/env python3
"""UNTESTED ON A PHONE. Host check: check-05-journal.sh (a real journal on a throwaway volume, Mac HFS driver).

What: turns on the HFS+ journal of a FINISHED raw root image, as the very last step before img2simg, after hfsroot_owners.py
(which refuses journaled images). macOS's `diskutil enableJournal` makes the journal files. This script then sets
kJIJournalNeedInit, so xnu's hfs_early_journal_init writes a new journal header sized for the phone's block device on the
first mount instead of opening one made for hdiutil's 512-byte sectors. Same state NOTES.md records for journaled image v4.

Apply, macOS only, on a copy you can lose:
  simg2img.py --consume ROOT.simg ROOT.img && 05-enable-journal.py ROOT.img && img2simg.py --consume ROOT.img ROOT.simg
then a full userdata flash. Expect `hfs: Initializing the journal` in the first boot's log and no
`mounting dirty non-journaled` line on any later boot.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ATTR_UNMOUNTED, ATTR_INCONSISTENT, ATTR_JOURNALED = 0x100, 0x800, 0x2000
JI_IN_FS, JI_NEED_INIT = 0x1, 0x4


def header(f):
    f.seek(1024)
    vh = f.read(512)
    if vh[:2] not in (b'H+', b'HX'):
        sys.exit('not an HFS+/HFSX image')
    return struct.unpack_from('>I', vh, 4)[0], struct.unpack_from('>I', vh, 12)[0], struct.unpack_from('>I', vh, 40)[0]


def run(*cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def main():
    img = sys.argv[1]
    with open(img, 'rb') as f:
        attrs, _, _ = header(f)
    if attrs & ATTR_JOURNALED:
        sys.exit('already journaled')
    if not attrs & ATTR_UNMOUNTED or attrs & ATTR_INCONSISTENT:
        sys.exit('volume is dirty or inconsistent: run fsck_hfs first')
    mnt = tempfile.mkdtemp(prefix='hfsj')
    litter = tempfile.mkdtemp(prefix='hfsj-litter')
    run('hdiutil', 'attach', '-nobrowse', '-mountpoint', mnt, '-imagekey', 'diskimage-class=CRawDiskImage', img)
    try:
        run('diskutil', 'enableJournal', mnt)
        for name in ('.fseventsd', '.Spotlight-V100', '.Trashes'):    # macOS's mount litter, never on the phone
            p = os.path.join(mnt, name)
            if os.path.exists(p):
                shutil.move(p, litter)
    finally:
        run('hdiutil', 'detach', mnt)
        run('/usr/bin/trash', mnt, litter)
    with open(img, 'r+b') as f:
        attrs, jib, bs = header(f)
        if not attrs & ATTR_JOURNALED or not attrs & ATTR_UNMOUNTED or not jib:
            sys.exit('journal not enabled or volume not clean: attrs %#x jib %d' % (attrs, jib))
        f.seek(jib * bs)
        flags = struct.unpack('>I', f.read(4))[0]
        if not flags & JI_IN_FS:
            sys.exit('journal is not inside the volume: flags %#x' % flags)
        f.seek(jib * bs)
        f.write(struct.pack('>I', flags | JI_NEED_INIT))
    disk = run('hdiutil', 'attach', '-nomount', '-imagekey', 'diskimage-class=CRawDiskImage', img).split()[0]
    try:
        r = subprocess.run(['fsck_hfs', '-fn', disk.replace('/disk', '/rdisk')], capture_output=True, text=True)
    finally:
        run('hdiutil', 'detach', disk)
    print(r.stdout.strip().split('\n')[-1])
    if r.returncode:
        sys.exit('fsck_hfs rc %d:\n%s' % (r.returncode, r.stdout))
    print('journal enabled, need-init set')


if __name__ == '__main__':
    main()
