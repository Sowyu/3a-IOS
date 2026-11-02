# Stability and visual bugs: plan

2026-11-02. I worked from logs, source and the shared cache only. Nothing touched the phone, and every prepared fix is **untested on a device**. Kernel patches are not compiled, because kernel builds were out of scope. Prepared files are in `plans/stability/`, and paths below are relative to `EverIOS/`.

Labels: **Proven** means I read it in code or logs. **Inferred** means it fits the evidence but nothing has confirmed it.

## What changed the priorities

I found three things the earlier reports did not have.

1. **backboardd restarts the whole UI about every 3 minutes of use.** This hurts more than the GPU hangs.
2. **chronod runs with no entitlements because of a parser limit.** This is the best lead yet on the Waiting widgets.
3. **Recent hangs share one signature, and the CP is in the first IB2 of the submission.** That signature also allows much faster detection.

### Finding 1: exits at about 3800 command buffers

Proven: `[N] backboardd: 3 failed command buffers: exiting` follows three `submission failed` lines. The completed count at exit is close every time, even across boots.

| Session (bgmtl.log) | Command buffers at exit | Notes |
|---|---:|---|
| ast9 | 4042 | |
| ast14, ast15 | 3626 | Same event, the log is appended across boots |
| ast19 | 3776 | Not in the captured kernel log window |
| ast22, first process | 3932 | No RECOVERY had happened yet |
| ast22, second process | 3748 | |
| ast22, third process | 3624 and still alive | The session ended here |

In ast22 the restarts are at `ast22-linear.txt:12578` and `:19214`, about 165 s and 180 s of frames apart. Each one takes SpringBoard and PosterBoard down with it. Proven: a power press at `:12587` reached `0 clients`, so input is lost during the restart.

Inferred: a fixed table or limit fills up, and the sessions before ast22 were mostly too short to reach it. The log does not say which limit, because `chl_vk.c:2127` throws away the VkResult. Suspects, most likely first:

1. `SUBMIT_BOS_MAX` is 4096 (`SweetAdreno.cpp:74`, checked at `:884`), and Turnip names every live BO on every submission (`freeze.md`). A refusal returns with no log line.
2. `GEM_MAX` is 16384 handles per client, if handles leak. GEM_NEW also fails silently.
3. `SOBJ_MAX` is 8192 in `sweet_drm.c:551`, which would need two leaked syncobjs per command buffer.
4. `g_maps[16384]` in `sweet_drm.c:178` silently stops recording mappings when full.

Patch 2054 logs all the kernel refusals, tracks the BO list size, and lifts suspect 1. The client patch prints the VkResult.

### Finding 2: chronod has no entitlements

Proven: every boot logs `SWEET AMFI: com.apple.chronod: unreadable entitlements (14343 bytes), granting none`, and the same for deviceaccessd, photoanalysisd and Podcasts. `sweet_entitlements_create` uses `OSUnserializeXML`, and its string code (`OSUnserializeXML.y:659`) accepts only `&amp;`, `&lt;` and `&gt;`, jumping to `error` on anything else. I pulled three of the four signatures from the gpu14 root. chronod has 4 `&quot;`, deviceaccessd 6 and photoanalysisd 8, inside their temporary-exception SBPL. I did not pull Podcasts.

chronod holds `com.apple.private.chrono-extension-host` and `com.apple.runningboard.launchprocess`. Inferred: with neither granted it cannot launch widget extensions, which fits the Waiting widgets. In ast22 `WidgetRenderer_Default` runs 3 times, but the only widget extension exec is `MessagesSettingsWidgetKitExtensi`. Patch 2055 fixes the parsing, and its host check runs on the real blobs.

### Finding 3: the hang signature

`plans/stability/data/hangs.csv` covers every log, and `tools/decode_hang.py` decodes the dumps.

