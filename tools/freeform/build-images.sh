#!/bin/bash
# Freeform trial boot images: plans/devloop/build-loop.sh's next-loop-sb plus sweetappmetal=Freeform, and the stub fallback.
# Builds only under plans/freeform/out, on a copy of the shim. Touches no phone.
#   build-images.sh KERNEL          a kernel built with 2130-asteroids-app-metal-exec.patch (refused otherwise)
#   build-images.sh --lengths       boot-arg lengths only
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root="$here/../.."; repo="$root/ios-asteroids"; nf="$root/next-fix/boot"
args=$(sed -n "s/^args='\(.*\)'\$/\1/p" "$nf/build.sh")
sb=$(sed -n "s/^sb='\(.*\)'\$/\1/p" "$nf/build.sh")
[ -n "$args" ] && [ -n "$sb" ] || { echo "cannot read the args from $nf/build.sh" >&2; exit 1; }
loop='sweetthermal=0 sweetfb_tickfix=1 sweetfetchsync=1 sweetdeadman=600 sweetselftest=1'
no_syserr=${args/ sweetsyserr=backboardd,MobileSafari/}
metal="$no_syserr $loop $sb sweetappmetal=Freeform"
stub="$metal sweetappstub=1"
if [ "${1:-}" = --lengths ]; then echo "ff-appmetal ${#metal} of 607, ff-appstub ${#stub} of 607"; exit 0; fi
kernel=${1:?usage: build-images.sh KERNEL}
# 2130's log line: a kernel without it would boot the trial image with no inject in Freeform, and the run would mislead
strings "$kernel" | grep -q 'SWEET EXEC: appmetal inject for' || { echo "$kernel has no 2130 (no 'SWEET EXEC: appmetal inject')" >&2; exit 1; }
out="$here/out"; mkdir -p "$out"
shim="$out/shim"; [ ! -e "$shim" ] || /usr/bin/trash "$shim"; cp -R "$root/ios-sweet/asteroids/shim" "$shim"
mk() {
	[ ${#2} -le 607 ] || { echo "$1: ${#2} bytes of boot-args, over 607" >&2; exit 1; }
	python3 -I "$repo/tools/mkpayload.py" "$kernel" "$root/ux-fix/ux-420pt-2x-840x1860.adt" "$out/$1.payload" --cmdline "$2"
	ASTEROIDS_DTB="$repo/research/asteroids-hw/live.dtb" MKBOOTIMG="$root/ref/tools/mkbootimg.py" bash "$shim/build.sh" "$out/$1.payload" "$out/$1-boot.img"
	echo "$1: ${#2} of 607 bytes, $(stat -f %z "$out/$1-boot.img") bytes of image"
}
mk ff-appmetal "$metal"
mk ff-appstub "$stub"
