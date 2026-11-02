# GPU next: BO growth and launch hangs

2026-11-02. Worked from logs and source only. Nothing ran on the phone, and every fix here is **untested on a device**. Paths are relative to `EverIOS/`, and `K/` is `ios-asteroids/research/xnu-local/dxb/xnu/`.

## Next steps

1. Apply `plans/gpu-next/chlorophyll-prologue-free.patch` in `next-fix/Chlorophyll` and `next-fix-readback-check.patch` in `next-fix`, then rebuild the root (20 min). `build.sh` fails its `readback-check.py` without the second patch.
2. Apply `2059` then `2063` in the kernel tree and build the kernel (45 min).
3. Add `sweetgpufast=1000` to `next-all`. That fills the boot line exactly (607 of 607 bytes), so nothing else fits beside it.
4. Use the phone for 15 minutes, opening the Safari keyboard three times, then read the log for the checks under "What the next log should show".

Nothing here touches blur, Liquid Glass or the 840x1860 canvas.

## Problem 1: BO growth

### Root cause

**Proven from code:** prologue command buffers are never freed. `pro_begin` allocates a new `VkCommandBuffer` from `pro_pool` for every prologue (`next-fix/Chlorophyll/air/chl_vk.c:510`). `pro_retire_slot` destroys the fence and drops the holds, but leaves the command buffer (`chl_vk.c:530`). `vkFreeCommandBuffers` is not even in the function table, and `pro_pool` is never reset (the only `vkResetCommandPool` is on a per-command-buffer pool at `chl_vk.c:2128`).

**Proven from code:** each of those keeps at least one BO on the list. `tu_BeginCommandBuffer` writes `tu_init_hw` into `cmd_buffer->cs` (`mesa/src/freedreno/vulkan/tu_cmd_buffer.cc:4400`), which allocates a 16 KB command stream BO through `tu_cs_add_bo`. Only `tu_cs_finish` or `tu_cs_reset` can release it, and neither runs without a free. Turnip names every live BO on each submission.

A prologue opens whenever a command buffer's window made a texture, zeroed one or uploaded, and `run_now` makes more. That is why the rate looks like one per command buffer and is not tied to them.

### The rate, checked

Your numbers hold, with one correction. Line 12135 also has a 2576 that its neighbour's text hides. I joined the kernel and client logs through `gpu_holds=`, which both print.

| BOs named | Kernel fence | Backboardd cbufs (range) | Fence minus cbufs |
|---:|---:|---:|---:|
| 1040 | 1032 | 322 to 639 | 552 |
| 1645 | 2064 | 848 to 1125 | 1078 |
| 2061 | 3091 | 1604 to 1759 | 1410 |
| 2576 | 4119 | 2194 to 2615 | 1715 |
| 4043 | 7194 | 3909 to 4238 | 3121 |
| 5105 | 9243 | 4971 to 5188 | 4164 |

- BOs against command buffers fit 0.855 per cbuf plus 650.
- That reaches 16384 at about 18400 cbufs, not 16000. At this session's 10.4 cbuf/s (5188 in 497 s) that is about 30 minutes.
- BOs against "fence minus cbufs" fit 1.13 per submission plus 484, with r² 0.997.

Fence minus cbufs is every submission that is not a backboardd Metal command buffer. Prologues are in it, along with other clients. So the list follows those submissions (inferred) and the code says why.

### Candidates ruled out

