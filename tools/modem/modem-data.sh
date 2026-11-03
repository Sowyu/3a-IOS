# Wait for the 600-second modem session and bounded cleanup. Relative sleep ignores network clock corrections.
st_finish_delay=15
step modem_data
sleep 660
st_frame="$ST_OUT/01-modem_data.png"
if ! timeout 30 sweetshot "$st_frame" 3 || [ ! -s "$st_frame" ]; then
    say "FAIL checked modem data-session screenshot"
    finish
fi
step modem_data_done
printf '%s\n' 'nonce=@RUN_NONCE@ phase=body-complete shot=0' > /private/var/tmp/modem-data-complete.txt || { say "FAIL completion marker"; finish; }
sync
