#!/bin/bash
# Owner touch-timing image (2026-11-03): next-combined's combo-ub (kernel-daily2, UBWC, keep + astwait) with sweettouchirq=7
# (stats line per touch) and dev.py's loop args. To fit 607 bytes it drops sweetkeys_ms=5 (keys, not used here) and
# keepsyms=1 (symbols in panic logs only). Needs the next-luma-ub driver on the root (plans/lag/owner-ub.sh). Touches no phone.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root="$here/../.."; repo="$root/ios-asteroids"; nf="$root/next-fix/boot"
args=$(sed -n "s/^args='\(.*\)'\$/\1/p" "$nf/build.sh")
[ -n "$args" ] || { echo "cannot read the args from $nf/build.sh" >&2; exit 1; }
args=${args/ keepsyms=1/}
name=${NAME:-touch-owner}
daily="${args/ sweetsyserr=backboardd,MobileSafari/} sweetthermal=3 sweetfetchsync=1 sweetsb=1 sweetfb_ast_hw=1"
owner="$daily sweettouchirq=7 sweetfb_keep=1 sweetfb_astwait=1 sweettu=sysmem,nocb,nolrz sweetdeadman=900 sweetselftest=1"
[ ${#owner} -le 607 ] || { echo "$name: ${#owner} bytes of boot-args, over 607" >&2; exit 1; }
kernel=${KERNEL:-$root/next-combined/kernel-daily2/kernel.development.sweet}     # KERNEL=... for kernel-touch (2154, tap times)
out="$here/out"; mkdir -p "$out"
shim="$out/shim"; [ ! -e "$shim" ] || /usr/bin/trash "$shim"; cp -R "$root/ios-sweet/asteroids/shim" "$shim"
python3 -I "$repo/tools/mkpayload.py" "$kernel" "$root/ux-fix/ux-420pt-2x-840x1860.adt" "$out/$name.payload" --cmdline "$owner"
ASTEROIDS_DTB="$repo/research/asteroids-hw/live.dtb" MKBOOTIMG="$root/ref/tools/mkbootimg.py" bash "$shim/build.sh" "$out/$name.payload" "$out/$name-boot.img"
echo "$name: ${#owner} of 607 bytes, $(stat -f %z "$out/$name-boot.img") bytes of image"
