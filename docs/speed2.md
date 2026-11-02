# Speed after the tick fix: measured, and what comes next

2026-11-02. Host analysis only, with no phone. Every patch here is untested on hardware. Labels are **Measured** (logs show it), **Inferred** (my reading or arithmetic), **Forecast** (`tick-sim.py`) and **Missing** (no data).

Reproduce from the repo root with `python3 -I plans/speed2/tools/speed2.py`, `speed2b.py` and `tick-sim.py`. Saved output is beside them in `plans/speed2/`. Patches are in `plans/speed2/patches/`.

Two log traps affect every number below:

- `ast23-var/bgmtl.log` and its `bgl-backboardd.log` start with ast22's lines, byte for byte (107 and 1449 lines). `frame-budget.py` mixes the boots, so `speed2.py` slices them. ast24's root is fresh, so its log is one boot.
- ast24 has two windows with the display off (46 s in `t=767`, 3.8 s in `t=1008`). Rates over those windows read far too low, so I subtract that time. ast22 has no display-off line.

## Bottom line

The tick fix works and removed about half the dropped frames. Sustained animation is still 20 to 34 flips/s, and the compositor averages 37.6 frames/s over its animating seconds (`frames:`). The next limit has moved off the tick. Frames now run almost one at a time: encode, GPU wait and idle sum to the frame period, with only 4 to 9% overlap. Stable 60 is plausible on light and medium scenes. On the heaviest glass scenes it depends on an unknown I name below. Sustained 120 Hz with Liquid Glass is not supported by this data.

The largest visible stutter is not a speed issue: about 30 s of frozen display around the GPU recoveries at `t=968` to `998`, out of 497 s. No lever below touches that.

## 1. Did the tick fix do what the plan predicted?

Mostly yes, with about two thirds of the predicted flip gain. **Measured**, animating windows (at least 2 flips/s):

| Measure | ast22 (old) | ast24 (tick fix) | Plan predicted | Verdict |
|---|---:|---:|---:|---|
| Swaps/s | 18.95 | 19.5 | same | Comparable by luck |
| Flips/s | 13.22 | 16.8 | 18.9 | +27%, predicted +43% |
| Dropped of swaps | 30.2% | 14.0% | under 10% | Halved, target missed |
| Busiest 10 s, flips/s | 21.5 | 34.4 | 34.4 | Met, though the workload differs |
| Idle ticks/s | 57 | 60.3 | about 60 | Met |
| `tick_gap_us max` | none logged | 25 to 33 ms | under 20 ms | Missed |

Matching on load removes the workload difference:

| Swaps/s band | ast22 flips/s, drop | ast24 flips/s, drop | Flip gain |
|---|---:|---:|---:|
| 10 to 20 | 11.5, 18.5% | 12.1, 14.2% | +5% |
| 20 to 30 | 15.6, 37.5% | 21.4, 12.1% | +37% |
| 30 to 40 | 19.4, 42.4% | 28.4, 16.0% | +46% |

Your counts are right. The exact identity is `copy_frames 4154 + coalesced 673 = swaps 4827`, 13.9% dropped. The 4156 flips include two that came from no swap. `dropped` equals `coalesced`, so epoch drops are 0.

**Why the gain stopped short, Inferred.** Copy 12.95 ms plus latch 3.95 ms is 16.9 ms on average, already over the 16.67 ms period, so a busy tick always overruns. `handler_us max` clusters at about 16, 24.7 and 31.6 ms, which fits a long copy plus a latch wait in 8.33 ms panel steps. The other part is the gate in `gpuGateOpen`: it checks only the newest swap, so a newer unfinished swap can replace a finished one (see rank 2).

**Why `swap_to_flip_us avg` reads 42 ms.** It is a cumulative average with outliers, and it is latency, not throughput.

- The counter never resets while `max` does (`SweetFramebuffer.cpp:1545`).
- Two samples crossed display-off periods: 46.16 s and 3.79 s. Removing them gives 30.2 ms over 4152 flips.
- Per-window means from the cumulative values are 25 to 36 ms in animation, and 18.7 to 19.7 ms in the steady 10 Hz stretches.
- **Inferred** split of the 30 ms: 17 ms handler, about 8 ms average wait for the next tick, and the rest GPU gate wait.

