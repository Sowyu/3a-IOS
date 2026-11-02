#!/bin/bash
# Host checks for 01-shim-recovery.patch, in a scratch copy. Touches no source tree and no phone.
#   plans/recovery/check-recovery.sh [PAYLOAD]    PAYLOAD defaults to exp-boot/base.payload, only used to test the build
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
payload=${1:-$root/exp-boot/base.payload}
vb=$root/backup-asteroids/vendor_boot_a.img; rc=$root/backup-asteroids/recovery_a.img; ib=$root/backup-asteroids/init_boot_a.img
live=$root/ios-asteroids/research/asteroids-hw/live.dtb
stock=$root/recovery/ramboot/stock.kernel
t=$(mktemp -d "$here/.check.XXXXXX"); trap '/usr/bin/trash "$t"' EXIT
mkdir -p "$t/asteroids"; cp -R "$root/ios-sweet/asteroids/shim" "$t/asteroids/shim"; /usr/bin/trash "$t/asteroids/shim/out" 2>/dev/null || true
git -C "$t" init -q; git -C "$t" apply "$here/01-shim-recovery.patch"
shim=$t/asteroids/shim; test=$here/test

echo "== patch against the live tree"
git -C "$root/ios-sweet" apply --check "$here/01-shim-recovery.patch" && echo "ok   git apply --check"

echo "== host test, real live.dtb and ramdisks"
python3 -I "$test/make_fixture.py" "$vb" "$rc" "$ib" "$t"
defs=$(python3 -I "$shim/recovery-defs.py" "$vb" "$rc" "$ib")
cc -O2 -Wall -Wextra -Werror -Wno-unused-function -I"$shim" $defs -o "$t/test_recovery" "$test/test_recovery.c"
"$t/test_recovery" "$live" "$t/initrd.bin" > "$t/host.log" || { cat "$t/host.log"; exit 1; }
cat "$t/host.log"

echo "== build without recovery: the same recipe still works"
export ASTEROIDS_DTB=$live MKBOOTIMG=$root/ref/tools/mkbootimg.py
"$shim/build.sh" "$payload" "$t/plain.img" | tail -1
if aarch64-elf-nm "$shim/out/shim.elf" | grep -q recovery_; then echo "FAIL recovery code in the plain build"; exit 1; fi
echo "ok   no recovery code in the plain build"

echo "== build with the real Image"
gzip -dc < "$stock" > "$t/Image.stock"
export SHIM_RECOVERY_VENDOR_BOOT=$vb SHIM_RECOVERY_RAMDISK=$rc SHIM_RECOVERY_INIT_BOOT=$ib
SHIM_RECOVERY_IMAGE=$t/Image.stock "$shim/build.sh" "$payload" "$t/real.img" | tail -1
python3 -I "$here/check-abl.py" "$t/real.img" "$t/Image.stock" "$payload"
SHIM_RECOVERY_IMAGE=$t/Image.stock SHIM_RECOVERYPROBE=1 "$shim/build.sh" "$payload" "$t/probe.img" > /dev/null && echo "ok   probe build"

echo "== QEMU: the jump itself, with a mock Linux"
aarch64-elf-gcc -c "$test/mock_linux.S" -o "$t/mock.o"
aarch64-elf-ld -Ttext=0 -e 0 -o "$t/mock.elf" "$t/mock.o"
aarch64-elf-objcopy -O binary "$t/mock.elf" "$t/mock.bin"
aarch64-elf-gcc -DDTB_PA=0xb4539000 -DKERNEL_PA=0xa8080000 -c "$test/stub.S" -o "$t/stub.o"
aarch64-elf-ld -Ttext=0 -e 0 -o "$t/stub.elf" "$t/stub.o"
aarch64-elf-objcopy -O binary "$t/stub.elf" "$t/stub.bin"
SHIM_RECOVERY_IMAGE=$t/mock.bin "$shim/build.sh" "$payload" "$t/mock.img" > /dev/null
python3 -I "$test/qemu_jump.py" "$t" "$shim/out/Image" "$live" "$t/initrd.bin" "$t/recovery.log"
cat "$t/recovery.log"
get() { awk -v k="LX $1=" 'index($0,k)==1{print substr($0,length(k)+1)}' "$t/recovery.log"; }
[ "$(get 0)" = 00000000b4539000 ] || { echo "FAIL x0 is not the DTB"; exit 1; }
[ "$(get 1)$(get 2)$(get 3)" = 000000000000000000000000000000000000000000000000 ] || { echo "FAIL x1-x3 not zero"; exit 1; }
pc=$(get p); [ $((0x$pc & 0x1fffff)) = 0 ] || { echo "FAIL Linux not 2 MiB aligned: $pc"; exit 1; }
[ $((0x$(get s) & 5)) = 0 ] || { echo "FAIL MMU or D-cache on"; exit 1; }
[ $((0x$(get d) & 0x3c0)) = 960 ] || { echo "FAIL interrupts not masked"; exit 1; }
[ "$(get e)" = 0000000000000004 ] || { echo "FAIL not EL1"; exit 1; }
echo "ok   recovery mode reached Linux at $pc, x0=DTB, x1-x3=0, MMU and D-cache off, DAIF masked, EL1"

