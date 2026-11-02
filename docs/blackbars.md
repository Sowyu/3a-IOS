# Black bars during home page swipes

2026-11-02. No phone, adb, fastboot, commit or push. Nothing outside `plans/blackbars/` and this file was written. The evidence does not decide the cause, so the deliverable is a diagnostic. It has not run on a device.

## Result

I traced the kernel copy and flip path, and it cannot scan out a half-written frame. The MDP underrun bit never fired in five boots. None of the 5 unique final targets from the gpu16 root (ast26, ast27) contains a bar, though only 3 of them show the home screen. The bars caught before that were all in the render target, and the glass sampler patch may have removed them.

The cause of what the owner sees is therefore not established. If the report came from the gpu16 root, no capture has caught it yet. If it came from gpu15, the grey band below is the best match and is probably already fixed. I don't know which root the report came from.

## Display path: ruled out by reading the code

`SweetFramebuffer.cpp` line numbers are from the current tree. This boot uses `direct 0 early 0`, so `fEarly` is false and the swap timer never arms. All copying happens in `vsyncFired`, inline on the work loop (`:1285` to `:1303`).

- A slot is never written while scanned. `present` picks `back = front == 1 ? 2 : 1` (`:3955`), and `front` only changes after a confirmed latch (`:241`).
- The flip waits for the copy. `sweet_rows_run` blocks until `pending == 0` (`:3766`), so a scaler thread cannot finish after the flip however late it runs. A 69 ms copy only delays the flip.
- Late stores cannot leak past the flip. Helpers run `dsb sy` before reporting done (`:3715`), and `astAddress` runs `dsb sy` before the address write (`:162`), which covers the presenter's own band.
- The old slot is free when it is reused. `astWait` needs the flush bit clear and the frame counter past a value read after the flush (`:111`, `:168`). The line counter read right after is small (3 to 29 at first latch), so the panel is at the top of a new frame.
- No flip happens without a copy. `ast.repeated` is only a counter (`:1301`), and a repeated tick leaves the last flip on the panel.
- `clean=always` does not apply. It is the zero-copy mode's setting and `fZc` is off. The slots are mapped write-combined (`:2030`), so they need no cache maintenance.
- Slots that were never written are not shown. The first frame into each slot is a full copy, or a copy of the front when the layer does not cover the canvas (`:3960`).
- The deadline tick from 2051 only changes when the next tick is armed (`:1344`). It touches no slot, which fits bars being reported in ast23 without it.
- The stride and row ranges are fixed. The panel is 4320 bytes a row and the copy covers rows 0 to 2392. The line buffers are sized for four threads (`:856`).

Scaler bands are panel rows 0 to 797, 797 to 1594 and 1594 to 2392, which are canvas rows 0 to 620, 620 to 1240 and 1240 to 1860. No captured bar has an edge on those rows. That proves little, because captures are taken before the scaler.

The two black paths in the code are outside swipes. `blackout` needs `fPowerOff`, and `bzero` at `:3998` needs the first layerless swap after a wake.

### Underrun

An MDP underrun would draw a sharp horizontal bar and would get worse under memory load, so I checked it specifically. `astFlip` tests bit 26 of `0xae01014` on every flip (`:233`) and falls back with `counter/INTF1 underrun`. That message appears in none of the five logs, over at least 10,647 flips.

The bit should latch. Nothing clears the register, and all five first-latch lines read `irq=8000100` at varying line counts, so the vsync bit 27 stays set. `tear-work/dpu_7_2_sc7280.h:195` maps INTF_1 underrun to bit 26 and vsync to bit 27. That catalog is the nearest one in the tree, not this SoC's. The diagnostic also prints the per-INTF status at `+0x1c4` to close that gap.

### Remaining hole