Patch 2080 logs a true window mean.

## 2. What does `sweetfetchsync=1` cost?

Between 3.9 and 8 ms of commit-to-done per command buffer against ast22, and I cannot attribute all of that to the sync. I would keep it on.

**Measured.** 65,361 syncs over 5,188 command buffers: 12.6 per command buffer and 2.85 per pass. The busiest 10 s window had 5,982, which is 598/s.

**Measured**, least-squares fits of commit-to-done per command buffer against passes per command buffer, on windows with no hold timeout:

| Boot | Sync | Fit, ms | Windows | 2.5 passes | 5 passes | 8 passes | Median |
|---|---|---:|---:|---:|---:|---:|---:|
| ast22 | none | 4.34 + 0.82p | 41 | 6.4 | 8.4 | 10.9 | 9.8 |
| ast23 | global, every draw | 4.21 + 1.74p | 9 | 8.6 | 12.9 | 18.1 | 11.2 |
| ast24 | selective | 6.40 + 1.56p | 17 | 10.3 | 14.2 | 18.9 | 12.6 |

These limits apply:

- Selective sync costs about the same as the global flush here. The draw-level selection did not save much per pass, though ast23 has only 9 windows and never passes about 6 passes per command buffer.
- ast24 also changed the kernel, and the tick fix makes scaler threads busier. ast23 is a second boot.
- Within ast24, sync count does not predict the cost. Aligned on pipeline counts, the sync coefficient is -0.03 ms per sync (7 points, r² 0.31). One stretch with 15.7 syncs per command buffer took 9.1 ms, and one with 5.4 took 12.7 ms at the same pass count.
- So 0.46 ms per sync (5.8 ms over 12.6) is an upper bound, not a measurement. Commit-to-done also includes queueing and the real commit.

**Acceptable?** Yes for now. It clears a visible defect, and it costs at most about 6 ms at 5 passes inside a 30 ms frame period. Near 60 fps that same 6 ms would be a third of the budget, so revisit it then.

**Cheaper sync.** A framebuffer-local Vulkan barrier would not avoid the expensive part. Reading `tu_flush_for_access`, a colour-write to input-read barrier asks Turnip for a colour CCU clean, a UCHE invalidate and a stage wait, all still in sysmem. The later by-region upstream work uses subpass fences, which are not the sysmem path (`fetch-fix.md`). It would also add a new dependency path.

`chl-fetch-sync-color.patch` adds `sweetfetchsync=2`, the same length on the boot line. It keeps colour CCU clean, UCHE invalidate and wait-for-idle, and drops depth clean, CCU invalidates, blit clean, descriptor invalidate, `WAIT_MEM_WRITES` and `WAIT_FOR_ME`. The idle wait stays, so my guess is a small saving, not a large one. ast23 and ast24 only proved the full set clears the tear, so this needs a tear capture.

## 3. Frame budget now

The old 23 ms is now accounted for: it is CPU encoding plus idle time before the next frame. **Measured** from 99 `perf:` seconds at 20+ command buffers/s, per command buffer:

| Draws per cb | Seconds | cb/s | Encode, ms | GPU wait, ms | Between, ms | Sum of three, ms per s |
|---:|---:|---:|---:|---:|---:|---:|
| Under 15 | 3 | 170 | 2.7 | 3.7 | 1.8 | 1381 (queue burst) |
| 15 to 30 | 14 | 41 | 4.5 | 7.2 | 14.5 | 1087 |
| 30 to 50 | 24 | 39 | 5.6 | 10.8 | 10.4 | 1043 |
| 50 to 80 | 22 | 37 | 7.4 | 12.3 | 9.0 | 1067 |
| 80 to 120 | 30 | 35 | 9.1 | 15.8 | 5.2 | 1040 |
| 120 and up | 6 | 23 | 25.1 | 21.4 | 1.0 | 1073 |

**Measured**, fits: encode is 1.43 + 0.108 ms per draw (r² 0.30), GPU wait is 5.23 + 0.121 ms per draw (r² 0.33). So 100 draws cost about 12 ms of encoding CPU. Across all 137 animating seconds, the mean is 37.6 fps and intervals are 41% at or under 17 ms, 23% at 25, 15% at 34 and 21% at 100. Only 14 of 173 logged seconds reached 55 fps. Worst-case stalls per second: `max wait` p90 84 ms, `max encode` p90 77 ms.

