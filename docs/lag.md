# Lag: why animations run at 25 to 35 frames/s

2026-11-02, evening. Runs ast65 to ast82 on the phone through `plans/devloop/dev.py`. Labels: **Proven** means a run's own trace shows it, **Inferred** means my reading of those numbers, **Missing** means no data. Liquid Glass, blur, passes and the 840x1860 canvas are untouched by everything here.

## The answer

Frames are serial because CoreAnimation's render thread spends most of each heavy frame inside our driver's kernel calls, and it starts the next frame only on its own timeline after the previous one is submitted. Encoding was not CoreAnimation's work. Then the display dropped finished frames whenever a newer swap arrived before the next tick.

Two changes help:

1. A staging-buffer pool in chl. Every texture upload allocated, mapped, freed and unmapped a fresh BO, four kernel calls each.
2. `sweetfb_keep=1`, an existing kernel switch (2082). A tick shows the replaced frame when it has finished and the newest one has not.

Measured on the same `ui` script, kernel 2141, hardware scaling on. Both arms ran the same light trace build, without the profiler, alternating A, B, A, B. Shown fps is frames that reached the panel inside animation bursts, from `burstfps.py`:

| Arm | Runs | Shown fps in bursts | Median | Swaps/s in bursts | Heavy (80+ draws) period p50 |
|---|---|---:|---:|---:|---:|
| A, before: no pool, `sweetfb_keep` off | ast74, ast76 | 39.5, 40.0 | **39.8** | 47.7, 46.2 | 24.6, 25.2 ms |
| B, after: pool and `sweetfb_keep=1` | ast75, ast77 | 46.5, 47.7 | **47.1** | 49.7, 49.3 | 19.2, 19.0 ms |
| Keep alone | ast78 | 45.4 | | 47.5 | 25.7 ms |
| Pool alone | ast79 (and ast72) | 38.6 (39.3) | | 50.5 (50.1) | 18.8 ms |

So B shows 18% more frames, +7.3 fps. Keep supplies most of that. The pool cuts the heavy frame period by about 6 ms, but on its own it shows fewer frames, because the display then drops more finished frames (see fix 2). **Ship them together, never the pool alone.** `speed3.py`'s diluted "frames" figure moves the same way: 27.8 and 27.9 before, 28.6 and 29.2 after. Every run had 0 recoveries and 0 backboardd restarts.

Stable 60 is still not reached. Light frames, under 80 draws, now run at 16.7 to 17.5 ms. The heaviest glass frames are limited by GPU time and CoreAnimation's pacing, as described below.

## How I measured

Kernel 2140 extends `kern.sweet_fb_trace` with absolute times for SwapBegin, the swap, the tick that took it, its copy, the hold release and the latch, plus a ring of every vsync tick. Kernel 2141 adds every GPU submission and every fence wait that returned, with the pid. A trace build of `bgmtl` records, per command buffer: created, first encoder, commit, hold gate passed, submitted, GPU done on the completion thread, handlers done, the raw GPU timestamps, draws, the IOSurface and the thread. A utility thread writes both into `bgmtl.log` once a second, all on `mach_absolute_time`. The GPU's timestamps turned out to be the same 19.2 MHz clock: no GPU start in 3331 frames came before its own commit, so I could place GPU work on the same axis (**Proven**, ast66).

Tools, all read-only, in `plans/lag/tools/`:

- `lagtrace.py RUN [--show N]` lays out frames and their gaps.
- `burstfps.py RUN...` gives shown and swapped frames per second inside animation bursts.
- `gpuwho.py RUN` lists who submits and waits on the GPU around each frame.

`bgmtl.log` is appended across boots on one root, so every tool reads only the last boot. My first profile totals mixed boots, and I redid them.

## Timeline of heavy frames before the fix

ast66, a home swipe, 51-draw glass frames. Times in ms from the first frame's start. "gpu" is the GPU's own timestamps.

```
frame  start  encode->commit  submit  swap   gpu start..end   next start   tick that showed it
  1      0.0     0.1 -> 81.8    83.2   88.8    82.9 .. 104.9     106.8        105.3
  2    106.8   106.8 -> 111.8  113.0  116.8   112.3 .. 133.8     135.1        138.5
  3    135.1   135.1 -> 140.1  140.9  144.5   140.5 .. 161.7     163.0        dropped
  4    163.0   163.1 -> 166.7  167.9  171.8   167.2 .. 187.5     188.7        188.6
```

Per draw-count tier, medians over the six baseline trace runs ast66 to ast71, in ms:

| Draws | Period | Start to commit | Commit to submitted | GPU busy | GPU end to next start |
|---|---:|---:|---:|---:|---:|
| 0-50 | 16.4-16.9 | 1.2-3.0 | 1.0-2.2 | 0.9-3.1 | 8.7-13.0 |
| 50-80 | 17.6-19.5 | 5.1-6.4 | 1.7-2.0 | 5.3-8.5 | 2.0-3.6 |
| 80-120 | 19.2-22.7 | 6.7-7.9 | 1.8-2.3 | 6.7-10.1 | 1.9-2.7 |
| 120+ | 26.3-27.9 | 10.2-11.2 | 1.6-1.9 | 12.4-13.2 | 0.6-1.2 |

Light frames already run at 60. Heavy ones are serial: start, encode, submit, GPU, then the next start 1 to 3 ms after the GPU ends. Only 4 to 7% of pairs overlap at all.

## What the next frame waits for

**Proven, not our completion handler.** In ast67, of 532 heavy frames that started near the previous GPU end, 276 started before chl's completion thread had even seen the fence, and 286 before the handlers ran. CoreAnimation has no other GPU signal from us: bgmtl has no Metal events, and 2141 shows backboardd is the only process that submits to or waits on the GPU (6436 submissions, 6435 waits, all pid 64, ast69).

