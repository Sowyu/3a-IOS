# Freeform drawing: compute on the A810, PencilKit's needs, and the phase 2 plan

2026-11-02. Desk work only: no phone, adb, fastboot or flash, and nothing written to `next-fix` or the kernel tree. Everything prepared is under `plans/freeform/`. Paths are relative to `EverIOS/`, and `K/` is `ios-asteroids/research/xnu-local/dxb/xnu/`.

Tags: **Proven** means I read it in code, a log, or ran it on this Mac. **Inferred** means reasoning with no run behind it. **Unknown** means no evidence.

## Verdict

- Compute looks fixable, and may already work. Nothing on record shows a compute kernel hanging the A810. The two ast9 and ast10 hangs "after the first compute pipelines" were the two-IB split that 2100 fixes. ast10b hung with compute skipped. All 75 analysed hangs had 2 IBs, and none of the 28 decoded dumps held a `CP_EXEC_CS`.
- The luma "garbage" of ast20 has two known causes that are not the A810. One, compute `setBufferOffset:` was ignored until 2026-11-01, and CA rebinds the luma output at offsets 0, 16, 32 and 48. Two, those kernels use threadgroup memory, barriers, texture reads and 1024-thread groups, and PencilKit uses none of these.
- PencilKit's three kernels are plain per-thread buffer code. The phone's translator turns all three into NIR on this Mac. I ran it on the host.
- Two bugs would have stopped Freeform even with working compute. Both are fixed in `next-ff`. Chlorophyll never passed `MTLFunctionConstantValues` to its translator, and one PencilKit shader with a function-constant `[[color(n)]]` did not translate.
- Phase 2a proves compute alone with one push and no kernel build. Phase 2b, Freeform itself, needs a kernel with 2130.

## Phone test plan

Times are for whoever runs it. Each `dev.py` run takes about 5 minutes. Check HANDOFF first: `dev.py`'s recovery step still uses the old route and must work with the current `boot_a`.

### Phase 2a: compute alone, about 15 minutes

No new kernel, no boot-arg change, and backboardd's libraries stay as they are.

1. Run `plans/freeform/selftest/build.sh` and `plans/freeform/tests/specialize_check.sh` on the Mac.
2. Run this, with the loop image the lag agent uses (`next-loop-sb` was built from the 2100 kernel):
   ```
   plans/devloop/dev.py plans/devloop/out/next-loop-sb-boot.img \
     /usr/local/bin/hcitest=plans/freeform/selftest/out/chl-selftest \
     --test ../../../freeform/selftest/compute --note compute1 \
     --pull /private/var/tmp/chl-selftest.log --key 'CHL '
   ```
   `--test` resolves to `plans/freeform/selftest/compute.sh`. `hcitest` is the Bluetooth probe, which nothing runs at boot. It is the existing root path the push replaces.
3. Read the `SELFTEST: CHL` lines in the log, or `chl-selftest.log` in the run directory.

| See | Means | Next |
|---|---|---|
| `RESULT PASSED`, `recoveries before: N` equals `after` | Compute works on the A810, including PencilKit's real `sdfPen_kernel` | Phase 2b |
| `FAIL ... uint results wrong` or `half results wrong` | Compute runs but computes wrong values | Debug in the self-test, never under Freeform |
| `FAIL timeout`, or the recovery count changed | Compute hangs the A810 | Stop. Decode the hang with `plans/gpu-next/tools/decode_hangs.py` |
| Trivial passes, `pencilkit:` fails | A gap in translating a real kernel | The FAIL line names the stage |

4. Put the Bluetooth probe back when done, in the next push or on its own: `/usr/local/bin/hcitest=plans/freeform/selftest/hcitest.orig`.

### Phase 2b: Freeform, about 1 hour with the kernel build

