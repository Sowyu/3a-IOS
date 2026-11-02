#!/bin/bash
# Loop boot images for the input tests (plans/input/README.md), a copy of plans/lag/build-lag.sh: next-fix's ADT and boot-args,
# the hands-free loop args and the hw scale, plus each variant's input args. Writes only plans/input/out. Touches no phone.
#   build-input-loop.sh KERNEL      (a kernel built with 2150..2153 applied)
# The loop's sweetfb_tickfix=1 is dropped: 2081 turned it on by default, and the 18 bytes are needed. in-div also drops
# sweetpanicfb=10, and so does in-ca120. Every line is checked against the 607-byte limit by mkpayload.py.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd); root="$here/../.."; repo="$root/ios-asteroids"; nf="$root/next-fix/boot"
kernel=${1:?usage: build-input-loop.sh KERNEL}
out="$here/out"; mkdir -p "$out"
args=$(sed -n "s/^args='\(.*\)'\$/\1/p" "$nf/build.sh")
sb=$(sed -n "s/^sb='\(.*\)'\$/\1/p" "$nf/build.sh")
[ -n "$args" ] && [ -n "$sb" ] || { echo "cannot read the args from $nf/build.sh" >&2; exit 1; }
loop='sweetthermal=0 sweetfetchsync=1 sweetdeadman=600 sweetselftest=1'
shim="$out/shim"; [ ! -e "$shim" ] || /usr/bin/trash "$shim"; cp -R "$root/ios-sweet/asteroids/shim" "$shim"
mk() {
	python3 -I "$repo/tools/mkpayload.py" "$kernel" "$root/ux-fix/ux-420pt-2x-840x1860.adt" "$out/$1.payload" --cmdline "$2"
	ASTEROIDS_DTB="$repo/research/asteroids-hw/live.dtb" MKBOOTIMG="$root/ref/tools/mkbootimg.py" bash "$shim/build.sh" "$out/$1.payload" "$out/$1-boot.img"
	echo "$1: ${#2} of 607 bytes"
}
base="${args/ sweetsyserr=backboardd,MobileSafari/} $loop $sb sweetfb_ast_hw=1"
all='sweettouchirq=7 sweetkeys_ms=5 sweetpwrfast=1 sweetfb_lead=3000'
mk in-stats "$base sweettouchirq=4"
mk in-all "$base $all"
mk in-panel "${base/sweetpanel=0/sweetpanel=1} ${all/sweetpwrfast=1/sweetpwrfast=3}"
mk in-div "${base/ sweetpanicfb=10/} $all sweettouchdiv=4"
# optional, frame pacing (agree with the lag agent first): ticks at 120 Hz; then CoreAnimation told at 120 Hz too
mk in-120 "${base/sweetfb_hz=60/sweetfb_hz=120} $all"
b120=${base/sweetfb_hz=60/sweetfb_hz=120}
mk in-ca120 "${b120/ sweetpanicfb=10/} $all sweetfb_ca120=1"
