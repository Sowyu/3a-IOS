# Dev sessions over SSH, and sharing the phone

For iterative work, like a new dylib, a driver tweak, a restarted app or a screenshot, use one SSH session instead of a reboot per change. Keep plain dev.py runs for timing measurements, since the USB poll adds load every 1 ms. Also keep them for A/B series that must stay comparable.

## Images

All three run on the daily kernel series plus USB gadget v5 (`next-combined/kernel-daily-usb5`, the same build as `plans/usb/gadget/kernel-v5`). They use combo-ub's args with `sweetusb=4`, built by `plans/usb/gadget/build-usb-dev.sh`. Sol's GO covers controlled dev-session test boots (`docs/usb-gadget-audit5.md`). Daily use is still NO-GO (`docs/usb-gadget-audit4.md`).

- `plans/usb/gadget/out-v5/usb-dev-t-boot.img` lasts up to about 11 minutes, with deadman 960 s.
- `plans/usb/gadget/out-v5/usb-dev-boot.img` lasts up to about 115 minutes, with deadman 7200 s. Other agents may hold a session for 30 minutes at most, unless the modem is idle all evening.
- Both use hw scale, so `sweetshot` frames can come out blank. If you need readable frames, build a variant without `sweetfb_ast_hw=1`, as `plans/stale/build-stale.sh` does.

## A session

1. Take the phone (see below).
2. Start the session in the background from the project root. Pushes like `ROOTPATH=LOCALFILE` work as in any run:
   `python3 plans/devloop/dev.py plans/usb/gadget/out-v5/usb-dev-t-boot.img --test usbdev --note NAME-dev --key 'ASTEROIDS USB|SWEET USB'`
3. About 80 s after the boot starts, the Mac has the interface (usually `en8`), and dropbear answers about a minute later:
   `ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@fe80::5eff:fe77:ee02%en8`
   If en8 is missing, use the newest `enN` with an `fe80::` address in `ifconfig`.
4. Work over SSH:
   - copy files with `scp -6`
   - restart processes with `killall backboardd`, `killall SpringBoard` or an app's name
   - take frames with `sweetshot /private/var/tmp/x.png 3` and copy them back
   - send touches with `sweettouch` (see `plans/devloop/selftest/hook.sh`)
   - open apps with `sweetopen BUNDLEID`
5. End the session with `ssh ... touch /private/var/tmp/usbdev.stop`. The phone returns to fastboot and dev.py pulls the log. Files you copied over SSH stay on the root, so put the known-good ones back before you stop.

## Sharing the phone

Priority runs SIM/modem first, then battery, then everything else.

- Before any phone use, `touch /tmp/phone-AGENT-wants`, for example `phone-modem-wants`, `phone-battery-wants` or `phone-spacing-wants`. Trash it with `/usr/bin/trash` when you go back to desk work for 15 minutes or more.
- Wait until three things hold:
  - no higher-priority `wants` file exists
  - no `dev.py` process runs (`pgrep -f 'dev\.py'`)
  - the phone is in fastboot (`fastboot devices`) or OrangeFox (`adb devices` shows `recovery`)
  The modem agent waits only for the last two.
- Lower-priority agents also take the lock with `mkdir /tmp/phone.lock` (atomic), run, then `/usr/bin/trash /tmp/phone.lock`. If the mkdir fails, someone else is starting, so wait.
- If the phone is in iOS with no dev.py running, the owner has it. Wait.
- `plans/phone-queue.sh JOBFILE` runs queued jobs only when no `wants` file exists.
- Never put the string `devloop/dev.py` in a long-running waiter's command line. Wait on pids.