**Measured**, frame cost on clean windows, per command buffer (4,069 command buffers, 4.5 passes): commit to done 13.5, texture create 2.16 (61% pool hit), texture free 2.77, BO_SYNC 0.25, zeroing 0.10, surface copies 0.10, completion 0.11. The hit rate is lower than ast22's 68 to 80%.

**Inferred, and the most important line here.** The three columns sum to about 1040 to 1090 ms per second, so frames barely overlap. A pipelined renderer would overlap encode with the previous frame's GPU time and sum far above 1000. At 80 to 120 draws, 29 ms period is about 9 + 16 + 5 ms in series. If that stays serial, GPU and encode work alone cap this tier near 40 fps. What makes it serial is **Missing**. `kern.sweet_fb_trace` has `wcall` and `wret`, which show whether it is `SwapWait`.

**A logging bug.** The `submit` column is always near 0 because `t_flushed` is read before `chl_cmdbuf_commit` (`bgmtl.m:2044`). The real commit, including `vkQueueSubmit` and the kernel, is hidden in "gpu wait". `chl-perf-commit-time.patch` adds a true `commit:` line.

**BO growth. Measured, for the other agent.**

- `submissions now name` rises from 1040 at `t=767` to 5105 at `t=1099`, about 0.97 BOs per swap and 12 per second of session.
- No growing cost shows up to 5105 BOs. Adding BO count to the commit-to-done fit gives +0.22 ms per 1000 BOs and leaves r² at 0.68.
- GPU wait per command buffer by BO range: 13.1 (1000 to 2000), 11.2 (2000 to 3000), 7.9 (3000 to 4000), 11.1 (4000 to 5105). Encode falls over the same range.
- Limits: BO counts are interpolated between high-water lines, and first-boot warm-up easing off could mask a real rise. There is also no direct commit timing yet.
- The kernel cost is still structural: a 5105-entry copy-in, an O(BOs × cmds) `is_cmd` loop, and a full write-combine sweep every 32nd submission, all under the GPU lock (`SweetAdreno.cpp:1050`). Patch 2080 logs `submit_us`, `lock_wait`, `sweep_avg`, `sweep_max` and `bos_avg`.
- **Inferred:** at this rate `GEM_MAX` 16384 arrives after about 11,600 more swaps. That is a harder limit than the time cost.

## 4. The icon delay

Almost all of it is first-boot. **Measured:**

| | ast22, first boot | ast24, first boot | ast23, later boot |
|---|---:|---:|---:|
| backboardd pipelines | 174 in 52.9 s | 264 in 43.5 s | 192 in 4.15 s |
| Largest single pipeline | 43.6 s | 32.7 s | 0.52 s |
| backboardd exec to first flip | about 51 s | about 40 s | under 10 s |
| iconservicesagent surfaces created | 41 | 25 | 2 |

The stats lines are 10 s apart, so those times are 10 s coarse. In ast24 the icon surfaces begin at `t=702`, 20 s after the first flip. The first 32 pipelines take 36.3 s before the first command buffer. After that, first-boot compilation is about 7 s spread across the session. Animation is only slightly worse early on: 34.8 fps in the first 30 logged seconds, 38.2 after.

The little cluster sits at 93 to 100% in all three boots, with over 1000 keystore calls each from `searchd`, `modelcatalogd` and `coreduetd` in ast24. So background load does not vanish after first boot, at least in ast23's short session.

**Missing:** iconservicesagent's own pipeline times. `bgmtl.log` only has backboardd, and that process has its own per-user cache.

**Would a persistent cache help?** One already exists and works: the ir3 disk cache in `/private/var/tmp/chlorophyll-cache/<uid>` is why 43.5 s becomes 4.15 s. Two things remain:

1. **Seed it for fresh roots, with no code.** Copy that directory from a session into root assembly. The key includes the library's identity, so reseed after any `libchlorophyll` rebuild. This removes the 30+ s blank on first boot. **Inferred**, not tested.
2. **The AIR cache is not on the current path.** `plan-speed.md` rank 7 is right that it is missing, though the code exists. `air_metal.c` has one, but `chl_vk.c:1771` calls `air_to_nir` directly, and `chlorophyll-air` appears 0 times in `libchlorophyll.dylib`. That leaves `VfxXgh+A2Xghfc` at 150 to 520 ms per variant in ast23, with a cached binary. The upper bound is 4.15 s per boot, about 2.7 s of that on this one shader. It removes first-use hitches, and sustained fps stays the same.

