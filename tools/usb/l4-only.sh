#!/bin/bash
# One-off, 2026-11-03: USB level 4 on the v4 kernel after the owner set "Allow accessories to connect" to Always.
#   l4-only.sh [PID]   waits for PID (a running dev.py) to exit first, by pid only
cd "$(dirname "$0")/../../.." || exit 1
G=plans/usb/gadget
[ -n "${1:-}" ] && while kill -0 "$1" 2>/dev/null; do sleep 5; done
L=/tmp/usb-attended.log
echo "== $(date +%T) level 4 (v4), checker waits for port 22" >> $L
python3 -I $G/host/usb-check.py 4 $G/runs/l4-v4d.txt > $G/runs/l4-v4d-check.log 2>&1 &
chk=$!
python3 -u plans/devloop/dev.py $G/out-v4/usb-l4-boot.img --test usb --note usb-l4-v4d --key 'ASTEROIDS USB' --key 'SWEET USB' >> $L 2>&1
rc=$?
kill -0 $chk 2>/dev/null && kill $chk; wait $chk 2>/dev/null
log=$(ls -t ios-asteroids/logs/ast*-usb-l4-v4d.txt 2>/dev/null | head -1)
v=$(python3 -I $G/host/usb-verdict.py 4 "$log" $G/runs/l4-v4d.txt); echo "$v" >> $L
[ $rc -eq 2 ] && say "The USB test needs you. Hold power and volume down until fastboot shows."
case "$v" in
*"USBVERDICT L4 PASS"*) say "USB level 4 passed. SSH over the cable works." ;;
*) say "USB level 4 did not pass. Details are in the log." ;;
esac
echo "== $(date +%T) done rc=$rc" >> $L
