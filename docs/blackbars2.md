# Remaining black artifacts while the screen changes

2026-11-02. No phone, adb, fastboot, commit or push. Project files were written only under `plans/blackbars2/` and here. Two scratch scripts went in `/tmp` and are in the Trash. The fix has not run on a device.

## Result

The ast36 hits have three separate sources, and only one of them is still live.

| Hits of 276 | Source | Status |
|---|---|---|
| about 181 | Launch zebra from the 3382 pitch of `analysis/glass.md`, in Calendar, Safari, Notes and boot | Fix already in `next-fix`, which ast36 did not have |
| 86 | Calculator, whose UI is really black | Not a bug |
| 7 | The kernel copying a surface while CoreAnimation renders the next frame into it | **The owner's bars. Fixed by `plans/blackbars2/hold-wait.patch`** |
| 2 | Swap 1, the first black frame of the boot | Not a bug |

These counts come from the deltas of the 10 s counter lines. The split is approximate at boot, where two lines fall inside one window.

## Cause of the bars

In this display mode, nothing stops CoreAnimation rendering into a surface the kernel is still copying. CoreAnimation starts each partial frame by filling the damage rectangle with opaque black, so a copy that overlaps the render picks up black blocks with hard edges. They vanish once the screen stops changing, because no render is then running.

The evidence, link by link:

1. **Every bar outside launches lands on an overlap.** Swaps 141, 938, 1138, 2056 and 2411 all print `source changed during its copy` for the same swap. That is 5 of 5, against 44 such swaps in 2416 checks, or 21 in about 260 during the home swipes. Both home swipe hits are in this group.
2. **The fence proves the overlap.** Swaps 938 and 940 are both surface 6, and both copies end with `fence last=1888`. Nothing was submitted between those two lines, so frame 940's work was already submitted before the copy of 938 from that same surface ended. The log has three more pairs like this: 1640/1642, 1644/1646 and 2429/2431. This assumes frame 940 drew something, which its own moved source supports.
3. **The black is what CoreAnimation draws first.** In `ast23-capture` CB900 (`73371D6C…-64-900.txt`) the final pass has `surface=3 load=1`. Its first draws use `PBGRAXm_A2Xhf` with vertex colour (0,0,0,1) over 448,192 to 608,1216, the same rectangle as the frame's scissor. That is `ff000000` in the shape of the damage.
4. **The code allows it.** `swapWaitInner` returns 0 at once unless `fEarly` is set (`SweetFramebuffer.cpp`, `swapWaitInner`), and this boot has `early 0`. The 0235 comment in that file records that the iOS 18 `IOMFBFlipBook` recycles the surface of swap N-1 as soon as swap N reports complete. I did not repeat that disassembly on iOS 26.

`analysis/blackbars.md` left this as its remaining hole. The display side stays cleared, with `scanned_writes=0` and no underrun.

**Not proven:**

- Why CoreAnimation ignores the hold is not established. `docs/DISPLAY_HW.md` found no caller of `IOSurfaceIsInUse` on iOS 18, which fits ignoring it.
- The px value printed is the slot's, not the source's, so opaque black in the source is inferred from the capture and from the row check.
- Swap 141 is `src=not-dark` because the GPU painted those rows by the time the check read them. That is what an overlap should look like, but one line can't show it.
- At swap 940 the frame's own fence should already be done, so the write during that copy may belong to a still later frame, or the fence may pass before the writes land. The test below separates these.

## Launch zebra

The kernel samples every 16th panel row, so the 4-in-32 stripes alias. `tools/bar-read.py` predicts the runs from the pitch alone, with the bilinear scaler blending neighbouring rows: `16-128, 144-208, 224-288`. Swaps 15, 1401, 1613 and 1885 print exactly those rows, which is the last frame of each launch. The frames before them are the zoom animation and have other scales.

Calendar is white on this phone (`frames/11-com.apple.mobilecal.check.png`), so these black rows are not its content. This also answers the unknown in `analysis/glass.md`: invalid ASTC blocks come out as opaque black on the Adreno. It also ties the zebra to the launch for the first time, since no capture had caught it.

ast36 has no `no-copy image` lines in `bgmtl.log`, so it ran without the pitch fix. ast42 and ast44 have them. No run has combined the pitch fix with the bar check, so the fix is not yet proven in this measure.

## Candidates checked

| Candidate | Finding |
|---|---|
| Recycled or zeroed target with LOAD | Drawables are stable imported surfaces, outside the pool. `load forced to clear` only covers pool textures, so 0 says nothing about drawables. No zeroed drawable is needed to explain the hits. |
| Offscreen sampled before rendered | No hit needs it. It would show as a bar with no moved source, and the only such frames are swap 1 and Calculator. Thin cases below the sampling limit stay possible. |
| New surface shown before its first draw | Only swap 1 fits. The later surfaces (ids 37, 38, 46 to 48, 56, 57) have no hits at their creation. |
| `GPU hold expired` | 6 in the boot. The only one near a bar is 22 log lines before swap 15, which is a zebra frame. `gpuGateOpen` returns false on expiry, so no unfinished frame is authorised. |
| Kernel damage path | `preserves` copies the front slot, not black. `slot_only` is 2 of 276. Swap 141 is above, and swap 1881 is a launch frame whose first run has a different zebra phase. |
| Sampler defaults | Not involved in any hit here. `glass-sampler.patch` was already in this run. |

