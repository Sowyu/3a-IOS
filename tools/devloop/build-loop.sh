#!/bin/bash
# Loop boot images for the root that is on the phone: next-fix's kernel, ADT and boot-args, plus the two loop args. Builds
# only under plans/devloop/out, on a copy of the shim, because the shim's build.sh writes its own out/. Touches no phone.
#   build-loop.sh [KERNEL]
# next-all is at 589 of 607 bytes and the loop needs 33 more. next-loop drops sweetexecenv (the status bar dylib, 73 bytes).
# next-loop-sb keeps the dylib for status bar tests and drops sweetsyserr instead, so those syscall logs go.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root="$here/../.."; repo="$root/ios-asteroids"; nf="$root/next-fix/boot"
kernel=${1:-$root/next-fix/kernel/kernel.development.sweet}
out="$here/out"; mkdir -p "$out"
args=$(sed -n "s/^args='\(.*\)'\$/\1/p" "$nf/build.sh")
sb=$(sed -n "s/^sb='\(.*\)'\$/\1/p" "$nf/build.sh")
[ -n "$args" ] && [ -n "$sb" ] || { echo "cannot read the args from $nf/build.sh" >&2; exit 1; }
loop='sweetthermal=0 sweetfb_tickfix=1 sweetfetchsync=1 sweetdeadman=600 sweetselftest=1'
shim="$out/shim"; [ ! -e "$shim" ] || /usr/bin/trash "$shim"; cp -R "$root/ios-sweet/asteroids/shim" "$shim"
mk() {
	python3 -I "$repo/tools/mkpayload.py" "$kernel" "$root/ux-fix/ux-420pt-2x-840x1860.adt" "$out/$1.payload" --cmdline "$2"
	ASTEROIDS_DTB="$repo/research/asteroids-hw/live.dtb" MKBOOTIMG="$root/ref/tools/mkbootimg.py" bash "$shim/build.sh" "$out/$1.payload" "$out/$1-boot.img"
	echo "$1: ${#2} of 607 bytes"
}
mk next-loop "$args $loop"
no_syserr=${args/ sweetsyserr=backboardd,MobileSafari/}
mk next-loop-sb "$no_syserr $loop $sb"
# one change each against next-loop-sb, for unattended comparison with --test ui
mk next-loop-bar "$no_syserr $loop $sb sweetfb_barcheck=1"
mk next-loop-hw "$no_syserr $loop $sb sweetfb_ast_hw=1"
mk next-loop-amp "$no_syserr $loop $sb sweetamp_prio=20"
mk next-loop-gpu8 "$no_syserr $loop $sb sweeta810level=8"
mk next-loop-keep "$no_syserr $loop $sb sweetfb_keep=1"
mk next-loop-120 "${no_syserr/sweetfb_hz=60/sweetfb_hz=120} $loop $sb"
mk next-loop-hwtest "$no_syserr $loop $sb sweetfb_ast_hw=1 sweetfb_hwtest=60"
mk next-loop-cputest "$no_syserr $loop $sb sweetfb_hwtest=60"
mk next-loop-thermal "$no_syserr ${loop/sweetthermal=0/sweetthermal=1} $sb"
mk next-loop-ext "$no_syserr $loop $sb sweetext=1"