**Proven, the render thread is idle.** A sampler read the committing thread's stack 8 ms after each commit, 600 times (ast67). 545 were in `CFRunLoopRun` waiting in `mach_msg`, 8 were in our commit, and the rest in QuartzCore. So CoreAnimation is not blocked in a call of ours. It goes back to its run loop and is woken later.

**Proven, not a hard GPU gate and not the kernel tick.** Over three runs, a fit of the next frame's SwapBegin gives about 11 ms + 0.84 x (start to submitted) + 0.3 x (GPU time after submit), with a 4.4 to 5.3 ms residual spread. Against the GPU end alone the slope is 0.53. SwapBegin is spread over the whole 16.7 ms after a kernel tick. 20% of heavy frames start before the previous GPU end.

**Inferred: CoreAnimation's own pacing.** After it submits a frame, the render server waits about 11 to 13 ms on average, with a spread the size of one vsync, and starts the next. So the period is mostly our encode time plus one of its own vsync waits. That made the encode time the lever.

**Proven, not double buffering.** The flipbook uses two surfaces, IDs 2 and 3, in every run. `CA_FORCE_TRIPLE_BUFFERED_LATENCY=1` in backboardd's environment changed nothing: still two surfaces, 25.7 against 26.2 to 26.8 animating fps (ast68). The inject's old `bgmtl-bb.triple` switch sets `CA_FORCE_TRIPLE_BUFFERING`, which no longer exists in iOS 26's QuartzCore. QuartzCore 26 has `CA_FORCE_TRIPLE_BUFFERED_LATENCY`, `CA_ENABLE_PREFERS_TRIPLE_BUFFERED_LATENCY` and `CA_FORCE_DOUBLE_BUFFERING` in one debug-option table. I did not find what reads it.

Also ruled out, **Proven**: hold-wait cost nothing in these runs (busy 0 of about 3000 commits), `waitUntilScheduled` and `waitUntilCompleted` (9 and 6 calls a run), `presentDrawable` (never called), and other processes on the GPU.

## Where the encode time went

A 2 ms sampler on the render thread, only while it was between creating a command buffer and its submit returning (ast71, 6708 samples, last boot):

- 81% had chl, Turnip or libdrm as the innermost non-system code. QuartzCore itself was 12%.
- 66% were inside a kernel call made by our code: `IOConnectCallStructMethod` through libdrm's `sweet_call`, and `IOConnectMapMemory64` and its unmap.

The call chains, symbolized against the unstripped `chl-obj/raw.dylib`:

| Share | Chain |
|---|---|
| about 25% | `chl_texture_replace` -> `buffer_new` -> `tu_AllocateMemory` -> GEM_NEW, then `vkMapMemory` -> IOConnect map |
| about 10% | prologue retire -> `obj_release` -> `tu_FreeMemory` -> GEM_CLOSE and unmap of those same staging buffers |
| about 13% | `chl_texture_create` -> `tu_AllocateMemory` -> GEM_NEW, the texture pool's misses (72% hit rate) |
| about 7% | `vkQueueSubmit` |
| about 4% | `sweet_drm_bo_sync` from `-[BGMTLTexture chlTextureFor:]` on render targets |

Every `replaceRegion` upload made a new host buffer and freed it once the GPU had copied it.

## Fix 1: staging pool in chl

`plans/lag/patches/chl-staging-pool.patch`. Upload staging buffers come from a pool by power-of-two size class, 16 KB to 16 MB, with 24 MB at most kept. A buffer goes back into the pool in `destroy_obj`, where it used to be freed. That runs only when its last reference goes, after the prologue that copied from it retired on the GPU, so reuse is as safe as the free was. Readbacks keep their own buffers. `bgmtl.log` gains a `staging:` line with hits, misses and pooled MB.

**Proven, ast72:** 21834 hits against 693 misses. Encode samples went from 6708 to 5014, kernel-call samples from 4428 to 2704, and QuartzCore stayed at about 800. Per heavy tier:

| Draws | Period before (ast66-71) | Period after (ast72) | Start to commit | Commit to submitted |
|---|---:|---:|---:|---:|
| 80-120 | 19.2-22.7 | 16.7 | 6.7-7.9 -> 3.3 | 1.8-2.3 -> 0.8 |
| 120+ | 26.3-27.9 | 20.7 | 10.2-11.2 -> 5.7 | 1.6-1.9 -> 0.7 |

The BO count rose from about 1030 to 1288 at most, with no cost visible in submit time.

## Fix 2: show the frame that finished

**Proven, ast72.** With faster encoding, CoreAnimation now swaps the next frame 1 to 3 ms after the previous GPU end. That is usually before the next tick, so the kernel replaced a frame that had just finished with one that had not, and waited again. In one 385 ms stretch of 21 ms glass frames, 17 frames rendered and 3 reached the panel. The kernel's own counters agree: 468 frames were dropped with their GPU work done, against 38 still drawing. Shown frames per second fell from 42.4 to 39.3 while swaps rose from 46.9 to 50.1.

`sweetfb_keep=1` (2082) keeps the replaced frame and shows it at a tick where the newest one is still on the GPU. It measured no gain in speed2 because the encode was then slow enough that this case was rare. With the pool it showed 381 kept frames in ast73, and shown fps rose to 45.3.

Cost, **Proven**: the kernel now sometimes holds both of CoreAnimation's surfaces. In ast75, ast77, ast78 and ast80, hold-wait engaged on 4 or 5 commits per run. Each run also had 4 hits on its 50 ms limit, all within backboardd's first 20 to 29 commits at boot, before the UI was up. The gate then rests for 120 commits (37 to 77 skipped). The keep-off runs had none. In those commits CoreAnimation may draw into a surface the kernel still holds, which is the black-bar mechanism of `blackbars2.md`, but only during boot. **Inferred:** a kept surface is not released while nothing new is swapped at boot. Release `fPrevHold` when the panel is off or at the first swap if it matters.