`bgmtl.m:559` says `dirs_cleaner` empties tmp at boot, yet ast23 shows the cache surviving. If a later boot shows 40 s compiles again, move the directory to `/private/var/db/bgmtl`.

## 5. Ranking for a stable 60

Changed from `plan-speed.md`: the show-finished-frame patch and scheduling move up, zero-copy moves down, and the AIR cache leaves the fps list. Gains are for animating scenes. **Forecast** numbers come from `tick-sim.py`, which matches ast24's latency (33 ms against 30) but overstates drops (40% against 16%), so read them as direction only.

| Rank | Lever | Expected gain | Evidence | Cost, risk |
|---:|---|---|---|---|
| 1 | Hardware scale, `sweetfb_ast_hw=1` | Swap-to-flip 30 to about 21 ms. Handler 17 to about 8.5 ms, so ticks stop overrunning. Frees about 1.3 cores of scaler threads at 34 flips/s | **Measured** copy 12.95 ms on 3 threads. Copy time under hardware scaling is an estimate of 3 to 6 ms | One boot, no build. Blank screen recovers by RAM boot |
| 2 | Show the newest finished frame, patch 2082, `sweetfb_keep=1` | Drops from 14% toward 5%, and more flips at high load. **Forecast** at steady 60 and 13 ms GPU wait: 31 flips/s without it, 53 with | **Measured** 3 to 16 hold ticks/s while animating. The share of drops it fixes is **Missing**, and 2080's `drop_held` gives it | Kernel build. Holds one more surface, so watch texture misses |
| 3 | Find what makes frames serial | If removable, the 80 to 120 draw tier goes from about 35 fps toward 60 | **Inferred** from the table in section 3. Could be nothing fixable | One trace read |
| 4 | Scheduling, `sweetamp_prio=20` | Encode up to 2× faster if it is on little cores. Unknown share | **Measured** 0.108 ms per draw is very slow, and the little cluster is at 93 to 100%. Thread placement is **Missing** | One boot, low risk |
| 5 | GPU clock, `sweeta810level=8` | At most 26% of the GPU share of a 9 to 21 ms wait, so 2 to 4 ms | 763 to 960 MHz. GPU busy is still **Missing**. DDR votes have no local data | One boot. Stop on more recoveries or heat |
| 6 | Textures and pool | Up to about 3 ms of the 4.9 ms create and free, if hit rate returns to 80%+ | **Measured** 61% hit | About a day |
| 7 | Zero-copy flip | About 4 ms more swap-to-flip over hardware scale | **Forecast** 24 to 19 ms. Removes the copy that hardware scale has already shrunk | 1 to 2 days, high flicker risk |
| 8 | Cheaper sync, `sweetfetchsync=2` | Likely under 2 ms, a guess | Section 2 | Root build, tear capture |
| Not fps | AIR cache and seeding | First boot and first-use hitches only | Section 4 | Seed: no code |

## 6. Is 120 Hz realistic?

No for sustained Liquid Glass. That is my plain answer from this data.

**What it needs:** every stage under 8.33 ms, assuming the stages overlap, which they do not yet.

| Stage | Needed | Now, typical glass scene | Best measured |
|---|---:|---:|---:|
| Display handler | Under 8.3 ms | 16.9 ms | About 8.5 ms with hardware scale, estimated |
| Encode | Under 8 ms | 9 to 25 ms | 2.7 ms under 15 draws |
| GPU wait | Under 8 ms | 11 to 21 ms | 3.7 ms under 15 draws |
| Frame overlap | Pipelined | Serial | None seen |

**What must change:**

1. The display needs zero-copy plus a non-blocking latch, since the latch alone averages 4 ms.
2. Frames must overlap.
3. Encoding needs to get about 2× faster.
4. GPU work on glass passes needs about 2× throughput.
5. `fVsyncUS` and CoreAnimation's pacing must follow, and neither has been tried above 60.

