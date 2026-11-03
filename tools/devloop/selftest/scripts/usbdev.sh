# USB dev session (plans/usb/gadget/README.md, "Dev session"). Body for dev.py's self-test, --test usbdev. Keeps the boot
# alive for SSH over the cable until /private/var/tmp/usbdev.stop exists or 300 s before sweetdeadman, then the hook's
# finish() returns the phone to fastboot and dev.py pulls the log. Logs ue0 and dropbear every 5 minutes.
step usbdev
ud_dm=$(echo " $st_arg " | sed -n 's/.* sweetdeadman=\([0-9]*\) .*/\1/p')
ud_bt=$(/var/jb/usr/sbin/sysctl -n kern.boottime 2>/dev/null | sed -n 's/^{ sec = \([0-9]*\),.*/\1/p')
[ -n "$ud_bt" ] || ud_bt=$(( $(date +%s) - 180 ))
ud_end=$(( ${ud_dm:-600} - 300 ))
rm -f /private/var/tmp/usbdev.stop
say "usbdev open: ends $ud_end s after boot, or at touch /private/var/tmp/usbdev.stop"
ud_i=0
while :; do
	ud_up=$(( $(date +%s) - ud_bt ))
	if [ -e /private/var/tmp/usbdev.stop ]; then
		say "usbdev stop file at $ud_up s"
		break
	fi
	if [ "$ud_up" -ge "$ud_end" ]; then
		say "usbdev time over at $ud_up s"
		break
	fi
	if [ $((ud_i % 30)) -eq 0 ]; then
		say "usbdev $ud_up s: $(ifconfig ue0 2>&1 | grep -E 'status|inet6' | tr '\n' ' ' | cut -c1-200) dropbear $(ps -ax 2>/dev/null | grep -c '[d]ropbear')"
	fi
	ud_i=$((ud_i + 1))
	sleep 10
done
rm -f /private/var/tmp/usbdev.stop
shot usbdev     # one frame: dev.py calls a self-test with none "selftest-no-frames" and exits 1 (ast147)
step usbdev-done
