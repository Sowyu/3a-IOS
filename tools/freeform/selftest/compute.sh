# plans/freeform phase 2a: compute alone. Body for plans/devloop/selftest/hook.sh, run by dev.py as
#   --test ../../../freeform/selftest/compute
# /usr/local/bin/hcitest is chl-selftest here (pushed over the Bluetooth probe; put hcitest.orig back afterwards).
step compute-selftest
shot before-selftest
timeout 90 /usr/local/bin/hcitest > /private/var/tmp/chl-selftest.log 2>&1
say "chl-selftest exit $?"
while IFS= read -r l; do say "CHL $l"; done < /private/var/tmp/chl-selftest.log
shot after-selftest check
step home-swipes
touch_ swipe 330 400 40 400 180
sleep 1
shot page-left check
touch_ swipe 40 400 330 400 180
sleep 1
shot page-right check
step settle
sleep 20
shot settle
