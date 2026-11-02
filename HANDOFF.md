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

## What works

- Cold boot, and OrangeFox from the fastboot menu's Recovery mode
- Liquid Glass with no search band, black bars or zebra, and the icon tear fixed
- GPU hang fix, 0 recoveries in 9 scripted runs
- Real iPhone 11 status bar with even corners
- Widgets that need no internet
- The hands-free test loop, `tools/devloop/`

## Open, most wanted first (updated 22:05)

1. **Lag.** Found and partly fixed (`docs/lag.md`). Frames were serial because our driver made 4 kernel calls per texture upload. `patches/lag/chl-staging-pool.patch` pools those buffers, and `sweetfb_keep=1` (kernel 2082) shows a finished frame instead of dropping it. Together they took shown frames in animation bursts from a median of 39.8 to 47.1 per second over 2 runs each. **But keep brings back black bars**: the kernel holds both CoreAnimation surfaces and hold-wait times out after 50 ms. The owner saw them in ast84. The lag agent is now making CoreAnimation use a third surface the way real iPhones do, by making our display behave like Apple's, with the 2090 bar logger as proof. Do not make keep default until that run shows 0 bars. The root currently has the pool payload (`plans/lag/next-keep/out-metal`) pushed by ast84; `next-fix/out-metal` is the previous known-good.
2. **Freeform.** Compute works on the A810: ast83 ran a trivial kernel and PencilKit's real `sdfPen_kernel`, all results correct, with no GPU recovery. Patch 2130 (inject into Freeform only) failed two Sol audits, `docs/input-ff-audit.md` and `docs/2130-reaudit.md`. The third version (22:15) refuses chroots on both sides, builds the path from the real root, is inert byte for byte with the arg off, and takes the MAC decision from `exec_handle_sugid` itself. Its host check, `tools/freeform/tests/`, passes 52 of 52. Sol's third audit (`docs/2130-audit3.md`) is a GO for a test boot with `sweetappmetal=Freeform`, and safe with the arg off. Its nonblocking risks: a broken injected library can kill Freeform before its watchdog starts, and a bad GPU dispatch can stall the compositor. Next: build it into the combined kernel, then run phase 2b in `docs/freeform.md`.
3. **Input** (`docs/input.md`, `patches/input/`). Sol: 2150, 2151 and 2153 GO. 2152, the instant power button, is NO-GO twice. The second audit (`docs/2152-reaudit.md`) found that a panel command that keeps failing, or a timed-out one, can leave the screen dark with no bound, and that a failed flip abandons the kept image. Decision: 2152 is narrowed to the pixel path (`sweetpanel=0`, the only mode the phone uses), does nothing in panel modes 1 and 2, keeps its rollback through flip failures, and stops holding CoreAnimation surfaces while dark. The input agent is rewriting it, then a fresh Sol audit. The combined kernel goes ahead without 2152.
4. Wi-Fi, then a modem probe for mobile data. `docs/research-wifi.md`.
5. Sound and a real battery reading. `docs/plan-power.md`.
6. Small ones:
   - Glass flicks white, and stayed white in Calculator
   - Calendar widget stays a skeleton
   - App top spacing could still be tighter
   - Only 24 GB of storage is visible
   - Apple logo early in boot (the bootloader's Nothing logo can't safely change: no logo partition, signed ABL)
   - The fixed 10 s wait in boot
   - A `com.apple.mobile...` service crashed 4 times in ast84, not seen before

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
