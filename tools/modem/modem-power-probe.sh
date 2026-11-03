# Read-only power prerequisites with durable completion evidence.
st_finish_delay=15
step modem_power_probe
sleep 120
st_frame="$ST_OUT/01-modem_power_probe.png"
if ! timeout 30 sweetshot "$st_frame" 3 || [ ! -s "$st_frame" ]; then
    say "FAIL checked modem power screenshot"
    finish
fi
step modem_power_probe_done
printf '%s\n' 'nonce=@RUN_NONCE@ phase=body-complete frame=/private/var/selftest/frames/01-modem_power_probe.png shot=0' > /private/var/tmp/modem-power-probe-complete.txt || { say "FAIL completion marker"; finish; }
sync