1. When the lag agent is done with `K/`, run `git apply plans/freeform/2130-asteroids-app-metal-exec.patch` in `K/`, then `USE_KDK=1 tools/xnu-local-build.sh --remake -j12 26.5`. That takes about 45 minutes.
2. Run `plans/freeform/build-images.sh KERNEL`. It refuses a kernel without 2130. It makes `plans/freeform/out/ff-appmetal-boot.img`, at 561 of 607 boot-arg bytes, and `ff-appstub-boot.img` at 576.
3. If `next-fix` changed since today, rebase first: `patch -p1 --dry-run < plans/freeform/next-ff.patch` in the new tree, then build there. Otherwise use `next-ff/out-metal` as built.
4. Run this:
   ```
   plans/devloop/dev.py plans/freeform/out/ff-appmetal-boot.img \
     /usr/local/bin/hcitest=plans/freeform/selftest/out/chl-selftest \
     /usr/local/lib/bgmtl/bgmtl-inject.dylib=plans/freeform/next-ff/out-metal/bgmtl-inject.dylib \
     /usr/local/lib/bgmtl/libchlorophyll.dylib=plans/freeform/next-ff/out-metal/libchlorophyll.dylib \
     --test ../../../freeform/selftest/freeform --note freeform1 \
     --pull /private/var/tmp/chl-selftest.log --pull /private/var/tmp/bgmtl-app.log \
     --key 'CHL ' --key 'SWEET EXEC: appmetal'
   ```
   The script runs the compute self-test first and skips Freeform unless it passes. Then it opens Freeform, makes a board, picks the pen, draws three strokes and takes a checked frame after each. The first stroke gets 90 s for shader compiles. The tap coordinates are guesses, so read the frames.
5. Read the results in this order:
   1. The console has `SWEET EXEC: appmetal inject for /Applications/Freeform.app/Freeform`.
   2. `bgmtl-app.log` has `BGMTLDevice registered`.
   3. It has `specialize: on for ..., constant setters on MTLFunctionConstantValuesInternal hooked`.
   4. It has `specialize: paint_kernel with N constants: ...` and `pipeline paint_kernel: buffers ...`.
   5. Frame `freeform-stroke1` shows a stroke, and the recovery count is unchanged.

| See | Means | Next |
|---|---|---|
| No `SWEET EXEC` line | The kernel lacks 2130, or the exec path or the file's own path is not exactly `/Applications/Freeform.app/Freeform` | Check `dev.py --print-args` on the image |
| `SWEET EXEC: appmetal inject refused for ...: REASON` | The binary is set-id, restricted, hardened, library-validated, privileged, or changed credentials | Read the reason. The variable became `SWEET_APPMETAL_DENIED=` and dyld ignores it |
| `constant setters ... NOT FOUND` | iOS names the constant-values class differently | Read the class from the log line and add it to `bg_specialize_install` |
| `nothing is bound there` in `bgmtl-app.log` | A pipeline declares a buffer PencilKit never binds | Check that the pipeline got its constants (the `specialize:` line) |
| `the library handed back a function object it had specialized before` | `_MTLLibrary` caches function objects | Wrap the function instead of attaching the values to it |
| `UNIMPLEMENTED` | A Metal selector Chlorophyll lacks | Add it |
| Stroke missing, no recovery, no errors | Geometry or fetch is wrong | Turn on `/private/var/tmp/bgmtl.faildump` and compare with the self-test |
| A recovery right after the stroke | A PencilKit render pass or kernel hangs. The inject's watchdog exits Freeform and writes its kill switch | Boot `ff-appstub` to get the crash-free board without strokes |

To go back, boot any image without `sweetappmetal`. The pushed libraries behave as today without it.

## Why compute failed before

What NOTES and the logs show, all **Proven**:

| Run | Compute | Result |
|---|---|---|
| ast9, ast10 | luma kernels on | Hung "right after the first compute pipelines", rbbm `0xec0405` |
| ast10b | luma skipped | **Still hung**, at fence 1161 |
| ast20 | luma on | 4 recoveries, about the rate without compute (ast21 had 5). Contrast went extreme |
| Kernel 2063 and later | luma skipped | 75 of 75 hangs were 2-IB submissions. The 28 decoded dumps of the seven scripted runs were all graphics, 0 `CP_EXEC_CS` (`hangs.md`) |

So "compute hangs the A810" was never shown. The hang timing matched app launches, which compile pipelines and draw many passes. That is the same pattern that fills a 4096-dword IB and triggers the split, and 2100 now puts all IBs of a submission in one frame. The split did not depend on what the IB held. A dispatch landing after the cut would also have run in the wrong thread mode, so 2100 removes that too. **Inferred.**

The ast20 garbage has two stronger explanations than the GPU:

1. Before 2026-11-01, `BGMTLComputeEncoder setBufferOffset:atIndex:` did nothing (`render-bugs.md` §1). The skipped-dispatch log in `ast20-var/bgmtl.log` shows CA binds buffer 2 of the same object at offsets 0, 16, 32 and 48 across its luma dispatches. If CA moved it with `setBufferOffset:`, every result landed at one offset. **Proven** that the setter was missing and that CA uses offsets. **Unknown** which setter CA calls, so **Inferred** that this made the garbage.
2. The luma kernels need what PencilKit does not: a threadgroup-memory argument (`setThreadgroupMemoryLength:`), three `air.wg.barrier`, `read_texture_2d`, `ushort` system values, and 64x16 = 1024 threads per group (`compute_average_luma`). Any of these can break on its own. **Proven** from the AIR (`ast13-dumps`).

What the code path does, read in full (**Proven**):

- `chl_compute_pipeline_create` translates once. `chl_dispatch` compiles one Vulkan pipeline per threadgroup size, up to 8. The first dispatch of a size compiles on the encoding thread.
- `chl_compute_begin` and `chl_compute_end` put a full ALL_COMMANDS memory barrier around the encoder, and every dispatch is followed by a compute-to-compute barrier. That covers PencilKit's case of a kernel writing vertices that the next render pass reads.
- Turnip's `tu_dispatch` is upstream Mesa 26.2.5 code, and the A810 is in upstream's device table (`freedreno_devices.py:1362`, 32 KB shared memory, 1 slice). I found nothing a8xx-specific missing. Whether ir3 output for compute is right on this GPU is **Unknown** until phase 2a.

## What PencilKit's kernels use

From the AIR in `plans/appmetal/evidence/pk-root/default.metallib`, disassembled with `llvm-dis` and translated with the phone's own translator built for this Mac (`plans/freeform/work/host/air2nir2`). **Proven.**

| Feature | `paint_kernel` | `particle_kernel` | `sdfPen_kernel` | Chlorophyll |
|---|---|---|---|---|
| Threadgroup memory, barriers | none | none | none | not needed |
| Atomics, SIMD-group functions | none | none | none | not needed |
| Textures, image writes | none | none | none | not needed |
| Argument buffers | none | none | none | not needed |
| Constant buffer | 128 B struct with two `float3x3` | 56 B | 8 B, plus a 4 B float at index 3 | UBO, translated |
| Device buffers | read points, write 24 B vertices with half fields | same | same, 36 B vertices | SSBO, 16-bit stores, translated |
| `thread_position_in_grid` | `uint`, guarded `gid + 1 < n` | same | same | translated |
| Function constants | 2, gate buffers 3 and 4 | 2 | none | **was the gap, fixed** |
| Internal calls, loops, `switch` | yes | yes | yes | translated |

All 34 PencilKit functions translate with `next-ff`'s translator. Before it, 33 did.

## What was missing, and the fixes

### 1. Function constant values never reached the translator

**Proven.** `bgmtl.m` built every pipeline with `struct chl_function { ..., NULL, 0, ... }`, so `chl_vk.c:translate` used `air_module_function_constants`. That gives every constant the value 0 and marks every one as defined. PencilKit makes 25 functions with `newFunctionWithName:constantValues:error:`, sets them with `setConstantValue:type:atIndex:` (52 calls), and uses only `bool` and `uint` constants.

Three effects follow:

- Five required `uint` constants (`COLOR_ATTACHMENT_INDEX` in three functions, `SOURCE_` and `DESTINATION_COLOR_ATTACHMENT_INDEX`, `i1 true` in the AIR) choose attachments. At 0 they all point at colour 0.
- `paint_kernel`'s buffers 3 and 4 exist only when `LIVE_STROKE_PARTICLES` is defined. With every constant defined, the kernel always reads them. `flush_bindings` then refuses each dispatch where PencilKit binds nothing there: "the compute stage reads [[buffer(3)]] and nothing is bound there". I checked this on the host. With only constant 2 defined, the NIR has no buffer 3 or 4 access, and with 2 and 3 defined it has both.
- Apple's own `_MTLLibrary` specialises through the device's compiler, which this device lacks. Its result is **Unknown**. I did not rely on it.