Visual check:

- ast81 ran the clean pool payload (`next-keep`) without hardware scaling, so its screenshots are readable. The home screen, Settings, Calculator, Notes with keyboard, Control Center glass and the app switcher look the same as ast53's. Icons, widgets and blur are intact. I saw no regression from the pool.
- ast81 did not have keep. For keep I looked at speed2's ast38 (`b1-keep`, no hardware scaling). Its home page 2 and Control Center glass look normal.
- Screenshots are one still per gesture, so a bar that appears only mid-animation would not show in them. `hold wait:` lines show none during animation.

## What I did not change, and what is next

- **GPU time**, **Proven:** 12.4 to 13.2 ms per 120+ draw frame, and 15 to 21 ms for some 50-draw glass frames. That is now the limit in the heaviest scenes, about 48 fps at best with perfect overlap. `chl-bind-cache.patch` (speed2) still applies cleanly and claims some GPU time. It is untested.
- **Texture memory on pool misses** is now the largest kernel cost in encode, about 20% of its samples. A memory-level pool like the staging one would remove it, but reused memory must read as zero as fresh BOs do, which costs a clear per take. I did not build it.
- **CoreAnimation's pacing**, **Inferred:** the 11 to 13 ms it waits after each submit is its own. Finding the reader of `CA_FORCE_TRIPLE_BUFFERED_LATENCY` in QuartzCore, with the ipsw workaround below, is the way to learn whether a triple-buffered latency mode can be enabled.
- **Metal GPU timing properties**, **Proven no effect:** bgmtl did not implement `GPUStartTime`, `GPUEndTime`, `kernelStartTime` or `kernelEndTime`. I added them from the trace's commit, submit and completion times (`next-lag-gt`). ast80 showed 46.8 fps, against 46.5 and 47.7 for B, with an unchanged 19.2 ms heavy period. CoreAnimation's pacing does not depend on them. Not kept.
- **Screenshots with hardware scaling are unreadable.** Every `_hw` run's `frames/` shows stripes, because the shot reads the scanned buffer with the wrong stride. That predates this work. Visual checks need a run without `sweetfb_ast_hw=1`.

ipsw opens this cache's split files only with a `.symbols` file. A copy of `.01` with the expected UUID at 0x58 works for `dyld str` and `dyld image`. I made it in `/tmp/lagdsc` with hard links, and the research copy is untouched.

## Runs

| Run | Kernel | User build | Note | Shown fps in bursts | Swaps/s in bursts | Heavy period p50 |
|---|---|---|---|---:|---:|---:|
| ast66 | 2140 | trace | first timeline | 43.1 | 47.1 | 24.8 |
| ast67 | 2140 | trace + idle stack sampler | run loop wait found | 41.2 | 47.3 | 24.2 |
| ast68 | 2140 | trace + sampler, launcher with `CA_FORCE_TRIPLE_BUFFERED_LATENCY` | no effect, still 2 surfaces | 40.5 | 46.5 | 26.1 |
| ast69 | 2141 | trace, stock launcher | only backboardd on the GPU | 41.9 | 46.6 | 25.3 |
| ast70 | 2141 | trace + encode profiler | 81% of encode in our code | 42.6 | 47.6 | 24.9 |
| ast71 | 2141 | trace + profiler with call chains | chains above | 42.4 | 46.9 | 25.7 |
| ast72 | 2141 | + staging pool, profiler | encode halved, shown down | 39.3 | 50.1 | 18.8 |
| ast73 | 2141, keep | + staging pool, profiler | 381 kept frames | 45.3 | 49.6 | 19.3 |
| ast74 | 2141 | A: light trace, no pool | before | 39.5 | 47.7 | 24.6 |
| ast75 | 2141, keep | B: light trace, pool | after | 46.5 | 49.7 | 19.2 |
| ast76 | 2141 | A | before | 40.0 | 46.2 | 25.2 |
| ast77 | 2141, keep | B | after | 47.7 | 49.3 | 19.0 |
| ast78 | 2141, keep | light trace, no pool | keep alone | 45.4 | 47.5 | 25.7 |
| ast79 | 2141 | light trace, pool | pool alone | 38.6 | 50.5 | 18.8 |
| ast80 | 2141, keep | B + Metal GPU timing properties | no effect | 46.8 | 50.6 | 19.2 |
| ast81 | next-fix, no hw | clean pool (`next-keep`), no trace | visual check, frames normal | n/a | n/a | n/a |
| ast82 | next-fix (base image) | `next-fix/out-metal` | restore of the known-good files, clean | n/a | n/a | n/a |

The ast66 to ast73 trace builds differ, so compare only within A and B. ast65 stopped at the push because `/usr/local/lib/bgmtl/holdwait` does not exist on this root. Nothing was written. "keep" is `sweetfb_keep=1`. Every image is `next-loop-hw`'s arguments with the kernel named.

## What to make default

1. `chl-staging-pool.patch` into the Chlorophyll build, with `sweetfb_keep=1` added to the boot args (`tools/boot/next-flash-args.txt`, `plans/devloop/build-loop.sh`), together.
2. Before flashing, one non-hw run of the pool with keep, to see both in readable frames, because ast81 had no keep. A keep fix for the boot-time hold is optional.
3. Not the triple-buffer environment, not the GPU timing properties, and not the pool without keep.
4. The 2140 and 2141 trace patches are diagnostics with no measurable cost. I left them out of `patches-12377` and reverted the xnu tree. The rebuilt `fakeroot` kernel has no trace but is not byte-identical to `next-fix/kernel`, because the incremental relink differs. `next-fix/kernel` is unchanged.

