# Liquid Glass visual bugs

2026-11-02. Interim report: the search band is first because the owner asked for it, and it has a fix candidate. The other three bugs are not finished. No phone, adb, fastboot, commit or push. Nothing outside `plans/glass/` and this file was written. The patch has not run on a device.

## Zebra pattern when an app opens

Added 2026-11-02. No phone commands or commits. Project files were written only under `plans/glass/` and here. I also used a few scratch scripts in `/tmp` during both rounds and moved them to the Trash. `plans/glass/launch-stripes.patch` has not run on a device.

### Best lead

A full-screen ASTC 4x4 image is uploaded with the wrong row pitch, and that produces this pattern. The bug is proven and reproduced. That the owner's zebra is this image is inferred, because no capture has caught the zebra.

`chlTextureFor:` works out a no-copy image's pitch as length / rows (`next-fix/Chlorophyll/src/bgmtl.m`, the `_ncLength / rows` line). Every capture that binds a full-screen ASTC image (ast19, 20, 24, 25, 26, 27, 29) records the same texture: 840x1860, fmt 204, one level, `noCopyBytes=1572864`.

- The real data is 465 block rows of 3360 bytes, 1562400 bytes in all. The recorded length is that rounded up to 16 KB pages.
- The code computes 1572864 / 465 = 3382, so `chl_texture_replace` starts each row 22 bytes later than the last.
- ASTC blocks are 16 bytes, so only every eighth block row lands on a block boundary. That leaves one good band of 4 screen rows every 32 rows, and the rows between are misread as garbage or sheared.

### Reproduction

`plans/glass/repro/decode.m` takes a real 840x1860 ASTC image, encoded by `sips` from an ast33 Calendar screenshot. It applies the same row-copy loop and decodes on the Mac GPU. At 3360 the image is clean. At 3382 it has full-width lines every 32 rows over its whole width (`plans/glass/repro/compare.png`, which shows 3360, 3382 and 3376 side by side). The test image comes out upside down, which comes from the encoding and does not affect the pattern.

Apple's GPU shows bad blocks as magenta. I don't know what colour the Adreno gives. `ios-asteroids/NOTES.md:439` recorded "full-width black/white row stripes" in a Health launch on ast20, and a comment at the YCbCr logging in `bgmtl.m` recalls the same boot.

### Did it exist before the sampler patch?

Yes, by this evidence. The ast20 note predates `glass-sampler.patch`, and the same ASTC texture with the same length is in the ast19 and ast20 captures. The pitch line came in with the ast16 fix. `TimgA2Xhf_Ixrg`, the shader that samples this image, has no inline sampler in its AIR, and its sampler is a normal API one. So the sampler patch does not change how it is read. It may have made the stripes easier to notice, but I found nothing showing that.

### What is not proven

- No captured frame has stripes. 113 portrait targets from all captures and 34 device screenshots, including the new batch, score 0 on the detector. The selftest screenshots are taken about 8 seconds after opening, long past the launch image.
- In the captured frames this image does not reach the visible result. In ast26 CB1209 it is drawn into a 900x1864 offscreen pass and the wallpaper is then redrawn over it, so that dump looks clean. Whether it is the launch image is inferred from its size, its opaque shader and the owner's timing. Its pixels were never dumped because the capture skipped fmt 204.
- The true pitch could be 3376 instead of 3360, since both fit the page-rounded length. 3360 is the only tight value. The new capture dumps the caller's bytes, which settles it.

### Other launch paths checked

- SpringBoard creates a full-screen `420f` surface 8 log lines after `SELFTEST: open` in ast31 (`SWEET SURF: SpringBoard create id 52 840x1860 fmt 0x34323066 bpe 4 bpr 3392 planes 2`). It is logged in bgmtl as plane 0 and plane 1 with 848-byte rows. The decode in `chlTextureFor:` uses the plane pitches, so I found no stripe mechanism there. A surface still holding zero bytes would decode to green. This is the second lead, and the new capture dumps it.
- A plane of a two-plane surface wrapped as its own texture would go through the plain copy, which reads the base address at the top-level pitch (3392 for this surface). That would be wrong, but no capture shows such a texture. The patch logs it if it happens and does not fix it.
- Pool textures are not the source. `analysis/blackbars.md` shows targets are imported surfaces, and a dirty pool texture gives old pixels rather than regular lines.
- The refused imports (240x240 fmt 30, 210x465 fmt 80 with 864-byte rows, 740x440 fmt 80) go through the copy with the surface's own pitch, which is correct.

### Patch

`plans/glass/launch-stripes.patch` applies over the current `next-fix/`, with `patch -p1` or `git apply` from that directory.

- **Fix:** new `bg_nc_pitch` uses the tight pitch for compressed or mipmapped images. An uncompressed image keeps length / rows only when that divides exactly. The first 8 lengths that do not divide exactly go to bgmtl.log.
- **Trigger:** a third word in the capture file, `v2 0 launch`, turns off the periodic captures and arms two others, up to 8 per boot:
    1. A draw that first samples a portrait client image (no-copy bytes or a surface, at least 400 wide, not 16F, never rendered to). It saves the frame, the image, and for no-copy images the caller's raw bytes as `…-clientN.bin`.
    2. A stripe detector. It copies three 4x256 strips of each armed finished frame and fires when at least 12 rows step in luma across all three. It logs the rows and captures the next frame.
