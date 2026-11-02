# Does the VIG0 hardware scaler crop or shift the picture?

## Result

By computation, patch 2053 does not crop, zoom or move the picture by a visible amount. Every source column and row reaches the panel. The whole image sits 0.143 panel px left of and above the CPU scaler's image, which nobody can see.

The canvas that the hardware scaler reads is also the same as on the CPU boot. In the same dylib state the green battery fill spans canvas columns 763..814 on both (ast42 hardware, ast43 CPU).

So nothing I can compute or measure explains a battery touching the right edge. A shift or crop of 33 panel px would need hardware behaviour that Linux's values do not predict, or a different dylib state than the one logged. No scan-out has been measured, and only a photo can settle that. Patch 2111 draws a chart for that photo.

I found one register value that differs from Linux: the vertical preload. Patch 2110 fixes it. A vertical preload cannot explain a horizontal 33 px, and I don't know what it does on this hardware beyond that, so I expect at most a row-level effect.

## Register comparison

Linux means `linux-ref/dpu_plane.c` and `dpu_hw_util.c` (v6.16) and `tear-work/dpu_hw_sspp.c`. The scaler base 0xa00 and version come from `dpu_vig_sblk_qseed3_3_3` in `linux-ref/dpu_hw_catalog.c`, and the phone confirms the base by reading 0x3003 at pipe+0xa00.

| Register | Patch 2053 writes | Linux | Same? |
|---|---|---|---|
| SSPP_SRC_SIZE 0x00 | 0x07440348, 840x1860 | source rect | yes |
| SSPP_SRC_XY 0x08 | not written, boot value 0 (log `xy=0/0`) | 0 | yes |
| SSPP_OUT_SIZE 0x0c | not written, boot value 0x09580438, 1080x2392 (log) | dest rect | yes |
| SSPP_OUT_XY 0x10 | not written, boot value 0 (log) | 0 | yes |
| 0x04 image size | 0x07440348 | not written | extra, same size as the source |
| SSPP_SRC0_ADDR 0x14 | slot base, no offset | fetch address | yes |
| YSTRIDE0 0x24 | 3392 (840 x 4 rounded up to 64) | plane pitch | yes, 8 padding px per row are never fetched |
| PE LR, TB 0x100.. | 0 | 0 | yes |
| PE REQ_PIXELS 0x108.. | 0x07440348 | source size | yes |
| QSEED OP 0x04 | 0xa0034401, reads back 0x20034401 | same bits | yes, see below |
| PHASE_STEP 0x10..0x1c | x 0x18e38e, y 0x18e208 | `mult_frac(1 << 21, src, dst)`, same numbers | yes, 21 fractional bits |
| PHASE_INIT 0x90..0x9c | 0 | 0 | yes |
| PRELOAD 0x20 | 0x03040304 | H 4, **V 2** for version >= 0x3000, so 0x02040204 | **no** |
| SRC_SIZE 0x40, 0x44 | 0x07440348 | source size | yes |
| DST_SIZE 0x48 | 0x09580438 | dest size | yes |

Other items:

- Multirect was never logged. 2053 does not touch it, and ABL stages one solid pipe, so I expect 0. 2111 logs it.
- The CTL layer 0x1000005 stages VIG0 and the border. The stage has no rect in Linux, because the only placement is SSPP_OUT. No boot-layout rect can remain there.
- The LM size was never logged either. 2111 logs it.

Bit 31 of the op value reads back 0. Linux writes that bit too. My inference is that the block ignores it for a bilinear filter, and it cannot change geometry.

## Exact pixel map

Host check: `plans/hwscale/test/qseed-check.c`. The hardware samples source position `i * step / 2^21`, starting at 0.

| Axis | Step | Sources used | Last output samples | Rounding drift |
|---|---|---|---|---|
| x, 840 to 1080 | 1631118 | 840 of 840 | 1079 -> 839.2221 | 0.0001 source px |
| y, 1860 to 2392 | 1630728 | 1860 of 1860 | 2391 -> 1859.2218 | 0.0006 source px |

No column or row is dropped, and the check asserts that. Output 0 samples source 0, and the last output is past the last source centre, so it blends in the replicated edge. That edge handling is the same as Linux's and I could not read it from a local spec.

