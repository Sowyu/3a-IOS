# Roadmap to a daily-drivable phone

2026-11-02. This merges `plan-stability`, `plan-speed`, `plan-power`, `plan-boot`, `plan-usb`, `plan-apps`, `research-wifi`, `statusbar-compare` and `fetch-fix`. Every time below is agent time plus owner boots, and all of them are estimates. Nothing in the new candidate has run on a phone.

## What daily use means here

| Wanted | Verdict | When |
|---|---|---|
| Stable UI, no tearing, smooth | Possible. The tear cause is still unproven | Stages 1 to 3, days |
| Survives a flat battery or forced reset | Possible, needs the boot work | Stage 2, days |
| YouTube | 480p with sound is possible through Safari. The official app is not | Stage 6 and 7, 2 to 3 months |
| Texting friends | Telegram Web first, then a from-source client. iMessage and SMS are not possible | Stage 6, 6 to 10 weeks |
| Hotspot | Impossible. There is no cellular uplink. The phone can join someone else's hotspot | Never |
| Real battery percent and sound | Possible, shares the ADSP port | Stage 5, 3 to 5 weeks |

## Stage 0: the candidate built today

Everything below is in `next-fix/`. The kernel compiled and linked. Risky changes stay off unless a boot arg turns them on.

Kernel `next-fix/kernel/kernel.development.sweet`, patches in `ios-asteroids/xnu/patches-12377`:

| Patch | Does | Default |
|---|---|---|
| 2051 | Display tick on a deadline. It fixes the bug that discards 30 to 43% of finished frames | Off, `sweetfb_tickfix=1` |
| 2052 | Flip timing log | On, log only |
| 2053 | VIG0 hardware scale | Off, `sweetfb_ast_hw=1` |
| 2054 | GPU hang recovery on no progress, not elapsed time | On |
| 2055 | Recovery count for the client | On |
| 2056 | Raises the BO limit behind the UI restart suspect and logs refusals | On |
| 2057 | Entitlements containing `&quot;` now parse, for chronod and three others | On |
| 2060 | Any reset lands in fastboot, panic auto-exits, deadman timer | Off, `sweetfbarm`, `sweetpanicfb`, `sweetdeadman` |
| 2061 | Gunyah watchdog | Off, `sweetgwdt=N`. Has a data-loss risk |
| 2062 | USB probe v2, reads only | Off, `sweetusb=1` |
| 2070 to 2073 | Thermal guard, GPU thermal level, CPU floors, power survey | `sweetthermal` defaults to acting, so every image says it: `sweetthermal=0`, or `1` (log only) in `next-thermal`. The others are off |

The numbering collision is resolved. Stability's 2052 to 2055 became 2054 to 2057. Their folders under `plans/stability/` keep the old numbers.

GPU driver `next-fix/out-metal`: fetch sync with a guard that refuses it when binning is on, the submission result in the log, recovery epoch, GPU timing behind a file, and perf logging that survives the tmp cleaner.

Root `next-fix/ios26-gpu15j-root.simg`: PNG wallpapers, daemon throttle, `sweetsb` dylib installed, `sweetappcheck` diagnostic, generalised app containers, journal on. `next-fix/ios26-gpu15-root.simg` is the same root without the journal, kept as the fallback.

Boot images in `next-fix/boot/`. The boot-arg line holds only 607 bytes and used to cut off silently, so `mkpayload.py` now refuses an overflow.

## Stage 1: test day (about 40 minutes of owner time)

Each step answers one question.

1. Boot `exp-boot/flushall-boot.img` on the current root. Tear gone means stale fetch is real. No flash.
2. Flash `ios26-gpu15j-root.simg` (about 10 minutes). This erases the current iOS state.
3. Boot `next-all`. Use it for 10 minutes, past about 4000 command buffers.
4. Boot `next-nosync` if the icons look different, as the A/B for the tear.
5. Boot `next-thermal` once, separately. Its first read comes 20 s in, and `SWEET THERMAL:` readings calibrate the limits. A bad read panics into fastboot.
6. Settings > Shut Down after each boot, then Power + Volume Down.

Pass criteria:

- No `3 failed command buffers` line. If it returns, `submit refused` or `[chl] vkQueueSubmit` names the real limit.
- The four `unreadable entitlements` lines are gone and widgets show content.
- Flips per second rise during a fixed animation. `coalesced` falls from 30% of swaps to under 10%.
- `hfs: Initializing the journal` on the first boot, no `mounting dirty` line after.
- `sweetfbarm: armed`, and the next boot reads `0x7148 = 0x00`. If it does not read 0, stop using `sweetfbarm`.

Fallbacks: `next-nosb` if SpringBoard will not start, `next-notick` if swaps fall, `next-plain` for kernel changes alone. A root that will not mount means reflashing the plain simg.

## Stage 2: stop losing the phone (2 to 3 days, 6 to 10 boots)

Do this before any long unattended use. A reset or flat battery still boots Android today, which wipes the root.

1. Apply `plans/boot/01`, then flash the shim to `boot_a` and `boot_b`, never `set_active`. Cold boot 8 times to expose any retry countdown.
2. Apply `plans/boot/03` and `04`, so restart stays in iOS with a guard against boot loops.
3. Use `plans/boot/06-hfs-put.py` for file updates. A driver update drops from a 10 minute reflash to seconds and keeps state.
4. Test what a flat battery does once, attended. It may boot-loop on plug-in, and the fix is about 10 lines in the shim.
5. Calibrate thermal limits from Stage 1, then default to `sweetthermal=3`.

