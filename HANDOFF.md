# Handoff: read this first

Updated 2026-11-02, about 22:20 AEDT. Update this file whenever the state changes, and keep it the first thing a new agent reads.

## Phone state right now

- iOS 26.6.2 cold boots from the power button, with no Mac.
- `boot_a` holds the recovery-capable shim plus iOS, a full 100 MiB partition image with the signed vbmeta moved. It was built by `tools/ablfix/make_relocated.py` from `plans/recovery/out/recovery-boot.img`, the iOS image `next-flash-boot.img` with the recovery chain added.
- `boot_b` holds the plain iOS image, without the recovery chain, as a fallback.
- `boot_a` is marked **successful** since 19:00 (both GPT copies, `tools/boot/phone_mark.py`, audited by Sol). A START boot left its count at 6, so the countdown is stopped. `boot_b`: retry 7, not successful. Any flash to `boot_a` clears the mark: rerun `phone_mark.py SERIAL NEWDIR` then `--write`, from OrangeFox, after updating its pinned `WANT_A`/`WANT_B` bytes to the reviewed live values.
- `misc` was reset from `backup-asteroids/misc.img` at 18:39, because a stale `boot-recovery` command turned every boot into a recovery boot. It read empty at 18:44.
- `recovery_a`, `init_boot_a` and `vendor_boot_a` are stock, byte for byte the backups.
- `fastboot reboot recovery` now reaches OrangeFox (sometimes needs resending), and `tools/devloop/dev.py` uses it. Hands-free runs work again (ast64).
- Input latency: four kernel patches prepared, off by default, not yet run (`patches/input/` 2150 to 2153, `docs/input.md`): key poll period, touch on interrupt with sample timestamps, instant screen blank on power, and latching frames closer to the tick. Test after the lag agent, with a Sol audit of 2152 first, since it changes panel power.
- Running on 2026-11-02 evening: a lag agent drives the phone through `dev.py` (`plans/lag/`, `analysis/lag.md`); a Freeform agent does desk work (`plans/freeform/`, `analysis/freeform.md`).

## Overnight 2026-11-03, 01:00 to 08:00

The owner asked for the lag to be worked on autonomously overnight. The lag agent owns the phone after `plans/night-queue.sh` finishes (about 01:20): it picks a lever, runs it hands-free with `dev.py`, measures the share of 1-vsync gaps at 60 Hz, and keeps or drops it. Rules: RAM boots only, no flashing or boot-path writes, no 30 Hz cap, no visual reductions, keep and astwait together, stop on any run that needs a button. It ends by pushing the best known-good driver to the root. The Freeform and USB agents work at the desk only. The USB patches get a Sol audit before any boot, and their phone tests wait for the owner. The Mac is kept awake with `caffeinate`.

## What works

- Cold boot, and OrangeFox from the fastboot menu's Recovery mode
- Liquid Glass with no search band, black bars or zebra, and the icon tear fixed
- GPU hang fix, 0 recoveries in 9 scripted runs
- Real iPhone 11 status bar with even corners
- Widgets that need no internet
- The hands-free test loop, `tools/devloop/`

## Open, most wanted first (updated 22:05)

