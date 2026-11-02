# The fixed run, about 3 minutes after SpringBoard is up. Coordinates are guesses until frames confirm them.
step idle
shot idle-start
sleep 30
shot idle-end

step home-swipes
for i in 1 2 3; do
	touch_ swipe 330 400 40 400 180
	sleep 1
	shot "page-$i-left" check
	touch_ swipe 40 400 330 400 180
	sleep 1
	shot "page-$i-right" check
done

step apps
for app in com.apple.Preferences com.apple.mobilecal com.apple.calculator; do
	openapp "$app"
	sleep 8
	shot "$app" check
	touch_ swipe 187 600 187 200 300     # scroll, for frame rate
	sleep 2
	go_home
	shot "$app-closed" check
done

step keyboard-safari
openapp com.apple.mobilesafari
sleep 10
shot safari check
touch_ tap 187 760                       # address bar
sleep 4
shot safari-keyboard check
go_home

step keyboard-notes
openapp com.apple.mobilenotes
sleep 8
shot notes check
touch_ tap 345 770                       # new note
sleep 4
shot notes-keyboard check
go_home

step control-center
touch_ swipe 350 3 350 420 300
sleep 3
shot control-center check
touch_ swipe 187 790 187 300 200
sleep 2
go_home

step spotlight
touch_ swipe 187 300 187 520 300
sleep 3
shot spotlight check
go_home

step app-switcher
touch_ swipe 187 808 187 420 1400        # slow, with no way to hold at the end: sweettouch lifts when it stops
sleep 3
shot app-switcher check
go_home

step settle
sleep 5
shot settle
