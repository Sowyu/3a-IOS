#!/bin/bash
# Loop boot images for the USB gadget levels (README.md), a copy of 3a-IOS/tools/input/build-input-loop.sh: next-fix's
# ADT and boot-args, the hands-free loop args (sweetfbarm=1, sweetpanicfb=10, sweetdeadman=600, sweetselftest=1) and
# the hw scale, plus sweetusb=N. Writes only plans/usb/gadget/out. Touches no phone.
#   build-usb-loop.sh KERNEL      (the combined kernel rebuilt with 2160 and 2161)
# mkpayload.py refuses any line over 607 bytes; these are 548 or 549.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root="$here/../../.."; repo="$root/ios-asteroids"; nf="$root/next-fix/boot"
kernel=${1:?usage: build-usb-loop.sh KERNEL}
out="$here/out"; mkdir -p "$out"
args=$(sed -n "s/^args='\(.*\)'\$/\1/p" "$nf/build.sh")
sb=$(sed -n "s/^sb='\(.*\)'\$/\1/p" "$nf/build.sh")
[ -n "$args" ] && [ -n "$sb" ] || { echo "cannot read the args from $nf/build.sh" >&2; exit 1; }
loop='sweetthermal=0 sweetfetchsync=1 sweetdeadman=600 sweetselftest=1'
shim="$out/shim"; [ ! -e "$shim" ] || /usr/bin/trash "$shim"; cp -R "$root/ios-sweet/asteroids/shim" "$shim"
mk() {
	python3 -I "$repo/tools/mkpayload.py" "$kernel" "$root/ux-fix/ux-420pt-2x-840x1860.adt" "$out/$1.payload" --cmdline "$2"
	ASTEROIDS_DTB="$repo/research/asteroids-hw/live.dtb" MKBOOTIMG="$root/ref/tools/mkbootimg.py" bash "$shim/build.sh" "$out/$1.payload" "$out/$1-boot.img"
	echo "$1: ${#2} of 607 bytes, $(wc -c < "$out/$1-boot.img") bytes image"
}
base="${args/ sweetsyserr=backboardd,MobileSafari/} $loop $sb sweetfb_ast_hw=1"
mk usb-l2 "$base sweetusb=2"        # own the halted core, our event buffer, one CmdIOC event; no RUN_STOP
mk usb-l3 "$base sweetusb=3"        # plus enumerate with ep0 only, then soft-disconnect after 20 s
mk usb-l4 "$base sweetusb=4"        # plus CDC-ECM ue0, SSH from the Mac over IPv6 link-local
mk usb-l4hs "$base sweetusb=20"     # level 4 with DCFG.DEVSPD forced to high speed, only if usb-l4 links at SuperSpeed and fails
