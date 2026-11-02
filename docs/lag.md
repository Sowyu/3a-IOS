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
