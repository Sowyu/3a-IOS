#!/bin/bash
# ONE-TIME, owner-approved: mark boot_a successful on the phone, from OrangeFox (adb root). Reads LUN 4 again, re-plans with
# mark_successful.py (which refuses anything but the expected state), then writes four 4 KiB blocks of /dev/block/sde in the
# planned order (backup entries, backup header, primary entries, primary header), reading each back before the next.
# Stops at the first mismatch. Saves everything it read in $OUT.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:?set OUT to an empty directory for the reads and the plan}
DEV=/dev/block/sde
[ "$(adb get-state 2>/dev/null)" = recovery ] || { echo "phone not in recovery"; exit 1; }
[ "$(adb shell cat /sys/block/sde/size | tr -d '\r')" = 8388608 ] || { echo "sde is not the 4 GiB LUN 4"; exit 1; }
[ "$(adb shell readlink -f /dev/block/by-name/boot_a | tr -d '\r' | sed 's/[0-9]*$//')" = $DEV ] || { echo "boot_a is not on $DEV"; exit 1; }
adb shell "dd if=$DEV of=/tmp/p.bin bs=4096 skip=1 count=5 2>/dev/null; dd if=$DEV of=/tmp/b.bin bs=4096 skip=1048571 count=5 2>/dev/null"
adb pull /tmp/p.bin "$OUT/primary.bin" >/dev/null; adb pull /tmp/b.bin "$OUT/backup.bin" >/dev/null
python3 -I - "$here" "$OUT" <<'P'
import sys, os
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1]); import mark_successful as m
out = sys.argv[2]
p, b = (open(os.path.join(out, n), 'rb').read() for n in ('primary.bin', 'backup.bin'))
s = open(os.path.join(sys.argv[1], '../../backup-asteroids/gpt-sde.bin'), 'rb').read()
try:
    writes = m.plan(p, b, s)
except m.Refuse as e:
    sys.exit('REFUSED: %s' % e)
with open(os.path.join(out, 'plan.txt'), 'w') as f:
    for n, (lba, old, new) in enumerate(writes):
        open(os.path.join(out, 'old%d.bin' % n), 'wb').write(old)
        open(os.path.join(out, 'new%d.bin' % n), 'wb').write(new)
        f.write('%d %d\n' % (n, lba))
print('plan ok:', [hex(l) for l, _, _ in writes])
P
while read n lba; do
	adb push "$OUT/new$n.bin" /tmp/new$n.bin >/dev/null
	cur=$(adb shell "dd if=$DEV bs=4096 skip=$lba count=1 2>/dev/null | sha256sum" | cut -c1-64)
	[ "$cur" = "$(shasum -a 256 "$OUT/old$n.bin" | cut -c1-64)" ] || { echo "write $n: LBA $lba changed since the plan, stopping"; exit 1; }
	adb shell "dd if=/tmp/new$n.bin of=$DEV bs=4096 seek=$lba count=1 conv=notrunc,fsync 2>/dev/null; sync"
	got=$(adb shell "echo 3 > /proc/sys/vm/drop_caches; dd if=$DEV bs=4096 skip=$lba count=1 iflag=direct 2>/dev/null | sha256sum" | cut -c1-64)
	[ "$got" = "$(shasum -a 256 "$OUT/new$n.bin" | cut -c1-64)" ] || { echo "write $n: read-back of LBA $lba differs, stopping"; exit 1; }
	echo "write $n at LBA $lba verified"
done < "$OUT/plan.txt"
echo DONE