- **Upload slabs:** `cb_retire` returns them to `free_slabs`, so they are bounded by peak use (`chl_vk.c:2113`).
- **Descriptor pools:** Chlorophyll creates none. It pushes descriptors with `vkCmdPushDescriptorSetKHR`.
- **Command pool per command buffer:** `cb_recycle` reuses them. A leak of those would cost at least 3 BOs per cbuf (`cs`, `draw_cs`, `sub_cs`), and the total slope is 0.855 (inferred).
- **Query pools:** only with `bgmtl.gputs`, and at most one per recycled command buffer (`chl_vk.c:2085`).
- **Retained buffers:** `cb_retire` releases every hold, and `chl_buffer_release` runs in `dealloc`. I could not prove from code that CoreAnimation releases each `BGMTLBuffer`, so `bo sources:` now counts them.
- **Freed in Vulkan, kept on Turnip's list:** `msm_bo_finish` calls `tu_bo_list_del` at refcount zero. With `has_set_iova` false no zombie path runs (`tu_knl_drm_msm.cc:1180`).
- **192 MB texture pool:** 256 slots with 5 s expiry (`bgmtl.m:430`). `made ~6049 MB` against 474 MB of GPU pages at the end shows textures do get freed.
- **`sweet_drm` handle leak:** `nr_bos` is Turnip's own list, so a handle Turnip forgot would not be named. It cannot cause this line.
- **`tu_cs` spares:** capped at 8 per stream, and they only matter while a command buffer lives.

### The fix

`pro_retire_slot` now frees the command buffer. Every caller holds `pro_lock`, which also guards all recording from that pool, and it only runs after the fence completed, the submit failed with out-of-memory, or the device is closing. A slot whose fence is still out keeps its command buffer.

What bounds the list afterwards:

- Prologue command buffers stop at 8 in flight plus 1 open. The Vulkan runtime recycles freed ones, so each keeps at most a few BOs.
- `chl_cmdbuf`s stop at the peak in flight, which Metal's queue semaphore caps at 64.
- Slabs stop at peak use.
- Pool textures stop at 256.
- Live CoreAnimation textures and buffers follow what is on screen.
- Pipelines grow with distinct shaders only.

The list should level off near the baseline. That baseline is the 490 to 940 left after subtracting the fit above, and it rose during ast24. I can't say from this log whether the rise is live textures and pipelines or prologues that hold more than one BO.

### Did the exits come from the 4096 limit?

Very likely yes, though no log shows it directly, because no session with the old limit printed the BO count. The evidence is that the exits follow submissions and not command buffers.

| Exit | Cbufs at exit | Fences in that process | Estimated BOs |
|---|---:|---:|---:|
| ast22, first | 3932 | 7212 | 4180 |
| ast22, second | 3748 | 7178 | 4349 |
| ast9 | 4042 | 7238 | 4086 |
| ast14 | 3626 | 6253 | 3445 |

- Three exits landed within 1% of 7200 fences while their cbuf counts differ by 8%. That spread is what you would expect from a limit on this list.
- In ast24, fence 7194 had about 4000 BOs, and the session crossed 4096 at about cbuf 4000.
- ast24 kept going to 5105 with no `submit refused` and no failed command buffer, on the one kernel with the limit lifted.
- The estimate interpolates fences by log line and assumes ast24's fit. ast14 misses by 16%, with five recoveries shortly before its exit (lines 13469 to 14141). That may be noise or a second cause.

On cost, I found no measured slowdown yet. `submit` stays at 0 to 2 ms per second in `perf:` lines up to 5105 BOs. The kernel's share is unmeasured, and that column may not cover it. The code does copy 16 bytes per BO, compare each BO against up to 128 commands, and walk all BO pages every 32nd submission, all but the copy under the GPU lock.

## Problem 2: hangs

### A correction: the keyboard freeze was about 36 s

`RECOVERY 2` did not cost 5 s. `perf:` lines give these longest waits:

| Recovery | Longest wait | Path |
|---|---:|---|
| 1 | 5388 ms | frozen, 5 s cap |
| 2 | 30182 ms | `moving but no fence for 30 s` |
| 3 | 5394 ms | frozen, 5 s cap |

Swaps sat at 3026 across the `up_s` 968, 978 and 988 lines, then reached only 3034 by 998.

### What the hung submissions held

