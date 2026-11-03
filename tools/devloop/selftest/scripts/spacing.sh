# Top spacing of apps and the home screen, stock then fixed, in one boot (docs/app-safearea2.md, round 6). Needs sweetsb v6:
# with /private/var/tmp/sweetsb-probe.on at process start it logs to /private/var/tmp/sweetsb-real.log, and each label written
# to /private/var/tmp/sweetsb-probe makes the foreground app and SpringBoard log their layout. sweetsb-app.off and
# sweetsb-home.off turn the two fixes off for processes started after they appear. Pull the log with
# --pull /private/var/tmp/sweetsb-real.log. About 24 minutes: use an image with sweetdeadman=1800.
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
	touch_ swipe 187 808 187 300 250     # lock screen, if it is showing
	sleep 2
	go_home
}
APPS="com.apple.Preferences:Preferences com.apple.mobilesafari:MobileSafari com.apple.mobileslideshow:Photos
com.apple.MobileSMS:MobileSMS com.apple.mobilemail:MobileMail com.apple.Music:Music com.apple.mobiletimer:MobileTimer
com.apple.weather:Weather com.apple.DocumentsApp:Files com.apple.AppStore:AppStore com.apple.Maps:Maps
com.apple.MobileAddressBook:Contacts com.apple.reminders:Reminders com.apple.calculator:Calculator com.apple.camera:Camera
com.apple.mobilecal:MobileCal com.apple.mobilenotes:MobileNotes com.apple.journal:Journal com.apple.Passwords:Passwords
com.apple.Health:Health"
measure() {
	step "measure-$1"
	go_home
	probe "$1-home1"
	touch_ swipe 40 400 330 400 180      # Today view, left of page 1
	sleep 2
	probe "$1-today"
	go_home
	for i in 2 3 4 5; do                 # the last pages: App Library
		touch_ swipe 330 400 40 400 180
		sleep 2
		probe "$1-page$i"
	done
	go_home
	touch_ swipe 100 3 100 520 300       # Notification Center, the lock screen layout
	sleep 3
	probe "$1-coversheet"
	touch_ swipe 187 808 187 300 250
	sleep 2
	go_home
	for a in $APPS; do
		openapp "${a%%:*}"
		sleep 7
		probe "$1-${a##*:}"
		go_home
		kill_ "${a##*:}"
	done
	# Owner reports. sweettouch units are 375x812 of the 420x930 screen; the stock bar sits 18 pt (16 units) lower.
	dy=0; [ "$1" = before ] && dy=16
	openapp com.apple.mobilecal               # "+" at the top right does nothing
	sleep 7
	touch_ tap 336 $((63 + dy))
	sleep 3
	probe "$1-cal-plus"
	go_home
	kill_ MobileCal
	openapp com.apple.mobilenotes             # bar buttons stay blurred or stay after leaving a note
	sleep 7
	touch_ tap 75 $((213 + dy))              # the first note
	sleep 3
	probe "$1-note-open"
	touch_ tap 37 $((63 + dy))               # back
	sleep 1
	shot "$1-note-left-1s"
	sleep 4
	probe "$1-note-left-5s"
	go_home
	kill_ MobileNotes
}

step spacing-state
# Guard records the last boot left behind, before this boot's apps overwrite them. start 2 = that app's next launch was blocked.
for f in /private/var/mobile/Library/Caches/sweetsb-real-*.state /private/var/tmp/sweetsb-real.state; do
	[ -e "$f" ] && say "guard state $f: $(cat "$f")"
done
say "this boot $(/var/jb/usr/sbin/sysctl -n kern.bootsessionuuid 2>/dev/null)"
rm -f $SP/sweetsb-real.log $SP/sweetsb-probe
touch $SP/sweetsb-probe.on $SP/sweetsb-app.off $SP/sweetsb-home.off
sleep 60                                 # SpringBoard's crash guard marks a start healthy after 60 s; kill it sooner and the next start runs stock
respring stock
measure before
rm -f $SP/sweetsb-app.off $SP/sweetsb-home.off
respring fixed                           # the stock pass took minutes, so this SpringBoard is past its 60 s
measure after

step reopen
# The guard case: open, leave at once, kill, open again. v5 ran this second launch stock.
openapp com.apple.Preferences
sleep 2
go_home
kill_ Preferences
sleep 1
openapp com.apple.Preferences
sleep 7
probe reopen-Preferences
go_home

rm -f $SP/sweetsb-probe.on $SP/sweetsb-probe
cp $SP/sweetsb-real.log "$ST_OUT/sweetsb-real.log" 2> /dev/null
grep -E "home (top|unchanged|shift)|guard blocked|failed" $SP/sweetsb-real.log | head -40 | while read -r l; do say "$l"; done