## Files

- `plans/lag/patches/chl-staging-pool.patch`, the fix, against `next-fix`. Built payload in `plans/lag/next-keep/out-metal`.
- `plans/lag/patches/2140-asteroids-frame-trace-v3.patch` and `2141-asteroids-gpu-trace.patch`, kernel diagnostics.
- `plans/lag/next-lag/`, the trace build; `next-lag-base/`, the same without the pool; `next-lag-gt/`, the GPU-time variant.
- `plans/lag/build-lag.sh`, loop images into `plans/lag/out/`, and `plans/lag/run-arms.sh`, alternating runs.
- `plans/lag/tools/`: `lagtrace.py`, `burstfps.py`, `gpuwho.py`, `shownfps.py`, `dscmap.py`.
- `plans/lag/kernel/kernel-2140.sweet`, `kernel-2141.sweet`, and images `plans/lag/out/lag-hw-boot.img`, `lag-hw2-boot.img`, `lag-hw2-keep-boot.img`.

The phone root has `next-fix/out-metal`'s `bgmtl-inject.dylib` and `libchlorophyll.dylib` again (ast82). `bgl-launch` went back in ast69. The phone was left in OrangeFox.

## Black bars with keep, and an honest SwapWait

2026-11-02, late evening. Runs ast84 to ast96. ast84 is the owner's run. ast85 to ast96 used my trace build of the pool (`plans/lag/arms/fix2`) and kernels with 2140 to 2143. Labels as above.

### The cause

With `sweetfb_keep=1`, the kernel can hold both of CoreAnimation's surfaces: the kept frame and the pending one. In copy mode SwapWait answered "complete" at once (`swapWaitInner` returned early unless `fEarly`), so CoreAnimation started drawing into the kept surface. bgmtl's hold-wait then waited in `commit` and gave up after 50 ms.

