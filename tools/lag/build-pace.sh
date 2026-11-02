#!/bin/bash
# Pacing A/B loop images on the combined kernel: next-combined/build.sh's combo-loop-nohw args with hardware scale, keep, astwait
# and the input args. A: 60 Hz. B: CoreAnimation and the tick at 30 Hz (sweetfb_hz=30 sweetfb_vsync=1). Writes plans/lag/out only.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); root="$here/../.."; repo="$root/ios-asteroids"; nf="$root/next-fix/boot"
kernel="${KERNEL:-$root/next-combined/kernel/kernel.development.sweet}"   # KERNEL=... for the 2144 scheduler arms
out="$here/out"; mkdir -p "$out"
args=$(sed -n "s/^args='\(.*\)'\$/\1/p" "$nf/build.sh")
shim="$out/shim"; [ ! -e "$shim" ] || /usr/bin/trash "$shim"; cp -R "$root/ios-sweet/asteroids/shim" "$shim"
mk() {
	python3 -I "$repo/tools/mkpayload.py" "$kernel" "$root/ux-fix/ux-420pt-2x-840x1860.adt" "$out/$1.payload" --cmdline "$2"
	ASTEROIDS_DTB="$repo/research/asteroids-hw/live.dtb" MKBOOTIMG="$root/ref/tools/mkbootimg.py" bash "$shim/build.sh" "$out/$1.payload" "$out/$1-boot.img"
	echo "$1: ${#2} of 607 bytes"
}
base="${args/ sweetsyserr=backboardd,MobileSafari/}"
loop='sweetthermal=0 sweetfetchsync=1 sweetdeadman=600 sweetselftest=1 sweetsb=1 sweetfb_ast_hw=1'
input='sweetfb_keep=1 sweetfb_astwait=1'   # lead and the input args left out: 607 bytes; lead moves the tick phase only   # the two input args left out: 607 bytes, and they touch input, not display pacing
if [ -z "${ONLY:-}" ]; then
mk pace-A60 "$base $loop $input"
mk pace-G60 "$base $loop $input sweettu=noubwc,nocb,nolrz"   # GMEM allowed (Turnip autotune) instead of forced sysmem
mk pace-N60 "$base ${loop/ sweetfb_ast_hw=1/} $input"   # no hw scale: readable self-test frames
mk pace-M47 "$base $loop $input sweetamp_prio=47"
# hardware overlap ordering (lumaov payload) with the full-drain fetch sync off
mk pace-O60 "$base ${loop/sweetfetchsync=1/sweetfetchsync=0} $input"
lo=${loop/sweetfetchsync=1/sweetfetchsync=0}; mk pace-O60N "$base ${lo/ sweetfb_ast_hw=1/} $input"
# forced GMEM with the fetch sync off: fetch reads on-chip tile memory, ordered by FLUSH_PER_OVERLAP (stock Turnip, lumatrace)
mk pace-GF60 "$base $lo $input sweettu=gmem,noubwc,nocb,nolrz"
mk pace-GF60N "$base ${lo/ sweetfb_ast_hw=1/} $input sweettu=gmem,noubwc,nocb,nolrz"
fi   # big cores first only for user-interactive threads (default 31)

# 2144 scheduler arms (KERNEL=plans/lag/kernel/kernel-2144.sweet ONLY=sched): stock placement against steering with UI preemption
if [ "${ONLY:-}" = sched ]; then
	mk pace-S0 "$base $loop $input sweetamp=0"
	mk pace-U47 "$base $loop $input sweetamp_ui=47"
fi

# UBWC on (lossless framebuffer compression), sysmem kept: ONLY=ubwc
if [ "${ONLY:-}" = ubwc ]; then
	mk pace-UB60 "$base $loop $input sweettu=sysmem,nocb,nolrz"
	mk pace-UB60N "$base ${loop/ sweetfb_ast_hw=1/} $input sweettu=sysmem,nocb,nolrz"
fi

# CoreAnimation pacing switches through the launcher (arms/calat): ONLY=calat
if [ "${ONLY:-}" = calat ]; then
	mk pace-CL1 "$base $loop $input sweetcalat=1"
	mk pace-CL2 "$base $loop $input sweetcalat=2"
fi

# UBWC on kernel 2144 with its default idle steering (KERNEL=kernel-2144 ONLY=ubs)
if [ "${ONLY:-}" = ubs ]; then
	mk pace-UBS "$base $loop $input sweettu=sysmem,nocb,nolrz"
fi
