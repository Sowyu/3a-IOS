# Input latency: touch and the power button

2026-11-02, desk work only. Nothing here ran on the phone. Patches, scripts and the test plan are in `plans/input/` (see its `README.md`).

Labels: **Measured** means a log shows it. **Code** means I read it in the kernel tree. **Model** means a host simulation built on measured or code numbers. **Inferred** means reasoning with nothing run behind it. **Missing** means no data exists yet.

## Where a touch's time goes today

One finger move, from the panel to light, at 25 to 35 frames/s with hardware scale on (ast66 to ast70, the lag agent's trace build):

| Stage | Today | Label | Source |
|---|---|---|---|
| Controller scan and report | unknown, report rate unknown | Missing | |
| Report ready to HID event sent | 2.3 ms average, 0.9 of it the SPI read | Model, Code | `sweet_touch_asteroids.c`: 2 ms wait plus a 93-byte read at about 1 MHz |
| Touch-down from idle | up to 4 ms more | Code | idle check every 4 ms |
| HID event to backboardd to the app | not timed | Missing | |
| App waits for its update cycle | about 8 ms average, up to 16.7 | Inferred | 60 Hz vsync notification |
| App handles it and commits | not timed | Missing | |
| Render server waits for its tick | about 8 ms average, up to 16.7 | Inferred | same |
| Render, frame start to swap | p50 5 ms, mean 12 ms | Measured | `lagtrace.py`: encode p50 2.2 and mean 7.0, submit 1.3 and 2.3, to swap 1.0 and 1.7 |
| Swap to the tick that takes it | p50 9.4 ms, p90 20.7, mean 11.2 | Measured | `lagtrace.py` |
| Tick to latch | p50 7.1 ms, mean 6.2 to 7.4 | Measured | `lagtrace.py`; the wait for the latch alone is 3.8 to 6.1 ms (`latch_us avg`) |
| Scan-out down to the finger | about 4 ms | Inferred | half of a 120 Hz frame |

The mean sum is about 55 ms plus the app's own work, and p90 is near 100 ms. The touch driver is about 4% of it. The display path after the swap is about a third, and it's the part the kernel controls.

Things I checked that are not costs:

- HID delivery runs on the platform work loop, which only the two HID services use. It does not share the framebuffer's work loop, whose tick spins about 6 ms per frame waiting for the latch (Code: `IOResources::getWorkLoop`, `KernelConfigTables.cpp`).
- Kernel log lines on the key path cost microseconds. `sweetfb_noconsole=1` mutes the console, so `printf` only appends to the RAM log (Code: `serial_console.c:_cnputs`).
- Both poll threads run at priority 81, above every non-realtime user thread (Code).

### Does iOS's touch pipeline wait for the display link?

Yes, I'm fairly sure, but nothing ran to prove it. iOS 26's UIKit (23G90 shared cache) has `_UIUpdateSequence` with `HIDEventsItem`, `LowLatencyHIDEventsItem`, `CADisplayLinksItem`, `CATransactionCommitItem` and `LowLatencyCATransactionCommitItem`, plus `_UIUpdateCycleEnabled`. So the app takes touches at the start of an update cycle that the display link paces. A faster notification to CoreAnimation would halve both 8 ms waits for an input-driven frame, even at low throughput. The cost is unknown: CoreAnimation may then start twice as many frames that the GPU cannot finish. `sweetfb_ca120=1` already exists (patch 2081) and has never been booted (the two logs that name it ran at `sweetfb_hz=60`). It is an optional variant in the test plan.

`sweetfb_hz=120` alone did not change swap to flip before hardware scale: ast40 26.5 ms against 25.7 to 26.8 at 60 Hz, when the copy dominated (Measured). With the copy gone, a 120 Hz tick should cut swap to tick from about 11 ms to about 7 (Inferred).

### What timestamps and the report rate do

Today the event time is when the driver sends it, about 0.9 ms after the read started. Reads are 3 ms apart, so a 120 Hz report reaches iOS 6 or 9 ms after the last one. The timestamp jitter is about 1 ms at 120 Hz and 1.4 ms at 240 Hz (Model). That is ±12% noise on every velocity UIKit computes, which shows in flicks. With reads on the IRQ edge and the time the edge was noticed as the stamp, the jitter is 0.17 ms (Model). The touch service reports a 10 ms report interval. I added `sweethid_touch_us` to correct it, but I don't know whether iOS reads it, so it stays a later A/B.

## The power button today

Lock, from the code and the order of log lines (the kernel log has no timestamps, so most of this is Missing until 2150 runs):

1. The keys thread polls the PMIC every 20 ms, so a press waits 10 ms on average (Code). The PMIC's own debounce comes first. Linux asks for 15.6 ms by default, and the value here is unknown (2150 logs it).
2. The HID event goes to backboardd and SpringBoard. SpringBoard turns the display off after the release, not the press: every lock in the logs has `display off` after `key=0 down=0` (ast9 lines 18540 then 18548, and ast6 lines 10260 then 10662 with a GPU recovery between them) (Measured order). The cache has `SBDoubleClickSleepWakeHardwareButtonInteraction`, so it may also wait out a double-click window (Inferred).
3. iOS calls `RequestPowerChange(0)`. The next vsync tick, up to 16.7 ms later, blacks out the frame: a memset and a flip, up to 8.3 ms to latch (Code).
4. With `sweetpanel=0`, the panel stays lit drawing black. With `sweetpanel=1`, it gets DCS 28 (display off) at the next front porch, then 10 (sleep in), which keeps the panel thread busy for 220 ms (Code).

Wake: iOS asks for display on before the key comes up (ast9 5374 down, 5387 on, 5399 up; ast7 19082, 19093, 19097) (Measured order). The kernel presents the latest frame on the next tick, gated on the GPU. With `sweetpanel=1` it only then sends 11, waits 80 ms, sends 29 and waits 10 ms. So `sweetpanel=1` makes wake 90 ms slower than today unless the panel is woken earlier (Code).

What `sweetpanel` does now (Code, `sweet_dsi_asteroids.c` and `SweetFramebuffer.cpp`):

- `0`: no panel commands.
- `1` (the default when the arg is absent): display off and on follow iOS's power state through a panel thread.
- `2`: also the brightness service with the CoreBrightness calibration.

The `push_surface` abort came with mode 2's backlight service. backboardd asserted in `ogl-context.cpp` when it drew offscreen targets such as the wallpaper poster and recents. Mode 1 had no crash (ast5-p1). `plan-power.md` infers that this was the CPU renderer's path, which backboardd has since left. **Mode 1 has never run with the asteroids flip or hardware scale**: every log from ast10 on has `sweetpanel=0`. The tear-free work set it to 0 because blackout then stopped flipping, and today's `blackout()` keeps flipping ("flip retained").

PON interrupt: not cheap. No sweet driver takes a GIC SPI today, only IPIs and PMI (Code). PON would need PMIC interrupt-enable writes (the keys driver is read-only on purpose), SPMI arbiter interrupt routing, and PDC setup under Gunyah. A 5 ms poll costs about 400 SPMI reads per second, well under 1% of a core, and cuts the average wait from 10 to 2.5 ms.

## Fixes prepared

All of these are off by default, and each is a boot arg. Each one passes a syntax check with the build's own flags and adds no new kind of warning.

Apply order:

1. Upstream `asteroids-ios26`, which ends at 2042.
2. The local patches 2043 to 2049 (`ios-asteroids/xnu/patches-12377/`, now also in `3a-IOS/patches/kernel/`). 2047 creates `SweetFbScale.h`.
3. 2051 to 2120.
4. Optionally the lag agent's 2140 to 2143.
5. 2150, 2151, 2153, and 2152 last.

Step 5 is checked on the 2120-series base and on the live tree. The corrected steps 1 to 3 have not been re-checked from a clean upstream.

| Patch | Boot args | Does | Expected |
|---|---|---|---|
| 2150 | `sweetkeys_ms=N` (2 to 100, default 20) | Key poll period. Timing lines: `display off/on (power key down X ms, up Y ms ago)`, `first frame after display on: N ms`, `SWEET PANEL: display ... (rc, N ms)`, key edges with `t_us`, PON debounce at boot | Press seen 7.5 ms sooner at 5 ms; the iOS share of lock and wake becomes measurable |
| 2151 | `sweettouchirq` bits: 1 read on the IRQ edge, 2 stamp at the read start (when the edge was noticed, not the controller's scan time), 4 one stats line per touch; `sweettouchirq_us` (500); `sweethid_touch_us` | Reads on the edge while touched, with a 12 ms fallback | Report to sent 2.3 to 1.1 ms, jitter 1 to 0.17 ms (Model). The stats line measures the report rate |
| 2152 v3 | `sweetpwrfast=1`, `sweetpwrfast_ms` (300 to 3000, default 2000). Only with `sweetpanel=0` | The screen goes black at a short power release, in parallel with iOS. iOS's display off confirms it. A touch, another key, or the window passing without iOS brings the kept image back | Lock is black about 10 to 30 ms after release, whatever iOS is doing |
| 2153 | `sweetfb_lead=US` (1500 to 7000, suggest 3000) | Moves the vsync tick, at most 1 ms per frame, until its flip latches that long after the tick | Tick to latch 7.2 to 2.9 ms (Model). About 3 ms off every frame on average, and 4 on boots like ast66 |

### 2152 v3, pixels only, after Sol's re-audit (`analysis/2152-reaudit.md`)

Sol rated v2 NO-GO. Findings 1 to 3 were about the panel commands, 4 about pixel rollback when a flip fails, 5 about the apply order, and 6 about CoreAnimation holds. The owner decided to narrow 2152 to the pixel path, since the phone only runs `sweetpanel=0`.

1. **With `sweetpanel=1` or `2`, 2152 does nothing.** It logs `sweetpwrfast ignored` and stays off. The patch does not touch `sweet_dsi_asteroids.c`, and it adds no DCS command and no panel state. v2's retrying planner and its sleep-out readiness gate are gone. Panel modes behave exactly as on the 2120 base. Findings 1, 2 and 3 cannot arise, because no DCS command is ever part of a speculative step. `host-check/patch_scope_check.py` enforces this. 2152 also stays off without the asteroids flip and with zero-copy scanout.
2. **Finding 4.** The kept image is a debt, `fFastKept`, from the black flip until either a flip back has latched or iOS has turned the display off. The pure function `sfs_fast_keep_next()` in `SweetFbScale.h` chooses the next step, and no step clears the debt on a failure. If any flip fails to latch, `astFallback` stops flipping and scans the boot buffer. That buffer is black, because it is seeded from the black front, or plainly black with hardware scale. So 2152 writes the kept image into it: a copy without hardware scale, a scale of the canvas with it (`fastShowInBoot`, the copy path's nearest-neighbour branch). If the black flip itself fails, the image goes back at once. The debt is retried on every vsync tick and on a 50 ms timer, 1 s once scanout is frozen.
3. **Finding 6.** While the screen is black, the tick still takes each frame, copies it into the kept buffer without flipping, and releases its CoreAnimation holds at the 2120 point. So holds live as long as they do without 2152, and the kept buffer holds the newest frame when the screen comes back. Keeping the pixels needs no CA hold.
4. **Finding 5.** The apply order is corrected above, and in `plans/input/README.md`.

Bound when flips fail: the window (2 s by default), plus a present already running on the work loop, plus 25 ms for the failed latch and 25 ms for the fallback's latch, plus one copy or scale into the boot buffer. If the fallback cannot put scanout back on the boot buffer (`ast.frozen`), the old driver has no display path for any frame. 2152 still writes the image there and keeps the debt until iOS turns the display off. Nothing is bounded in that state.

One known difference from a normal flip: if a GPU recovery lands during the copy of a frame into the kept buffer, the normal path would drop that frame. The kept buffer keeps it. The GPU gate already proved that frame's own work finished before the copy started.

Checks, all on the Mac. `host-check/fast_power_model.c` runs v2 (the audited code) against v3 on the pixel path. The pure parts are real code (v3's `sfs_fast_drain` and `sfs_fast_keep_next`). The glue is a line-by-line mirror, and the model covers:

- flip failure, `ast.stopped` and `astFallback`, with and without hardware scale
- frozen scanout
- the GPU gate and epochs
- CoreAnimation's two surfaces with hold-wait, with and without `sweetfb_keep`

| Scenario | Measure | v2 | v3 |
|---|---|---|---|
| GPU recovery during the dark step (v1 finding) | dark while iOS on | 2000 ms | 2000 ms |
| Wake press drained late (v1 finding) | dark while iOS on | 11 ms | 11 ms |
| Black flip fails, hardware scale, no valid frame | dark while iOS on | 7899 ms (never back) | 0 ms |
| Flip back fails, hardware scale, frame voided | dark while iOS on | 7899 ms | 2000 ms |
| Flip back fails, no hardware scale | dark while iOS on | 7899 ms | 2000 ms |
| Flip back fails and scanout freezes | debt dropped while unpaid | 1 | 0 (kept until iOS off) |
| `sweetfb_keep`, iOS keeps drawing during the window | longest CA hold | 1994 ms | 16 ms |
| Same without keep | longest CA hold | 33 ms | 16 ms |

The 3000-run random walk is differential. Each run is repeated with `sweetpwrfast=0`, and v3 may add one window of darkness to that baseline and nothing else: no longer leak of an old image while iOS is off, no longer hold, no dropped debt. It found no violation. Its 8221 ms worst darkness is a run that is just as dark without 2152. Flip failures are injected into 2152's own flips only.

Each fix has a mutation that takes it out again, and every one is caught:

- a failed flip drops the debt
- the canvas is not put in the boot buffer
- a stop under a dark step does not pay at once
- the tick returns while dark
- in `sfs_fast_keep_next`, a stopped flip path counts as paid
- in `sfs_fast_keep_next`, frozen scanout counts as paid

The glue mutations are in the model's mirror; the two header mutations are in real code. `fast_power_check.c` covers each rule of `sfs_fast_drain`, an exhaustive table of `sfs_fast_keep_next`, and a 2-million-step walk. Its generation and touch-cancel mutations fail as they should. `patch_scope_check.py` passes and catches its own two mutations: the panel guard removed, and a panel call added.

Not prepared:

- A GIC interrupt for touch or PON. It needs new GIC and PDC plumbing under Gunyah, and the poll fixes get most of the gain. Revisit if the 0.5 ms passes cost measurable power.
- Taking frames when the GPU finishes instead of at the next tick. `done -> tick taken` is 7 ms at p50, so this is the next big display cut, but it belongs to the lag agent's pacing work.
- A higher SPI clock as a patch. `sweettouchdiv=4` (4.8 MHz, DT allows 12 MHz) already exists and saves about 0.7 ms per read. It's the `in-div` variant.

## Biggest unknowns

1. How long iOS takes from the HID power event to `RequestPowerChange`, under light load and under load. The first owner check with 2150 answers this.
2. The touch controller's report rate, and whether its IRQ pulses for every report while a finger is still. The 2151 stats line answers both, and the 12 ms fallback covers the second.
3. Whether `sweetpanel=1` panel commands coexist with the asteroids flip and hardware scale. The `in-panel` run answers this.
4. Whether CoreAnimation at 120 Hz notifications helps or hurts at 30 frames/s.
