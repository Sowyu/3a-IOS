# Stale UI after a transition (3a-IOS/docs/stale-ui.md). Owner reports: Notes' three note buttons stay after leaving a note,
# Calendar's nav bar buttons stay blurred after a full-screen blur closes, Notes' "..." glyph goes blank. Use a no-hw image so
# frames are readable. Each case shoots the settled frame, then reopens the app from the home screen and shoots again: CoreAnimation
# redraws the whole app then. Stale pixels that survive the reopen come from a stale layer source (texture, backdrop, client
# surface). Stale pixels that clear come from the partial redraw (the surface held something CoreAnimation did not expect).
# About 6 minutes of body. sweettouch units are 375x812 of the 420x930 screen; app bars sit at y 50 since ast110.
kill_() { killall -9 "$1" > /dev/null 2>&1 || pkill -9 -x "$1" > /dev/null 2>&1; }
reopen() { go_home; openapp "$1"; sleep 4; shot "$2-reopen"; }

step notes-leave
openapp com.apple.mobilenotes
sleep 7
shot notes-list
for i in 1 2 3; do
	touch_ tap 75 213                    # the first note
	sleep 3
	shot "note$i-open"
	touch_ tap 37 63                     # back
	sleep 2
	shot "note$i-left-2s"
	sleep 5
	shot "note$i-left-7s"
done
reopen com.apple.mobilenotes notes-leave

step notes-new
touch_ tap 345 770                       # new note: keyboard and the morphing top right buttons
sleep 4
shot notes-new-4s
sleep 8
shot notes-new-12s
reopen com.apple.mobilenotes notes-new
go_home
kill_ MobileNotes

# Calendar's top bar: list (235,63), search (288,63), "+" (336,63); bottom bar: calendars (265,770), inbox (325,770). Each
# opens on a fresh Calendar, is shot open, closed by tapping it again or swiping its sheet down, and shot settled.
step calendar
for b in list:235:63 search:288:63 plus:336:63 calendars:265:770 inbox:325:770; do
	n=${b%%:*}; xy=${b#*:}; x=${xy%%:*}; y=${xy#*:}
	openapp com.apple.mobilecal
	sleep 6
	touch_ tap "$x" "$y"
	sleep 3
	shot "cal-$n-open"
	case $n in
	search) touch_ tap 340 63 ;;         # the field's close button sits where "+" was
	calendars|inbox|plus) touch_ swipe 187 140 187 790 300 ;;
	*) touch_ tap "$x" "$y" ;;
	esac
	sleep 2
	shot "cal-$n-closed-2s"
	sleep 5
	shot "cal-$n-closed-7s"
	reopen com.apple.mobilecal "cal-$n"
	go_home
	kill_ MobileCal
done
