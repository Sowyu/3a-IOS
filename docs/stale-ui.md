# Stale UI after transitions

2026-11-03. One phone run so far, ast154 (`stale-UB`), run by the owner's queue; I did not use the phone. Labels: **Measured** means a run's own log or frame shows it, **Code** means I read it in the source, **Inferred** means my reading, **Missing** means no data yet.

## The reports

- Calendar: a button blurs the whole screen. After going back, the nav bar buttons stay blurred. Everything else is fine.
- Notes: opening a note shows three buttons at the top right. After leaving the note they stay on screen.
- Notes: "the ... icon just went blank".
- The stale parts stay until something redraws that area.
- The owner's A/B: it happens on combo-ub (UBWC and the texture pool, `next-luma-ub`) and on combo-keep (no UBWC, `next-luma`). Both have `sweetfb_keep=1 sweetfb_astwait=1` and hardware scale.

The owner's photos in the attachments from 2026-11-02 (Calculator with white glass buttons, Notes with grey buttons and an invisible clock) are the luma bug of `glass-flat.md`, fixed in `next-luma`. They are not this bug.

## Already in the logs: a blank glass button in every run

**Measured.** `ui.sh` opens Notes, taps the new-note button and shoots 4 s later, with the keyboard up. In every readable (no hw) run I checked, from ast40 to ast131, the top right of that frame holds one plain glass circle with no glyph and no tint: ast40, 41, 49, 50, 51, 52, 53, 62, 81, 84, 90, 95, 96, 97, 98, 99, 106, 110, 119, 121, 131. Hardware-scale runs scramble the frame, so they can't be judged.

**Measured.** It does not depend on keep or astwait: ast81 and ast96 had keep off, ast84 and ast95 had keep without astwait, ast90 and ast131 had both. ast40 to ast53 predate the staging pool and 2142. It also shows with GMEM (ast121) and UBWC (ast131).

**Inferred.** This is the owner's blank "..." icon. iOS 26 shows a tinted Done checkmark there while editing. Whether it is the same mechanism as the stale buttons is not proven. It is the one case the logs already reproduce every time.

## Display side: ruled out

The kernel copies CoreAnimation's whole surface into a scan-out slot on every swap it shows, so the display cannot keep part of an old frame.

- **Measured, ast131:** every swap has one layer, `a 0,0 840x1860 b 0,0 840x1860`, and `preserves=0` all boot. A preserve is the only path that keeps part of the previous slot (`SweetFramebuffer.cpp`, `present`, the `astCovered` branch).
- **Code:** a surface that is neither pooled nor wrapped is mapped `kIOMapWriteCombineCache` in the kernel (`SweetSurface.cpp`, `kernelAddress`). The copy reads DRAM, so no stale CPU cache line can be copied. The low pool, which is cacheable, exists only with zero-copy, which is off.
- **Code:** keep changes which complete frame a tick shows (`vsyncFired`, `fPrev`), never which part of it. A kept frame is a whole earlier frame, and the next tick that finds the newest done shows it. `fDirty` is cleared only by that flush.

So the stale pixels are inside the surface CoreAnimation rendered (**Inferred** from the three points). Keep, astwait and the hold path can still matter only through what CoreAnimation draws, not through what the kernel shows.

## Hypotheses

| # | Hypothesis | Status |
|---|---|---|
| 1 | Buffer age: CoreAnimation redraws only damage into a surface that holds something other than what it last rendered there | Open. Its kernel half is ruled out above. The render half needs the reopen test below |
| 1b | Keep or astwait as the cause | Unlikely for the blank button (present with keep off, measured). Not yet tested for the two stale-button reports |
| 2 | A glass or backdrop texture cached across frames and never refreshed | Open |
| 3 | A missing Metal feature for invalidation (shared event, fence, purgeable state) | Open, not read yet |
| 4 | A read of the render target before earlier writes land (backdrop copy after the pass that drew under it) | Open. **Code:** Chlorophyll puts `full_barrier` (all writes to all reads) at every render pass start and end and around every blit, so between passes this needs a driver bug, not a missing barrier |
| 5 | A client layer's pixels refreshed late: a surface copied instead of imported, or an import whose seed and flush logic misses an update | Open. **Code:** imports are cache-cleaned once per command buffer whatever the seed says; copied surfaces are refreshed per command buffer unless a write-back is pending (`chlTextureFor:`). **Measured, ast131:** the copied, not imported surfaces are 240x240 and 228x162 RG8, 210x465, 246x264 and 740x440 BGRA, and 3 full-screen 840x1860 BGRA |

The test that splits them: after the stale frame, go home and reopen the app. CoreAnimation then redraws the whole app.

- Stale pixels gone after the reopen: the surface held pixels CoreAnimation did not expect, so hypothesis 1 (render side) or 4.
- Stale pixels still there: a layer's source is stale, so hypothesis 2 or 5. The buttons are glass, so 2 first.

## ast154: repro and the reopen split

`stale-UB` (combo-ub without hw scale, root driver `next-luma-ub`), `--test staleui`. Self-test done in 364 s, root clean, 0 recoveries, 0 backboardd restarts, `preserves=0` all boot.