- Proven: all 9 recovery snapshots in ast18 to ast22 have RBBM `0xec0405`, INT `0x900000`, and WPTR minus RPTR equal to 31.
- Proven: 31 dwords is the fence tail, so the ring held only this one submission.
- Proven: RPTR sits on the scratch write just after `CP_INDIRECT_BUFFER`, and the observed fence is one behind.
- Proven: the A8XX decode of `0xec0405` is CP and UCHE busy plus the aggregate bits. HLSQ, VPC, CMP and VBIF_GX are clear.
- Proven: BR-IB2 equals the first IB2 of IB1 in 12 of 13 dumps from ast7 on (ast17 is the exception), where submissions hold up to 29 IB2s. With IB2 chosen at random, that pattern would be about a 1 in 10^11 event.
- Proven: IB1 is fetched to dword 320 to 384, long past the first IB2 call at about dword 80, and IB2 `rem` is 0. So these registers show fetch position. The exact stuck packet is unknown.
- Proven: of the kicks with RBBM `0xec0405`, 197 of 198 were on a CP position that later hung. Without ast16 that is 20 of 20. Kicks on healthy positions show other values.
- Proven: INT bit 23 is `RBBM_HANG_DETECT`, and Linux recovers on it (`a8xx_gpu.c:1231`). Older hangs (ast3 to ast6, some of ast14) did not have it, so it can only be a fast path.

Inferred, medium confidence: the hang is tied to starting work on a drained ring, with UCHE stuck, and not to any particular shader. The workloads preceding hangs vary, with app launches common (`InCallService`, `Podcasts`, `FaceTime`), but those are only the nearest execs.

**Correction to `gpu-hangs.md`:** the `CCU_RESOLVE` events at the top of these IB2s sit inside `CP_COND_REG_EXEC ... 0x33`. Inferred: sysmem skips them, so they are not evidence of a resolve hang. Proven: the flags were `sysmem,noubwc,nocb,nolrz` in ast18 to ast22, so sysmem alone does not stop these hangs.

SMMU faults are also real: `read, terminated`, FSYNR0 `0x50003`, at addresses in free space, up to fault 24 in ast22. None sits next to a hang, so I rate them as a separate bug that could feed one (inferred).

## 1. Crashes by process, last 5 sessions

| Process | ast18 | 19 | 20 | 21 | 22 | Symbolicated cause | Verdict |
|---|---:|---:|---:|---:|---:|---|---|
| MessagesBlastDoorService | 7 | 8 | 0 | 7 | 11 | Swift assertion in `BlastDoor.Sandbox.initialize` (`BlastDoor+0x1560d0`) | Harmless today. Each crash costs a ReportCrash and a respawn |
| SBRendererService | 1 | 1 | 0 | 1 | 1 | `_NUAssertFailHandler` in NeutrinoCore after allocating 840x1860 and 486x1077 surfaces | Unknown harm, see below |
| mediaplaybackd | 1 | 1 | 0 | 1 | 1 | ObjC throw from CoreFoundation, `abort` via `objc_exception_throw` | Harmful for video. Restarts and recovers |
| SpringBoard | 0 | 0 | 3 | 0 | 0 | PosterFoundation and BaseBoardUI assertions | Harmful, caused by the corrupted root |
| MercuryPoster, ClockPoster, AmbientPhotoFrame | 0 | 0 | 1 each | 0 | 0 | PosterFoundation assertion | Same cause |
| devicerecoverytool | 0 | 0 | 1 | 0 | 0 | `could not get registry entry`, runs after launchd decides to reboot | Harmless |
| safetyalertsd | 0 | 0 | 1 | 0 | 0 | SIGSEGV far 0x54 in libdispatch, from a CoreRoutine block, during shutdown | Harmless, seen once |
| backboardd self-exit (no CRASH line) | ? | ? | 0 | 0 | 2 | Finding 1 | **Most harmful** |

ast20 proven: `hfs_mounthfsplus: SWEET: mounting dirty non-journaled root read-write`, then repeated `hfs_mark_inconsistent`, failed writes, three SpringBoard crashes, and `rebooting due to critical process crashes`. The cause, a forced reset during writes, is inferred. `daily-driver-gaps.md:90` and `:236` call the root journaled, and the kernel log says it is not. I trust the log.

Decisions:

- **Slow, do not remove:** fairplayd.H2, wifip2pd and bluetoothd. Proven: they respawn about every 10 s (ast22: 63, 62 and 63 execs in 637 s, each with an xpcproxy). The patch sets ThrottleInterval 60. bluetoothd is skipped when BT firmware is present.
- **Leave alone for now:** MessagesBlastDoorService. It has no launchd job of its own (its name is absent from `launchd.plist`), and it only matters once Messages has a network. Its real fix is making the sandbox state call succeed. Read the checked call before `0x218a640cc` first (`data/blastdoor-sandbox.txt`).
- **Fix properly:** mediaplaybackd. Get the selector with `sweetscan=mediaplaybackd`. `analysis/plan-apps.md` also covers it.
- **Diagnose:** SBRendererService, with `sweetscan=SBRendererService`. `app-crashes.md` infers that Core Image cannot make a renderer without a Metal device. It may render a wallpaper or snapshot image, so it stays a suspect for missing images.
- **Ignore:** safetyalertsd and devicerecoverytool. They only appear on the way to a reboot.

