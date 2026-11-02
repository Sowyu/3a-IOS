# Short run for a driver check, about 1 minute after SpringBoard is up.
step idle
sleep 15
shot idle
step apps
openapp com.apple.mobilecal
sleep 8
shot mobilecal check
touch_ swipe 187 600 187 200 300
sleep 2
go_home
shot home check