**Proven, ast84** (owner's run, pool + keep, no hw). There were 4 timeouts at boot. After them the gate skipped all of its next 120 busy commits, because the rest was counted in busy commits, not in time. With keep there are many busy commits, so those 120 unguarded commits landed in the first animations. Those 120 skips are the likely source of the bars the owner saw. In the steady state the gate waited successfully on 516 commits, 4.1 s in total.

### What real iPhones do, and what changed

On Apple hardware, CoreAnimation calls SwapWait(N) before each frame and draws into the older surface once it returns. The display answers only when the surface it is giving back is really free. Our copy-mode display did not.

- **2142, `sweetfb_astwait=1`.** In copy mode, SwapWait(N) answers complete only once no older swap's surface is held: neither the kept frame nor one being copied. It waits up to 1.5 frames, then answers `kIOReturnBusy`, which CoreAnimation's poll treats as "pending" (0221). This is Apple's contract, so CoreAnimation's own loop does the waiting, before it draws. Off unless the boot arg is set.
- **`bgmtl-hold-rest-2s.patch`.** The hold-wait rest after four misses now lasts 2 s, so it can no longer leak into later animations. `plans/lag/test/hold-check.py` covers it with a fake clock.
- **2143.** The 2090 bar check now also runs with hardware scale, by scanning the canvas-sized slot and hashing the source around its 1:1 copy. Diagnostic only.

A third surface is not on offer. **Proven:** CoreAnimation kept rotating two surfaces in every run, honest SwapWait included. In ast85 and ast87 it re-made one surface early, with ID 2 replaced by 75 or 129, but never added one. `CA_FORCE_TRIPLE_BUFFERED_LATENCY` changed nothing (ast68). In iOS 26 the "FlipBook" strings belong to the always-on display's frame store, not the swap chain. What makes CoreAnimation choose triple-buffered latency is still **Missing**. The ipsw workaround is above.

### Measured

SwapWait with 2142 answered after a real wait on 274 of 3149 swaps in ast85, median 3.8 ms, and on 296 of 3121 in ast87, median 5.4 ms. The shown-fps baseline with neither change is ast74 and ast76: 39.5 and 40.0.

| Run | Config | hold-wait timeouts / skipped | `src_moved` | Shown fps in bursts |
|---|---|---|---:|---:|
| ast85, ast87, ast89 | pool + keep + astwait, hw | 0 / 0 each | n/a, ast87 had bar check off under hw | 48.2, 47.2, 46.5 |
| ast92, ast94 | pool + keep + astwait, hw, bar check (2143) | 0 / 0 each | 3, 4 | 45.8, 47.9 |
| ast90 | pool + keep + astwait, no hw, bar check | 0 / 0 | 1 | 40.2 |
| ast86, ast91 | pool + keep, hw (control) | 6 / 0, 8 / 7 | ast91: 5 | 48.6, 47.5 |
| ast95 | pool + keep, no hw, bar check: the owner's config | 7 / 2 | 8 | 41.5 |
| ast88, ast93 | pool, keep off, hw | 0 / 0 each | ast93: 2 | 40.7, 37.6 |
| ast96 | pool, keep off, no hw, bar check | 0 / 0 | 4 | 37.8 |

With hardware scale, shown fps in bursts is a median of 47.2 over five fix runs, against a median of 38.6 for pool without keep (ast88 40.7, ast93 37.6, ast79 38.6) and 39.8 for neither. The fix keeps all of keep's gain. Without hw (CPU copy), the fix gives 40.2 and keep without astwait 41.5.

**Bar check, Proven for what it can see.** Every arm, the controls included, has the same hits:

- Boot and lock-screen black frames (swap 1, swap 17).
- Calculator, which is black.
- Dark-grey rows during home swipes, at full-scale rows 1376 to 1536 or canvas rows 1072 to 1200, with `px` `ff27…` to `ff40…`. They appear in pool-only runs with keep off (ast93, ast96) at the same rows and values. They are dark icons, not CoreAnimation's pure-black damage fill.

No run had a pure-black hit during the script after boot. That includes ast95, the owner's failing config. **So the 2090 check does not see the owner's bars.** It misses bars under 48 rows or under 3 columns, and it samples only the frame copied at each tick. Zero bars is therefore **not proven** by the logger.

The objective signs of the mechanism are hold-wait timeouts and skipped commits. They fell from 6 to 8 timeouts in the keep controls to 0 in all five fix runs, and to 0 skips. `src_moved` (a source changed during its copy) fell from 8 in the owner's config to 1 without hw. It is not zero in any arm, the pool-only ones included (2 and 4), so that write path predates keep and needs its own look.

### Verdict and what to make default

- Daily driver: `plans/lag/next-keep/out-metal/bgmtl-inject.dylib` and `libchlorophyll.dylib`, which are the staging pool plus the 2 s hold rest, with no trace.
- Daily kernel: 2142 in the series, and `sweetfb_keep=1 sweetfb_astwait=1` together in the boot args. Never keep alone. If the owner sees any bar with that pair, drop both boot args. The pool alone is bar-safe (0 timeouts) but shows no frame-rate gain.
- Still needed: one owner-watched run of pool + keep + astwait without hw, the configuration where the owner saw bars, before flashing.
- 2140, 2141 and 2143 are diagnostics, off unless their boot args are set.

Files for this part:

- `plans/lag/patches/2142-asteroids-honest-swapwait-copy-mode.patch`
- `plans/lag/patches/2143-asteroids-bar-check-hw-scale.patch`
- `plans/lag/patches/bgmtl-hold-rest-2s.patch`
- `plans/lag/test/hold-check.py`
- Kernels `plans/lag/kernel/kernel-2142.sweet` and `kernel-2143.sweet`
- Images `plans/lag/out/lag-{x,xbar,xbar3,kbar,kbar3,p,pbar3,xbar-nohw,kbar-nohw,pbar-nohw}-boot.img`

The xnu tree in `research/xnu-local` has 2140 to 2143 applied. The root has the `fix2` trace build until it is replaced.

## Even pacing at 60 (night of 2026-11-03)

The owner felt keep + astwait as "smoother, but still very jittery", and 30 Hz as "a little better" but rejected it. So the goal is the share of shown frames one 60 Hz vsync apart.

Measured with `plans/lag/tools/pacing.py`, which counts the gaps between shown frames from the kernel's tick ring, and `miss.py`, which splits each frame into stages and calls it on time (1 vsync) or late (2 or more).

### Where the misses come from

**Proven, ast85, 87, 92, 94** (pool + keep + astwait, hw). Shown-frame gaps are 65 to 70% one vsync, 23 to 29% two, and 6 to 8% three or more. The flip rate, how often the gap changes from one frame to the next, is 0.30 to 0.36. Only 36 to 43% of animation time is in steady runs of 6 or more equal gaps.

Light frames, under 30 draws, are always on time. Among heavier frames, 38 to 42% are late, and in every one CoreAnimation started the frame 33 ms after the previous start instead of 17 ms. No frame started on time and finished late.

Late frames split into two kinds:

1. **Render-bound, about 40%.** The next frame starts 0 to 4 ms after the previous GPU end. Start to GPU end takes about 22 ms against 12 to 14 ms on time. The stages are CoreAnimation 0.7, encode 5 to 6, submit 0.8, GPU 13 to 15. There's no GPU queueing, and fence plus copy are under 1.3 ms each.
2. **Idle, about 40%.** The next frame starts 15 ms or more after the previous GPU end, so CoreAnimation had nothing to draw. **Inferred:** a client (SpringBoard) transaction arrived late.

### Levers measured or ruled out

- **GPU clock, Proven nearly useless.** At level 8 (960 MHz), GPU time per heavy command buffer was 17.9 ms in ast61, against 18.2 and 18.4 ms at 763 MHz in ast55 and ast63. That's 3% less for 26% more clock, and the bandwidth vote follows the level. GPU time grows about 0.13 ms per draw: 11.5 ms at 100 to 140 draws, 18 ms at 140+.
- **CPU frequency, Proven not a lever.** All clusters are already fixed at max: A520 1.80 GHz, A720 2.40 GHz, A720+ 2.50 GHz.
- **The renderer forced sysmem.** The root's `/private/var/db/bgmtl/bgmtl-bb.tudebug` holds `sysmem,noubwc,nocb,nolrz` (`bgl-backboardd.log` in every run). The G60 arm tries GMEM.
- **CPU placement, Proven a big lever.** In ast101 (A60, luma payload, trace with CPU numbers), 57% of heavy command buffers were created and committed on the A520 cores. Median encode for 100 to 160 draws, by the core that committed:

  | Core | Frames | Encode median |
  |---|---:|---:|
  | A520 | 339 | 5.4 ms |
  | A720 | 228 | 2.5 ms |
  | A720+ | 66 | 1.9 ms |

  The kernel's AMP steering (`sweetamp_prio`, default 31) only chooses among idle cores. During animations the big clusters run at 72 to 100% load, so the render thread often finds none idle.

ast101 by itself: shown 41.0 fps, gaps 65.3% one vsync, flip rate 0.36, steady 35.8%, and 45% of heavy frames late.

### GMEM: no effect (ast101, 103, 104, 105)

**Proven.** Allowing GMEM (`sweettu=noubwc,nocb,nolrz`, which overrides the root file; `bgl-backboardd.log` confirms `TU_DEBUG=noubwc,nocb,nolrz`) left GPU time unchanged. At 100 to 140 draws it was 11.3 and 11.6 ms against 11.4 and 11.6 ms on sysmem. At 140+ it was 18.1 and 18.5 ms against 18.2 and 18.5 ms.

| Run | Arm | Shown fps | 1 vsync | Flip rate | Steady | Heavy late |
|---|---|---:|---:|---:|---:|---:|
| ast101 | A60 (sysmem) | 41.0 | 65.3% | 0.36 | 35.8% | 45% |
| ast104 | A60 | 42.2 | 67.9% | 0.31 | 42.0% | 43% |
| ast103 | G60 (GMEM) | 43.2 | 69.3% | 0.29 | 50.1% | 42% |
| ast105 | G60 | 42.7 | 69.0% | 0.32 | 39.5% | 41% |

The difference is within run-to-run noise, and the GPU time shows no cause, so GMEM is dropped. 0 recoveries in all four.

**Inferred:** the reason is Chlorophyll's framebuffer-fetch sync (the tear fix). It forces every pass with a fetch draw to sysmem, and on each such draw it emits `ALL_CLEAN | ALL_INVALIDATE | WAIT_MEM_WRITES | WAIT_FOR_IDLE | WAIT_FOR_ME` (`turnip/tu_cmd_buffer.cc`, `chl_turnip_fetch_sync`). That's a full GPU drain and cache flush, up to about 300 a second. It fits a GPU time that tracks draws (0.137 ms per draw) and not clock or tiling. The trace now counts fetch syncs per command buffer, so the next runs can measure it.

**Proven, ast105:** the render thread runs at `pth_priority` 54 (policy 4, max 63), above the `sweetamp_prio=47` threshold that batch 1 tests.

### Batch 1: big-core steering and the bind cache, no effect (ast113 to 116)

| Run | Arm | Shown fps | 1 vsync | Flip rate | Steady | Heavy late | Heavy on A520 |
|---|---|---:|---:|---:|---:|---:|---:|
| ast113 | M47, `sweetamp_prio=47` (no effect possible, see below) | 41.8 | 66.7% | 0.31 | 43.6% | 40% | 55% |
| ast115 | M47 | 40.8 | 65.3% | 0.36 | 34.9% | 45% | 58% |
| ast114 | BC, `chl-bind-cache.patch` | 41.4 | 67.0% | 0.37 | 32.3% | 40% | 58% |
| ast116 | BC | 41.0 | 64.9% | 0.35 | 36.4% | 45% | 60% |

**Proven:**

- Raising the steering threshold did not move the render thread. It still committed 55 to 58% of heavy command buffers on the A520 cores (57% in ast101), because the big cores are busy, not mis-steered.
- The bind cache did not shorten encode: 5.4 ms on A520, as before.

Both are dropped. Steering only chooses among idle cores, so moving the render thread would take a change in the scheduler itself (a reservation or preemption for marked threads). I did not try that tonight.

### GPU time tracks the fetch syncs (ast113 to 116), but they are not its cause

**Correction, ast119.** I first marked the fit below as proven cause. Turning the sync off entirely left GPU time unchanged (below), so the fit measured correlation: fetch syncs count the full-screen glass draws, and those draws are the cost.

With per-command-buffer fetch-sync counts in the trace, a fit of GPU time on both draws and syncs gives:

| Run | Constant | Per draw | Per fetch sync |
|---|---:|---:|---:|
| ast113 | 0.95 ms | +0.013 ms | 0.507 ms |
| ast115 | 0.78 ms | −0.007 ms | 0.568 ms |
| ast114 | 0.83 ms | −0.022 ms | 0.644 ms |
| ast116 | 0.87 ms | −0.045 ms | 0.738 ms |

Draws cost nothing measurable once syncs are in the fit. Command buffers of 100 to 160 draws record a median of 32 or 33 fetch syncs.

The barrier is the tear fix (`analysis/fetch-fix.md`): before every draw whose fragment shader fetches an attachment written since the last sync, it emits a full clean and invalidate of every cache, a wait for memory writes, a wait for idle and a wait for the CP's ME.

**Candidate, `lumafs`.** The same sync with only what this dependency needs, matching Turnip's own barrier for a colour write followed by an input-attachment read: `CCU_CLEAN_COLOR | CACHE_INVALIDATE | CCHE_INVALIDATE | WAIT_FOR_IDLE`. The fetch draw still waits for the earlier draws and reads cleaned memory through freshly invalidated texture caches. Dropped are the depth clean, the UCHE clean, both CCU invalidates, the bindless invalidate, the wait for memory writes and the wait for ME. It passes the host checks, including `tear-check.py`. Whether the label tear stays fixed is **Missing** until the no-hw run's frames (batch 2).

**Light sync, Proven no gain (ast117).** Shown 43.1 fps, 70.4% one vsync, flip rate 0.28, steady 44.1%, 40% of heavy frames late. GPU time is 0.88 ms + 0.001 ms per draw + **0.561 ms per fetch sync**, the same as the full flush. So the cost is the wait for idle itself: every fetch draw waits for all earlier draws to drain, which serialises about 32 full-screen glass draws per heavy frame. The cache flags don't matter.

The second light run (ast118) was already running and finished. The no-hw run of this arm was cancelled, because an arm with no gain needs no tear check. `lumafs` is dropped.

**Next candidate, `lumaov`: ordering in hardware instead of a drain.**

- Chlorophyll already marks every fetch pipeline with rasterization-order attachment access (`chl_render.c.inc`, `cbs` flags).
- On A810, which has coherent UBWC flag caches, Turnip skips its sysmem prim-mode state and gives raster-order pipelines `GRAS_SC_CNTL.SINGLE_PRIM_MODE=FLUSH_PER_OVERLAP` (`tu_pipeline.cc`, `tu_pipeline_builder_parse_rasterization_order`). The label tear happened with exactly that mode, before the fetch sync existed.
- `lumaov` gives those pipelines `FLUSH_PER_OVERLAP_AND_OVERWRITE`, the mode Turnip uses for sysmem raster order on chips without coherent flag caches. It turns the full-drain sync off with `sweetfetchsync=0`.
- The hardware then flushes only where a primitive overlaps earlier fragments, not the whole GPU per draw.
- Whether that stops the label tear (black bands at the bottom of home-screen icons, `analysis/label-tear.md`) is tested first, in a run without hw scale so the frames are readable.

### Hardware overlap ordering: the tear returns, and GPU time does not drop (ast119)

`lumaov` with `sweetfetchsync=0`, without hw scale. **Proven:**

- 0 fetch syncs were recorded per heavy command buffer, and the boot args confirm `sweetfetchsync=0`.
- GPU time for 100 to 160 draws was a median of **13.0 ms** (536 command buffers). With the sync on it was 11.3 to 11.6 ms. So removing every full drain does not shorten GPU time.
- The **label tear is back**: torn bands at the bottom of the Photos, Mail, News, Maps and Settings icons in `frames/04-page-1-right.check.png`, and a garbled battery glyph.

`lumaov` is dropped. Its second run (ast120) was already running and finished. The other one was cancelled.

**Conclusion, Inferred from ast61, ast103, ast105 and ast119.** GPU time on heavy frames is the pixel work of the full-screen glass draws themselves. The evidence:

- A 26% higher clock gave 3% (ast61).
- Allowing GMEM changed nothing while the sync forced those passes to sysmem (ast103, ast105).
- Removing the drains changed nothing (ast119).

That points at memory traffic in sysmem rendering.

**Next, `GF`:** force GMEM (`sweettu=gmem,...`) with the fetch sync off, on stock Turnip. In GMEM a fetch reads on-chip tile memory, ordered by `FLUSH_PER_OVERLAP` as Turnip intends, so it could fix the tear and cut memory traffic together. No-hw first, for the tear and for recoveries, since GMEM hung often before the 2100 fix.

### Forced GMEM with the sync off: slower and torn (ast121)

**Proven:**

- GPU time rose to 15.0 ms at 100 to 160 draws and 30.6 ms at 160+, against about 11.4 and 18.3 ms in sysmem.
- New artifacts appear on the App Store, Wallet, Settings and Photos icons in the home-screen frame.
- 0 recoveries.

Dropped. Nothing cheap is left on the GPU side. At this resolution the glass passes cost what they cost.

### CPU placement: the steering never ran (ast122 and the build config)

**Proven:**

- The sweet kernel is built without `CONFIG_SCHED_SMT`. Its compile flags define only `CONFIG_SCHED_AUTO_JOIN`, `CONFIG_SCHED_DEFERRED_AST`, `CONFIG_SCHED_SFI` and `CONFIG_SCHED_TIMESHARE_CORE`.
- The AMP steering (`sweet_amp_wanted_map`, `sweetamp`, `sweetamp_prio`) exists only inside `choose_processor_smt()`, so it was never compiled. The "idle-core steering on" log line only means the masks were set. That is why M47 changed nothing: **ast113 and ast115 were not a real steering test**, since `sweetamp_prio` had no code behind it.
- The compiled `choose_processor()` takes `lsb_first(idle_map)`, the lowest-numbered idle CPU. CPUs 0 to 3 are the A520 cores.
- ast122 sampled per-process CPU once a second (`proc_pidinfo`) during the 51 animating seconds. User processes used about 1.5 of 8 cores: backboardd 624 ms/s, SpringBoard 294, MobileSafari 82, Preferences 82, PerfPowerServices 75, MTLCompilerService 55, the rest under 50 each.
- Clusters 1 and 2 (A720, A720+) idle at 0 to 4% load. Cluster 0's constant 100% is one A520 core that never idles. It is likely a kernel thread, not a user process, and it is not followed up here.

So the big cores sit mostly idle while the render thread runs on an A520 57% of the time, at about twice the encode time.

**Fix, 2144** (`plans/lag/patches/2144-asteroids-amp-steering-in-choose-processor.patch`; numbered 2160 at first, which the USB gadget work already uses), built in `research/xnu-local` (my tree, with 2140 to 2143). A copy of `xnu-combined` did not build: its build directory kept absolute paths into the original tree, so headers came in twice. Both scheduler arms therefore run on the same 2144 kernel, which lacks 2130 and 2150 to 2153, none of which the pace images use. Stock placement is `sweetamp=0`; the test arm is `sweetamp_ui=47`.

1. The steering moves into the `choose_processor()` that is compiled: an idle core of the thread's class first, and an idle hint of the wrong class is dropped when one of the right class is free.
2. A new boot arg, `sweetamp_ui=N`: a thread at `base_pri >= N` that finds no idle big core preempts the big core running the lowest priority below its own, instead of taking an idle little core.

### 2144 measured: placement fixed, pacing not (ast123 to 126)

Same 2144 kernel in both arms. The payload is `lumatrace` with CPU numbers and the process CPU sampler.

| Run | Arm | Heavy on A520 | Encode 100-160 on A520 / A720 | Shown fps | 1 vsync | Flip rate | Steady |
|---|---|---:|---|---:|---:|---:|---:|
| ast123 | stock (`sweetamp=0`) | 56% | 5.3 / 2.3 ms | 43.1 | 70.3% | 0.31 | 42.8% |
| ast125 | stock | 59% | 5.5 / 2.8 ms | 41.0 | 64.9% | 0.31 | 41.1% |
| ast124 | `sweetamp_ui=47` | 25% | 5.0 / 2.3 ms | 41.1 | 65.9% | 0.29 | 45.1% |
| ast126 | `sweetamp_ui=47` | 24% | 5.2 / 2.7 ms | 41.9 | 67.5% | 0.36 | 34.4% |

**Proven:**

- The steering works. Heavy command buffers on the A520 cores fell from 56 to 59% to 24 to 25%.
- On-time frames encode faster: 1.9 against 2.5 ms in ast124.
- CoreAnimation started 67% of heavy frames within one vsync of the previous start in ast124, against 57 to 62% in ast101, 104 and 123.
- Panel pacing did not change beyond noise.
- 0 recoveries in all four runs.

**Inferred:** frames that start on time still reach the panel a vsync late when their GPU work (14 to 15 ms on heavy glass frames) ends after the tick.

**Proven, gate:** the next frame's start does not line up with any event of the previous frame. Relative to the previous GPU end, completion, handlers, swap, SwapWait return, tick or hold release, it spreads over several milliseconds (`/tmp/lag-gate2.py`, ast123 and ast124). The "render-bound" kind above is a timing coincidence, not a gate. **Inferred:** CoreAnimation's own pacing decides, at each vsync, whether to start a frame.

2144 is not kept for the daily image: it shows no pacing gain, and it is a scheduler change. The patch stays in `plans/lag/patches/` if placement matters later, for battery or for other work.

### UBWC: GPU time down by a third to a half, encode doubled (ast127)

`sweettu=sysmem,nocb,nolrz` turns on the hardware's lossless framebuffer compression (UBWC) and keeps sysmem. Turnip compresses only its private, optimally tiled images; IOSurfaces stay linear. **Proven, ast127:**

| Measure | sysmem, no UBWC (ast101 to 126) | UBWC (ast127) |
|---|---:|---:|
| GPU, 100 to 140 draws | 11.3 to 11.6 ms | **7.4 ms** |
| GPU, 140+ draws | 18.1 to 18.5 ms | **10.2 ms** |
| Encode, late heavy frames | about 5 ms | **11.0 ms** |
| Shown fps | 41 to 43 | 40.7 |
| 1 vsync | 65 to 70% | 64.2% |
| Flip rate | 0.29 to 0.36 | 0.30 |

0 recoveries.

**The reason encode doubled:** bgmtl turns its texture pool off unless `noubwc` is set (`bg_texpool_cap`), so every intermediate texture became a fresh kernel allocation. The rule exists because a pooled texture is zeroed on the CPU, which "only reads as clear without UBWC". But the zero covers the whole allocation, metadata included, so a pooled texture ends up byte for byte the same as a fresh one, UBWC or not. Most takes are also cleared on the GPU by their first render pass (`zero_kept`).

**Next, `lumaub`:** UBWC with the pool kept. Two hw runs for timing, and one no-hw run so the frames show whether UBWC itself changes anything visible.

### CoreAnimation pacing switches (batch 8)

`bgl-launch` (`plans/lag/arms/calat`) sets `CA_DISABLE_LOW_LATENCY=1` (`sweetcalat=1`) or `CA_DISABLE_SERVER_TIMER_EARLY_WAKEUP=1` (`sweetcalat=2`) for backboardd, one per boot. It is built from next-luma's launcher, which is the one on the root; without the boot arg it behaves the same.

**CoreAnimation pacing switches, no effect (ast128, 129).** `CA_DISABLE_LOW_LATENCY` gave 66.6% one vsync with flip rate 0.31; `CA_DISABLE_SERVER_TIMER_EARLY_WAKEUP` gave 66.2% with 0.32. 41 and 43% of heavy frames were late. The launcher does not log the variables it sets, so whether CoreAnimation read them is **Missing**. Dropped.

### UBWC with the texture pool kept: the best arm tonight (ast130 to 132)

`lumaub` is `lumatrace` with the pool kept under UBWC (`plans/lag/patches/bgmtl-texpool-with-ubwc.patch`), with `sweettu=sysmem,nocb,nolrz` on the combined kernel. **Proven:**

| Run | Arm | Shown fps | 1 vsync | Flip rate | Steady | Heavy late | GPU 100-140 / 140+ draws |
|---|---|---:|---:|---:|---:|---:|---|
| ast101 | A60 | 41.0 | 65.3% | 0.36 | 35.8% | 45% | 11.6 / 18.5 ms |
| ast104 | A60 | 42.2 | 67.9% | 0.31 | 42.0% | 43% | 11.4 / 18.2 ms |
| ast123 | S0 (2144 kernel, stock placement) | 43.1 | 70.3% | 0.31 | 42.8% | 41% | |
| ast125 | S0 | 41.0 | 64.9% | 0.31 | 41.1% | | |
| ast127 | UBWC, pool off | 40.7 | 64.2% | 0.30 | 42.8% | 47% | 7.4 / 10.2 ms |
| **ast130** | **UBWC + pool** | 42.3 | 69.5% | 0.34 | 35.3% | 35% | 7.4 / 10.8 ms |
| **ast132** | **UBWC + pool** | 43.5 | 72.4% | 0.27 | 49.6% | 32% | 7.8 / 10.9 ms |

- The pool works: 16935 hits against 6913 misses in ast130.
- Late heavy frames took 16.8 ms from start to GPU end, against about 22 ms before.
- The 1-vsync share has a median of 71.0% against 66.6% for the four baselines. Late heavy frames fell from 41 to 45% to 32 to 35%.
- ast131 (no hw) frames show the home pages, Settings, Control Center glass and blur, and the app switcher looking as before, with no icon or label tear.
- 0 recoveries in all three runs.

This is the first lever tonight that moves pacing beyond noise. The gain is modest, about 4 to 5 points of 1-vsync share.
