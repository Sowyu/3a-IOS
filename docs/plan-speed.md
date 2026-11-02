# Plan: lag and frame rate

2026-11-02. Host analysis only. Nothing here ran on a phone, and every prepared fix is **untested**. Liquid Glass and blur stay fully on: no step reduces, skips or lowers the resolution of any visual effect. Labels are **Proven** (the code or logs show it), **Inferred** (my reading or arithmetic) and **Missing** (no data exists).

Files are in `plans/speed/`. Reproduce the numbers with `python3 -I plans/speed/tools/frame-budget.py ios-asteroids/logs/ast22-linear.txt ios-asteroids/logs/ast22-var/bgmtl.log`. Log lines below refer to `ios-asteroids/logs/`. Kernel lines refer to `research/xnu-local/dxb/xnu/iokit/Kernel/arm/SweetFramebuffer.cpp`.

## What I found

**The display tick throws away about a third of the frames the GPU finished.** `vsyncFired` runs the copy and the flip inside the tick, then arms the next tick a full period later (`:1265`). So each flip costs two periods' worth of time.

- A least-squares fit over the 10 s windows gives 17.6 ms per tick plus 16.95 ms per flip in ast22, with a mean error of 2.4% over 63 windows. ast21 gives 17.7 + 17.5 and ast19 gives 17.7 + 18.6.
- The per-flip figure matches the logged copy (12.7 ms) plus latch (4.06 ms). **Proven.**
- The resulting ceiling is about 29 flips/s, whatever the GPU does.
- Swaps do exceed it: ast19's busiest window had 57.5 swaps/s and 26.2 flips/s, and ast22's had 34.4 and 21.5.
- Swaps never shown: 3126 of 10365 in ast22 (30%), 1483 of 3654 in ast19 (41%), 1266 of 2947 in ast21 (43%).

Patch 2051 fixes this with about 30 lines. A CPU scaler at 12.7 ms costs far less than the 17 ms period suggests once it stops pushing the schedule back.

**"11 to 12 fps" is a session average, and it hides two different states.** 7237 flips in 637 s is 11.4/s, which includes idle windows and boot.

- Animating windows run 15 to 21 flips/s.
- A steady 10.0 swaps/s appears in long stretches (up_s 1179 to 1189, 1331 to 1381) with 1 to 2.5 passes per command buffer, 3 ms commit to done, no coalescing and no holds. That rate is what something asks for, not what the hardware can do. **Proven** that it is not the 100 ms GPU hold, since timeouts are 0 and holds are 0 to 2 per 10 s. What drives it is **Missing**. The same 10 Hz appears in ast8 (`analysis/texture-pool.md`).

Judge fixes by flips/s during a fixed animation, never by a session mean.

**The render side runs well under its deadline on the GPU, but most of the frame is unmeasured.** See the budget below.

## Frame budget

Per command buffer, ast22, windows of at least 50 command buffers, split by passes per command buffer. There are 1.09 command buffers per swap (11304 over 10365). Do not add these into a pie chart: commit to done overlaps the others, and `freed in` may run on another thread.

| Measure | Heavy, 14+ passes | Medium, 5 to 14 | Light, under 5 | Recovery spikes |
|---|---:|---:|---:|---:|
| Windows / command buffers | 9 / 1818 | 21 / 5140 | 18 / 3469 | 3 / 363 |
| Command buffers/s | 20.2 | 24.5 | 19.3 | 12.1 |
| Period, ms | 49.5 | 40.8 | 51.8 | 82.6 |
| Passes per command buffer | 18.7 | 8.6 | 2.3 | 10.1 |
| Commit to done, ms | 18.9 | 12.1 | 8.9 | 60.9 |
| Texture create, ms (hit rate) | 2.71 (80%) | 2.31 (73%) | 0.63 (68%) | 45.4 (65%) |
| Texture free, ms | 4.19 | 1.47 | 0.37 | 2.99 |
| Pool zeroing, ms | 0.43 | 0.16 | 0.03 | 0.29 |
| BO_SYNC, ms (calls) | 0.30 (1.6) | 0.26 (1.1) | 0.20 (1.1) | 0.29 (1.0) |
| Surface copies, ms | 0.09 | 0.09 | 0.00 | 0.05 |
| Completion handlers, ms | 0.09 | 0.12 | 0.06 | 0.05 |
| Holds per command buffer | 0.44 | 0.21 | 0.14 | 2.80 |