**What is physically unlikely:** item 4. The clock offers at most +26%, the canvas cannot drop, passes cannot be skipped, and blur at 840x1860 needs real bandwidth. Forced sysmem might be improved by autotune, but GMEM boots had 36 to 49 recoveries (`gpu-hangs.md`).

**What might work:** 90 to 120 fps on very light scenes. Seconds under 15 draws per command buffer spent 6.4 ms in encode plus GPU wait, and one burst reached about 170 command buffers/s. That would need the display path fixed first, and no data shows a real scene holding it. Peak so far is 34.4 flips/s over 10 s.

**Is a 120 Hz tick with slower content useful?** A little, and it is cheap. A tick every 8.33 ms halves the average wait for the next tick from about 8 to about 4 ms. **Forecast:** swap-to-flip 33 to 31 ms today, 24 to 22 ms with hardware scale. That is the most it can do for touch latency, since touch still waits on CoreAnimation at 60 Hz. It does not raise throughput on its own, and pacing stays uneven because the tick is not locked to the panel. It adds 60 more timer wakes per second, so measure idle power. Try it after hardware scale.

## Next steps, in order

1. Boot `next-all` again on the same ast24 root (15 minutes, no build). This gives the first real later-boot baseline for `perf:`, icons and BO growth.
2. Apply `boot-speed2-images.patch` and run `boot/build.sh`, then boot `next-hw` (15 minutes). Check for `hw scale programmed`, no `fallback` line, `copy_us avg` under 7000, and a matching picture. Remove the arg if the screen is wrong.
3. Boot `next-amp` (15 minutes). Compare encode per draw against the 0.108 ms line.
4. During an animation, read `kern.sweet_fb_trace` for `wcall` to `wret` (10 minutes). This addresses rank 3.
5. Apply kernel patches 2080, 2081 and 2082 in order and build, and add `chl-perf-commit-time.patch` to the root (your build time plus 1 boot). Read `drop_done`, `drop_held`, `win_avg`, `submit_us` and `commit:`.
6. If `drop_held` is the larger share, boot with `sweetfb_keep=1`. Verify flips rise and `pool busy misses` do not. Then try `sweetfb_hz=120`.
7. Boot `next-gpu8`, then apply `chl-fetch-sync-color.patch` and boot `next-fs2` with a tear capture.
8. Seed the shader cache in the final root, after the last `libchlorophyll` rebuild.

## Boot line

The field holds 607 usable bytes and `next-all` uses 589, which leaves 18 free.

- `sweetfb_ast_hw=1`, `sweetamp_prio=20` and `sweeta810level=8` each bring it to 606.
- Patch 2081 turns the tick fix on by default, so `sweetfb_tickfix=1` can be dropped, which frees 18 bytes. After that kernel, `next-notick` must pass `sweetfb_tickfix=0`, or it will silently have the fix on.
- `sweetfb_hz=120` costs 1 byte, `sweetfb_keep=1` costs 15, and `sweetfetchsync=2` costs none.
- Dropping `sweetpanicfb=10`, as `next-flush` does, frees 17 more for a combined boot.

## Prepared patches

All are unapplied. Hashes are in `plans/speed2/SHA256.txt`. Kernel patches apply in number order in `ios-asteroids/research/xnu-local/dxb/xnu`. I used 2080 and up to stay clear of the BO agent. Renumber if needed.

| Patch | Does | Checked | Not checked |
|---|---|---|---|
| `2080-asteroids-frame-timing-v2.patch` | Window mean swap-to-flip, drops split into done and held, submit cost against BO count | `git apply --check` on the tree, `-fsyntax-only` with the build's flags | Kernel build, phone |
| `2081-asteroids-tick-120.patch` | `sweetfb_hz=120` ticks at 8.33 ms and tells CoreAnimation every second tick. `sweetfb_ca120=1` tells it every tick. Tick fix on by default | Same, in series | Same |
| `2082-asteroids-show-finished-frame.patch` | `sweetfb_keep=1`: while the newest swap waits for the GPU, show the one it replaced if finished. Frames stay in order | Same, in series | Same, no host test of the logic |
| `chl-perf-commit-time.patch` | True commit time in `bgmtl.log` | `git apply --check` in `next-fix`, `bgmtl.m` syntax check for iOS | Phone |
| `chl-fetch-sync-color.patch` | `sweetfetchsync=2` | Apply check, both files compile and link with the build's flags, `fetch-check.py` passes | Phone, tear |
| `chl-air-cache.patch` | AIR cache on the chl path, on when `/private/var/db/bgmtl/chl.aircache` exists, plus an `air front end:` time line always | Apply check, compiles and links with the build's flags | Phone, whether cached NIR renders the same |
| `boot-speed2-images.patch` | `next-hw`, `next-amp`, `next-gpu8`, `next-fs2` in `next-fix/boot/build.sh` | Apply check, lengths computed | Run |

