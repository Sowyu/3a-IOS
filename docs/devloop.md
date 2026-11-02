# One-command development loop

2026-11-02. Nothing here ran on a phone. I ran no adb or fastboot except `--dry-run`, built no kernel or root, and committed nothing. Files are in `plans/devloop/`.

Labels: **Proven** means I read it in code or a log, or ran it on the Mac. **Inferred** means reasoning with no run behind it. **Unknown** means only the phone can answer.

## Run it

1. Run `plans/devloop/tests/check.sh`. It takes a few minutes and needs no phone.
2. Use Settings > Shut Down on the phone, then hold Power + Volume down for fastboot. A dirty root would cost one extra boot.
3. Watch the first real run: `plans/devloop/dev.py plans/devloop/out/next-loop-boot.img --test quick --note first`.

A normal cycle with a new driver:

```sh
plans/devloop/dev.py plans/devloop/out/next-loop-boot.img /usr/local/lib/bgmtl/bgmtl-inject.dylib=NEW.dylib --note tiles
```

Results land in `ios-asteroids/logs/astNN-note.txt` and `logs/astNN-note/`, which holds frames, named files, `result.json`, `summary.txt` and the undo file. Exit 0 means the self-test finished, 1 means it ran and did not, 2 means it stopped with a message. It ends in recovery, ready for the next cycle.

A new kernel or boot-args change needs `plans/devloop/build-loop.sh [KERNEL]`, which writes only to `plans/devloop/out`. Add `--no-push` if no root file changes.

## 1. `06-hfs-put.py` on a journaled root

**Writing is safe on a clean volume with an empty journal (proven on the Mac and in the source). The original `--undo` is not safe, so I patched it.** `dev.py` uses the patched copy, `plans/devloop/06-hfs-put.py`. `patches/06-hfs-put.patch` brings `plans/boot/06-hfs-put.py` to the same state.

Why the write path is safe:

- `replay_journal` returns at once when start equals end (`hfs_journal.c:1184`), so an empty journal cannot overwrite outside edits.
- A dirty journal gets refused before any write. The check reads the journal header even when the volume header says clean, because a volume header can lag behind the journal.
- The tool writes only file blocks, the bitmap, the catalog record and both volume headers. The journal and its pointers stay byte-identical, which the test asserts.

What `tests/check-hfs-journal.sh` ran, on a 128 MiB volume with a journal the Mac driver really used, made with `05-enable-journal.py`:

- `fsck_hfs -fn` passes after a same-size edit, after growth with new blocks, and after a later mount with more writes.
- The Mac driver reads the new bytes through the journal.
- `--state` and a real put both refuse a live snapshot with transactions in flight, and the refused put changes no byte.
- Undo right after a put restores the image byte for byte.

The test proves the reader problem too. In the live snapshot a raw read listed 3063 entries in a directory where the mounted volume listed 3093.

Patches to the tool:

- **Undo guard.** The original `--undo` after a mount produced `Invalid sibling link` and "found corrupt" from `fsck_hfs`, which I reproduced. It now refuses unless the volume header matches what the put left. After any boot, roll back by putting the old file. `dev.py` saves the old script as `renegade-firstboot.before`, but not your replaced files, so keep those.
- **`--state`.** It prints clean or why not, exit 0 or 3, and writes nothing.
- **A wrapped journal.** Pointers at the end of the journal now count as empty, as `replay_journal` does. The original refused needlessly.
- **A catalog record across two extents.** It is now refused. The original wrote contiguous bytes to the wrong place.
- **Coalesced writes and `--undo-dir`.** Contiguous changed blocks go in one `dd`. The original spent one `adb` call and a 1 MiB read-back on every 4 KiB block.

**What a reader must do:** run `06-hfs-put.py --adb SERIAL --state` first. `dev.py` does this and prints `ROOT FILES MAY BE STALE` when it is not clean. On a dirty root, `sweet-hfs-pull.py` can miss recent files, return old contents, or return blocks since reused. The kernel log ring is raw and is not affected. To read the truth, boot iOS once so the mount replays, exit cleanly, then pull.

skipped: journal replay in the reader, add when a crash run's root files matter more than the log.

Still unproven: the adb write path has never run, and `adb exec-in` on OrangeFox is unknown (inferred fine, because `exec-out` works and uses the same service). Writes are checked by read-back.

## 2. `dev.py`