Unknowns: whether flashing keeps `slot-successful:a`, and whether the phone charges with the ADSP down.

## Stage 3: hands-free testing (1 day, then every later stage goes faster)

1. Run the shim USB probe from `plans/usb/patches/shim-usbprobe-plain-ldr.patch`. The earlier aborts came from a writeback load, which Gunyah cannot emulate. It takes 15 minutes and needs one photo.
2. Prove `sweetpanicfb` and `sweetdeadman` with one boot each.
3. Apply `plans/usb/patches/root-selftest.patch` and run `plans/usb/host/handsfree.py`. The Mac boots an image, the phone self-tests and returns to fastboot, and the Mac pulls logs and compares frames. About 5 to 8 minutes per unattended cycle.

Add `sweetgwdt` last and attended. A watchdog bite that ignores the reason boots Android.

## Stage 4: look and feel (1 to 2 weeks, runs alongside everything)

In order of gain per effort:

1. Icon tear: use the Stage 1 result. If `sweetfetchsync=1` works, measure its cost from the `fetch-sync count` line and keep it. If neither step 1 nor 3 fixes it, the stale fetch theory is wrong, so capture the target draw by draw.
2. Hang rate: `sweetgpufast=1000` once one boot proves bit 23 appears only on hung positions. Freezes drop from about 5 s to about 1 s.
3. Hardware scale: `sweetfb_ast_hw=1`. Copy drops from 12.7 ms to perhaps 3 to 6 ms.
4. Status bar: `sweetsb` fixes sizes. It does not fix position. Ours sits at y 38.6, where a real Dynamic Island phone sits near 29. Wifi and signal icons stay absent until Wi-Fi exists. In-app status bars may need their own injection.
5. Zero-copy flip, high risk, 1 to 2 days. Then AIR front-end cache for the 340 to 420 ms shader stalls.
6. HEIC: `heic-fix/decode_probe.m`, half a day. Wallpapers stay on PNG meanwhile.

Fixed rules: no reduced Liquid Glass or blur, canvas stays 420x930 pt at 2x, no bars, zoom or stretch.

## Stage 5: power (1 to 2 weeks after Stage 2)

1. Boot `next-screen` for screen off on Power. Retest brightness with `sweetpanel=2`.
2. Boot `next-survey` with a charger on and off to learn if it charges under iOS.
3. Add 2072 CPU floors, measured against fixed clocks.
4. Port the ADSP start and `ShengBattmgr.c` for the real battery percent. Add low-battery shutdown, which depends on it.

Estimated screen-on life 4 to 6 hours and screen-off 11 to 19 hours after steps 1 to 3, unmeasured. Real suspend is unlikely.

## Stage 6: Wi-Fi (4 to 7 weeks to first internet, 6 to 10 to stable)

Route: unmodified ath11k on a LinuxKPI layer, as HakiOS did on the same chip family. Check its licence first.

1. Desk work: lift the plumbing, generate QMI tables, build ath11k. 4 to 7 days, no phone.
2. Read-only boot probe. Add SID 0x1400 to the 2062 dump. 1 to 2 days.
3. Power sequence and WPSS start. 3 to 6 days.
4. GLINK, QRTR and WLFW. 3 to 5 days.
5. SMMU view, rings and scan. 1 to 3 weeks. This is the step that may not resolve at all.
6. Join, DHCP, page load. 4 to 8 days.
7. Interrupts, block ack, saved passwords, WPSS recovery. 1 to 3 weeks.

Needs Stage 2 first, because a kernel fault here must not reach Android. Add the kernel table entry for privilege 10001 at step 6, or WebKit's networking looks broken.

USB gadget and SSH: try in parallel with a one week time box. If the eUSB2 PHY blocks it, drop it. SSH over Wi-Fi is the fallback, and the hands-free loop still works.

## Stage 7: apps

1. Run `sweetappcheck` on the first gpu15 boot, already in the root. It answers JIT, H.264 decode, AVPlayer without audio, keychain and Metal.
2. WKWebView test app showing local text and video. 1 to 3 days.
3. Telegram Web on a secondary account once Wi-Fi works. No background delivery.
4. `m.youtube.com` in Safari, silent first.
5. Sound. Silent HAL plug-in in 1 to 2 days if AVPlayer stalls without a device, then the real port in 3 to 5 weeks, sharing the ADSP.
6. YUV in a shader for video speed, 2 to 4 days.
7. Telegram-iOS from source, plus a background job that posts local notifications, 1 to 2 weeks.

Do not keep personal secrets on this phone. Data at rest is plain, so use a secondary account.

## Biggest risks

1. The 3800 limit may not be the BO cap. 2056 then only names the real one.
2. GPU hang cause is unknown. Recovery gets faster, but the hangs continue.
3. The SMMU step of Wi-Fi may not work under Gunyah.
4. The first flash from `boot_a` may start a retry countdown.
5. JIT may not run under this kernel, which would make YouTube's page very slow.

## Skipped on purpose

Hotspot, iMessage, SMS, USB host and Ethernet, real suspend, kernel-side fsck, a separate data volume until reflashes cost real data.
