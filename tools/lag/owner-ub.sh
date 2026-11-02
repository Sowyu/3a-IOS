#!/bin/bash
# Owner hands-on of the lag night's best arm (2026-11-03): push the UBWC + texture pool driver (next-luma-ub) to the root
# from OrangeFox, then RAM-boot next-combined/out-daily2/combo-ub-boot.img (kernel-daily2 with 2144, keep + astwait,
# sweettu=sysmem,nocb,nolrz). Nothing is flashed. To go back: plans/lag/owner-ub.sh --restore (pushes next-luma).
#   owner-ub.sh [--restore]     the phone must be in OrangeFox (adb recovery)
set -euo pipefail
cd "$(dirname "$0")/../.."
src=plans/lag/next-luma-ub/out-metal
[ "${1:-}" = --restore ] && src=plans/lag/next-luma/out-metal
serial=$(adb devices | awk '$2=="recovery" {print $1; exit}')
[ -n "$serial" ] || { echo "phone is not in OrangeFox (adb recovery)" >&2; exit 1; }
PUT=plans/devloop/06-hfs-put.py
python3 -I $PUT --adb "$serial" --state || { echo "root not clean: run a dev.py cycle first" >&2; exit 1; }
undo=plans/lag/owner-ub-undo; mkdir -p $undo
python3 -I $PUT --adb "$serial" --undo-dir $undo \
	/usr/local/lib/bgmtl/bgmtl-inject.dylib=$src/bgmtl-inject.dylib \
	/usr/local/lib/bgmtl/libchlorophyll.dylib=$src/libchlorophyll.dylib
[ "${1:-}" = --restore ] && { echo "daily driver restored"; exit 0; }
adb -s "$serial" reboot bootloader
until fastboot devices | grep -q fastboot; do sleep 2; done
fastboot boot next-combined/out-daily2/combo-ub-boot.img
echo "booted combo-ub"