It reuses `handsfree.py`'s wait, serial and verdict functions and `framecmp`, plus `sweet-log-pull.sh` and `sweet-hfs-pull.py`.

The steps are:

1. Wait for fastboot or recovery, and refuse an image without `sweetfbarm=1`, `sweetdeadman=N` and `sweetselftest=N`.
2. Enter recovery and stop if the battery is under 15%.
3. Check the root. If dirty, boot once to replay it, and stop if it stays dirty.
4. Put the files, save the log for comparison, go to fastboot.
5. `fastboot boot` and wait until the phone returns. The deadline is the deadman plus 120 s.
6. Enter recovery, pull the log, root state, battery, named files and frames, then print the summary.

The summary covers frame rate overall, while drawing and per step, flips, dropped, recoveries, backboardd restarts, maximum BOs, crashes by process, frames with no visible effect, blank frames and key lines. On `ast24-next.txt` it gives 9.7 frames/s, 4156 flips, 673 dropped, 3 recoveries, 5105 BOs.

Decisions you may want to know about:

- The allowlist refuses every `fastboot reboot`, including `reboot recovery`, which `/tmp/pull-log.sh` uses. `dev.py` runs `fastboot boot OrangeFox.img` instead and keeps that script's retry when the phone ignores it.
- `--undo` is refused by the allowlist. It stays a person's decision, and the stop message prints the command.
- If the kernel writes no new log, the old log would read as a result. `dev.py` compares against a log saved before the boot and reports `no-new-log`.

Every wait has a timeout and every command dies with its children after one. Mocks cover the failure paths: ignored boot, boot stall, dirty root, put failure, hard hang with and without a button, no phone, no recovery, low battery and stale log.

## 3. Self-test

**No reflash is needed (inferred).** `06-hfs-put.py` cannot create files, and the journaled root on the phone has no `sweetselftest` job. Those would need a 30-minute flash that also erases state. So `dev.py` puts the self-test at the top of the existing `/usr/local/libexec/renegade-firstboot.sh`, a root job that runs at every boot. It first takes out any earlier hook, so repeat runs are stable. The tools it needs are in that root, and `research/sweetnet/ios18/{sweetshot,sweettouch,sweetopen}` have the same SHA-256 as the root's copies (proven).

- `selftest/hook.sh` does nothing without `sweetselftest=N` and with `sweetselftest=0`, and a stub run under `/bin/sh` confirms it. It reads the boot-args with `/var/jb/usr/sbin/sysctl`, which is on the root but has not run on iOS 26. If it fails, the test does not run and the result reads `deadman`.
- `selftest/scripts/ui.sh` runs idle 30 s, home swipes, 3 apps, a Safari and a Notes keyboard, Control Center, Spotlight and the app switcher. It has 167 s of scripted sleeps. `quick.sh` is the short driver check.
- `sweetfbarm` and `sweetdeadman` are in every loop image, and `dev.py` refuses images without them.
- If the job has not finished first boot, the hook logs `SKIP` and lets it run. The phone's job finished in ast24.

If the `sysctl` route fails, use `patches/root-selftest.patch`. It passes `git apply --check` against the current `root_tweaks18.py`, and so does the older `plans/usb` one. It needs a root build and flash. Build the tool as in `plans/usb/README.md`, then run `plans/devloop/dev.py --print-script ui > ios-asteroids/research/sweetnet/sweetselftest.sh`.

**Frames (reader proven in code, output unknown).** Patch 2045 sends `kern.sweet_fb_shot` to `ast.va[ast.front]` at panel size, so the reader follows the current path and a frame is 360x797 at scale 3. Patch 2053 is off by default. With `sweetfb_ast_hw=1` the reader would use the wrong width and stride. `sweetshot` writing a PNG through ImageIO on iOS 26 is unknown, and a frame can mix two frames.

**Touch limits on iOS 26 (all unknown or inferred).** `sweettouch` ran on the Poco F1 on iOS 18 only.

- backboardd may ignore events from a sender ID it does not know, which would make every gesture do nothing.
- Edge gestures may not fire, because a real digitizer sends more fields than `sweettouch` does.
- It has one finger, and no hold. The app switcher relies on a slow 1400 ms drag, and the finger lifts when it stops.
- It cannot type text. Keys would be taps at guessed positions.
- The coordinates are guesses until frames confirm them. It scales 375x812 points to fractions of the screen.
- It assumes no passcode.
- `home` sends a menu press to an identity with no home button, so the script does the swipe first.