`IOSurfaceIsInUse` still reads the kernel's word on iOS 26. `tools/dsc-export.py` decodes `_IOSurfaceClientIsInUse` as `ldr x8,[x0,#0x78]; ldr x8,[x8,#0x18]; cmp; cset`, after a `ldrb w8,[x0,#0xdd]` branch to another path that I did not read. `ipsw` would not open this cache without its `.symbols` file.

## Fix

`plans/blackbars2/hold-wait.patch`, in `Chlorophyll/src/bgmtl.m`, is off unless `/usr/local/lib/bgmtl/holdwait` starts with `1`.

- `renderCommandEncoderWithDescriptor:` notes the canvas-sized IOSurfaces a command buffer renders into, in backboardd only.
- `commit` polls `IOSurfaceIsInUse` on them every 200 us before submitting, up to 50 ms.
- The GPU therefore cannot start on a surface until the kernel lets go of it.
- Four timeouts in a row skip the wait for 120 commits, so a stuck hold can't slow every frame.

There is no deadlock. The kernel's tick depends only on fences already submitted, and a newer swap drops an uncopied frame's hold anyway. Glass, blur and the canvas are untouched. The cost is time waiting in `commit`, reported as `wait_us`, and it occurs only where the overlap would have happened.

I put this in Chlorophyll instead of making SwapWait honest in the kernel for two reasons. It pushes without a reflash, and it does not depend on how iOS 26 CoreAnimation treats a busy answer.

The hold also covers the flip's latch wait, so a commit can wait a little longer than the copy needs. That is marked in the code, with releasing the hold after the copy as the upgrade if `wait_us` costs frame rate.

## Next run

About 5 minutes, using the existing kernel and boot image:

```sh
plans/devloop/dev.py plans/devloop/out/next-loop-bar-boot.img \
  /usr/local/lib/bgmtl/bgmtl-inject.dylib=plans/blackbars2/out/bgmtl-inject.dylib \
  /usr/local/lib/bgmtl/libchlorophyll.dylib=next-fix/out-metal/libchlorophyll.dylib \
  /usr/local/lib/bgmtl/holdwait=plans/blackbars2/holdwait \
  --note holdwait
python3 -I plans/blackbars2/tools/bar-read.py ios-asteroids/logs/astNN-holdwait.txt ios-asteroids/logs/astNN-holdwait/bgmtl.log
```

`bgmtl.log` should have `hold wait: on`, then every 10 s `hold wait: commits=N busy=N wait_us total=N max=N timeouts=N skipped=N`.

| Log | Meaning | Next step |
|---|---|---|
| `src_moved` near 0 (44 in ast36), no `mid-render` frames, `busy` above 0 | Confirmed and fixed | Check `frames/s by step` against home-swipes 34.9 in `summary.txt`. If it fell, release the hold after the copy |
| `src_moved` still high and `busy` above 0 | Waiting does not cover the writer: the fence passes before the writes land, or another path writes | Read the fence ordering in `sweet_a810_fence_passed` against the last cache flush |
| `busy=0` with `src_moved` high | `IOSurfaceIsInUse` does not see the hold, through the `+0xdd` branch | Read the other path, or read the state word directly |
| `timeouts` rising | A hold outlasts 50 ms | Look for a frozen copy or the display being off |
| No launch-class frames | The pitch fix holds in this measure | None. Calculator stays in `other`, since it is really black |
| Bars still seen with `src_moved` near 0 | A different cause, or under 48 rows | `other` rows name the frames. Capture on them with `v2 0 launch` |

The owner should also watch home swipes for bars directly, since the self-test only samples them.

## Checks run

- `patch --dry-run -p1` in `next-fix/` passes, and `git apply --check` passes on a copy.
- The patched `bgmtl.m` compiles for iOS arm64 with the build's flags and links into `plans/blackbars2/out/bgmtl-inject.dylib`, signed ad hoc. It links against the current `next-fix/out-metal` libraries and imports `_IOSurfaceIsInUse`.
- `plans/blackbars2/test/hold-check.py` passes. It compiles the real gate against a fake hold and covers a free surface, a release during the wait, the 50 ms limit, the rest after four misses, and the reset.
- `launch-check.py` passes on the patched copy. The other `next-fix` host checks did not run, because they don't cover this code.

skipped: blit encoders into the drawable, since no capture shows one. Add them if `src_moved` stays above 0 with `busy` above 0.

## Files

- `plans/blackbars2/hold-wait.patch`
- `plans/blackbars2/holdwait`
- `plans/blackbars2/out/bgmtl-inject.dylib`
- `plans/blackbars2/test/hold-check.py`
- `plans/blackbars2/tools/bar-read.py`
- `plans/blackbars2/tools/dsc-export.py`
- `plans/blackbars2/build/`, with the original and patched source and the object