Display, separately on the tick thread: copy 12.7 ms average (20 to 28 ms max), latch 4.06 ms average (8.6 ms max, half of the panel's 8.33 ms frame plus the flush), hold ticks 6.2/s while active.

**Inferred from this table:**

- In the heavy tier, 49.5 ms minus 18.9, 2.7, 4.2 and 0.8 leaves about 23 ms not accounted for. That is encoding, CoreAnimation's own work, thread wake-ups, and any wait for a display-held surface or for the next frame's start.
- Even in the heavy tier, GPU work plus queueing could fit 50 fps if it were serial and nothing else ran. So the GPU is probably not the first limit on what reaches the panel, though it becomes one near 50 fps.
- The three spikes are 5.4 to 5.6 s of texture creation each, which matches the 5 s `WAIT_PATIENCE_MS`, and there are exactly 3 `RECOVERY` lines (`ast22-linear.txt:16278,18547,24147`). Allocation blocks for a whole recovery. That is 15 s of 637 s, plus two backboardd exits (`bgmtl.log:32,69`). It belongs to the stability plan, and no speed work removes it.

**Shader stalls, Proven from `bgl-backboardd.log`:** 742 pipelines cost 71 s. By launch: 52.9 s (the first `VfxXgh+A2Xghfc` alone took 43.6 s), then 12.9 s, then 5.4 s. Even on the third launch, 8 pipelines take over 200 ms, including `VfxXgh+A2Xghfc` at 340 to 420 ms each. The disk cache helps the first compile but not those. **Inferred:** the AIR-to-NIR front end runs before any cache, so each costs roughly 24 frames at 60 fps. The log has no timestamps, so when they land is **Missing**.

**Missing data:**

- GPU busy time per frame (`chl-gpu-timing.patch` adds it).
- Encode and submit time. `bg_perf_note` already logs these if `/private/var/tmp/bgmtl.perf` exists, but no log has a single `perf:` line.
- Swap-to-flip latency, tick gaps and the split of the copy (2052 adds the first two).
- Thread placement and the thread that runs texture frees.
- Measured DDR state.
- Who asks for the 10 Hz.
- Any matched comparison with UniXNU. Their 70 fps is iPadOS 17 without Liquid Glass, so I would not treat it as a target for 14 to 25 passes.

## Levers, ranked by gain per effort

Gains are for animating windows and are **Inferred** unless marked. Effort is my agent time, excluding CI and boots.

| Rank | Lever | Expected gain | Effort | Risk |
|---:|---|---|---|---|
| 1 | Tick on a deadline, patch 2051 | Mean animating flips 13.2 to about 18.9/s (+43%), busiest 21.5 to 34.4/s, by replaying logged swaps. Ceiling 29 to about 56/s (host simulation) | Done | Low. The 3 scaler threads get busier and may slow encoding |
| 2 | Timing data: `bgmtl.perf`, 2052, GPU timestamps | None itself. Decides ranks 4 to 10 | Done | Low. Timestamps add GPU commands, so they are off by default and never used for a speed run |
| 3 | Hardware scale, patch 2053 | Copy 12.7 ms to perhaps 3 to 6 ms, freeing about 2 big cores' worth at 30+ flips/s. Output unchanged in kind (bilinear) | Done, plus debugging | Medium. Bad registers mean a blank or garbled screen until reboot |
| 4 | Zero-copy flip from the splash region | Copy 12.7 ms to 0. UniXNU saw 68% of frames within 17 ms against 23% with copying, on their workload. Also removes the source read | 1 to 2 days | High. Ownership bugs flicker (UniXNU T42 to T50) |
| 5 | Find the 10 Hz requester | Power and heat, cleaner metrics. No gain under load | 1 hour with sampler data | Low |
| 6 | Non-blocking latch | Saves 4 ms from the tick. Matters only if the tick stays above 16.7 ms after 2051 and 2053 | 2 hours | Medium. Reusing a slot before the latch tears |
| 7 | AIR front-end cache | Removes most of the 340 to 420 ms stalls, and 5 to 13 s per launch in total | 1 day | Medium. A stale cache key gives wrong shaders |
| 8 | GMEM or autotune instead of forced sysmem | Unknown, possibly large on small offscreens. Existing switch: `sweettu=noubwc,nocb,nolrz` | 10 minutes, one boot | High. GMEM boots had 36 to 49 recoveries (`analysis/gpu-hangs.md`), and ast22 sysmem had 3, though these are unmatched |
| 9 | GPU clock and DDR vote | At most 26% on GPU-bound work from 763 to 960 MHz. HakiOS measured 24 to 28% fps from DDR votes, UniXNU T65 felt none | Clock: boot-arg. DDR: 2 hours | Medium. Level 8 had more recoveries upstream, and more heat |
| 10 | BO pool misses and frees | Up to about 5 ms per heavy command buffer (create 2.7, free 4.2, at most). NativeAdreno `014bd04` reserved a pool for a similar cost | 1 day | Medium |
| 11 | Backboardd scheduling | 2x on any render thread stuck on an A520, unknown share. `sweetamp_prio=20` exists | 1 boot | Low |
| 12 | 120 Hz and `CA_HEARTBEAT_RATE` | None until flips pass 60/s | Later | Low |

**No gain, so I prepared nothing:**

- **BO_SYNC policy.** It costs 0.20 to 0.30 ms per command buffer, about 1% of a frame. The per-command-buffer sync exists because of ast16c's stale icons, and UniXNU's T82c shows how badly this area can fail. Add it if GPU timing ever shows lock contention here.
- **Luma compute kernels.** `BGMTL_SKIP_COMPUTE` removes work, so running them can only cost time. Their value is correctness, tracked outside this plan.
- **Other `CA_*` variables.** Prior art found no effect on frame steps, and ast19 shows CoreAnimation already swaps faster than the tick (57.5 swaps/s against 32.4 ticks/s). So it does not pace 1:1 from our vsync notification. **Proven** for that window.

**Rejected outright:** a lower canvas, Reduce Transparency, skipped passes, and releasing frames before their fence. The first three cut visual quality, and the last brings tearing back. Keep the GPU gate exactly as it is.

## Phases

### Phase 0: baseline with today's kernel (no build, 15 minutes per boot)

1. Add an empty `/private/var/tmp/bgmtl.perf` to the root. Check first that nothing clears `/private/var/tmp` before backboardd starts.
2. Define one fixed script and use it for every later boot: 30 s idle, 10 home swipes, 5 app opens and closes, Control Center 5 times, Spotlight 5 times, 20 s switcher.
3. Boot, run the script, pull `/var` and the kernel log.
4. Add one boot with `sweetfb_threads=4` to see how the copy scales. Skip it if time is short.

Depends on nothing. Risk is low. **Verify:** `perf:` and `frames:` lines appear, and `frame-budget.py` reproduces a roughly 29 flips/s ceiling. Note that the logger's `<=25` histogram bin is wrong until `chl-gpu-timing.patch` is applied, because it compares 200 ms.

### Phase 1: release the tick (1 hour plus CI, 2 boots)

1. Apply 2051 and 2052 to the kernel tree, then build through CI. These patches have never been compiled in a kernel.
2. Boot once without `sweetfb_tickfix` and once with `sweetfb_tickfix=1`, same root.

Depends on the kernel owner picking numbers: other sub-agents may also be using 2051 and up, so renumber if needed. Risk is low. A bad result means reverting the boot-arg.

**Verify:**

- `stats hz` ticks (`flips + repeated`) rise from 57/s to about 60/s when idle.
- `coalesced` falls from 30% of swaps to under 10%.
- Active flips/s rise toward swaps/s.
- `tick_gap_us max` stays under about 20000 outside recoveries.

**Then decide:** if swaps/s fall, the scaler threads are starving encoding, so try `sweetfb_threads=2` and move straight to Phase 3.

### Phase 2: fill in the budget (1 hour plus 1 root build, 2 boots)

1. Apply `chl-gpu-timing.patch` to a copy of fetch-fix, rebuild, and add `/private/var/tmp/bgmtl.gputs` for one diagnostic boot.
2. Compute: GPU busy per command buffer, `commit to done` minus busy (queue, kernel and completion latency), and GPU idle per second.
3. Read `kern.sweet_fb_trace` during an animation for per-swap swap, scan and latch times.

Risk: timestamps add commands, and hangs here are not understood. Watch the `RECOVERY` count against Phase 1. **Verify:** busy is below commit to done in every window, and `slowest` is plausible (a value near zero means the A810 clock source is not usable).

**Then decide:** if GPU idle exceeds 25% during animation, the CPU side is the limit, so do ranks 10 and 11 before 8 and 9. If busy exceeds 15 ms per frame, do ranks 8 and 9 first.

### Phase 3: hardware scale (half a day plus CI, 3 to 6 boots)

1. Apply 2053 after Phase 1 and boot with `sweetfb_ast_hw=1`.
2. Compare a photo of fine text and glass edges with the CPU scaler.
3. Read the logged scaler version. The Linux code branches only on 0x1002 and below 0x2004, and bilinear loads no LUT, so v3.3 should take the same register set. **Inferred:** the catalog names `dpu_vig_sblk_qseed3_3_3` for VIG0 but I could not fetch its block definition.

Risks:

- This is the first write to these registers under Gunyah on this chip.
- `kern.sweet_fb_shot` will misread a canvas-layout front buffer. Fix it if the capture path uses it.
- A swap that names the default surface falls back to CPU scaling for the rest of the boot. ast22 logged none in its first 16 swaps.

**Recovery:** remove the boot-arg and RAM boot. **Verify:** the `hw scale programmed` line appears, there is no `fallback` line, `copy_us avg` is under 7000, the picture matches, and no underrun appears. Force a panic once to confirm the panic text is readable.

### Phase 4: zero-copy (1 to 2 days plus CI, 5+ boots)

Depends on Phase 3, because the pipe must already scale a canvas.

1. Allocate CoreAnimation's 840x1860 surfaces from the splash region, 0xe3940000 to 0xe6440000. That is 45 MB, or 6 canvases at stride 3584. Existing low-pool code (`sweetfb_lowpool`, `sweetfb_lowpoolbase`, `s->pooled`) was built for this on the Poco F1. **Proven** that the MDP fetches at least the three slots at 0xe3940000, 0xe431c000 and 0xe4cf8000. **Inferred** that the rest of the region is equally mapped, so display SMMU stream 0x1c00 needs no programming. Probe the last slot before trusting it. If it faults, the UniXNU T40k bank setup is the alternative, and it is much riskier.
2. Port `zcPresent`'s hold logic: a surface stays owned by the display until the next latch (UniXNU T42 to T43), and flips wait on the fence (T49).
3. Program the fetch stride from the surface.
4. Keep the copy path as the fallback for any swap that is not one full-canvas pooled surface.

**Verify:** `zc_frames` is near swaps, the copy is 0, flicker tests stay clean over 800+ flips, and `icon-tear` captures do not regress. Risk is high, so keep it behind a boot-arg until a full session passes.

### Phase 5: remaining stalls (2 to 3 days, as Phase 2 directs)

1. Identify the 10 Hz requester from the sampler stacks in ast22 and a `bgmtl_trace` window. The stacks need symbolizing, as in `analysis/switcher-slow.md`.
2. Add the AIR-to-NIR cache: key on library hash, function, constants and dylib UUID, stored in a durable directory. **Verify:** third-launch pipeline time falls under 1 s and rendering captures match.
3. Time who runs texture frees. If they run on the render thread, move them to the existing pool-free queue. **Verify:** `freed in` stays the same while encode falls.
4. Find why the hit rate is 73 to 80% with 200 to 500 MB made per 10 s. `pool busy misses` is low, so log missed shapes first, then consider a reserved pool. **Verify:** hit rate above 90% in the script.
5. Try `sweetamp_prio=20`, comparing encode time.

### Phase 6: GPU throughput (1 hour plus boots, only if Phase 2 says GPU-bound)

Test one at a time, after the stability plan has lowered recoveries:

1. `sweeta810level=8` for 960 MHz. Stop if recoveries rise or the phone gets hot.
2. `sweettu=noubwc,nocb,nolrz` for autotune. The fetch-fix sync already forces its own passes to sysmem. Stop at any colour flip or hang rise, and revert.
3. A `sweeta810bw` boot-arg in `a810_stage11`, which today takes `bw` from `ShengGmuPerfVote`: level 6 votes 5 of a table of 8. Only table values are possible. First read what `SWEET TOUCH: bandwidth vote MC0 ... max (0)` means (`ast22-linear.txt:2613`), since it already votes something.

**Verify each** with matched script runs: GPU busy, flips/s, recoveries per minute.

### Phase 7: past 60 fps (later)

Run the tick at the panel's 120 Hz and set `CA_HEARTBEAT_RATE=120` once zero-copy is stable and swaps pass 55/s. Today `fVsyncUS` is a compile-time 16667.

## Prepared fixes

All are in `plans/speed/patches/`, unapplied. Hashes are in `plans/speed/SHA256.txt`. Do the first two first.

| Patch | Does | Checked | Not checked |
|---|---|---|---|
| `2051-asteroids-vsync-tick-deadline.patch` | `sweetfb_tickfix=1` arms ticks from an absolute deadline | `git apply` in series, `test/tick-check.c` passes (29 flips/s today, 56 fixed) | Kernel compile, phone |
| `2052-asteroids-flip-timing-log.patch` | New 10 s timing line, fills the trace for flips | `git apply` in series | Kernel compile, phone |
| `chl-gpu-timing.patch` | GPU timestamps behind a file, fixes the perf histogram bin | `git apply`, both files compile for iOS, symbol exported | Link in `build.sh`, phone |
| `2053-asteroids-vig0-hw-scale.patch` | `sweetfb_ast_hw=1` scales in VIG0, restores on fallback and panic | `git apply` in series, `test/qseed-check.c` passes | Kernel compile, phone, register behaviour |

The kernel patches are diffs against `research/xnu-local/dxb/xnu` as it stands. Apply them in number order.

## Biggest unknowns

1. How many more swaps CoreAnimation produces once they stop being dropped, or whether scaler threads then starve it. The 43% figure assumes swaps stay the same.
2. What fills the 23 ms unaccounted for in a heavy frame. If it is encoding on the CPU, none of the display or GPU work raises the limit near 40 fps.
3. Real GPU busy time. Without it, ranks 8 and 9 are guesses.
4. Whether VIG0's scaler registers accept writes under Gunyah, and whether the whole splash region is fetchable.

## Incident during this work

My first compile check for `chl-gpu-timing.patch` used BSD `sed`, which did not rewrite `-o`, so it wrote a patched `chl_vk` object and a depfile into `chl-work2/mesa-turnip26/build-ios`. That broke the no-build-directory rule.

I restored it by recompiling from the unchanged source with the original command, resetting its mtime to the value in `.ninja_log`, and trashing the stray depfile. **Verified:** relinking with the restored object in a scratch directory is byte-identical to the existing `libchlorophyll.dylib` (`59c8d226...`), and no file in `build-ios` is newer than the start of the check. `ninja -n` still reports work, but `-d explain` traces it to the `git_sha1.h` phony target, not to this object.