Frames named `.check.` must differ from the frame before, and the summary lists those that did not. This shows a gesture did nothing. It cannot show a tap hit the wrong thing.

## 4. Boot-args

The line limit is 607 bytes and `next-all` is at 589. The loop adds `sweetdeadman=600 sweetselftest=1`, 33 bytes.

- `next-loop`, 546 bytes, drops `sweetexecenv` for the status bar dylib, 73 bytes. Status bar frames will differ from a normal boot.
- `next-loop-sb`, 586 bytes, keeps the dylib and drops `sweetsyserr=backboardd,MobileSafari`, so those syscall logs go.

Both are built in `plans/devloop/out`, from next-fix's kernel, ADT and args. `build-loop.sh` builds on a copy of the shim, because the shim's `build.sh` writes its own `out/`.

## 5. Shim USB probe

The exact build command is `plans/devloop/build-usbprobe.sh`. I ran it. It copies the shim to `plans/devloop/out/shim-usbprobe`, applies `plans/usb/patches/shim-usbprobe-plain-ldr.patch` there, builds with `SHIM_USBPROBE=1`, and checks `usbprobe.o` has no writeback load. It wrote `plans/devloop/out/usbprobe-boot.img`, and `ios-sweet` stays untouched.

Boot it by hand with `fastboot boot plans/devloop/out/usbprobe-boot.img`, because `dev.py` refuses it. It stops at its screen, so it costs one photo and one Power + Volume down.

## 6. What still needs a person

- A dead battery. Charging during loops with the ADSP down is unknown, and `dev.py` stops under 15%.
- A hard hang, since patch 2061's watchdog is not in this kernel's loop args. `dev.py` waits up to 30 minutes for Power + Volume down.
- Judging visuals. Frame compares catch blank and changed frames, and only with a reference, whose thresholds are guesses.
- A `fastboot boot` stall in Sending. It was seen on the Poco F1 and is unknown here.
- New files or launchd jobs, which still need `renegade update` and a flash.
- A root that stays dirty, for example when a pushed driver crashes every boot. Boot with `--replay-image` and a known good image, or by hand.

## 7. Cycle times

Only the sizes and parse time are measured: a 160 MiB catalog parses in 0.6 s from a file, and `ui.sh` has 167 s of sleeps. The rest is inferred from `/tmp/pull-log.sh`, ast22's boot timing, and your 3-minute log pull.

| Step | Time, inferred |
| --- | --- |
| Check the root, pull the script, put files | 1 to 2 min |
| To fastboot and boot | 30 s |
| Boot to SpringBoard | 2 min |
| `ui.sh`, or `quick.sh` | 4 min, or 1.5 min |
| Back to fastboot | 30 s |
| Recovery, log, files, frames | 2 to 3 min |

- `ui.sh` with a root push takes about 10 to 12 minutes unattended, against 30 minutes plus a person today.
- `quick.sh` takes about 7 to 9 minutes.
- `--no-push` saves 1 to 2 minutes.
- A dirty root adds 6 to 10 minutes.
- A stuck boot ending at the deadman takes about 16 minutes.

## Unproven, biggest first

1. Whether `sweetreboot` or the deadman leaves the journal empty and the volume clean on the phone. The code unmounts the root (`kern_shutdown.c:286`, `hfs_journal.c` `journal_close`), but no second boot of a journaled root has run. If it stays dirty, `dev.py` stops at the replay and no put can run. This is the assumption the whole loop rests on, and the first run answers it, because the summary prints the root state.
2. Whether ABL clears the reason. Read the `0x7148 = ` line in the summary. 0x00 means cleared. 0x04 means it may not be, since recovery's `adb reboot bootloader` also writes it. Then stop using `sweetfbarm`, as `plan-usb.md` says.
3. Whether `sweetdeadman` runs. It has not run on the phone, so the deadline of 720 s for `sweetdeadman=600` is untested on real timing.
4. Whether HID touch works on iOS 26, and the other unknowns in section 3.
5. Whether `fastboot boot` of the 100 MiB OrangeFox image works here. `/tmp/pull-log.sh` used `fastboot reboot recovery`, which the brief refuses.
6. Whether writes through `adb exec-in` work on OrangeFox.
