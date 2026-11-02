# Flat glass buttons and the invisible clock

2026-11-02, desk only. I did not touch the phone, adb, fastboot or dev.py, and I did not edit any driver tree. Files written: this report, `plans/glass/luma-compute.patch` and `plans/glass/test/luma-frames.py`. Scratch work was in `/tmp/gf`, moved to the Trash at the end. The patch is not applied and not built into anything on the phone. Liquid Glass and blur are untouched: the patch adds work, it removes none.

## Root cause

Chlorophyll drops CoreAnimation's two luma kernels, so the luma values that UIKit uses to pick light or dark glass are never computed. UIKit then styles the top-edge glass and the status bar from bytes nobody wrote.

- `plans/lag/next-keep/launch/bgl-launch.c:281` sets `BB_SKIPCOMPUTE_DEFAULT` to `"compute_average_luma,compute_sum_luma"`, and `:605` exports it as `BGMTL_SKIP_COMPUTE`.
- `plans/lag/next-keep/Chlorophyll/src/bgmtl.m:1641` marks both pipelines as skipped, and `:2691` returns from `dispatchThreadgroups:` without dispatching. The log line at `:2694` says it: "outputs NOT synthesized".
- `next-fix/` has the same lines.

The flat look itself is expected. Glass over a uniform white or black page has nothing to refract, so it shows as a plain disc. The bug is that the disc has the wrong appearance, light on black and dark on white, and the clock flips with it.

## Mechanism in iOS 26.6

I read the iOS 26.6 shared cache that is on this Mac at `/private/tmp/lagdsc`.

- QuartzCore has the backdrop luma feature: the `CABackdropLayer` keys `tracksLuma`, `lumaUpdateRate`, `lumaSubrect` and `allowsFilteredLuma`, the delegate callback `backdropLayer:didChangeLuma:`, and the kernels `compute_average_luma` and `compute_sum_luma`. It also has `CA_DISABLE_LUMA_COMPUTE` and a render path named `calculate-average-luma` and `calculate-average-luma-blit-surf`.
- UIKit has per-edge luma styling: `bridgedSetLumaUserInterfaceStyle:forEdge:`, `_scrollPocketLumaObserverDidUpdate:`, `bottomScrollPocketLuma`, `brightLumaThreshold`, `_hasInProcessStatusBarLumaTracking`, `_materialDidReceiveLuma:` and `_lumaUserInterfaceStyleSpringDuration`.

That matches the photos. The nav bar buttons and the status bar text share the top scroll-edge luma, so they go wrong together. The battery icon has fixed colours, so it looks normal.

The two kernels, from the AIR in `render-fix3/air-dumps/` and the dispatch log in `ast95-bars-Kbar-nohw/bgl-backboardd.log:133-149`, work like this:

1. `compute_average_luma` reads the backdrop texture in 64x16 threadgroups and writes one float4 partial sum per threadgroup to `[[buffer(2)]]`.
2. `compute_sum_luma` runs one thread over those partials, read through `[[buffer(1)]]` at the same offset. It writes the total as one float4 to `[[buffer(2)]]`, right after the partials.
3. The CPU then reads that float4. In ast95 the partials sit at offsets 0, 32 and 64 of a 256 KB buffer, and the totals at 16, 48, 80 and 112.

Nothing else writes that range, so the value the CPU reads is whatever the memory already held.

## Proven

- Every logged boot from ast3 to ast96 ran with both kernels skipped. Each `bgl-backboardd.log` has `BGMTL_SKIP_COMPUTE=compute_average_luma,compute_sum_luma` and "its dispatches are SKIPPED" for both pipelines. That includes ast96.
- The self-test's own screenshots show the bug, and `plans/glass/test/luma-frames.py` measures it. Only the nohw runs count, because hw runs' screenshots are scrambled:

| Run | Calculator top buttons, want dark | Notes buttons, want light | Notes clock, want dark |
|---|---|---|---|
| ast81 | 28, right | 252, right | right |
| ast84 | 215, wrong | 117, wrong | white, wrong |
| ast90 | 215, wrong | 252, right | right |
| ast95 | 215, wrong | 252, right | right |
| ast96 | 215, wrong | 117, wrong | white, wrong |

- ast96 is tonight's run, the one in the owner's photos. Its Notes frame shows "30 Notes", as the photo does. ast95 shows 29, and every run adds one note.
- The appearance is wrong in both directions within one boot. In ast84 and ast96 Calculator gets light glass and Notes gets dark glass. A broken backdrop texture cannot do that, because both apps' glass draws correctly apart from the style. Hypothesis 2, a black backdrop, is not needed to explain these photos.
- The skip was added for hangs in ast9 and ast10. ast10b hung with the kernels already skipped. 3a-IOS/docs/freeform.md records that no decoded hang dump since kernel 2063 has a compute IB. The reason for the skip was never shown.
- Both kernels translate with the host build of the phone's translator, `plans/freeform/work/host/air2nir2 --report`: 199 and 78 NIR instructions, with the threadgroup memory, barriers and `read_texture_2d`. 64x16 = 1024 threads fits the A810's limit: threadsize 64 times 16 waves, no double threadsize, from `freedreno_devices.py` and `tu_device.cc:1226`. The 16 KB of threadgroup memory fits in 32 KB.
- `setBufferOffset:` is implemented in the compute encoder, `bgmtl.m:2666`. freeform.md names its absence before 2026-11-01 as the likely cause of the ast20 "garbage" contrast, the one time the kernels ran.

