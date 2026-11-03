# Wait for the bounded modem runtime and every cleanup path.
st_finish_delay=15
step modem_offline
sleep 240
st_frame="$ST_OUT/01-modem_offline.png"
if ! timeout 30 sweetshot "$st_frame" 3 || [ ! -s "$st_frame" ]; then
    say "FAIL checked modem offline screenshot"
    finish
fi
step modem_offline_done
printf '%s\n' 'nonce=@RUN_NONCE@ phase=body-complete shot=0' > /private/var/tmp/modem-offline-complete.txt || { say "FAIL completion marker"; finish; }
sync