- **Off by default:** without the third word nothing changes, and without the file there is no capture work at all.
- **Tools:** `capture-convert.py` now accepts fmt 500 and 550, whose dumps are the stand-in BGRA.
- **Limit:** the detector looks at 256 rows at mid-screen, so a pattern with a period above about 40 rows gets too few steps to fire. Armed mode also costs trace work on every command buffer until the quota is used, which can change timing.

### Checks run

- `patch --dry-run -p1` on `next-fix/` and `git apply --check` on a copy both pass.
- `bgmtl.m` compiles for iOS arm64 with the build's flags, to `plans/glass/build/launch/bgmtl.o`, with one warning on a line the patch does not touch. It has not been linked into a dylib or signed.
- All 10 host checks in `test/` pass on the patched copy, including the new `launch-check.py`, which runs the real pitch and detector code. `flow.sh` and `policy_test` did not run, since they only cover the launcher.
- `plans/glass/test/stripe-real.py` runs the detector on real pixels: score 0 on 113 targets and 34 screenshots, 0 on the clean reproduction, 16 on the zebra reproduction, with 12 needed to fire.

### What the test will show

Put `v2 0 launch` in `/private/var/db/bgmtl/shatter.capture`, boot the patched build, open a few apps from cold, and pull with `next-fix/pull-capture.sh`.

- **Fix working:** no zebra, no `stripes cb=` lines, and `bgmtl.log` has `no-copy image 840x1860 fmt 204: length 1572864 is not rows x pitch, pitch 3360 (length / rows is 3382)`. A `launch capture cb=… client image 840x1860 fmt 204 … no-copy 1` line then ties this image to the launch.
- **Zebra still there:** `stripes cb=… first at +…` lines give the row positions and the next frame is captured. The cause is then something else. The `launch capture` lines and dumps name the images used around it, including the 420f surface.
- **Checking the pitch:** rename a captured `…-client0.bin` as the input to `plans/glass/repro/decode`, for example `decode FILE out 3360 3376`. The sharp output shows the real pitch and what the image is.

## Search band

### The draw

The band is the dock's glass, drawn a second time 256 pixels higher. It is not a failed search pill. The pill is a separate draw and it renders.

In `ast23-capture/shatter-C250…-64-1200.txt`, line 2091, the pipeline is `variable_blur_vert_lph+glass_background_no_bleed_lph`. This draw uses scissor `0,1454,840,406`, a full-screen quad from -16,-16 to 864,1888, and an 832x256 RGBA16F `sdf_texture` in slot 4.

### Intended shape and bounds

The vertex data places the 832x256 mask at x 31, y 1606. The next draw, which uses the same texture, has scissor `32,1607,776,221`, so the dock's shape is x 32 to 808, y 1607 to 1828. The search pill's own draws use a 192x128 mask at x 339, y 1506, with scissor `340,1507,160,64`.

CoreAnimation draws glass as a full-screen quad and relies on the SDF to limit it to the shape. The shader's inline sampler for that mask is word `0x7bff0000082a49`, which decodes to clamp_to_edge, linear, mip nearest. The dumped 192x128 mask has zero in R and A along its top row and left column, so a clamped read outside the texture gives zero coverage. The dock draw should therefore paint nothing above y 1607.

### Why it appears

The pixels behave as if that sampler repeats.

- `plans/glass/test/band-geometry.py` measures the band's last row at 1569 and the dock's at 1825, an offset of exactly 256, the mask's height.
- Over 46 rows the band's bottom-left corner matches the dock's shifted by 256, with a maximum error of 1 pixel.
- The band's hard top edge sits at y 1454, the top of the dock draw's scissor, where the mask's v is -0.594 and a clamped read would give nothing.
- Width wraps too. In ast24 CB900 the pill mask wrapped by 192 predicts 146 extra pixels at x 534 to 539, y 1522 to 1555. The target shows a step of 28.7 across the scissor edge on those rows and 5.8 on the other rows.

### Root cause

- `next-fix/Chlorophyll/air/chl_vk.c:2322` (`flush_bindings`) skips every binding with an immutable sampler, so no descriptor write is issued for it.
- `chl-work2/mesa-turnip26/mesa/src/freedreno/vulkan/tu_descriptor_set.cc:1377` copies an immutable sampler into a push set only when a write names that binding (`copy_immutable_samplers`).
- `tu_cmd_buffer.cc:5039` (`tu_push_descriptor_set`) has no other immutable handling. It copies `set->mapped_ptr` to the GPU, and that memory comes from `vk_realloc` and is never cleared.
- `tu_shader.cc:987` turns the sampler into a bindless handle into that set, so the shader reads those bytes as its sampler.