Fix, in `bgmtl.m` between `BEGIN specialize` and `END specialize`, active only with `BGMTL_SPECIALIZE=1`:

- The constant setters on `MTLFunctionConstantValuesInternal` record each scalar per object.
- `newFunctionWithName:constantValues:error:` on the library's class returns the plain function with the values attached.
- Both pipeline kinds pass the values to the translator.

The inject sets `BGMTL_SPECIALIZE=1` only for the trial app, so backboardd is unchanged. `tests/specialize_check.sh` runs the block against this Mac's real Metal.framework, and it passes.

### 2. `[[color(n)]]` whose n is a function constant

**Proven.** `copy_framebuffer_fragment` reads `[[color(SOURCE_COLOR_ATTACHMENT_INDEX)]]` and writes `[[color(DESTINATION_COLOR_ATTACHMENT_INDEX)]]`. The AIR holds a global that `air.static_init` sets, not an integer. The input path refused it ("an air.render_target input without a target"), and the output path silently used colour 0.

`air_to_nir.c` now has `md_render_target`, which reads the global through the existing static-init evaluator. On the host, the constants 4 and 1 give a fetch from `DATA4` and a write to `DATA1`.

I translated 58 compositor modules (the glass corpus and the ast13 dumps) with both translators and got byte-identical NIR. Of PencilKit's 34 functions, only this one changes, from a refusal to a translation.

### Still open

- Framebuffer fetch of colour 4 needs a pass with five attachments. The fetch path exists (`air_nir_vk.c`), but this case is **Unknown**.
- `newPipelineLibraryWithFilePath:` answers nil, and PencilKit stores it without checking. Memoryless textures get ordinary memory. Both are low risk.
- `setConstantValue:type:withName:` is not recorded. PencilKit does not call it.
- If `_MTLLibrary` returns the same function object for two `newFunctionWithName:` calls, the second specialisation replaces the first. The code logs it. **Unknown** whether it happens.

## Risk to the compositor

- One GPU, one ring, one address space, and no preemption. A compute hang stalls backboardd until the watchdog recovers the GPU after 1 s, and that recovery voids every client's fences. That is how torn icons happen. Phase 2a exposes the home screen to this once, for a few seconds of compute. **Proven** design, **Inferred** cost.
- `chl-selftest` reads the recovery count before and after and fails on a change. The inject's watchdog exits Freeform on the first recovery and writes `bgmtl.off.com.apple.freeform`. That limits the trial to one hang per boot. It cannot stop the first one.
- A wild write is the other danger, since a client's bad address can reach another client's buffers. The self-test feeds `sdfPen_kernel` points whose destination indices stop at vertex 115. It allocates 256 vertices and checks that the 140 after 115 stay untouched.
- Pushed libraries: backboardd loads the new `libchlorophyll` and inject in phase 2b. Its shaders translate to identical NIR, and both new code paths need either `BGMTL_SPECIALIZE` or `sweetappmetal`. **Proven** on the host, **Inferred** on the phone.
- Memory: Freeform carries its own Turnip instance, and the inject sets no texture pool for it. Freeform's real use is **Unknown**.

## Prepared files

Phase 2a needs no root rebuild: `/usr/local/bin/hcitest` exists on the root, and the push tool may grow a file. Phase 2b needs none either. The two libraries exist on the root, and 2130 arms the app by boot-arg because the root has no `bgmtl.arm`. New paths created at run time (`/private/var/tmp/chl-selftest.log`, `bgmtl-app.log`, `chlorophyll-cache/freeform`) are written by the phone, not pushed.

`plans/freeform/`:

- `next-ff/` is `next-fix` without images, built with its `build.sh`, and `verify.py` passes. The unchanged copy reproduced `next-fix/out-metal` apart from the embedded source path of `tu_cmd_buffer.cc`. Sibling symlinks (`gpu-fix4`, `chl-work2`, `render-fix3`, `label-fix`) keep its `../` paths working. `rebuild-chl.py` now resolves the build directory and also builds this tree's `air_to_nir.c` into a copy of `libair.a`.
- `next-ff.patch` holds all source changes against `next-fix`, and applies there by dry run. It has the specialisation, the colour index, the inject's app mode from `plans/appmetal` plus `BGMTL_SPECIALIZE`, and the app's stderr sent to `bgmtl-app.log`.
- `2130-asteroids-app-metal-exec.patch` was rewritten after Sol's NO-GO (`input-ff-audit.md` findings 3 to 5). See "2130 after the audit" below. `work/k2130/make_2130.py` regenerates it from `K/`, and `tests/old/2130-audited.patch` is the audited version, byte for byte.
- `tests/compile_check_2130.py` runs `git apply --check` in `K/` and a `-fsyntax-only` compile of `kern_exec.c` with the build's flags. Both pass, with 0 new diagnostics once line numbers inside identifier names are ignored.
- `tests/exec_env_check.py` compiles the real `exec_extract_strings`, 2130's helpers and its lines around `exec_handle_sugid` on the host, from both patch versions. Every audit finding is reproduced on the audited code and fixed on the current one.
- `tests/specialize_check.sh` is described above. It passes.
- `selftest/chl-selftest.c`, `chl_selftest.ll` and `build.sh` make the compute self-test. It calls libchlorophyll's C API directly, with no Metal.framework. The kernel is hand-written AIR because this Mac has no Metal toolchain. It has to be assembled with `llvm-as` from llvm@22, since llvm 23 writes a record the reader refuses.
- `selftest/compute.sh` and `selftest/freeform.sh` are the `dev.py` test bodies. Both compose with `dev.py --print-script` and pass `sh -n`.
- `selftest/hcitest.orig` is the root's Bluetooth probe, taken from the gpu17j image, for the restore.
- `build-images.sh` makes the trial images and refuses a kernel without 2130.
- `SHA256.txt` has the hashes.
- `work/` holds scratch: the host-built translators, build logs, and PencilKit's extracted AIR. `work/pk` is Apple's code, so never commit it.

2120 is neither applied nor cleanly unapplied in `K/` (`SweetFramebuffer.cpp` differs both ways), and `K/bsd/dev/sweet_fb.c` changed at 19:09 today. 2130 touches only `kern_exec.c`, so neither affects it. Check `git apply --check` again before applying.

skipped: running PencilKit's `paint_kernel` and `particle_kernel` in the self-test (they need real stroke data to check), and the `isMetalAvailable` fallback. Add them if phase 2b's strokes come out wrong, or if compute fails on the phone.

## 2130 after the audit

Sol's NO-GO (`analysis/input-ff-audit.md`) had three 2130 blockers and one composition note. The rewrite:

| Finding | Change |
|---|---|
| 3: stripping with the args absent | The inject and combined variables are stripped only while `sweetappmetal=Freeform` is on. 2058's own `sweetsb` stripping is unchanged |
| 4: basename match, symlinks | Only `sweetappmetal=Freeform` turns it on, and other values are refused with a log line. The exec path must equal `/Applications/Freeform.app/Freeform`, and so must `vn_getpath` of the vnode being executed |
| 5: restricted, hardened, MAC transitions | After `exec_handle_sugid`, before the strings are copied out, the insertion is refused for P_SUGID, a MAC label transition, set-id mode bits, `CS_RESTRICT`, `CS_RUNTIME`, `CS_REQUIRE_LV`, `CS_FORCED_LV`, or the installer, datavault and NVRAM flags |
| Composition | With `sweetsb=1` Freeform gets one variable, sweetsb first. An insert appended by `sweetexecenv` now counts as existing, so 2058 and 2130 add no second variable |

How the refusal works: extraction cannot know the signature flags or the credential change, so it inserts as before. If the later check refuses, the variable is renamed in place to `SWEET_APPMETAL_DENIED=`, which has the same length. No string or pointer moves, and dyld ignores the name. The MAC answer comes from the same query `exec_handle_sugid` makes, asked just before it, because that function does not keep the answer. The root's Freeform is ad-hoc signed with flags `0x2` and has no `__RESTRICT` segment, and `sweet_amfi.c` changes no CS flags, so the real binary passes. **Proven** from the root image and the source.

Still open:

- A refused combined variable takes sweetsb with it. 2058 alone would have given a restricted Freeform sweetsb. That only applies to the one exact path, and the refusal is the safer direction.
- dyld also treats a `__RESTRICT` segment as restricted, and the kernel does not see that at this point. Freeform has none.
- 2058 still inserts sweetsb into restricted and hardened UI apps. That is out of 2130's scope.
- A library that dyld accepts and that then fails in a dependency or constructor still kills Freeform at load. The watchdog cannot cover that.
- The near-capacity E2BIG predates 2058 (audit, capacity section).
- Nothing here ran on a phone. Sol should re-audit before the kernel is built.

## 2130 after the re-audit

Sol's second NO-GO (`analysis/2130-reaudit.md`) had two blockers. Both are fixed, and so are both should-fixes.

| Item | Change |
|---|---|
| Blocker 1: chroot-relative path | Injection is refused if the calling process or the new process has a chroot (`fd_rdir`), and the vnode's path is built from the machine's root (`VN_GETPATH_NO_PROCROOT`). The console says `refused ...: a chroot` |
| Blocker 2: behaviour change when off | Every 2130 line is a no-op unless `sweetappmetal=Freeform`, the `sweetexecenv` bookkeeping included. The log line for a refused value is gone too |
| Should-fix 3: MAC decision asked twice | `exec_handle_sugid` hands back its own `mac_transition` through a new argument, written only with the feature on. The second query is gone |
| Should-fix 4: tests | `tests/exec_env_check.py` now covers 32-bit and 64-bit pointers, append thresholds, neighbouring bytes on a refusal, the derived pointer vector, chroots, and changes made inside `exec_handle_sugid` |
| Note on value search | The late scan only runs for the exact Freeform exec, so the same value that `sweetexecenv` sets for another app is left alone |

The check builds four versions from the tree: base (2058 alone), the first audited version, the re-audited version and the current one. Each finding reproduces on the version where it was found and passes on current. With the trial off or refused, current equals base in all 23,040 executions of the matrix, compared on string bytes, counters, the pointer vector and console lines. `tests/old/2130-reaudited.patch` is the re-audited version, byte for byte.

Still open: the items listed under "2130 after the audit". Nothing has run on a phone. Sol should audit this version before the kernel is built.

## Phase 2b on the daily driver (2026-11-03)

`plans/lag/next-ff2` is `plans/lag/next-luma`, which is next-keep's staging pool and 2 s hold rest plus `plans/glass/luma-compute.patch`, with `plans/freeform/next-ff.patch` on top. The only hunk that failed was `rebuild-chl.py`'s path fix, which next-luma already had. The luma fix tags `compute_average_luma` and `compute_sum_luma` by name and writes their results back after a dispatch. Freeform's change only passes constants when a pipeline is made, so the two do not overlap. `build.sh` passes with all its host checks, and so does `verify.py`. `chl_vk.c`, `tu_cmd_buffer.cc`, `bgl-launch.c` and `test/` are identical to next-luma's.

`combo-daily-ff-boot.img` has no `sweetdeadman` or `sweetselftest`, so `dev.py` refuses it, and adding them would take it to 610 of 607 bytes. `plans/freeform/build-images.sh` builds `out/ff-owner-boot.img` from the same combined kernel with the same args, without `sweetkeys_ms=5` and with `sweetdeadman=900 sweetselftest=1`, at 594 bytes. Its test body, `selftest/freeform-owner.sh`, opens Freeform and leaves the owner 6 minutes, taking a frame every 30 s.

With `sweetappmetal` off, backboardd differs from next-luma only in the translator's function-constant `[[color(n)]]`. No compositor shader we have uses it: 58 modules give identical NIR. The specialisation code waits for `BGMTL_SPECIALIZE`, which only the trial app sets, and the inject's app mode waits for Freeform's bundle id. 2130 is byte-for-byte 2058 when off (`tests/exec_env_check.py`).

## ast100: why phase 2b failed (2026-11-03)

### backboardd ran without Chlorophyll: 2130 stripped the launcher's variable