**Measured, Notes: repro.** Frame 01, Notes' list on a fresh launch, has the "..." glyph in its top-right glass button. Frame 02, the note open, has a blank glass circle there instead of the note's buttons. Frames 03 and 04, back on the list 2 s and 7 s later, have a blank circle where the "..." was, slightly larger than before. Notes 2 and 3 repeat it exactly (frames 05 to 10). The new-note frames at 4 s and 12 s (12, 13) show the same blank circle.

**Measured, the split.** Frame 11 goes home, reopens Notes and shoots 4 s later: the circle is still blank. The reopen animates the whole app from the home screen, so CoreAnimation redraws every pixel of the app. A buffer-age or partial-redraw fault would be gone after a full redraw. So hypotheses 1 (render side) and 4 are out for this case, and the fault is in what CoreAnimation draws on every frame. Frame 14 (new note, reopen) shows the home screen: Notes did not come forward within 4 s, so it says nothing.

**Measured, Calendar: no repro.** The list menu (a pull-down: Single Day, Multi Day, List), search, and "+" all opened and closed, and the nav bar is sharp in every closed and reopened frame (16 to 26, zoomed). The calendars button did nothing (27). The inbox sheet opened and my swipe did not close it (31 to 34). None of these is a full-screen blur, so the owner's button is still unknown. Side note for the "+" report: "+" opened the New event sheet here (frame 23).

**Not seen:** Notes' three buttons staying after leaving the note. In this run the note's buttons never drew in the first place (frame 02), which is the same family seen from the other side: the glass buttons are there, their glyphs are not, and the state does not recover.

**Inferred.** Because the blank survives a full redraw, either a layer's source is wrong (hypotheses 2 and 5), or the draw that puts the glyph on the glass produces nothing in this state on every frame (new hypothesis 6 below). The first frame has the glyph with the same driver, so the glyph pipeline works at least in the list's first state.

| # | After ast154 |
|---|---|
| 1 | Out for the Notes case: the blank survives a full redraw. The kernel half was already out |
| 1b | Keep and astwait: out for the blank button (ast81, ast96). `stale-0` tests the rest |
| 2 | Open, now a lead: the glyph sits on glass, and the button changes shape across the transition |
| 3 | Open |
| 4 | Out for the Notes case, same reason as 1 |
| 5 | Open. **Measured, ast154:** the only copied, not imported surfaces this boot are 240x240 RG8 (ids 167, 173, 176, 188). `write-back 0`, `stale refreshes stopped 0`, `load forced to clear 0`. Nothing in this boot's `bgmtl.log` names a refused draw, pass or dispatch |
| 6 | New: the glyph draw fails on every frame in the post-transition state, through a translation or ordering bug in its pipeline. Vibrant glyphs on glass use the `Bvcm` shaders, which read the destination (fetch), and `sweetfetchsync` changes how those reads are ordered |
| 7 | New: UIKit's own state never finishes the transition (an animation completion that never arrives), so the glyph is really hidden. It would look exactly like this. Nothing on the driver side tests it directly |

## Next runs

`plans/stale/build-stale.sh` now also builds two arms on `stale-UB`, and `ONLY="name ..."` builds only those:

- `stale-P0` adds `sweetpool=0`, which bgl-launch already reads: Chlorophyll's texture pool off, nothing pushed. Pooled textures come back zeroed and are cleared by their first pass, and an offscreen glyph cache would live in one.
- `stale-F0` sets `sweetfetchsync=0`. Labels may tear in it (`lag.md`), so judge only the glyph.

How to read them: the glyph back in `stale-P0` frames 03, 04 and 11 points at the pool. Back in `stale-F0` points at fetch ordering (hypothesis 6). Still blank in both, with `stale-0` also blank, leaves hypotheses 2, 3, 5 and 7. The next step then is a capture of the frame after the round trip, which needs a `shatter.capture` file on the root (a flash, since dev.py cannot create files), or a capture trigger in the driver.

## Repro

`plans/devloop/selftest/scripts/staleui.sh` (copy in `tools/devloop/selftest/scripts/`):

1. Notes: open the first note, go back, shoot at 2 s and 7 s, three times, then reopen and shoot.
2. Notes: new note, shoot at 4 s and 12 s, reopen and shoot.
3. Calendar: on a fresh Calendar each time, open and close the list, search, "+", calendars and inbox buttons, shooting open, 2 s and 7 s after closing, and after a reopen. I don't know which one is the owner's blur button, so it tries all five.

34 frames, about 5 minutes of body. The coordinates for Notes come from the spacing script. Calendar's come from ast131's frame. Host check: the body runs under `sh` with stub tools (34 shots in order), and `dev.py --print-script staleui` passes `sh -n`.

Images, `plans/stale/build-stale.sh` (copy in `tools/stale/`), all on `next-combined/kernel-daily2`, without hw scale, `sweetdeadman=900`:

- `stale-UB`: combo-ub's display args (keep, astwait, `sweettu=sysmem,nocb,nolrz`)
- `stale-K`: combo-keep's display args (keep, astwait, lead)
- `stale-0`: neither keep nor astwait

`sweetkeys_ms=5` is left out to fit 607 bytes. It sets key polling only.

## Missing

- A frame of the three Notes buttons staying, or of the blurred Calendar buttons.
- Whether no-hw reproduces what the owner saw on hw-scale images.
- The owner session log (log-before.txt of the first dev.py run after 14:43).
- Which Calendar button the owner meant.