The shader therefore gets whatever is at that offset: never-written memory or leftovers from an earlier push with another layout. On this GPU `A6XX_TEX_REPEAT` is 0, so zero bytes mean repeat with nearest filtering.

**Proven:** the missing write and the driver code path from source; the wrap from captured pixels and vertex data.

**Inferred:** that the stale bytes are what produce the repeat. The bytes at that offset were not captured. Supporting signs are the stair-stepped, unblurred wallpaper edge inside the band and dock in the ast23 CB1200 target, which fits nearest filtering and a zero maximum LOD. The census also fits: 8 shader names across the two traces I checked use inline samplers, and the 4 repeat, nearest samplers among them are the only ones whose requested state equals a zeroed word, so those would look correct by accident.

### Fix candidate

`plans/glass/glass-sampler.patch` applies over `next-fix/`, with `patch -p1` or `git apply` from that directory. `flush_bindings` now issues a write for immutable sampler bindings, and its three arrays grow from 32 to 64 entries. That is the most it can need: `make_layout` caps other descriptors at 32, plus 16 inline samplers per stage. The added cost is one small sampler copy per inline sampler per draw. Blur, Liquid Glass, canvas and scaling are untouched.

Checks run:

- `patch --dry-run -p1` and `git apply --check` pass on a copy of the source.
- The patched `chl_vk.c` compiles for iOS arm64 with the build's own flags, to `plans/glass/build/obj/chl_vk.c.o`. It has not been linked into a library or signed.
- `python3 plans/glass/test/sampler-check.py plans/glass/build/b/Chlorophyll/air/chl_vk.c next-fix/Chlorophyll/air/chl_vk.c` passes. It checks source only.
- `plans/glass/venv/bin/python plans/glass/test/band-geometry.py` passes against the captures.

To build it, apply the patch to `next-fix/` and run the normal `build.sh` and `verify.py`.

### Risk

Every inline sampler now gets the state its shader asked for, including in `variable_blur_downsample_frag_lph`. Glass should look blurrier and smoother than in the captures, because it is probably reading only mip 0 today. Real LOD selection could expose problems the wrong sampler was hiding, such as a read of a mip attached in the same pass (see `analysis/capture-v2.md`). The owner should watch the blur as well as the band.

### What settles it

Boot the patched library with the capture on. The band should be gone, with the small pill at about x 340 to 500, y 1507 to 1571, and the dock's draw should paint nothing above y 1607. If the band survives, the stale-sampler link is wrong and the next capture has to read back the sampler bytes.

## Glass duplicated along its size

Best lead: same cause as the band, not separately proven. The band and pill slivers are this duplication at small scale, repeating at the mask's own size. A larger scissor or animating glass would show more copies as a grid. This fits the owner's description. Stale bytes change with draw order, which could explain why it comes and goes. Why it clears after idle is not explained. The periodic captures did not catch a full grid. In CB900 frames the old pixels stay on screen because CoreAnimation redraws only a damage rectangle (`ast24` CB900: x 416 to 608, y 384 to 1568, load action load), so one wrong frame can persist in the regions it does not redraw.

Fix status: covered by the same patch if the lead is right. Not verified.

## Grey flicker

Not finished. Captured targets do show flat grey (about 60,60,60) dock and band, in ast24 CB900 and CB1200 and ast23 process 7337 CB900, while ast23 process C250 CB1200 shows clear glass. In those ast24 frames the grey sits outside the damage rectangle, so it was painted by an earlier frame. I have not found that earlier frame. The garbage sampler is a candidate because the backdrop `source_texture` uses an inline sampler too. A black backdrop texture is the other candidate (`analysis/icon-tear.md` found black inputs). Neither is proven.

## Black bars

Not finished. In part this may be the same grey or black regions held by partial redraw: CB900 of both boots shows rectangular blocks with hard edges at damage boundaries, in the render target itself, so at least those are not a scan-out artifact. Whether they match what the owner saw is not established.

The kernel question is only partly answered. In `SweetFramebuffer.cpp:1378`, with `ast.enabled`, `gpuGateOpen` logs the hold expiry and returns false, so that path does not authorize showing an unfinished frame, and the message is true for `swapFired`. I have not traced the vsync copy path or whether a swap can scan out a surface whose copy is incomplete.

## Not done

- Capture trigger change (hold-a-button or sampled on glass passes): not written.
- The `SWEET A810: stalled fence` lines near the hold expiries: not examined.
- Mask dumps: the 832x256 dock mask was not dumped in any capture, so its shape is taken from the corner match and the next draw's scissor.

## Files

- `plans/glass/glass-sampler.patch`
- `plans/glass/test/band-geometry.py`, `plans/glass/test/sampler-check.py`
- `plans/glass/tools/`: `drawlist.py`, `drawdump.py`, `extract-air.py`, `montage.py`, `crop.py`, `f16.py`
- `plans/glass/drawlists/`, `plans/glass/frames/`, `plans/glass/air/`
- `plans/glass/build/b/` (patched source), `plans/glass/build/obj/chl_vk.c.o`
