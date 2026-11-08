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

## P0: the passcode dev session (owner present, about 15 minutes)

P0 is the passcode series' first look at iOS 26's own Passcode flow. It exists because the selector numbers are
Inferred from the iOS 16.7 map, and P0 on 2026-11-04 already caught one: selector 42 is five scalars with no structure
input, so a handler that read `structureInput` answered Unsupported for every verify and the pane backed out. P0 is
also how a later change is checked the same way.

**This is a security-sensitive session.** Read `plans/passcode/audit-brief.md` first. The image needs a Sonnet 5.5 GO
for its exact hash before it boots, because 2321 is the only thing between a stranger and the phone.

**Image:** `plans/usb/gadget/out-v5/usb-dev-t-boot.img` with `--key 'SWEET AKS'`, about 11 minutes with deadman 960 s.
The short image, so the session cannot outlast the owner's patience.

**Runner duties (before and after):**

1. Back up everything keybagd and profiled keep under `/private/var` before the owner touches Settings:
   - `/private/var/keybags/` (the system bag, and any per-user bags)
   - `/private/var/mobile/Library/Preferences/` (profiled writes its configuration here)
   - `/private/var/protected/` if it exists
   Save the tar and its SHA-256 under `plans/passcode/p0-backup/`, which is git-ignored. Never commit the tar: it
   contains device state.
2. Start the session, wait for the interface, and confirm the log is flowing before the owner arrives. A capture that
   starts late misses the only interesting seconds.
3. Watch `dmesg` for `SWEET AKS:` lines and keep the raw stream, not a filtered one. The first P0 lost the pane's own
   errors because only kernel lines were kept.

**The owner's part, in their words:** open Settings > Passcode. Try either "Turn Passcode On" or "Change Passcode" with
a throwaway code, something they have never used and do not care about. Then lock the phone with the side button and
wake it. That is all. About two minutes of tapping, then they can hand the phone back.

Say plainly, before they start: nothing is encrypted, the code is a throwaway, and the session ends in 11 minutes no
matter what.

**After the owner is done:**

1. Check that nothing persisted: compare the tar from step 1 with a fresh one. The P0 on 2026-11-04 showed no changes,
   and that is the expected result for `usb-dev-t`, which does not carry the passcode patch.
2. If something did persist, put back the files keybagd and profiled own with
   `tools/devloop/06-hfs-put.py` from recovery, before the next boot. Name the exact paths that changed in the log.
3. End the session with `ssh ... touch /private/var/tmp/usbdev.stop` and let `usbdev.stop` do its own teardown. Do not
   kill the session by hand.
4. Move the captures into `plans/passcode/p0-backup/` with the date in the name, and keep them local.

**What to read out of the capture:** every `sel` number with its scalar and structure counts, in order, with the
process name. That is the iOS 26 selector map for the passcode flow, which is the thing this session exists to
produce. The 2026-11-04 run gave `42 in 5/0`, `31 in 1/0 out 0/32768` and SpringBoard polling `26`/`27` about twice a
second; a session with 2321 in the kernel should show `42` succeeding and `31` returning a dictionary instead of
`0xe00002c7`.