Only `RECOVERY 1` has a dump, because `dumpHang` runs once per boot. Decoded: `plans/gpu-next/data/hang-ast24.txt`.

| | Recovery 1 | Recovery 2 | Recovery 3 |
|---|---|---|---|
| Fence | 1191 | 5845 | 5853 |
| Hang bit | yes | **no** | yes |
| RPTR, WPTR | 0x1558, 0x1577 | 0x486, 0x48f | 0x19c, 0x1bb |
| IB1 size | 540 dwords | 128 dwords | 947 dwords |
| IB1 rem | 156 | 0 | 595 |
| Nearest execs | `CollectionsPoster` | `kbd`, `InputUI` | `WebContent`, 8 fences after recovery 2 |

**Proven, recovery 1:** four sysmem passes, each with one IB2. The BR and BV IB2 registers both hold the first IB2, 419 dwords with two indexed draws of 48 and 6 indices. The later passes are scissored to 64x8, 236x10 and 840x1860, and the first looks about 62x6 from its resolve rectangle. That is very little work. It fits the first-IB2 signature from `plan-stability.md`. No flush sits between its two draws, so no fetch-sync barrier fired inside that pass.

**Proven, recovery 3:** same registers, the 31-dword tail, RPTR on the scratch write after the IB. Whether the first IB2 is involved is unknown without a dump.

**Recovery 2 is a different hang.** Proven: RPTR 0x486 is just past BR's `CP_WAIT_TIMESTAMP` at 0x481, BR IB1 and IB2 `rem` are 0, and the BV fence word is still 0x16d4 (`memptrs` slot 3). So BR finished its stream and BV's fence never arrived. Inferred: BV is stuck in the IB and BR polls that word, and the polling reads cause the flicker in `VBIF_GX_BUSY` (0xec0425 and 0xec0405). That flicker counted as movement, which is why the 5 s cap never fired. Patch 2059 fixes that part.

**Inferred, weak:** new shaders. `hang_vs_pipeline.py` finds a slow build or a 16th-pipeline line in or just before 3 of 3 hang windows, against 35 of 189 windows overall. Recovery 2's window is 30 s long and recovery 3 follows it, so builds may fall inside the stall. Only ast24 has `perf:` lines. App launches cause both compiles and hangs, so this may be correlation only.

**Ruled out, mostly:** lost write-combining. `no longer write-combined` appears 0 times in ast22, ast23 and ast24, so I would not spend a boot on `gpu-fix2/2044`.

### Is the fast path safe? Yes, turn it on

Decision: use `sweetgpufast=1000`.

- **Proven:** ast24 is the only log whose kicks print INT. 7 of 56 kicks have bit 23, all on the two hung positions (0x1558 five times, 0x19c twice). 0 of the 49 others have it.
- **Proven:** both `would recover here` lines were followed by a recovery at that position.
- **Earlier support:** all 9 recovery snapshots in ast18 to ast22 have INT `0x900000` (`plan-stability.md`).
- **Limit:** this rests on 2 independent hangs, and it does not catch recovery 2's class. Those wait for the 5 s cap, with 2059.
- **Worst case:** the path also needs all 12 words frozen for 1 s, and the host check covers a sticky bit on a working GPU. A false positive would void one frame's work.

Expected freezes for the same three hangs: about 1.4 s, 5.4 s and 1.4 s, so about 8 s in place of 41 s.

Length: `next-all` is 589 bytes in ast24 and ` sweetgpufast=1000` adds 18, which is exactly `mkpayload.py`'s limit of 607. Any further argument needs another one dropped.

### What the flag comparison supports

Almost nothing. From `plans/gpu-next/tools/rates.py`:

| Arm | Recoveries / cbufs | Per 1000 | Expected at base | Chance by luck |
|---|---:|---:|---:|---:|
| Base flags, ast18 to ast22 | 9 / 20661 | 0.44 | | |
| ast23 `flushall,syncdraw` | 0 / 2106 | 0 | 0.92 | 40% |
| ast24 fetch-sync | 3 / 5188 | 0.58 | 2.26 | 39% |

