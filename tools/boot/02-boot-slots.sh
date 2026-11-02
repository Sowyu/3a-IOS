#!/bin/bash
# UNTESTED ON A PHONE. Host check: check-02-slots.sh (a stub fastboot).
# What: the only fastboot commands standalone boot needs, with the guards. It never runs set_active, erase, -w or
# flashing lock/unlock: set_active clears the slot's successful flag, and a lock state change makes ABL ask recovery to wipe.
# Apply: nothing to apply. Run it from anywhere with the phone in fastboot, one step at a time (plan-boot.md phase 2):
#   02-boot-slots.sh status
#   02-boot-slots.sh try IMG            RAM boot, nothing written
#   02-boot-slots.sh flash a|b IMG      needs CONFIRM=flash; saves getvar output before and after
#   02-boot-slots.sh restore a|b        stock boot image from backup-asteroids; needs CONFIRM=restore
# A restored boot_a followed by a normal boot starts Android, which formats userdata. Restore only when you mean that.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
BACKUP=${BACKUP:-$here/../../backup-asteroids}
BOOT_SIZE=100663296
die() { echo "error: $*" >&2; exit 1; }
var() { fastboot getvar "$1" 2>&1 | sed -n "s/^$1: *//p" | head -1; }

one_device() {
	local n
	n=$(fastboot devices | grep -c fastboot || true)
	[ "$n" = 1 ] || die "need exactly one device in fastboot, found $n"
}
status() {
	local v
	for v in product unlocked current-slot slot-successful:a slot-successful:b slot-unbootable:a slot-unbootable:b slot-retry-count:a slot-retry-count:b; do
		echo "$v: $(var "$v")"
	done
}
check_img() {
	[ -f "$1" ] || die "no file $1"
	[ "$(head -c 8 "$1")" = 'ANDROID!' ] || die "$1 is not a boot image"
	[ "$(stat -f %z "$1")" -le "$BOOT_SIZE" ] || die "$1 is larger than the 96 MiB boot partition"
}
check_slot() { case "$1" in a|b) ;; *) die "slot must be a or b" ;; esac; }
check_phone() {
	one_device
	[ "$(var unlocked)" = yes ] || die "bootloader does not report unlocked"
}

cmd=${1:-}
case "$cmd" in
status)
	one_device
	status ;;
try)
	check_img "${2:?image}"
	one_device
	fastboot boot "$2" ;;
flash)
	check_slot "${2:-}"
	check_img "${3:?image}"
	[ "${CONFIRM:-}" = flash ] || die "set CONFIRM=flash to write boot_$2"
	check_phone
	[ -s "$BACKUP/boot_$2.img" ] || die "no stock backup of boot_$2 in $BACKUP"
	status | tee "boot_$2-before.txt"
	fastboot flash "boot_$2" "$3"
	status | tee "boot_$2-after.txt"
	diff "boot_$2-before.txt" "boot_$2-after.txt" || echo "slot flags changed: read plan-boot.md phase 2 before rebooting" ;;
restore)
	check_slot "${2:-}"
	[ "${CONFIRM:-}" = restore ] || die "set CONFIRM=restore to write the stock boot_$2"
	img=$BACKUP/boot_$2.img
	[ "$(stat -f %z "$img")" = "$BOOT_SIZE" ] || die "$img is not the $BOOT_SIZE byte backup"
	[ "$(head -c 8 "$img")" = 'ANDROID!' ] || die "$img is not a boot image"
	check_phone
	fastboot flash "boot_$2" "$img"
	status ;;
*)
	sed -n 2,10p "$0"; exit 2 ;;
esac
