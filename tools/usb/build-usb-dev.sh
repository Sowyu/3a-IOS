#!/bin/bash
# USB dev-session images (README.md, "Dev session"), on the template of plans/input/build-touch-owner.sh: next-combined's
# combo-ub (UBWC, keep + astwait, sweetthermal=3) on the v5 kernel (the daily tree plus 2160/2161 v5) with sweetusb=4 and
# dev.py's arming, sweetdeadman=7200 and sweetselftest=1. To fit 607 bytes it drops keepsyms=1 (symbols in panic logs
# only), sweettouchirq=3 (touch is polled instead) and sweetkeys_ms=5 (keys polled at the default rate). That is still
# 611, so it also drops sweetusblog=0, which v5 makes the default: the live UDP log stays off. Touches no phone.
#   usb-dev     sweetdeadman=7200: a session of up to 115 minutes (the self-test ends 300 s before the deadman)
#   usb-dev-t   the same with sweetdeadman=960, for a test run under 20 minutes
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root="$here/../../.."; repo="$root/ios-asteroids"; nf="$root/next-fix/boot"
args=$(sed -n "s/^args='\(.*\)'\$/\1/p" "$nf/build.sh")
[ -n "$args" ] || { echo "cannot read the args from $nf/build.sh" >&2; exit 1; }
args=${args/ keepsyms=1/}
daily="${args/ sweetsyserr=backboardd,MobileSafari/} sweetthermal=3 sweetfetchsync=1 sweetsb=1 sweetfb_ast_hw=1"
dev="$daily sweetfb_keep=1 sweetfb_astwait=1 sweettu=sysmem,nocb,nolrz sweetusb=4"
kernel=${KERNEL:-$here/kernel-v5/kernel.development.sweet}
out="$here/out-v5"; mkdir -p "$out"
shim="$out/shim"; [ ! -e "$shim" ] || /usr/bin/trash "$shim"; cp -R "$root/ios-sweet/asteroids/shim" "$shim"
mk() {
	[ ${#2} -le 607 ] || { echo "$1: ${#2} bytes of boot-args, over 607" >&2; exit 1; }
	python3 -I "$repo/tools/mkpayload.py" "$kernel" "$root/ux-fix/ux-420pt-2x-840x1860.adt" "$out/$1.payload" --cmdline "$2"
	ASTEROIDS_DTB="$repo/research/asteroids-hw/live.dtb" MKBOOTIMG="$root/ref/tools/mkbootimg.py" bash "$shim/build.sh" "$out/$1.payload" "$out/$1-boot.img"
	echo "$1: ${#2} of 607 bytes, $(stat -f %z "$out/$1-boot.img") bytes of image"
}
mk usb-dev "$dev sweetdeadman=7200 sweetselftest=1"
mk usb-dev-t "$dev sweetdeadman=960 sweetselftest=1"
