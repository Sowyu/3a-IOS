# Follow-up to spacing.sh (ast152), about 9 minutes. Same switch files and probe log; pull /private/var/tmp/sweetsb-real.log.
# 1. The owner reports, stock then fixed: Calendar "+" at the top right, and the bar buttons after leaving a note.
#    In ast152 a Maps location alert covered these, so Maps only runs at the end here and its alert is dismissed.
# 2. App Library and Today with the library hook fixed to check the screen itself.
# 3. The guard: Maps and Settings opened, left and killed, then opened again. Both second launches must log "scene top 68 -> 50".
SP=/private/var/tmp
kill_() { killall -9 "$1" > /dev/null 2>&1 || pkill -9 -x "$1" > /dev/null 2>&1; }
probe() { echo "$1" > $SP/sweetsb-probe; sleep 3; shot "$1"; }
respring() {
	say "respring $1"
	kill_ SpringBoard
	sleep 12
	sp_n=0
	until timeout 30 sweetopen com.apple.Preferences > /dev/null 2>&1; do
		sp_n=$((sp_n + 1))
		if [ "$sp_n" -gt 24 ]; then say "FAIL springboard not back after respring $1"; break; fi
		touch_ home
		sleep 1
		touch_ swipe 187 808 187 300 250
		sleep 5
	done
	sleep 6
	go_home
	kill_ Preferences
	touch_ swipe 187 808 187 300 250
	sleep 2
	go_home
}
reports() {
	step "reports-$1"
	dy=0; [ "$1" = before ] && dy=16     # sweettouch units; the stock bar sits 18 pt (16 units) lower
	openapp com.apple.mobilecal
	sleep 7
	probe "$1-cal"
	touch_ tap 336 $((63 + dy))          # "+"
	sleep 3
	probe "$1-cal-plus"
	go_home
	kill_ MobileCal
	openapp com.apple.mobilenotes
	sleep 7
	probe "$1-notes"
	touch_ tap 75 $((213 + dy))          # the first note
	sleep 3
	probe "$1-note-open"
	touch_ tap 37 $((63 + dy))           # back
	sleep 1
	shot "$1-note-left-1s"
	sleep 4
	probe "$1-note-left-5s"
	go_home
	kill_ MobileNotes
}

rm -f $SP/sweetsb-real.log $SP/sweetsb-probe
touch $SP/sweetsb-probe.on $SP/sweetsb-app.off $SP/sweetsb-home.off
sleep 60                                 # SpringBoard's guard marks a start healthy after 60 s
respring stock
reports before
step library-before
go_home
touch_ swipe 330 400 40 400 180
sleep 1
touch_ swipe 330 400 40 400 180
sleep 2
probe before-library
go_home

rm -f $SP/sweetsb-app.off $SP/sweetsb-home.off
sleep 20                                 # this SpringBoard started about 70 s ago at the earliest
respring fixed
step home-after
probe after-home1
touch_ swipe 40 400 330 400 180
sleep 2
probe after-today
go_home
touch_ swipe 330 400 40 400 180
sleep 1
touch_ swipe 330 400 40 400 180
sleep 2
probe after-library
go_home
reports after

step reopen
for a in com.apple.Preferences:Preferences com.apple.Maps:Maps; do
	openapp "${a%%:*}"
	sleep 2
	[ "${a##*:}" = Maps ] && touch_ tap 254 460     # its location alert: Cancel
	go_home
	kill_ "${a##*:}"
	sleep 1
	openapp "${a%%:*}"
	sleep 7
	probe "reopen-${a##*:}"
	[ "${a##*:}" = Maps ] && touch_ tap 254 460
	sleep 1
	go_home
	kill_ "${a##*:}"
done

rm -f $SP/sweetsb-probe.on $SP/sweetsb-probe
grep -E "home (top|unchanged|shift|library)|guard blocked|failed" $SP/sweetsb-real.log | head -40 | while read -r l; do say "$l"; done
