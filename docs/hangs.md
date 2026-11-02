# GPU hangs on the A810: the kernel splits a render pass across two frames

2026-11-02. Worked from logs and source only. Nothing ran on the phone, and the fix is **untested on a device**. Paths are relative to `EverIOS/`, and `K/` is `ios-asteroids/research/xnu-local/dxb/xnu/`. No change touches Liquid Glass, blur or the canvas.

## Next steps

1. Apply the patch in `K/` with `git apply plans/hangs/2100-asteroids-a810-one-frame-per-submission.patch`, then build the kernel (about 45 minutes by the `gpu-next.md` figure).
2. Run `plans/hangs/build-variants.sh KERNEL` (a minute or two, not timed).
3. Run the variant list below. Start with `plans/devloop/dev.py plans/hangs/out/hang-fix-boot.img --test ui --note fix1` (5 minutes).
4. Read the result with `python3 -I plans/hangs/tools/check_next_run.py astNN-fix1.txt` from `ios-asteroids/logs`.

## The finding

Every hang in the logs is a submission with two IBs, and it hangs in the second one.

Turnip's main command stream starts in a 4096-dword BO. A render pass takes about 140 dwords of it, so a command buffer with about 29 passes fills the BO, and Turnip continues the stream in a second IB. Both IBs are one continuous stream, and Turnip passes both to the kernel as `nr_cmds = 2`.

`sweet_a810_msm_submit` then gives each IB its own frame and fence. The frame between the two halves ends by setting `CP_SET_THREAD_BOTH`. The cut usually falls after the pass's `CP_THREAD_CONTROL(BR | CONCURRENT_BIN_DISABLE)` and before its IB2 call. So the first pass of the second IB runs its draws with BV active as well as BR, a state Turnip never produces itself.

This also explains the old puzzles:

- "CP stuck at first IB2" is the only pass in the IB that lacks its thread packet, because the next pass's own packet restores BR.
- "The ring held only this one submission" and "fence one behind" are true because the first half finished and its fence passed.
- "Up to 29 IB2s" in `plan-stability.md` is the number of passes that fit before the cut.
- Hangs follow pass-heavy frames such as launches, and idle and home swipes have none.

## What is proven and what is inferred

**Proven from the logs:**

- 75 of 75 hung submissions with a history had 2 IBs (57 of 57 in the seven runs). Only 4 of the 525 submissions before them did, Fisher p about 2e-91. The history comes from kernel 2063, so older logs can't add to this count.
- The first IB of a hung submission is nearly a full BO: 4087 to 4096 dwords in 46 hangs, and 8190 to 8192 in 11.
- 61 of 61 dumps from ast7 to ast46 have BR-IB2 and BV-IB2 pointing at an IB2 that comes before the first `CP_THREAD_CONTROL` of the dumped IB. That covers both of the old exceptions (ast17, ast36), whose passes have two IB2s ahead of the thread packet.
- The dumped IB often begins mid-sequence, on `GRAS_A2D_SCISSOR_TL`, `RB_CNTL` or `CP_REG_TEST`, which shows the cut lands inside pass setup.

**Proven from code:**

- `K/bsd/dev/sweet_a810_msm.c:328` loops over IBs and calls `sweet_a810_submit_ib` for each.
- `K/bsd/dev/sweet_a810_cp.c` `emit_framed_fence` and `emit_prologue` put two BR/BV changes, flushes, a wait-for-idle and a final `BOTH` around every IB.
- Linux's `a7xx_submit` loops `CP_INDIRECT_BUFFER` inside one frame (`gpu-fix2/linux/a6xx_gpu.c:534`). A8xx uses that function.
- Upstream Mesa has the same 4096 start size (`gpu-fix2/mesa-main/.../tu_cmd_buffer.cc:4279`), so Turnip relies on the kernel keeping IBs in one frame. The bug is ours.

**Inferred, and only a phone run can settle it:**

- That BV running those draws is what wedges the GPU. The registers show BV entered that IB2, and I can't see inside the pipe.
- The frame in the gap also adds flushes, a fence and IFPC markers between the halves. The patch removes all of them, so success would prove the split, not which part of it.

**Unknown:**

- Why about 44% of splits hang and the rest don't. Splits landing between a pass's IB2 and the next pass's thread packet should be harmless, but the logs have no dump of a surviving split. 2100's new log line records each one, so the control run can test this.
- The exact stuck packet. IB2 fetch is complete (`rem 0`), and the IB1 position is the prefetch window (capped at dword 384), so the registers show fetch position only.

## Question 1: where the hangs are

Pooled over ast34 and ast36 to ast41: 57 recoveries in 20615 command buffers, 2.76 per 1000. They cost 16.6 s of freeze per run, 117 s in total. All 57 show in `data/hung_list.txt`.