## 2. GPU hangs and recovery

Rate: proven 9 recoveries in about 1146 s of frames across the 4 sessions that recovered, so one per about 2 minutes. Each one freezes the display for 5 s of waiting.

### The progress check (patch 2052, replaces 2050)

merged-audit finding 1 was that elapsed time alone can reset a healthy job. Each sample holds registers the kernel already reads: RPTR, RBBM_STATUS, the fence word, the BV read-pointer shadow, and IB1 and IB2 base and remaining for BR and BV.

- Waits of 1 s or more take a sample every 250 ms, shared across all waiters.
- Five seconds with nothing moving recovers. This covers the ast16 freeze, where only 2 s close waits ran.
- Samples more than 1 s apart restart the clock, because the gap proves nothing.
- WPTR is excluded, so new submissions behind a hung CP do not look like progress.
- Thirty seconds with movement but no fence recovers anyway.
- A long single wait with moving registers now returns a timeout and no longer resets.
- `sweetgpufast=MS` recovers after MS frozen with bit 23 up. It defaults to off and only logs `would recover here`.

Known limit, marked in the code: a real job that holds every bit still for 5 s still gets reset, the same as the base kernel. Turn on `sweetgpufast=1000` once one boot's logs show the bit never appears on a healthy kick (the kick line now prints INT). That alone would cut each freeze from about 5 s to about 1 s.

### Finding 2 from recheck-audit (patch 2053 plus client patch)

The kernel's INFO flags gain a capability bit and a recovery count. There is no ABI size change and no libdrm rebuild. Chlorophyll reads the count before commit and at completion, when the command buffer staged copies or has a capture. If it moved, it skips publication, capture and linear write-back. Handlers still run with Completed, so the three-failure exit is not triggered. The surface keeps its last good pixels until a redraw.

### Reducing hangs

Mitigation comes first and root cause is still open. Phase 3 lists the experiments.

Expected remaining causes of visible problems:

- **Spinners and sudden lock screens:** finding 1, inferred.
- **Freezes of about 5 s:** recoveries, proven.
- **Permanent freezes:** the ast16 path until 2052, proven. The pool free queue reduces it but does not close it.
- **Black app frames:** voided frames, then an idle compositor with nothing to redraw (inferred). Cold shader compiles after a restart add to it. ast22's second process logged 12.9 s of pipeline time in total.

## 3. Status bar

Recommendation: **prefer DynamicSplit tuned over Split828.** Split828's 227 pt gap covers 54% of the width on a phone with one small camera, and pushes the clock and battery 50 px outward. DynamicSplit already measures correctly and only its sizes are wrong.

Keep the 125⅓ pt centre gap at first, centred at x = 210 pt. The camera hole is much smaller than the gap, and I estimate 20 to 30 pt, but its size is not in the repo. Measure it from a photo. The items stay at their current height, centred at 38.5 pt, and I would not change height, because the top inset comes from the screen type. If it then looks too wide, narrow the rect in a second step. Each 10 pt less moves items about 5 pt inward (from the fit in `ui-issues.md`, not traced).

Prepared in `plans/stability/sweetsb/`:

- **Hooks:** `+[_UIStatusBarVisualProvider_DynamicSplit baseFontSize]` returns 14.2029 and `-normalIconScale` returns 1.0855. These are Split828's values times 420/414.
- **Clock:** proven at `0x18a71747c`, the clock size is a message send plus 1, so it should follow. I did not resolve that stub's selector, so this is inferred.
- **Loading:** only `sweetexecenv=SpringBoard:DYLD_INSERT_LIBRARIES=/usr/local/lib/sweetsb.dylib`. SpringBoard is not re-signed. Kernel 2040 adds the variable, and `bgl-launch` proves DYLD_INSERT works on backboardd, though the `root_tweaks18.py:950` comment says otherwise. Whether SpringBoard behaves the same is inferred.
- **Guards:** exact 840x1860 canvas, double return type, and hooks off from the third unfinished start in a boot.
- **Recovery:** boot an image without the boot-arg. Nothing is flashed. A missing dylib may stop SpringBoard from starting, so install the file first.
- **Diagnosis:** at 60 s it logs the value of the override key in SpringBoard and `_isEmbeddedScreen`, which answers the open question in `ui-issues.md`.

