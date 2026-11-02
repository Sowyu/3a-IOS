#!/bin/bash
# Hands-on boot images for the combined kernel (2130, 2150 to 2153 with 2152 v4, lag 2140 to 2143, all off unless set).
# next-fix's ADT and args without sweetsyserr, no self-test, plus the input args. Writes only next-combined/out. Touches no phone.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root="$here/.."; repo="$root/ios-asteroids"; nf="$root/next-fix/boot"
kernel="${KERNEL:-$here/kernel/kernel.development.sweet}"
out="${OUT:-$here/out}"; mkdir -p "$out"
args=$(sed -n "s/^args='\(.*\)'\$/\1/p" "$nf/build.sh")
[ -n "$args" ] || { echo "cannot read the args from $nf/build.sh" >&2; exit 1; }
shim="$out/shim"; [ ! -e "$shim" ] || /usr/bin/trash "$shim"; cp -R "$root/ios-sweet/asteroids/shim" "$shim"
mk() {
	python3 -I "$repo/tools/mkpayload.py" "$kernel" "$root/ux-fix/ux-420pt-2x-840x1860.adt" "$out/$1.payload" --cmdline "$2"
	ASTEROIDS_DTB="$repo/research/asteroids-hw/live.dtb" MKBOOTIMG="$root/ref/tools/mkbootimg.py" bash "$shim/build.sh" "$out/$1.payload" "$out/$1-boot.img"
	echo "$1: ${#2} of 607 bytes, $(wc -c < "$out/$1-boot.img") bytes image"
}
daily="${args/ sweetsyserr=backboardd,MobileSafari/} sweetthermal=3 sweetfetchsync=1 sweetsb=1 sweetfb_ast_hw=1"
input='sweettouchirq=3 sweetkeys_ms=5 sweetfb_lead=3000'
mk combo-daily "$daily $input"                       # power button: keys reach iOS faster, iOS blanks the screen itself
mk combo-daily-pwr "$daily $input sweetpwrfast=1"     # plus the instant blank (2152 v4, Sol GO with the frozen-scanout limit accepted)
mk combo-all "$daily $input sweetpwrfast=1 sweetfb_keep=1 sweetfb_astwait=1"   # plus keep-frames with honest SwapWait (2142), the lag fix: never keep without astwait
mk combo-keep "$daily $input sweetfb_keep=1 sweetfb_astwait=1"   # the daily candidate: owner on 2026-11-03, power button like an iPhone, so no sweetpwrfast
mk combo-keep-30 "${daily/sweetfb_hz=60/sweetfb_hz=30} $input sweetfb_keep=1 sweetfb_astwait=1 sweetfb_vsync=1"   # lag agent: CoreAnimation told 30 Hz, every frame shown for 2 vsyncs, for even pacing
mk combo-ub "$daily ${input/ sweetfb_lead=3000/} sweetfb_keep=1 sweetfb_astwait=1 sweettu=sysmem,nocb,nolrz"   # lag night 2026-11-03: UBWC on (needs plans/lag/next-luma-ub dylibs), lead dropped as in the measured arm
mk combo-daily-ff "$daily $input sweetappmetal=Freeform"   # Freeform phase 2b, only with its libraries pushed
# hands-free test of the kernel without hw scale, so the self-test's frames are readable
loop='sweetthermal=0 sweetfetchsync=1 sweetdeadman=600 sweetselftest=1'
mk combo-loop-nohw "${args/ sweetsyserr=backboardd,MobileSafari/} $loop sweetsb=1 $input sweetfb_keep=1 sweetfb_astwait=1"