The two patches that touch `chl_vk.c` apply together, tested with `chl-fetch-sync-color` first. Any `libchlorophyll` patch changes the library's identity, which invalidates the shader cache once.

Compile checks ran in a scratch directory, with no output in the build trees, and I trashed the scratch afterward. `next-fix`, its kernel and `SweetAdreno.cpp` changed at 11:18 during this work, from someone else's build. The apply checks above were rerun after that change, but the compile and syntax checks ran on the earlier sources.

## Biggest unknowns

1. What makes frames run in series. This decides whether the heavy tier can reach 60.
2. Real GPU busy time. `chl-gpu-timing.patch` from `plans/speed` adds it, and without it ranks 4 and 5 stay estimates.
3. Whether VIG0's scaler accepts writes under Gunyah. Rank 1 fails if not.
4. Whether ast24's later boots behave like its first. Step 1 answers that.

# Phase 2: what limits the frame rate now

2026-11-02, later the same day. Host analysis of ast34 to ast49, no phone. All patches are unapplied and untested on hardware. Labels as above. Reproduce with `python3 -I plans/speed2/tools/speed3.py RUN...` and `draw-trace.py`. `bgmtl.log` is appended across boots on one root, so `speed3.py` uses only the last boot; the first rows of every run's raw log are the same older boot. Only `ast49-fix1` of the hang batch existed when I wrote this.

## Corrections to my first report

- **Rank 2, show the newest finished frame, was wrong.** 94% of drops had a finished frame (`drop_done` 249, `drop_held` 15 in ast34, and 1316 against 15 in ast47). `sweetfb_keep=1` showed 213 kept frames with no gain: 27.9 fps against 24.6 to 28.6 for the other runs.
- **Hardware scale gives latency, no frame rate.** ast39: copy 1.35 ms, handler max 8.2 ms, swap to flip 16.5 ms, and 25.2 fps. The display is no longer the limit.
- **`sweetamp_prio=20` and the 120 Hz tick gave nothing measurable:** 27.5 and 28.6 fps against the same range.
- **0.108 ms per draw was too high.** Later runs fit 0.005 to 0.075 ms per draw for encode, with an intercept of 4 to 13 ms per command buffer. Most encode cost does not scale with draws.
- **The kernel submit is not a limit, even at about 1000 BOs:** 0.13 to 1.5 ms average, 50 to 90 us lock wait, a sweep of 1 to 3.5 ms every 32nd submission. Single outliers reach 60 to 118 ms.

## 1. The steady 10: nothing is capping

No wrong value forces 10, so there is no single value to change. I prepared no refresh-rate change. **Measured:**

- The rate is not a target rate. One-second `swaps_s` samples take many values up to 59.49 (ast24) and 54.96 (ast37), with no clustering on 60/n. `sweetfb_hz=120` changed nothing, idle stayed at 10.0.
- Inside an app it stops: `apps@194` reads 0.4 swaps/s in ast34, ast37 and ast39. It comes back on returning home (`settle`, and `app-sw@275`).
- Those frames are trivial: 9.7 command buffers/s, 37 draws, 2.5 ms encode, 4 ms GPU wait each. The compositor is idle 94% of the time at 10 Hz.
- The per-step numbers are diluted by the script's `sleep`s. Control center is a 300 ms swipe inside 5 s, so 13 to 22 says little. An animating second runs at about 26 to 35 fps (`frames:` lines).

**Inferred:** the requester is the home screen's Clock icon second hand, which is on page 1 in `frames/01-idle.png`. I could not confirm it in the binary: the local shared cache has no `.symbols` file for `ipsw`, and a raw search for the class name found nothing. `ui-idle-page2.patch` adds a step that sits on page 2 for 15 s. 0 frames/s there confirms it. Its cost is power, and no frame rate depends on it.

