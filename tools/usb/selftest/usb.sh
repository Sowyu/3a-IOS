# USB gadget levels (plans/usb/gadget/README.md). Body for dev.py's self-test: copy to
# 3a-IOS/tools/devloop/selftest/scripts/usb.sh and run dev.py with --test usb. The kernel does the USB work at boot;
# this only logs the phone's side and keeps the boot alive while host/usb-check.py tests from the Mac.
step usb
usb_lvl=$(echo " $st_arg " | sed -n 's/.* sweetusb=\([0-9]*\) .*/\1/p')
say "usb level ${usb_lvl:-none}"
say "usb ifconfig $(ifconfig ue0 2>&1 | tr '\n' ' ' | cut -c1-300)"
say "usb dropbear $(ps -ax 2>/dev/null | grep -c '[d]ropbear') process(es)"
case "$usb_lvl" in
3|19|4|20) sleep 150 ;;         # the Mac's window: level 3 disconnects itself, level 4 stays up
*) sleep 5 ;;
esac
say "usb ifconfig after $(ifconfig ue0 2>&1 | tr '\n' ' ' | cut -c1-300)"
step usb-done
