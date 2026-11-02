#!/bin/bash
# Runs USB levels 2, 3, 4 hands-free with plans/devloop/dev.py, stopping at the first non-PASS verdict.
#   usb-queue.sh [PID]   waits for PID (a running dev.py) to exit first, by pid only: never pgrep -f devloop/dev.py.
# The owner is awake overnight: anything that needs a button is announced with say.
cd "$(dirname "$0")/../../.." || exit 1
G=plans/usb/gadget
[ -n "${1:-}" ] && while kill -0 "$1" 2>/dev/null; do sleep 5; done
last_log() { ls -t ios-asteroids/logs/ast*-usb-l"$1".txt 2>/dev/null | head -1; }
for lvl in 2 3 4; do
	echo "== $(date +%T) level $lvl"
	chk=
	if [ "$lvl" != 2 ]; then
		python3 -I $G/host/usb-check.py $lvl $G/runs/l$lvl.txt > $G/runs/l$lvl-check.log 2>&1 &
		chk=$!
	fi
	python3 -u plans/devloop/dev.py $G/out/usb-l$lvl-boot.img --test usb --note usb-l$lvl --key 'ASTEROIDS USB' --key 'SWEET USB'
	rc=$?
	[ -n "$chk" ] && { kill -0 $chk 2>/dev/null && kill $chk; wait $chk 2>/dev/null; }
	if [ $rc -eq 2 ]; then
		say "The USB test needs you. Hold power and volume down until fastboot shows."
		echo "== level $lvl: dev.py needs attention"; exit 2
	fi
	log=$(last_log $lvl)
	if [ "$lvl" = 2 ]; then v=$(python3 -I $G/host/usb-verdict.py 2 "$log"); else v=$(python3 -I $G/host/usb-verdict.py $lvl "$log" $G/runs/l$lvl.txt); fi
	echo "$v"
	case "$v" in *"USBVERDICT L$lvl PASS"*) ;; *) echo "== stopping at level $lvl"; say "USB level $lvl did not pass. Details are in the log."; exit 1 ;; esac
done
say "USB level 4 passed. SSH over the cable works."
echo "== all levels passed $(date +%T)"