The fence and hold logic holds up on paper. `chl_cmdbuf_commit` submits before the scheduled handler lets the render server swap, and its last barrier flushes GPU writes for the CPU (`chl_vk.c:2168`). `swap` snapshots the fence after that. What I can't check from here is whether CoreAnimation always honours the in-use count at surface state `+0x18`. If it rendered into a held surface, the copy would read a half-drawn frame. The diagnostic tests this directly.

## Render target: what the captures show

The tools ran over all 25 final targets (18 unique, since `ast25` repeats the ast24 UUID and `ast27` the ast26 one). `plans/blackbars/frames/final-targets.png` has the full sheet.

| Boots | Frames with a bar | What it is |
|---|---|---|
| ast23 to ast25 | C8C949FE CB300 to 900, F8B8F54D CB600 and 900, 5AD14386 CB300 | Flat grey 60,60,60 band at canvas y 1454 to 1569 (panel 1870 to 2019) |
| ast23, ast24 | C8C949FE CB300 to 900, F8B8F54D CB600 | Whole wallpaper black on the home screen |
| ast24, ast26 | F8B8F54D CB300, F61F14B8 CB300 | Whole wallpaper black on the lock screen |
| ast26, ast27 | none on the home screen | Flat grey measures 0.00 in the band and dock areas in all 5 unique frames |

- `ast24` F8B8F54D CB900 is the one clear mid-swipe frame with a bar (`plans/blackbars/frames/midswipe.png`). The page is offset, the band is flat grey on a bright wallpaper, and it has a hole at exactly x 416 to 607. That hole is this frame's scissor `416,384,192,1184`.
- This proves the damage mechanism: CoreAnimation loads the surface (`load=1`), repaints a scissor, and leaves bad pixels from earlier frames outside it with hard edges.
- The black blocks in `ast23` 089D0194 CB600 and CB1200 are the Astronomy icon, which is black, at two page offsets. They are not bars.
- The gpu16 frames include one with the page dots showing (F61F14B8 CB600), and it is clean. That is one sample, and captures run every 300 command buffers, so a one-frame bar would usually be missed.

**Band and the glass patch.** `next-fix/Chlorophyll/air/chl_vk.c` already contains `plans/glass/glass-sampler.patch`, because a dry run reports it as already applied. The source and libraries are dated 11:18, the gpu16 root 11:27, and ast26 11:49. The band is in 6 of the 11 earlier unique home frames and in none of the 3 after, which is suggestive and no more: 3 clean frames could happen by chance at that rate. That the gpu16 root holds this library is inferred from those dates, as I did not read the image.

**Black wallpaper is still unexplained.** It is caught only in the first captures of a boot, and it is still there on gpu16 at the lock screen. If it ever returned for one frame inside a damage rectangle, it would give hard-edged black blocks with damage-shaped edges. No capture shows that mix, so this is a candidate and nothing more.

**Pool textures are ruled out as the drawable.** Targets are imported IOSurfaces with stable ids (2 and 3, or 4 and 5, in a boot), and they do not come from the pool. `load forced to clear` is 0 in the last `frame cost` line of all five boots, against 1246 to 7868 `clear kept`. Those clears are on the intermediate textures, whose passes use load 2 or 0.

## Swipes and copy time

- The per-10 s lines can't isolate a swipe, since the log has no touch marker. They only separate active windows from quiet ones.
- Windows with dropped swaps have a median handler time of about 32 ms (ast24 31.6, ast25 31.7, ast26 31.4, ast27 32.2). Windows without drops, where a boot has any, take about 16 ms.
- The 69 ms maximum is not from a swipe. In ast27 it arrives on the same log line as `preserves` going from 1 to 2 (`handler_us 75404`). In ast25 the 65 ms maximum does the same. A preserve is a full read of the front slot through a slow mapping, on a swap whose layer does not cover the canvas.
- Copy length cannot cause a bar on its own, because nothing flips until the copy ends. Swipes may matter because they add damage and swaps, which is a render-side effect.

`python3 -I plans/blackbars/tools/log-windows.py <log>` reproduces these numbers.

## Diagnostic: patch 2090