| Step | Cbufs (est) | Hangs | Per 1000 | Expected | p |
|---|---:|---:|---:|---:|---:|
| boot, before `idle` | 2858 | 7 | 2.45 | 7.9 | 1 |
| idle | 1359 | 0 | 0 | 3.8 | 0.06 |
| home-swipes | 2756 | 0 | 0 | 7.6 | 0.0009 |
| apps | 5074 | 27 | 5.32 | 14.0 | 0.002 |
| keyboard-safari | 1882 | 4 | 2.13 | 5.2 | 0.83 |
| keyboard-notes | 2233 | 14 | 6.27 | 6.2 | 0.007 |
| control-center | 2191 | 4 | 1.83 | 6.1 | 0.54 |
| spotlight | 1261 | 1 | 0.79 | 3.5 | 0.28 |
| app-switcher | 215 | 0 | 0 | 0.6 | 1 |
| settle | 18 | 0 | 0 | 0.1 | 1 |

- A hang's step is where its hung submission's time lands. The per-step cbuf split is an estimate from perf windows and sums to 19848 of 20615. Totals per run are exact to the last `frame cost` line.
- With ten steps tested, apps and home-swipes clear a 0.005 threshold. keyboard-notes is just past it, and the rest can't be told from the pooled rate.
- Each of the seven runs hangs once during boot, before the `idle` step, at a fence from 112 to 443.

**Settings do not differ beyond chance.** Per 1000: sb 2.22, bar 3.11, amp 2.19, keep 2.55, hw 3.81, 120 2.76, thermal 2.86. Chi-square 1.88 on 6 degrees of freedom, against 12.59 at 5%. With about 8 hangs a run, a setting would need to change the rate about 2.5 times to show in one run.

**Pipeline and pass kinds.** Hang windows have a median 61.8 draws per cbuf against 23.3 over all windows, which fits a pass count effect. No shader stands out: slow builds near hangs are 5 different names once each. A slow build or a 16th-pipeline line falls in or just before 19 of 59 hang windows, against 164 of 1075 other windows (p = 0.0016). So launches correlate, as both compile and render many passes, but 40 of 59 had none. `bgmtl.log` appends across boots, so only its last session counts here. `hang_vs_pipeline.py` reads the whole file and needs that fix before reuse.

## Question 2: what the IBs hold

From `data/dump_shape.txt` and `data/ib2_contents.txt`, 28 dumps in the seven runs, the first four of each boot:

- **Kind:** graphics in all 28. 0 `CP_EXEC_CS`, 0 `CP_BLIT` in the IB2s.
- **First IB2 size:** small, most often 419 or 383 dwords holding 2 draws. Range 137 to 1626 dwords, 1 to 17 draws.
- **Last packet fetched:** `CP_DRAW_INDX_OFFSET` in 28 of 28, because an IB2 ends in a draw. It says nothing beyond that.
- **Hang classes:** 46 with the hang bit (INT `0x900000`, freeze median 1.6 s) and 11 without (INT `0x100000`, RBBM mostly `0xec0425` or `0xfe0425`, freeze median 5.2 s, BR and BV `rem` both 0). Both classes are 2-IB submissions, so `0xec0405` and `0xec0425` are two ends of one bug. `gpu-next.md` showed the second class as BR waiting at `CP_WAIT_TIMESTAMP` on a BV fence that never arrives.
- **BV pointers** differ from BR's in 11 of 57 hangs, so BV is sometimes behind BR in the same IB.

Tested causes:

| Candidate | Verdict | Evidence |
|---|---|---|
| Two IBs split by a frame | **Common to all** | 75 of 75 against 4 of 525 |
| Fetch sync's WFI and `WAIT_FOR_ME` | Not required | 15 of 28 hung IB2s hold none; ast7 to ast22 with sync off have the same shape, 13 of 13 dumps |
| First use of a pipeline | Not required | 40 of 59 hang windows have no build line |
| SMMU faults | Not required | 0 faults in all seven runs, with 57 hangs |
| Private BO memory attributes | No sign | `no longer write-combined` 0 times in every log |
| Drained ring | No | 33 of 57 hung submissions found work still in the ring. The others are the reverse: 370 of 399 found it drained, so hanging on a drained ring is less common than usual |
| Client change | No | 0 of 57 |

## Question 3: is the sync sequence a cause?

No, not on this evidence. It sits in `draw_cs`, which is IB2, so it barely adds to the IB1 that overflows.

| Group | Hangs / extra IBs | Share | Cbuf rate per 1000 |
|---|---:|---:|---:|
| Sync off, manual, ast18 to ast22 | 6 / 91 | 7% | ast22: 0.27 |
| Sync on, manual, ast24, ast25, ast27 | 7 / 57 | 12% | ast24: 0.58 |
| Sync on, script, the seven runs | 56 / 128 | 44% | 2.76 |

- Extra IBs are fence minus submissions minus the 5 at startup, an upper bound. `data/hang_given_split.txt` has every log.
- ast22's lower cbuf rate is real, but it does not implicate the sync. Sync off against sync on in manual use is p = 0.25. The jump is between manual and script, p = 2e-5.
- **Samples cannot decide** whether the script itself or the sync plus script raises the share, because no sync-off run on the script exists. Variant 4 fills that gap, and I can't say why the script is so much more lethal.

Cheaper forms, for frame time rather than hangs:

- `chl-fetch-sync-color.patch` still passes `patch --dry-run` in `next-fix`. Try it only after hangs are settled, with a tear check, because only the full set has been shown to clear the tear.
- A by-region barrier gains nothing here. `speed2.md` traced it to the same sysmem flushes through `tu_flush_for_access`.

Keep `sweetfetchsync=1` until variant 3 passes.

## Question 4: the fix and the runs

**`2100-asteroids-a810-one-frame-per-submission.patch`**

- `sweet_a810_submit_ib` becomes `sweet_a810_submit_ibs`, which puts every IB in one frame with one fence, as Linux does.
- `sweetibsplit=1` keeps the old framing on the same kernel, for a control run.
- A new line `A810 submission of N IBs, fence F, first D dwords, split S` logs up to 64 several-IB submissions per boot. The 1024-submission line gains `, N with several IBs`.

Status:

- `git apply --check` passes in `K/` on the current tree, with 2059 and 2063 already there.
- `tests/compile_check.py` compiles `sweet_a810.c` and `sweet_a810_msm.c` with the build's flags.
- `tests/frame_check.py` runs the real `sweet_a810_cp.c` on the host. Default gives 2 IBs, 0 thread packets between them, 1 fence. Its control, `sweetibsplit=1`, reproduces `BOTH` between the IBs and 2 fences.

The ring is 8192 dwords, so 128 IBs (512 dwords) fit in one frame. No queued patch under `plans/` touches `sweet_a810_submit_ib` or that log line.

`chl-cmd-cs-16k.patch` is a fallback only. It starts the stream's BO at 16384 dwords, so the stream fits in one IB on any kernel, largest seen being 9089 dwords. It passes `patch --dry-run` in `next-fix`, and I did not compile it, since it changes one number and a comment. It adds 48 KB per command buffer and costs more per prologue allocation, so don't ship it with 2100.

### Variants, in order

Base line for all is `next-loop-sb`: `build-variants.sh --lengths` gives 538 of 607 bytes, and only `hang-ctl` adds to it, at 553. Each run takes 5 minutes.

| # | Image | Boot arg change | Runs | Expected |
|---|---|---|---:|---|
| 1 | `hang-fix` | none, kernel with 2100 | 2 | 0 recoveries, or only with `cmds 1` |
| 2 | `hang-ctl` | `sweetibsplit=1` | 1 | 6 to 10 recoveries, all 2 IBs |
| 3 | `hang-sync2` | `sweetfetchsync=2`, needs a root with `chl-fetch-sync-color.patch` | 1 | No hang change, tear frames checked |
| 4 | `hang-sync0` | `sweetfetchsync=0` | 1 | Tear returns, only to bound sync's cost and hangs |
| 5 | current `next-loop-sb` | old kernel, `libchlorophyll` with `chl-cmd-cs-16k.patch` | 1 | Only if 2100 misbehaves |

Command for each: `plans/devloop/dev.py plans/hangs/out/IMAGE-boot.img --test ui --note NOTE`. `sweetfetchsync=2` without the patched `bgl-launch` silently falls back to 0, so don't boot `hang-sync2` on the old root. Variant 3 needs a root rebuild (20 minutes) and invalidates the shader cache once.

### What the logs should show

Variant 1:

- `recoveries 0` in `summary.txt`. The base rate is 8.1 a run, so 1 or fewer in a run has Poisson p of about 0.3%, and 2 or fewer in two runs about 1e-5.
- Excess of fence over submissions stays at 5 through the run, where it reached 20 to 27 before.
- 15 to 25 `A810 submission of 2 IBs ... split 0` lines, with no recovery following any of them.
- `SWEET FB AST: GPU hold expired` should drop from 6 to 13 a run toward 0.
- About 16 s a run back, so frames/s in `apps` and `keyboard-notes` should rise. I can't size that from these logs.
- Icon tear still absent, since `sweetfetchsync=1` is unchanged.

How to read other results:

- Variant 1 clean and variant 2 back at 6 to 10: the cause is confirmed on one kernel.
- Variant 1 with hangs on `cmds 1`: a second cause, now visible alone. Decode with `plans/gpu-next/tools/decode_hangs.py`.
- Variant 1 with hangs still on `cmds 2`: 2100 did not do what `frame_check.py` says. Run variant 5 to test the split with no kernel change.
- Variant 2's split lines also tell which first-IB sizes survive, which answers the 44% question.

## Files

All under `plans/hangs/` except this page.

- `2100-asteroids-a810-one-frame-per-submission.patch` is the fix.
- `chl-cmd-cs-16k.patch` is the fallback.
- `build-variants.sh` builds the images and has `--lengths`.
- `tools/parse_hangs.py`, `dump_shape.py`, `ib2_contents.py`, `step_rates.py` and `all_logs.py` produce the numbers here.
- `tools/check_next_run.py` reads the next run.
- `tests/compile_check.py` and `tests/frame_check.py` are the checks.
- `data/` has their saved output and one decoded log, `decoded-ast34.txt`.
- `work/make_patches.py` regenerates both patches.
- `SHA256.txt` has the hashes.

skipped: decoding BV's internal state and a full surviving-split dump, add them if variant 2 leaves the 44% unexplained.