The CPU scaler is centre aligned: `(i + 0.5) * 7/9 - 0.5`, so `i * 7/9 - 0.111`. The hardware map is 0.111 source px further along, which draws the picture 0.143 panel px left and up on both axes. I left it, because Linux also programs init phase 0 and 0.143 px is invisible.

## What the frames show

`kern.sweet_fb_shot` reads the canvas at panel stride, so its frames look wrong, but each sample still has an exact byte offset in the canvas. `destride.py` undoes that. It shows the canvas that goes into the scaler, not the scan-out. The columns are good to about 3 canvas px because the capture takes every third pixel.

| Run | Mode | Green fill, canvas columns | To right edge |
|---|---|---|---|
| ast38 | CPU | 744..796 | 21.5 pt |
| ast39 | hardware | 744..796 | 21.5 pt |
| ast42 | hardware | 763..814 | 12.5 pt |
| ast43 | CPU | 763..814 | 12.5 pt |
| ast44 | CPU | 777..826 | 6.5 pt |
| ast45 | CPU | 737..789 | 25 pt |

Rows match as well, 37 or 38 to 61. ast39 and ast42 are the only hardware runs in `ios-asteroids/logs`. The ast42 fill should appear 32 panel px from the edge if the map above holds. `measured/crop-ast42hw-over-ast43cpu.png` shows the two batteries stacked.

## Fixes

- `patches/2110-asteroids-qseed-preload-v2.patch` makes the vertical preload 2 on version 0x3000 and up. Older scalers keep 3. It touches only `sweetfb_ast_hw`.
- `patches/2111-asteroids-hw-test-chart.patch` adds `sweetfb_hwtest=SECONDS`, off by default, clamped to 120.

Both pass `git apply --check` on the tree, separately and in sequence on a copy. The patched file passes `-fsyntax-only` with the kernel's flags, with 0 errors (`kernel-syntax.log`). Neither patch has been compiled into a kernel or run on a phone. Both still have to be copied to `ios-asteroids/xnu/patches-12377`.

The chart replaces each frame from the first flip until the time is up. If iOS draws nothing after that, the chart stays until the next frame, so one touch clears it. A partial frame just after the end can keep bits of the chart until the next full one. 2111 also maps LM0 at 0xae45000 for one read. That address comes from the SDM845 define in this file, and I expect it on this SoC but have not seen it read here. A bus error would only hit a boot with the boot-arg set.

## How the owner verifies it

The test takes about 20 minutes after the kernel build.

1. Apply 2110 and 2111, then rebuild the kernel.
2. Add these lines to `plans/devloop/build-loop.sh`. The hardware line is 573 of 607 bytes.
   ```
   mk next-loop-hwtest "$no_syserr $loop $sb sweetfb_ast_hw=1 sweetfb_hwtest=60"
   mk next-loop-cputest "$no_syserr $loop $sb sweetfb_hwtest=60"
   ```
3. Boot `next-loop-hwtest` and photograph the whole screen straight on when the chart appears. Compare it with `plans/hwscale/expected-chart.png`.
4. Boot `next-loop-cputest` with the same dylibs and take the same photo.
5. Paste the three `SWEET FB HWTEST:` log lines from the hardware boot.

Read the photo at the middle of each edge, because the curved corners hide the corners only.

| What you see | Meaning |
|---|---|
| 4 stripes on all four edges, outermost flush with the glass, photos match | No crop or shift. The earlier sighting was not the scaler |
| Stripes missing on one edge, and a gap or wider stripes on the opposite edge | Shift. 1 stripe plus 1 black band is 8 canvas px, 10.3 panel px |
| Stripes missing on opposite edges | Zoom or crop |
| Square's sides unequal | Stretch. The square is 600x600 canvas px, 771 panel px |

A 33 px shift would be obvious, because it removes about 3 of the 4 green stripes. The cyan bar marks the ast43 battery fill edge, at canvas columns 814 and 815 in rows 37..61. It should sit 31 to 33 panel px from the right edge, inside the innermost green stripe.

Expected log values on the hardware boot:

- `sspp size=7440348 xy=0 out=9580438 outxy=0 stride=d40 multirect=0 excl=0/0`
- `qseed step=18e38e/18e208/18e38e/18e208 preload=2040204 src=7440348/7440348 dst=9580438 phase=0/0/0/0`
- `lm out=9580438`
