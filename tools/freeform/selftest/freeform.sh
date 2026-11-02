# plans/freeform phase 2b: compute alone first, then Freeform with a Chlorophyll device (boot-arg sweetappmetal=Freeform).
#   --test ../../../freeform/selftest/freeform
# Coordinates are guesses until the frames confirm them: every gesture is followed by a checked shot.
step compute-selftest
timeout 90 /usr/local/bin/hcitest > /private/var/tmp/chl-selftest.log 2>&1
st_rc=$?
say "chl-selftest exit $st_rc"
while IFS= read -r l; do say "CHL $l"; done < /private/var/tmp/chl-selftest.log
if [ "$st_rc" != 0 ]; then
	say "FAIL compute self-test did not pass: Freeform skipped"
else
	step freeform
	openapp com.apple.freeform
	sleep 15
	shot freeform-open check
	touch_ tap 345 62                    # new board, the + at the top right
	sleep 8
	shot freeform-board check
	touch_ tap 300 770                   # the pen in the bottom toolbar
	sleep 3
	shot freeform-pen check
	touch_ swipe 80 380 300 430 700      # the first stroke: PencilKit compiles its shaders now
	sleep 90
	shot freeform-stroke1 check
	touch_ swipe 80 480 300 530 700
	sleep 10
	shot freeform-stroke2 check
	touch_ swipe 90 560 280 600 400
	sleep 10
	shot freeform-stroke3 check
	go_home
	shot freeform-closed check
fi
step settle
sleep 10
shot settle