## 2. What serialises frames

**The likely cause, from reading:** `bgmtl.m:1980`. `-[BGMTLCommandBuffer waitUntilScheduled]` in backboardd waits on `_done`, the group left at completion. Metal only promises scheduling there. If CoreAnimation calls it before each swap, each frame waits for its GPU work before anything else starts.

**Measured support:**

- Encode, GPU wait and idle add to 965 to 1040 ms per second in the 50+ draw tiers of ast47, ast48 and ast49.
- Swaps arrive with the GPU done. With hardware scale, swap to flip is 16.5 ms, of which the handler is about 5.4 and the wait for a tick about 8.3. That leaves about 3 ms for the fence gate, while GPU wait per command buffer is 6 to 19 ms.
- Only 1 to 10% of drops were still held by the GPU.

**Ruled out, Measured:** kernel `SUBMIT` (above), `BO_SYNC` (0.25 ms), and hold-wait as the main cause, since frames were serial before it existed. `chl_cmdbuf_commit` holds `queue_lock` across `vkQueueSubmit`, but the submit is the 0.13 to 1.5 ms above. Its true time is still **Missing** because `chl-perf-commit-time.patch` is not in those roots.

**Not proven:** that CoreAnimation calls it. Eleven boots of sampler stacks show no thread there, but they sample at 2 fixed, mostly idle moments. The patch answers this in one run. Its line counts calls and milliseconds either way. `scheduled calls=0` kills this theory, and the next suspect is CoreAnimation's own wait on a completed handler.

**Patch:** `chl-sched-nowait.patch`. With `/usr/local/lib/bgmtl/holdwait` holding `1a`, the wait is skipped except for command buffers that publish at completion (write-backs, captures, linear surfaces). The kernel's fence gate keeps an unfinished frame off the panel and the hold gate keeps CoreAnimation off a held surface. A file push of an existing path turns it on and off, with no reboot arg.

**Expected, Inferred.** At 80 to 120 draws (ast47), period is 27 ms: encode 6.9, GPU 12.9, idle 8.5. Overlapped, the slowest stage is about 15 ms, so 45 to 60 fps at best. At 120+ draws GPU wait is 19 to 27 ms, so 35 to 50 fps at best, and 60 is not supported there. GPU wait may include queueing, so GPU busy may be lower. It remains **Missing**.

**Hold-wait, measured:**

| Run | Commits busy | Mean wait when busy | Per commit | Max | Timeouts | fps |
|---|---:|---:|---:|---:|---:|---:|
| ast48 | 29% | 6.39 ms | 1.83 ms | 15.2 ms | 0 | 35.3 (ast47 without it: 33.5) |
| ast49 | 18% | 6.50 ms | 1.15 ms | 14.8 ms | 0 | 26.0 (scripted runs: 24.6 to 28.6) |

No measurable frame rate loss today. It does add to serialisation, and it will matter more once frames overlap, because `commit` blocks the render thread until the kernel lets go. `2120-asteroids-hold-release-after-copy.patch` releases the hold when the copy ends, which removes the latch wait (about 4 ms on average) from it. Hardware scale shortens the copy from 13 to 1.4 ms but not the wait for a tick.

**Risks when frames overlap:** more work in flight means more `pool busy misses`. Watch for black artifacts, tear, recoveries and misses.

Hangs: `ast49-fix1` has 0 recoveries against 6 to 10 before, with frame rate unchanged at 26.0. Hangs cost stutter but did not set the rate.

## 3. Encode

**Measured** from 119 capture traces, 5,157 draws: 14.7 draws per pass, 0.76 pipeline changes per draw, 1.87 buffer binds per draw, 0.69 texture binds per draw, and no redundant binds. Between draws, 34% change only offsets and 66% change a buffer, texture or sampler. No draw changes nothing.

Hot spots, from reading:

1. `flush_bindings` rebuilds and pushes every descriptor and push constant on every draw, even when only an offset moved. Offsets move the descriptor base too, so the push is always new. `vkCmdPushDescriptorSetKHR` also makes Turnip re-emit descriptor state, which adds GPU work.
2. `vkCmdBindVertexBuffers` runs per binding per draw, and `vkCmdSetPrimitiveRestartEnable` per draw.
3. Texture create and free cost 1.5 and 1.9 ms per command buffer in ast47 at a 72% hit rate. That is more than the draw slope in most runs, and it is not patched yet.