`plans/blackbars/patches/2090-asteroids-bar-check.patch`, behind `sweetfb_barcheck=1`. It only reads, and with the arg off it costs a bool test per present.

- It samples 8 columns on every 16th panel row of the slot, right before the flip. The cost is about 1200 uncached reads, and `check_us` reports what it really takes.
- A row counts as dark when 3 adjacent samples are black or flat grey up to 64.
- 3 or more dark sampled rows make a bar, and each bar is checked against the same row of the source surface.
- It hashes 128 source samples before and after the copy, to catch a surface changing during its copy.
- It checks that the slot about to be written is not on the panel.

A bar under 48 panel rows or spanning under 3 columns is missed. The dock can register if it is flat grey, which is why lines only print when the set of bars changes.

Log lines, grep `SWEET FB BAR:`:

```
SWEET FB BAR: armed, every 16 rows, 8 columns
SWEET FB BAR: swap=N slot=1 rows=800-1600 px=ff000000 src=dark band_edges=797,1594 copy_us=14210
SWEET FB BAR: source changed during its copy swap=N surface=5 fence last=N
SWEET FB BAR: writing a scanned slot 1 src=e431c000 flush=1 line=12
SWEET FB BAR: checks=N hits=N src_dark=N slot_only=N src_moved=N scanned_writes=N check_us max=N irq=8000100 intf_irq=N
```

The swap line prints at most 256 times a boot, and the last line prints every 10 s.

How to read a boot where the owner saw bars:

| Log | Meaning | Next step |
|---|---|---|
| `src=dark` at the time of the bar | The surface itself holds the bar, so it is render side | Capture on that swap; `px` tells black from the 60,60,60 grey |
| `src=not-dark` | The copy broke the frame, display side | `rows` against `band_edges` names the thread |
| `src_moved` rising with bars | The GPU wrote the surface during its copy, so the hold is not honoured | Fix the hold or fence |
| `scanned_writes` above 0 | The trace above is wrong on this hardware | Recheck `astWait` |
| Bit 26 in `irq`, or bit 0 in `intf_irq` | Underrun; it will also trigger the fallback if bit 26 | Look at bus votes |
| All zero and a bar was still seen | It is past the slot (MDP, DSC, panel), or too thin to sample | Lower `SFS_BAR_STEP` |

`src_moved` counts only changes at sampled spots, so a small change can go uncounted.

## Boot line

`next-all` is 541 bytes. `plans/blackbars/patches/boot-barcheck.patch` adds a `next-bar` image at 560 of 607 bytes. With the loop's args it would be 583 bytes, but `build-loop.sh` needs the arg added by hand, and that is not in the patch.

## Checks run

- `git apply --check` passes on the current tree, and also on a copy with 2080, 2081 and 2082 applied.
- The patched `SweetFramebuffer.cpp` passes `-fsyntax-only` with the kernel's flags on both bases, with 0 errors (`plans/blackbars/kernel-syntax.log` is the current tree's run). It has not been built or linked into a kernel.
- `plans/blackbars/test/bar-test.c` passes against a patched copy of the header, since the patch is not applied in the tree. It covers a thread band, the glass band, a dark icon, a short bar, a saturated dark colour, a bar on the last row, more bars than slots, and the hash.
- `patch --dry-run -p1` of the boot patch passes in `next-fix/`.

No driver patch is included, because nothing new on the driver side is proven.

## Files

- `plans/blackbars/patches/2090-asteroids-bar-check.patch`
- `plans/blackbars/patches/boot-barcheck.patch`
- `plans/blackbars/test/bar-test.c`
- `plans/blackbars/tools/`: `scan_targets.py`, `black_rects.py`, `montage.py`, `log-windows.py`, `syntax-check.py`
- `plans/blackbars/frames/`: `final-targets.png`, `midswipe.png`, `ast27.png`, `ast23-089d-cb600-dark.png`
- `plans/blackbars/kernel-syntax.log`
