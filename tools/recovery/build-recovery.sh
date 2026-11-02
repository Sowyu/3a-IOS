#!/bin/bash
# Build the two recovery boot images in a scratch copy of the shim. No source tree changes, no phone.
#   plans/recovery/build-recovery.sh PAYLOAD
# Writes plans/recovery/out/recovery-probe-boot.img (shows the checks, starts nothing) and recovery-boot.img (the real one).
# The ramdisk hash comes from these backups. Reflash recovery_a, vendor_boot_a or init_boot_a and the shim boots iOS until rebuilt.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/../.." && pwd)
payload=$(python3 -I -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "${1:?usage: build-recovery.sh PAYLOAD}")
out=$here/out; mkdir -p "$out"
t=$(mktemp -d "$here/.build.XXXXXX"); trap '/usr/bin/trash "$t"' EXIT
mkdir -p "$t/asteroids"; cp -R "$root/ios-sweet/asteroids/shim" "$t/asteroids/shim"; /usr/bin/trash "$t/asteroids/shim/out" 2>/dev/null || true
git -C "$t" init -q; git -C "$t" apply "$here/01-shim-recovery.patch"
gzip -dc < "$root/recovery/ramboot/stock.kernel" > "$t/Image"
export ASTEROIDS_DTB=$root/ios-asteroids/research/asteroids-hw/live.dtb MKBOOTIMG=$root/ref/tools/mkbootimg.py
export SHIM_RECOVERY_IMAGE=$t/Image SHIM_RECOVERY_VENDOR_BOOT=$root/backup-asteroids/vendor_boot_a.img
export SHIM_RECOVERY_RAMDISK=$root/backup-asteroids/recovery_a.img SHIM_RECOVERY_INIT_BOOT=$root/backup-asteroids/init_boot_a.img
SHIM_RECOVERYPROBE=1 "$t/asteroids/shim/build.sh" "$payload" "$out/recovery-probe-boot.img" | tail -1
"$t/asteroids/shim/build.sh" "$payload" "$out/recovery-boot.img" | tail -1
python3 -I "$here/check-abl.py" "$out/recovery-boot.img" "$t/Image" "$payload"
(cd "$out" && shasum -a 256 recovery-probe-boot.img recovery-boot.img)
