#!/bin/bash
# One controlled test run of the v5 dev-session image (README.md, "Dev session"), under 20 minutes: usb-dev-t
# (sweetdeadman=960, the self-test ends 660 s after boot) with the Mac checker's soak, 480 s of rounds both ways after
# the first checksum, with 90 s idle gaps. Refuses to start while a dev.py run is going. Waits on pids only. Exit 0 only
# when dev.py finished its self-test, the checker passed and the verdict passed (Sol audit 5 finding 1).
#   dev-v5-test.sh NOTE     use a fresh NOTE per run
cd "$(dirname "$0")/../../.." || exit 1
G=plans/usb/gadget
note=${1:-usb-dev-v5}
if pgrep -f 'devloop/dev[.]py' > /dev/null; then echo "a dev.py run is going: not starting" >&2; exit 3; fi
L=/tmp/usb-dev-v5.log
mkdir -p $G/runs
echo "== $(date +%T) $note: usb-dev-t, checker soak 480 s" >> $L
if ls ios-asteroids/logs/ast*-"$note".txt > /dev/null 2>&1; then echo "note $note was used before: pick a fresh one" >&2; exit 3; fi
: > $G/runs/$note.txt
python3 -I $G/host/usb-check.py 4 $G/runs/$note.txt --soak 480 --idle 90 > $G/runs/$note-check.log 2>&1 &
chk=$!
python3 -u plans/devloop/dev.py $G/out-v5/usb-dev-t-boot.img --test usbdev --note "$note" --key 'ASTEROIDS USB|SWEET USB' >> $L 2>&1
rc=$?
if kill -0 $chk 2>/dev/null; then kill $chk; wait $chk 2>/dev/null; crc=killed; else wait $chk; crc=$?; fi
log=$(ls -t ios-asteroids/logs/ast*-"$note".txt 2>/dev/null | head -1)
v=$(python3 -I $G/host/usb-verdict.py 4 "$log" $G/runs/$note.txt); vrc=$?
echo "$v" >> $L
echo "== $(date +%T) done dev.py rc=$rc checker rc=$crc verdict rc=$vrc log=$log" >> $L
[ $rc -eq 2 ] && say "The USB test needs you. Hold power and volume down until fastboot shows."
[ $rc -eq 0 ] && [ "$crc" = 0 ] && [ $vrc -eq 0 ]