## Inferred

- Why the stale value differs between boots and apps. The 256 KB result buffer is the same size as CoreAnimation's constants ring, so it probably comes from a pool and holds leftover bytes. UIKit thresholds the value into one of two styles, which is why ast84 and ast96 are pixel-identical. I did not read the buffer's bytes.
- That the owner's "glass flicks white in places" is the same thing: luma updates read different leftover bytes at different offsets.
- That the grey 60,60,60 dock and band in glass.md's "Not finished" section may be the dark luma style on the home screen. Not checked.

## Unknown

- Whether CoreAnimation's result buffer is a no-copy buffer. If it is, the GPU writes only Chlorophyll's copy, `bgmtl.m:1844` says so, and running the kernels alone would change nothing. The patch handles both cases and logs which one it is.
- Whether the ir3 compute output is numerically right on the A810. Nothing on this GPU has run these kernels with offsets working.

Hypothesis 1 asked about generateMipmaps, blit readbacks and small getBytes. CoreAnimation does not use those for luma here. It uses the compute path, because nothing sets `CA_DISABLE_LUMA_COMPUTE`. I did not trace the render fallback path.

## The fix: `plans/glass/luma-compute.patch`

It applies to `plans/lag/next-keep/` and to `next-fix/` with `patch -p1`. I dry-ran it on copies of both, and `git apply --check` passes.

1. `launch/bgl-launch.c`: the default skip list is empty, so both kernels run. Writing `compute_average_luma,compute_sum_luma` into `bgmtl-bb.skipcompute` brings the skip back without a rebuild.
2. `Chlorophyll/src/bgmtl.m`: after each luma dispatch, the command buffer records the destination range. At completion, before any handler runs, `bgLumaFinish:` copies those bytes from GPU memory into the caller's memory if the buffer is no-copy, and only those bytes, because the rest of a ring may already hold newer CPU data.
3. The same function logs `luma: result R G B A at OFFSET, no-copy|own buffer, completed`. It logs the first 64 results, then one every 2 s.

Checks run: both files compile for iOS arm64 with `build.sh`'s flags, into `/tmp`. The only warning is one the unpatched file already has. The launcher's host flow test, `test/flow.sh`, passes on the patched launcher. Not run: `build.sh`, `verify.py` and the full host check suite, since they write into the tree.

Risk: the kernels now run on every luma update, which is a few small dispatches a second. If ir3 compute is broken on the A810, the result is wrong values or a GPU hang. A wrong value is no worse than today. A hang is caught by the dead-man, and the summary's recoveries count shows it.

## Smallest phone test

One hands-free nohw run on a copy of the current arm with the patch applied. The nohw image is needed because hw screenshots are scrambled.

1. Copy the tree of the arm the phone runs now to a new directory, apply the patch there, and run its `build.sh`.
2. Run `python3 3a-IOS/tools/devloop/dev.py plans/lag/out/lag-kbar-nohw-boot.img /usr/local/lib/bgmtl/bgmtl-inject.dylib=COPY/out-metal/bgmtl-inject.dylib /usr/local/lib/bgmtl/libchlorophyll.dylib=COPY/out-metal/libchlorophyll.dylib /usr/local/lib/bergamota-gl/bgl-launch=COPY/out-metal/bgl-launch --test ui --note luma1-nohw`. Use the arm's own image if that is not `lag-kbar-nohw`.
3. Run `plans/glass/venv/bin/python -I plans/glass/test/luma-frames.py ios-asteroids/logs/ast*-luma1-nohw`.
4. Grep the run's `bgl-backboardd.log`.

What to grep for:

- `grep "Metal mode" bgl-backboardd.log | tail -1` should show `BGMTL_SKIP_COMPUTE=,`, and `grep -c "SKIPPED (BGMTL_SKIP" ` should show no new lines for this boot.
- `grep "luma: result" bgl-backboardd.log` gives the values and whether the buffer is no-copy. They should change with the screen: low behind Calculator's black, high behind Notes' white. Values that never change, NaN, or `NOT completed` mean the kernels are wrong on this GPU.
- `grep "dispatch skipped:" bgl-backboardd.log` should find nothing. That is the line Chlorophyll writes when it refuses a dispatch.
- `summary.txt` recoveries should stay at 0, as in ast95.

How to read it:

- Proven: `luma-frames.py` says ok for both apps, and the luma lines move with the screen.
- Kernels right but the result unread: the luma lines move but the frames are still wrong. Then CoreAnimation reads somewhere else, and the next step is tracing `backdropLayer:didChangeLuma:`.
- Disproven: the frames are still wrong and the luma values are sane and moving, with "completed" and an own buffer. Then the style comes from something other than this luma.