- **Proven, code:** `bgl-launch.c:585` sets `DYLD_INSERT_LIBRARIES=/usr/local/lib/bgmtl/bgmtl-inject.dylib`. That is byte for byte 2130's app-only string. With `sweetappmetal=Freeform` accepted, `sweet_appmetal_own` strips that exact inherited string on every exec, as the first audit asked, and backboardd is an exec of bgl-launch.
- **Proven, host:** `exec_env_check.py`'s build of the real kernel code, run on backboardd's exec with the launcher's environment, gives `BGMTL_BB=1 BGMTL_BB_RETRY=1` and no insert variable when the trial is on. When it is off, the variable stays. Base keeps it either way.
- **Proven, root:** the root's backboardd has no load command for the inject (`otool -L`), so the launcher's variable is the only way the inject reaches it.
- **Proven, logs:** every earlier boot in `bgl-backboardd.log` prints `bgmtl[64]: GPU ready` right after the injection line. This boot prints nothing from the inject, and `bgmtl.log` has no line for it. The assertion that follows is CoreAnimation's OpenGL/CPU renderer, which NOTES records failing the same way whenever backboardd runs stock (ast4, ast5, ast7p, ast8). The retry, pid 366, went the same way, which matches the owner seeing no glass, a respring, then black.
- So it was not next-ff2, not the luma fix, and not chance. backboardd never ran next-ff2's code at all.

### The freeze: most likely the stock renderer, not Freeform's GPU work

- **Proven:** the kernel never logged `A810 1 submissions`, which prints on a boot's first successful submit (`sweet_a810_msm.c:367`). No process submitted GPU work this boot. `bgmtl-app.log` shows Freeform opened a device and registered it, but has no library, `specialize:` or pipeline line: no PencilKit code ran, and the owner could not draw on a black screen.
- **Proven:** the 10-second stats stop after uptime 97 s, and the log ends without a panic, a RECOVERY or a hang report. The root was left unclean.
- **Inferred:** the stock renderer crashed again or hung, and launchd rebooted, as in ast4 and ast7p. The persistent log is not synced on every line, so the last seconds are likely lost. Freeform's device open is the only GPU-side event and cannot be excluded, but ast83 showed a second client opening a device works.
- **Unknown:** what exactly reset the phone.

### Fix

The kernel fix, which needs Sol's audit and a kernel build: 2130 inserts and strips a spelling only it produces, `/usr/local/lib/bgmtl//bgmtl-inject.dylib`. That is the same file, and the doubled slash makes the string 2130's alone. The combined form becomes `...sweetsb.dylib:/usr/local/lib/bgmtl//bgmtl-inject.dylib`, and `exec_env_check.py` gets a case where bgl-launch to backboardd with the trial on keeps its variable, which fails on the current code. Inferred: dyld opens the doubled-slash path normally; check that the first time on the phone. The stop-gap with the existing kernel is to change bgl-launch's spelling instead. That is one line and pushable, but it touches the daily launcher.

### 2130 v4 (2026-11-03, overnight)

`plans/freeform/2130-asteroids-app-metal-exec-v4.patch` is v3 with one change. 2130 inserts and strips `/usr/local/lib/bgmtl//bgmtl-inject.dylib`, a spelling only it produces, and its combined form ends in the same doubled-slash path. bgl-launch's `DYLD_INSERT_LIBRARIES=/usr/local/lib/bgmtl/bgmtl-inject.dylib` for backboardd is never 2130's any more, so the launcher stays as it is. v3 is unchanged and saved as `tests/old/2130-audit3.patch`. `2130-v3-to-v4.patch` is the increment for a tree that already carries v3.

dyld accepts the spelling. Proven from Apple's dyld-1378 source: `Loader::getLoader` opens an insert path as given, and falls back to `realpath()` only if that fails, with a comment that names paths containing `//`. The only `//` rule in dyld is the cache builder's install-name check, which does not apply here. The phone's dyld is dyld-1387 (`/usr/lib/dyld` on the root) and has the same insert-loop and realpath strings. `@loader_path` for libchlorophyll and libdrm becomes `/usr/local/lib/bgmtl//...`, which the kernel opens as the same file. Inferred: no behaviour difference on the phone. Confirm it on the first boot.

`tests/exec_env_check.py` (59 of 59, saved in `work/exec-env-check-v4.txt`):

- bgl-launch to backboardd with the trial on keeps its variable. This drops it on v3 and is fixed in v4, equal to base, with and without sweetsb.
- With the trial on, a process inheriting both spellings keeps the launcher's and drops 2130's.
- With the trial off, v4 equals base in all 23,040 execs of the original matrix, and in 384 more that inherit v4's own spellings.

The compile check passes with 0 new diagnostics, and `git apply --check` passes on the tree.