Rejected: an ADT route, for the reasons in `ui-issues.md` (no property supplies the rect, and the Split table needs native width 828). Unknown: whether in-app status bars use SpringBoard's process. If Settings still shows large items, apps need their own injection, and the 320-byte `sweetexecenv` buffer limits how many.

## 4. Other visual bugs

- **Wallpaper needing swipes:** proven in ast22 that the job now selects the poster (`:6510`) before registering the apps (last at `:9825`), and the first recovery is far later. The owner should say whether swipes are still needed. If they are, the stall theory in `ui-issues.md` is wrong for this boot and the idle compositor is the suspect.
- **Waiting widgets:** patch 2055 first.
- **White squares and pale tiles:** no capture ties these to a cause. `render-bugs.md` lists luma kernels and XR approximations as candidates, and the skipped luma compute is still in place. Capture one with capture v2 after the restart fix, then compare against the fetch and tear results.
- **App launch black frames:** add a redraw after recovery. `watchdog_bb` already polls kernel state each second, so it can notice the count change. The call that forces a CoreAnimation redraw is not identified. Find it in QuartzCore first.
- **HEIC:** root cause unknown (`heic-decode.md`). See Phase 5.

## 5. Touch, dropped input, power button

- **Proven costs:** 20 ms key polling (`sweet_keys.c:109`), about 3 ms touch polling, copy average 12.7 ms and max 33.9 ms, latch average 4 ms, and `gpu_hold_ticks=3414` by the end of ast22, about 58 s at 59 Hz.
- **Dropped input, proven:** presses during a restart reach 0 clients.
- **Dropped input, inferred:** touches during a 5 s hang have no visible result, and the 3-minute restarts probably explain much of the reported drop.
- **Power button delay, inferred:** the key reaches HID within one poll, so a long delay is downstream, in backboardd or a held frame. The kernel log has no key-to-display timing, so I cannot give a number.
- **Plan:** fix restarts and hangs first, then measure. Do not tune the touch driver before that.

## Phases

Agent time excludes waiting for the owner. Verification needs the owner's boot.

### Phase 1: stop the restarts and fix widgets (about 1 hour)

1. Apply 2054 and 2055 and the Chlorophyll log patch, then build the kernel and payload (45 min).
2. Apply `root-loop-throttle.patch` and rebuild the root (15 min plus build time).

Depends on: nothing else. Risk: low for logging and 2054. Medium for 2055, since four daemons gain real entitlements for the first time, and photoanalysisd may use more CPU.

Verify on the phone:

1. Use it for 10 minutes and look for `3 failed command buffers`. The pass is no such line after about 4000 command buffers.
2. Read `submissions now name N BOs` and any `submit refused` or `[chl] vkQueueSubmit` line. These name the limit if it fails.
3. Confirm the four `unreadable entitlements` lines are gone and widgets show content.
4. Confirm fairplayd.H2 execs drop to about 1 per minute.

### Phase 2: survivable hangs (about 1 hour)

1. Apply 2052 and 2053 and the epoch client patch.
2. Boot once at the default and collect `sweetgpufast would recover` lines plus the INT values on kicks.
3. If bit 23 appears only on hung positions, set `sweetgpufast=1000`.

Depends on: Phase 1 in the same kernel is fine. Risk: medium, as this changes when a reset happens. The worst case is a healthy static job being reset, as it is today.

Verify: `watch: frozen` is followed by `RECOVERY`, no recovery fires while the RBBM value changes between samples, and freezes last about 1 s with the fast path. `recovery during this command buffer` should appear after some recoveries, with no wrong surfaces after them.

### Phase 3: reduce the hangs (2 to 4 hours of agent time, several boots)

Depends on: Phase 1, so sessions last long enough to compare. Measure recoveries per 1000 command buffers, since sessions differ in length.