- ast23's zero is what the base rate gives 40% of the time, and it had fewer launches than ast24 by my count of 13 named apps (4 against 13).
- Fetch-sync did not stop hangs, since all three happened with it on. Its rate cannot be told apart from base.
- `flushall,syncdraw` needs 6878 cbufs with no recovery for p < 0.05, about 11 minutes of ast24-style use.

### Next experiment

1. Boot with this page's patches and read every `SWEET HANG hist` block. Two questions: did the hung submission follow a drained ring or a client change more often than the 7 before it, and did a `render pipeline ... at N ms` line land just before it? Both use `CLOCK_UPTIME_RAW` (mach time).
2. Run `flushall,syncdraw` for at least 6878 cbufs on a fixed script: Safari keyboard three times and five app launches. Keep the same script for the comparison.
3. Choose from the result, in this order:
   - A hang on a drained ring points to a kernel keep-busy or idle wait before the IB.
   - A hang after a new pipeline points to shader upload ordering, so dump that pipeline's shader BO next.
   - No recovery in step 2 points to a missing flush, so bisect `flushall` against `syncdraw`.

skipped: the draw-state and shader dump from `plan-stability.md` phase 3, add it when step 1 shows which IB2s to look at. The keep-busy kernel patch is also not written, add it if step 1 implicates the drained ring.

## What the next log should show

- `bgmtl.log`: a `bo sources:` line every 10 s, with `prologue cbs live` at 9 or below while `of N submitted` climbs.
- Kernel: `submission N names M BOs` every 1024 submissions, with M level. My estimate is under about 1500 at 5000 cbufs. If it still climbs by more than about 0.1 per cbuf, a second source exists and the `bo sources:` columns should name it.
- Kernel: `submissions now name` lines stop soon after startup, and none reach 4096.
- `bgmtl.log`: `max wait` near 1400 ms for hang-bit hangs and near 5400 ms for the other class, never 30000 ms unless pointers really move.
- Kernel: `watch: frozen with the hang bit up` before recoveries, with BV IB pointers in the line.

## Not proven

- That the prologue leak accounts for all the growth. BO growth runs 13% over those extra submissions, so some prologues may hold more than one BO, or a smaller second source may exist.
- That the old exits hit 4096. It is strongly supported, and the patches do not depend on it.
- That no real job shows only CP, UCHE and VBIF bits with frozen pointers for 5 s. 2059 would reset such a job, and I marked that in the code.
- Why any of these hangs start. The stuck packet is still unknown, and BV's state in recovery 2 is inferred.
- Any of this on the phone. The kernel patches are syntax-checked with the real build's flags, not built.

## Files

All under `plans/gpu-next/`.

- `chlorophyll-prologue-free.patch` applies to `next-fix/Chlorophyll`. It holds the fix, `bo sources:` and the pipeline times.
- `next-fix-readback-check.patch` applies to `next-fix` and keeps `build.sh`'s check compiling. The other eight `next-fix/test/*-check.py` scripts pass unchanged on the patched copy.
- `2059-asteroids-a810-watch-vbif.patch` is the watch fix.
- `2063-asteroids-a810-hang-history.patch` is the diagnostics. 2060 to 2062 and 2070 to 2073 are already taken.
- `tests/prologue_check.py` has a control that leaks 20000 of 20000 on the old code.
- `tests/watch_check.py` has a control where the old header fails the recovery 2 case.
- `tests/syntax_check.py` applies all three to temp copies and syntax-checks them.
- `tools/decode_hangs.py` decodes several dumps per log.
- `tools/hang_vs_pipeline.py` and `tools/rates.py` produce the numbers above.
- `data/hang-ast24.txt` and `data/rates.txt` are their outputs.
- `work/make_patches.py` regenerates the patches.
