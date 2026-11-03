#!/bin/bash
# Stale UI loop images (3a-IOS/docs/stale-ui.md): the owner's combo-ub and combo-keep args on kernel-daily2, without hw scale so the
# self-test frames are readable, with the self-test on and a 900 s deadman for the staleui body. Writes only plans/stale/out.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root="$here/../.."; repo="$root/ios-asteroids"; nf="$root/next-fix/boot"
kernel="${KERNEL:-$root/next-combined/kernel-daily2/kernel.development.sweet}"
out="$here/out"; mkdir -p "$out"
args=$(sed -n "s/^args='\(.*\)'\$/\1/p" "$nf/build.sh")
[ -n "$args" ] || { echo "cannot read the args from $nf/build.sh" >&2; exit 1; }
shim="$out/shim"; [ ! -e "$shim" ] || /usr/bin/trash "$shim"; cp -R "$root/ios-sweet/asteroids/shim" "$shim"
mk() {
	[ ${#2} -le 607 ] || { echo "$1: ${#2} bytes, over 607" >&2; exit 1; }
	python3 -I "$repo/tools/mkpayload.py" "$kernel" "$root/ux-fix/ux-420pt-2x-840x1860.adt" "$out/$1.payload" --cmdline "$2"
	ASTEROIDS_DTB="$repo/research/asteroids-hw/live.dtb" MKBOOTIMG="$root/ref/tools/mkbootimg.py" bash "$shim/build.sh" "$out/$1.payload" "$out/$1-boot.img"
	echo "$1: ${#2} of 607 bytes, $(wc -c < "$out/$1-boot.img") bytes image"
}
base="${args/ sweetsyserr=backboardd,MobileSafari/} sweetthermal=3 sweetfetchsync=1 sweetsb=1 sweettouchirq=3"   # sweetkeys_ms left out for the 607-byte line: key polling, not display
loop='sweetdeadman=900 sweetselftest=1'
mk stale-UB "$base $loop sweetfb_keep=1 sweetfb_astwait=1 sweettu=sysmem,nocb,nolrz"   # combo-ub without hw scale (next-luma-ub driver)
mk stale-K "$base $loop sweetfb_lead=3000 sweetfb_keep=1 sweetfb_astwait=1"             # combo-keep without hw scale
mk stale-0 "$base $loop"                                                               # neither keep nor astwait: judge stale buttons only