1. Extend `dumpHang` to several hangs per boot, and dump the nested draw-state groups of the hung IB2 (1 hour).
2. Run the planned `flushall,syncdraw` boot and compare rates.
3. Test the drained-ring idea. Add a kernel-side wait before the IB on a ring that was idle, or a keep-busy no-op, and compare.
4. Audit HWCG, because `gpu-hang-fix.md` notes that Linux runs a full sequence and this kernel does not. Compare idle transitions.
5. Log the client and pipeline for each SMMU read fault, and find what produces those addresses.

Risk: low for diagnostics. Unknown for cause: I cannot promise the hangs go away.

### Phase 4: status bar and crash diagnostics (about 1 hour)

1. Run `sweetsb/build.sh`, apply `sweetsb/root-sweetsb.patch`, and set `RENEGADE_SWEETSB`. Include this in the Phase 1 root if convenient.
2. Boot with the `sweetexecenv` argument, plus `sweetscan=mediaplaybackd,SBRendererService`.
3. Compare the clock and battery with the expected sizes in a photo, on the home screen and in Settings.

Risk: medium for that boot only. Booting without the argument undoes it.

Verify: `/private/var/tmp/sweetsb.log` shows both hooks and the override line. Measured battery width should be about 41 px against 48 px now.

### Phase 5: HEIC (half a day, plus boots)

1. Package `heic-fix/decode_probe.m` into a root and run the three-file test from `heic-decode.md`.
2. If the CPU bitmap is clean, the bug is in surface export or GPU import, so add paired CPU surface dumps next.
3. If it is corrupt, test forcing the frame path: an interposer that fails `VTTileDecompressionSessionCreate`, so CMPhoto takes its alternative at `0x1a738878c`. That code exists (proven), but a working fallback is inferred.
4. Load it into the decoding processes with `sweetexecenv`. Identify those first, because the decoder may be in a service.
5. Last resort for a software route: bundle a HEVC decoder and interpose at the ImageIO level, about 2 days and a real size and licence cost.

Keep converting wallpapers to PNG meanwhile. Photos and Safari grid HEICs stay scrambled until this phase lands.

Risk: low for the probe, medium for the interposer. Verify: a grid HEIC in Photos and in Safari matches the Mac decode.

### Phase 6: filesystem safety (half a day to design)

1. Check whether the root builder can create the HFS journal, since the log says there is none.
2. If it cannot, run `fsck_hfs` from recovery after an unclean stop, keeping in mind the segfault in `daily-driver-gaps.md:90`.

Depends on: nothing. Risk: high without care, because it changes the root format. Until then, use Settings > Shut Down. Verify: force a reset on a throwaway root and check the next mount line.

## Prepared files

Each patch states what it does and how to apply it in its header. Run each check with `python3` from any directory.

| Fix | Files | Host check |
|---|---|---|
| Submit limit | `2054-submit-limit/2054-asteroids-a810-submit-limit.patch`, `chlorophyll-submit-log.patch` | Apply check only |
| Entitlements | `2055-entitlement-escapes/2055-asteroids-entitlement-escapes.patch` | `ent_check.py` |
| Progress check | `2052-progress-watchdog/2052-asteroids-a810-progress-watchdog.patch` | `watch_check.py` |
| Epoch | `2053-recovery-epoch/2053-asteroids-a810-recovery-epoch.patch`, `chlorophyll-epoch.patch` | `epoch_check.py`; client compiled for iOS |
| Daemon throttle | `root-quiet/root-loop-throttle.patch` | `throttle_check.py` |
| Status bar | `sweetsb/` source, built dylib, `root-sweetsb.patch` | `sweetsb_check.py` |

All pass on the Mac. The kernel patches apply together in numeric order on a copy of the current `xnu-local` sources. Both root patches apply together on the current uncommitted `root_tweaks18.py`. `plans/stability/SHA256.txt` has the hashes, and `plans/stability/data/` and `tools/` hold the evidence and scripts.

## Limits

- The stuck packet and the reason for the first-IB2 pattern are unknown. Fetch registers do not show execution position.
- The limit behind Finding 1 is not identified. If 2054's logs name a different table, it needs another small patch.
- I did not fully trace the BlastDoor sandbox call. `dcache.py` was too slow, so I used raw disassembly, and calls in those dumps are unresolved.
- The `0xec0405` predictor rests on few independent hangs, and 177 of its 197 hits come from one stuck position in ast16.
