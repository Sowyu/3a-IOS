# Read-only IPA preflight. The nonce is replaced by the host runner.
st_finish_delay=15
step ipa_preflight
sleep 90
st_frame="$ST_OUT/01-ipa_preflight.png"
if ! timeout 30 sweetshot "$st_frame" 3 || [ ! -s "$st_frame" ]; then
    say "FAIL checked IPA preflight screenshot"
    finish
fi
step ipa_preflight_done
printf '%s\n' 'IPA_NONCE_PLACEHOLDER' > /private/var/tmp/ipa-preflight-complete.txt || { say "FAIL IPA completion marker"; finish; }
sync