`pushed_layout`, `pushed` and `pushed_pc` in `struct chl_cmdbuf` are left over from an earlier delta push. `flush_bindings` no longer uses them, and `tear_probe` still resets `pushed_layout`. A push makes a whole new set in Turnip, so a partial push is unsafe, and I left it out.

`chl-bind-cache.patch` skips a push only when layout and full contents match the last one, moves the descriptor base only every 32 KB (1 MB for storage) with the rest in the push word, and caches vertex binds and primitive restart. It logs `[chl] bind cache:`. **Expected, a guess:** 1 to 2 ms per command buffer, plus some GPU time. The 32 KB base assumes no constant block is over 32 KB. A wrong picture after this patch points there first.

## 4. GPU feature workarounds

Hangs and tear now have other explanations, so these can be retried. **Measured:** all 657 pipeline descriptors in ast47's captures have no depth or stencil format, so `nolrz` has nothing to win. 92% of colour area in traced passes loads existing contents (Metal load action 1), and half of the 174 passes are over 1 megapixel. GMEM would restore and store those, and passes with fetch sync are forced to sysmem anyway, so I expect little from dropping `sysmem`. `sweetir3=fp16` has no usable evidence: ast19's and ast20's logs are the same appended file. It turns the shader cache off, so its boots compile again.

`plans/speed2/build-variants2.sh` builds these, all within 607 bytes:

| Rank | Image | Added args | Line | Expected | Stop on |
|---:|---|---|---:|---|---|
| 1 | `v-gpu8` | `sweeta810level=8` | 555 | At most 26% of GPU wait, about 3 ms per command buffer | More recoveries than `v-base`, or `SWEET THERMAL` trips |
| 2 | `v-fp16` | `sweetir3=fp16` | 552 | Unknown, the only change that could cut shader time much | Tear or wrong colours in `frames/`, recoveries |
| 3 | `v-auto` | `sweettu=noubwc,nocb,nolrz` | 564 | Small, may be negative | Any recovery above `v-base`, colour flips |
| 4 | `v-ubwc` | `sweettu=sysmem,nocb,nolrz` | 564 | Likely negative: the pool turns off without `noubwc` (`bgmtl.m:492`), so textures cost more | Colour errors, BO growth |
| Base | `v-base`, `v-hw`, `v-hw-gpu8` | none, `sweetfb_ast_hw=1`, both | 538, 555, 572 | Reference; latency only | |

Not worth a run: `nolrz` off (no depth), `nocb` off (the launcher turns fetch sync off without `nocb`, so the tear returns), `sweetamp_prio=20` and `sweetfb_keep=1` (already measured), `sweetfetchsync=0` (tear). Use `hang-sync2` for `sweetfetchsync=2`.

Judge each by `speed3.py`: `frames:` fps, GPU wait per command buffer by draw tier, and recoveries. Step means are too diluted. Run-to-run noise so far is 24.6 to 28.6 fps on the same script, so a gain under about 3 fps needs repeating.

## Honest ceiling

Today the animating rate is 26 to 35 fps. Stable 60 is not supported by this data. If `waitUntilScheduled` is the serialiser, my estimate is 45 to 60 on mid scenes and 35 to 50 on the heaviest glass. Without GPU busy numbers I cannot say more. If `scheduled calls=0`, none of that gain exists yet.

## Files for this phase

- `plans/speed2/patches/chl-sched-nowait.patch`, with `plans/speed2/holdwait-1a`: apply check, `bgmtl.m` syntax check for iOS.
- `plans/speed2/patches/chl-bind-cache.patch`: apply check, compile and link with the build's flags.
- `plans/speed2/patches/2120-asteroids-hold-release-after-copy.patch`: apply check, `-fsyntax-only` with the build's flags, no build.
- `plans/speed2/patches/ui-idle-page2.patch`: applies to a copy of `ui.sh`.
- `plans/speed2/build-variants2.sh`: `--lengths` run, image build not run.
- `plans/speed2/tools/speed3.py`, `draw-trace.py`, with output in `speed3-out.txt` and `draw-trace-out.txt`.