1. **Lag.** Found and mostly fixed (`docs/lag.md`). Frames were serial because our driver made 4 kernel calls per texture upload. `patches/lag/chl-staging-pool.patch` pools those buffers. `sweetfb_keep=1` (kernel 2082) shows a finished frame instead of dropping it, but alone it brings back black bars: the kernel held both CoreAnimation surfaces, SwapWait said complete at once, and hold-wait timed out. Kernel 2142 with `sweetfb_astwait=1` makes SwapWait honest, the way Apple's display does, and `patches/lag/bgmtl-hold-rest-2s.patch` bounds hold-wait's rest by time. Pool + keep + astwait: shown frames in animation bursts went from a median of 39.8 to 47.2 per second over 5 runs, with 0 hold-wait timeouts. The bar check (`sweetfb_barcheck`, 2090 and 2143) never saw the bars the owner saw, even in their failing config. The owner's hands-on on 2026-11-03 with `combo-all` (keep and astwait on, hw scale) saw no black artifacts, and found it smoother but still jittery. Jitter, meaning uneven frame timing, is the next lag target, and the owner wants even pacing over a higher average. Measured on hw-scale trace runs: with the fix, 65 to 70% of shown-frame gaps are 1 vsync, 23 to 29% are 2, the cadence flips between frames 30 to 36% of the time, and only 36 to 43% of animation time is evenly paced. Cheapest fix, no new code: `sweetfb_hz=30 sweetfb_vsync=1` tells CoreAnimation the display is 30 Hz, so every frame is shown for 2 vsyncs. Owner image `combo-keep-30`; hands-free A/B `plans/lag/out/pace-A60-boot.img` and `pace-B30-boot.img` with the `plans/lag/arms/lumatrace` payload. The owner tried `combo-keep-30` on 2026-11-03: a little better, but they do not want a 30 Hz cap as the fix, adaptive or not. It only confirms that uneven timing is what they feel. The goal is every frame within one 16.7 ms vsync at 60 Hz. Breakdown (`docs/lag.md`): about 40% of heavy frames arrive a vsync late. Of the skipped vsyncs, about 40% are render-bound (GPU 13 to 15 ms, encode 5 to 6 ms), and about 40% are idle, with CoreAnimation having nothing new, probably a late SpringBoard transaction. GPU clock and CPU frequency are proven useless levers. The best bet is GMEM rendering instead of the forced sysmem (`sweettu=noubwc,nocb,nolrz`), since GPU time grows with draw count, not clock. Runs queued for 2026-11-03 night by `plans/night-queue.sh`: A60, G60, A60, G60, then app spacing v4, which puts the daily `next-luma` driver back. Rule: never `sweetfb_keep=1` without `sweetfb_astwait=1`. The daily driver is `plans/lag/next-keep/out-metal` (pool and the 2 s rest). CoreAnimation still uses two surfaces, not three.
2. **Freeform.** Compute works on the A810: ast83 ran a trivial kernel and PencilKit's real `sdfPen_kernel`, all results correct, with no GPU recovery. Patch 2130 (inject into Freeform only) failed two Sol audits, `docs/input-ff-audit.md` and `docs/2130-reaudit.md`. The third version (22:15) refuses chroots on both sides, builds the path from the real root, is inert byte for byte with the arg off, and takes the MAC decision from `exec_handle_sugid` itself. Its host check, `tools/freeform/tests/`, passes 52 of 52. Sol's third audit (`docs/2130-audit3.md`) is a GO for a test boot with `sweetappmetal=Freeform`, and safe with the arg off. Its nonblocking risks: a broken injected library can kill Freeform before its watchdog starts, and a bad GPU dispatch can stall the compositor. It is in the combined kernel. Its driver changes are merged onto the daily driver as `plans/lag/next-ff2` (next-luma plus `next-ff.patch`; inert for backboardd unless `BGMTL_SPECIALIZE`, which only the Freeform trial sets). Phase 2b, with the owner drawing by hand, uses `plans/freeform/out/ff-owner-boot.img` and the dev.py command in `docs/freeform.md`. First try, ast100 on 2026-11-03: failed. backboardd hit `Assertion failed: push_surface, ogl-context.cpp:1186` early in the boot and respawned, so the owner saw no glass and a spinner. Freeform then got the injection and registered its device, and about 10 s later the phone hard-reset (`reset-unknown`, dirty root). Cause (`docs/freeform.md`, "ast100"): with `sweetappmetal=Freeform`, 2130 strips its own insert string from every inherited exec, and that string is byte for byte the one bgl-launch gives backboardd, so backboardd ran stock (no glass, then CoreAnimation's GL fallback asserted in `push_surface`). backboardd never ran next-ff2, and no GPU work was submitted all boot. Fix in progress: 2130 v4 inserts and strips a spelling only it produces. Then a Sol audit and a kernel build. With `sweetappmetal` off, nothing is stripped, so daily images are unaffected. next-ff2 still needs one normal boot before it can be the daily driver.
3. **Input** (`docs/input.md`, `patches/input/`). Sol: 2150, 2151 and 2153 GO. 2152, the instant power button, is NO-GO twice. The second audit (`docs/2152-reaudit.md`) found that a panel command that keeps failing, or a timed-out one, can leave the screen dark with no bound, and that a failed flip abandons the kept image. Decision: 2152 is narrowed to the pixel path (`sweetpanel=0`, the only mode the phone uses), does nothing in panel modes 1 and 2, keeps its rollback through flip failures, and stops holding CoreAnimation surfaces while dark. v3 (22:25, SHA256 d335e9bd) does exactly that: no DCS, no panel changes, the kept image stays owed until a flip back latches or iOS turns the display off, and fallback shows the kept image. Its host checks pass. Sol's third audit (`docs/2152-audit3.md`): NO-GO with `sweetpwrfast=1`, safe with it off. Blockers: if the restore flip and the fallback both fail, scanout can freeze on black; and a presentation that aborts mid-frame can become the rollback image. The first needs a latch failure plus a fallback failure, and 91 phone runs never logged `SWEET FB AST: fallback`. Plan: v3 goes into the kernel off by default. v4 (`patches/input/2152-asteroids-fast-power-v4.patch`, 22:40) fixes the second: frames while black go to a separate stage buffer that only counts once a whole frame passed. Sol's fourth audit (`docs/2152-audit4.md`): GO for a test boot with `sweetpwrfast=1` and `sweetpanel=0` on the 840x1860 canvas, given the accepted frozen-scanout limit, and safe with it off. The stage buffer's size is not checked against larger canvases, so don't change the canvas geometry with `sweetpwrfast=1`. The owner tried it on 2026-11-03 and rejected it: a double tap gave odd effects, and the screen and iOS catching up with each other felt wrong and too responsive. They want the power button to behave like an iPhone, so `sweetpwrfast` stays off. The daily candidate image is `combo-keep`, and on it the owner called the power button great (2026-11-03): 2150's faster key polling with iOS doing the blanking. v4 is in the combined kernel, off unless `sweetpwrfast=1`.
4. USB networking and SSH. The shim probe on 2026-11-02 (`docs/plan-usb.md`, Phase 0 result) shows the USB core powered and clocked after ABL and cleanly halted, so no PMIC power-up is needed. Next is the gadget code, with ABL's stale event buffer at 0xD017B000 replaced first and the DMA gates kept. An agent started it on 2026-11-03 00:35 (`plans/usb/gadget/`, patches from 2160, off by default). Sol audits before any boot.
5. Wi-Fi, then a modem probe for mobile data. `docs/research-wifi.md`.
6. Sound and a real battery reading. `docs/plan-power.md`.
7. Small ones:
   - Glass light or dark picked at random, fixed on 2026-11-02. Cause (`docs/glass-flat.md`): the launcher had skipped CoreAnimation's luma compute kernels since ast9, so UIKit read unwritten bytes when it picked light or dark glass and the status bar style. `patches/glass/luma-compute.patch`, built as `plans/lag/next-luma` (the daily `next-keep` driver plus the patch), ran in ast98: 101 luma results, 0 GPU recoveries, and `tools/glass/luma-frames.py` reads dark Calculator glass, light Notes glass and a dark clock, all wrong in ast96. The checker needs numpy and a run name with `nohw`. `bgmtl-bb.skipcompute` brings the old skip back without a rebuild.
   - Calendar widget stays a skeleton
   - App top spacing: apps still lay out at 68 pt. ast97 and ast99 proved `sweetsb` (`docs/app-safearea2.md`) loads in every app and lowers the window's and `_viewSafeAreaInsetsFromScene`'s top to 50, but the root view controller takes the max of that and `+[UIViewController _statusBarOverlapAndMarginInfoForView:inWindow:]`, which reads the raw scene inset or `statusBarHeight` (68). The owner saw Calendar draw high first, then drop. v4 hooks the overlap method instead, with a timeline probe. Staged, not run: `dev.py statusbar-real/boot/next-loop-sb-null-boot.img /usr/local/lib/sweetsb.dylib=...app-safearea/sweetsb-install.dylib --note app-safearea4`.
   - Only 24 GB of storage is visible
   - Apple logo early in boot (the bootloader's Nothing logo can't safely change: no logo partition, signed ABL)
   - The fixed 10 s wait in boot
   - A `com.apple.mobile...` service crashed 4 times in ast84, not seen before

**ast98 (23:43), combined kernel plus the luma driver, nohw, keep and astwait on:** all 22 self-test frames, 0 GPU recoveries, 0 backboardd restarts, then a `reset-unknown` after the last frame, which 17 of the previous 45 runs also had. SpringBoard took 20 s to come up instead of about 5.

**Combined kernel, built 23:08:** `next-combined/kernel/kernel.development.sweet` (SHA256 10f64275...), from the live tree with the lag patches 2140 to 2143 (all off unless their boot args are set), plus 2130, 2150, 2151, 2153 and 2152 v4. Built in a separate copy, `ios-asteroids/research/xnu-combined/dxb`, with `XNU_LOCAL_DIR=$PWD/research/xnu-combined USE_KDK=1 tools/xnu-local-build.sh --remake -j12 26.5` from `ios-asteroids`. A full build took 22 minutes. Hands-on images come from `tools/boot/build-combined.sh`: `combo-daily`, `combo-daily-pwr` (plus `sweetpwrfast=1`) and `combo-daily-ff` (plus `sweetappmetal=Freeform`, needs its libraries pushed). Loop images for hands-free runs come from `tools/input/build-input-loop.sh KERNEL`. Next: one hands-free run of `in-all`, then a RAM boot for the owner, and a flash to `boot_a` only if they like it, with `boot_b` kept as today's fallback.

**Plan once those clear:** one kernel with 2130, 2150, 2151, 2153 and the keep fix, all audited except the keep fix. 2152 joins only after a Sol GO. Build it after the lag agent is done with the shared kernel tree, `ios-asteroids/research/xnu-local/dxb/xnu`. Then reflash `boot_a` with the recovery chain (`tools/ablfix/make_relocated.py`, above 14 MB) and `boot_b`. Then rerun `tools/boot/phone_mark.py` (pin the new live bytes first), and the owner tests by hand.

## Rules from the owner

- Never reduce Liquid Glass or blur. No bars, zoom, crop or stretch.
- They accept risk, including boot path experiments, but audit risky steps with Sol, `gpt-6.1-sol`, first. Skipping an audit cost an hour today.
- Test with the hands-free loop rather than asking them, where a script can do it.
- Use `say` when you need them, and also say it in chat.
- No AI attribution in commits. The repo is public, so never commit dumps, logs, serials, firmware or Apple files.

## Hard lessons from today. Do not repeat them.

1. **Boot slot images must end below 14118912 bytes.** The stock signed vbmeta lives inside `boot_a`/`boot_b` at that offset, and a footer in the last 64 bytes points to it. A longer image overwrites it. Flashing a smaller one later does not restore it, because fastboot writes only the image's length. Then every boot and every `fastboot boot` fails with `Load Error`, and ABL marks the slot unbootable. Repair with a full 100 MiB image: `plans/ablfix/boot_a-ios-fixed.img`, made by `tools/ablfix/make_fixed_boot.py`. For larger images, use `make_relocated.py`, which moves the vbmeta after the image and rewrites the footer. The unlocked ABL accepts that. `recovery_a`'s limit is 48357376. See `docs/ablfix.md`.
2. **Never `fastboot boot` a stock Linux kernel.** ABL replaces the image's ramdisk with stock `init_boot`, so it is an Android boot, which can format `userdata`.
3. **`misc` can hold a stale `boot-recovery`.** Then START and power-on go to recovery mode. If it ever holds `--wipe_data`, OrangeFox may wipe `userdata` by itself on start. Read `misc` from OrangeFox first: `dd if=/dev/block/by-name/misc bs=4096 count=1 | od -c`. Reset it with `fastboot flash misc backup-asteroids/misc.img`, which is 1 MiB and zero BCB.
4. `recovery_a` has no kernel, so ABL borrows `boot_a`'s. Our shim detects recovery mode from the DTB and initrd size, bootconfig and a ramdisk hash, then jumps to the stock kernel it carries. Any doubt boots iOS. Proven on the phone in both directions with the probe image. See `docs/recovery.md` and `tools/recovery/`.
5. **ABL takes one retry** per power-on, hard reset and menu START, and none for Power + Volume Down, `fastboot boot` or recovery. A flash resets to 7 and clears `successful`. Nothing in ABL ever sets `successful`. See `docs/bootslot.md`.
6. `fastboot reboot recovery` is often ignored. Use the menu, or send it again.

## Danger

- Android formats `userdata` when it boots, which destroys the iOS root. Keep stock boot images off both slots.
- Never run plain `fastboot reboot`, `set_active`, erase, `-w`, or a lock state change.
- The LUN 4 GPT also lists `xbl`, `abl`, `tz` and `hyp`. There is no EDL programmer, so a bad table write means a repair service.
- A root flash erases iOS state.

## How to work

- **Kernel:** `patches/kernel/` in number order on top of upstream `nothing-sm7635-devs/ios-asteroids` branch `asteroids-ios26`, which ends at 2042. Apply 2043 to 2049 from `patches/kernel/` too, since upstream lacks them and 2051 needs 2047's `SweetFbScale.h`. Build with `USE_KDK=1 tools/xnu-local-build.sh --remake -j12 26.5`.
- **Driver:** `patches/driver/` on top of the fetch-fix tree.
- **Boot image:** args in `tools/boot/next-flash-args.txt`. The line is limited to 607 bytes, and `mkpayload.py` refuses more.
- **Flashing a slot:** check the size first. Over 14118912 bytes, use `make_relocated.py`. Keep `boot_b` as a known-good fallback.
- **Test cycle:** `tools/devloop/dev.py IMAGE [ROOTPATH=FILE ...] --test ui --note NAME`, one at a time (check `pgrep -f devloop/dev.py`). It reaches OrangeFox with `fastboot reboot recovery`.
- **Never put the text `devloop/dev.py` in a long-running command line,** such as a waiter. `plans/lag/run-arms.sh` waits with `pgrep -f devloop/dev.py` before each run, so a waiter that names it blocks the queue forever. Wait on a pid instead. This cost 10 minutes on 2026-11-02.
- **File pushes:** `tools/devloop/06-hfs-put.py` replaces existing root files from recovery, without a reflash. It cannot create new paths and refuses a dirty journal.
- **Root build:** `tools/boot/build-root-example.sh`, 7 minutes to build, 3 for the journal, then about 9 to flash.

## Reports

Each one marks proven and inferred. Trust the newest and check code against claims. Several inferences were wrong today:

- Entitlements were not the cause of the widget failure.
- SpringBoard uses the `STUI` status bar classes in SystemStatusUI, not UIKit's.
- `waitUntilScheduled` was not the serial frame cause.
- `init_boot` did not cause the `Load Error`.

Start with:

- `docs/roadmap.md`
- `docs/speed2.md`
- `docs/hangs.md`
- `docs/blackbars2.md`
- `docs/ablfix.md`
- `docs/recovery.md`
- `docs/bootslot.md`