echo "== QEMU: normal boot must not reach Linux"
python3 -I - "$live" "$t/normal.dtb" "$t/initrd.bin" "$t/normal.initrd" "$vb" "$rc" "$ib" <<'PY'
import struct, sys
live, out_dtb, ini, out_ini, vb, rc, ib = sys.argv[1:]
d = bytearray(open(live, 'rb').read())
# The DTB ABL gives a normal boot: same node, shorter initrd. Stale recovery ramdisk stays in RAM: only the size shrinks.
i = open(ini, 'rb').read()
v = struct.unpack_from('<I', open(vb, 'rb').read(4096), 24)[0]
r = struct.unpack_from('<I', open(rc, 'rb').read(4096), 12)[0]
p = bytes(d).index(struct.pack('>Q', 0xb7fff3e8))
d[p:p + 8] = struct.pack('>Q', 0xb473a000 + v + 2564096 + 1361)
open(out_dtb, 'wb').write(d)
open(out_ini, 'wb').write(i)
PY
python3 -I "$test/qemu_jump.py" "$t" "$shim/out/Image" "$t/normal.dtb" "$t/normal.initrd" "$t/normal.log" fb | tail -3
if grep -q '^LX' "$t/normal.log"; then echo "FAIL normal boot started Linux"; exit 1; fi
echo "ok   normal boot stayed out of Linux"

echo "== QEMU: real shim, real stock kernel, real OrangeFox initrd (no phone hardware, it stops in first-stage init)"
SHIM_RECOVERY_IMAGE=$t/Image.stock "$shim/build.sh" "$payload" "$t/real2.img" > /dev/null
qemu-system-aarch64 -M virt,gic-version=3,dumpdtb="$t/virt.dtb" -cpu cortex-a76 -m 4G -display none 2>/dev/null
dtc -q -I dtb -O dts "$t/virt.dtb" -o "$t/virt.dts"
python3 -I - "$t/virt.dts" <<'PY'
import sys
s = open(sys.argv[1]).read()
s = s.replace('chosen {', 'chosen {\n\t\tbootargs = "earlycon=pl011,0x9000000 console=ttyAMA0 panic=-1 bootconfig";\n'
	'\t\tlinux,initrd-start = <0x0 0xb473a000>;\n\t\tlinux,initrd-end = <0x0 0xb7fff3e8>;', 1)
open(sys.argv[1], 'w').write(s)
PY
dtc -q -I dts -O dtb "$t/virt.dts" -o "$t/virt2.dtb"
python3 -I "$test/qemu_jump.py" "$t" "$shim/out/Image" "$t/virt2.dtb" "$t/initrd.bin" "$t/e2e.log" run30
for want in 'Load bootconfig: 1341 bytes' 'Freeing initrd memory' 'Run /init as init process' 'init first stage started'; do
	grep -q "$want" "$t/e2e.log" || { echo "FAIL no '$want'"; tail -5 "$t/e2e.log"; exit 1; }
	echo "ok   $want"
done

echo "== other planned shim patches still apply on top"
for p in "$root/plans/bootslot/01-shim-show-retries.patch"; do
	git -C "$t" apply --check --directory=asteroids/shim "$p" && echo "ok   $(basename "$p")" || echo "note $(basename "$p") does not apply on top: rebase it"
done
echo "check-recovery OK"
