# Lock and wake through iOS, five times, for the panel sleep path (sweetpanel=1) and 2152's state with iOS-driven changes.
# sweettouch lock injects the press in user space, so it skips the kernel keys driver: the kernel's "power key down" fields
# read -1 here. What this checks is that every display off and on is followed by "SWEET PANEL: display ..." with rc 0, that
# "first frame after display on" comes, and that the woken frames are not black. Copy to plans/devloop/selftest/scripts/.
step lock-wake
for i in 1 2 3 4 5; do
	sleep 5
	say "lock $i"
	touch_ lock
	sleep 4
	shot "locked-$i"
	say "wake $i"
	touch_ lock
	sleep 3
	shot "woken-$i" check
	touch_ swipe 187 808 187 300 250
	sleep 3
	shot "unlocked-$i" check
done
step after
openapp com.apple.Preferences
sleep 6
touch_ swipe 187 600 187 200 300
sleep 2
shot settings-scrolled check
go_home
