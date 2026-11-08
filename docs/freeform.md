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

## ast102: black board and hard reset (2026-11-03)

With v4, backboardd ran on Chlorophyll (glass, 0 restarts, 0 recoveries), and Freeform got its device.

### The black board: two translator gaps, both fixed in `plans/lag/next-ff3`

- **Proven** from `bgmtl-app.log`: `sixChannelBlend_fragment` was refused ("function constant 11 has no value"), and the draws that needed it ran with no pipeline. `uber_fragment` was built but its draw was refused, because it fetched `[[color(4)]]` and the pass has no attachment 4.
- **Proven** on the host with PencilKit's exact constants:
  1. PencilKit leaves `MULTIPLY_COLOR_ATTACHMENT_INDEX` (11) unset, and the module's static initializer reads it unconditionally. In Metal that value is just undefined; the translator refused it.
  2. `secondary_paint_framebuffer [[color(4)]]` exists only when `SECONDARY_PAINT` (17) is set, and PencilKit did not set it. The translator fetched it anyway.
- **Inferred:** these were the board's composite draws, so the canvas stayed black.
- Fix 1: an unset constant reads as 0, which is what the module defaults gave before.
- Fix 2: a `[[color(n)]]` input whose predicate the pipeline's constants make false becomes undefined. Fix 2 applies only to specialised pipelines, through the new `air_to_nir_options.specialized` field, appended last.
- On the host, every specialisation PencilKit requested in ast102 now translates: 12 functions. With default constants, the NIR of the 58 compositor modules and the 34 PencilKit functions is unchanged.

### Did the drawing kernels run?

- **Proven:** PencilKit built `paint_kernel` (3 specialisations) and `particle_kernel` (4 specialisations). It never built `sdfPen_kernel`.
- **Unknown:** whether any dispatch ran. next-ff2 did not log dispatches. next-ff3 logs each dispatch and commit in the trial app and fsyncs every line.

### The reset: not established

- **Proven:** the last synced kernel lines are a 2-IB submission (fence 1041) and one stalled-fence kick with RBBM `0xec3425`. Then there are no more synced A810 lines: no further kick, no RECOVERY, no SMMU fault, no panic, and a dirty root.
- **Proven:** just before that, Freeform's in-process CoreAnimation started rendering through Chlorophyll. It loaded QuartzCore's metallib, built Vfx pipelines, and copied a 250x376 IOSurface. Its framebuffer calls, selectors 74 and 88, are no-ops that backboardd also makes.
- **Inferred:** a stuck GPU would have produced more kicks or a recovery, so the whole system stopped within about a second.
- ast100 also hard-reset about 10 s after Freeform registered a device, with zero GPU submissions all boot. So the trigger may not be drawing at all.

### Next retry

1. Run hands-free first: `selftest/freeform-idle.sh` opens Freeform and a board and never draws. It needs no owner, so it fits a lag-queue slot.
2. Only if that survives, run the owner: `freeform-owner.sh` with one stroke of the default pen, then wait.

Both runs use `plans/lag/next-ff3`'s two dylibs and `plans/freeform/out/ff-owner-boot.img`, which is built on kernel-ff4.

### Containment, proposed and not built

- Kernel: give non-compositor submissions a time budget and attribute a stall to its client. Then a timed-out client is killed and refused further submissions, instead of every client's fences being voided. This helps only if the kernel keeps running, which in ast102 it did not.
- Userspace: hand the device only to PencilKit, not to Freeform's in-process CoreAnimation, which ran on the CPU in the builds where drawing only crashed Freeform. That needs `MTLCreateSystemDefaultDevice` to answer per caller, and is a design to check first.

## Hands-free retry (prepared 2026-11-06, not run)

ast109 and ast111 never reached a board. Their `bgmtl-app.log` sections have no PencilKit pipeline. Freeform opened on its sidebar (`CRLiOSSidebarViewController`, a 106-pt navbar at UIKit y 50 or 68), and the `+` tap at sweettouch y 62 did nothing (`no visible effect` in ast109). Their frames are also unreadable, because `sweetfb_ast_hw=1` captures come out striped (`docs/lag.md`). So no stroke has yet reached PencilKit on Chlorophyll.

The image is `plans/freeform/out/ff-daily-boot.img`, built by `plans/freeform/build-images.sh --daily MANIFEST` from the B-noK5 daily's manifest. It uses the daily's own kernel (B-noK5, SHA256 f78dd695e3f1..., which already carries 2130 v4; the first build picked the unapproved K5 kernel 981c13cc..., which the audit caught, and was rebuilt as image bb120a9d19b9...), so no kernel build is needed. The boot line is the daily's with four changes. It drops `sweetfb_ast_hw=1` for readable frames, drops `sweetdaily=1 sweetapn=... sweetapnauth=...` to fit 607 bytes, which also keeps the modem out of the run, and adds `sweetappmetal=Freeform sweetdeadman=900 sweetselftest=1`. That is 573 bytes. The run pushes next-ff3's inject, libchlorophyll and launcher, the stack ast109 survived.

The test body is `plans/freeform/selftest/freeform-handsfree.sh`:

1. It opens Freeform, taps the first sidebar row and then the first board, so an existing board opens.
2. It goes back, taps new board, and tries five pen positions in the bottom toolbar. It keeps the first one after which `bgmtl-app.log` gains PencilKit lines.
3. It draws five strokes, goes back to the grid for the thumbnail, and goes home.
4. It writes `ff-hf.txt` (Freeform's pid at each step, PencilKit line counts, dispatch and commit counts), this boot's slice of the app log, and any Freeform crash report from this boot.

A status-bar tap every 15 s keeps auto-lock from turning the display off during waits.

Run command (inside the /tmp/phone.lock protocol):

```
python3 -I plans/devloop/dev.py plans/freeform/out/ff-daily-boot.img \
  /usr/local/lib/bgmtl/bgmtl-inject.dylib=plans/lag/next-ff3/out-metal/bgmtl-inject.dylib \
  /usr/local/lib/bgmtl/libchlorophyll.dylib=plans/lag/next-ff3/out-metal/libchlorophyll.dylib \
  /usr/local/lib/bergamota-gl/bgl-launch=plans/lag/next-ff3/out-metal/bgl-launch \
  --test ../../../freeform/selftest/freeform-handsfree --note ff-handsfree \
  --pull /private/var/tmp/bgmtl-app.log --pull /private/var/tmp/ff-hf.txt \
  --pull /private/var/tmp/ff-hf-app.log --pull /private/var/tmp/ff-hf-crash.ips \
  --key 'SWEET EXEC: appmetal' --key 'SELFTEST: FF ' --key 'CRASH Freeform'
```

Then `python3 -u plans/smooth/run-smooth.py promote` before the owner boots the daily: next-ff3 is not a proven daily driver.

Afterwards the root holds next-ff3's three files, not the daily driver. `plans/smooth/run-smooth.py promote` puts `drv/clean` back on its pinned check image.

It passes only if all of these hold:

1. The console has `SWEET EXEC: appmetal inject for /Applications/Freeform.app/Freeform`.
2. `ff-hf.txt` names a pen position, and `ff-hf-app.log` has `app dispatch` lines for `paint_kernel`, `particle_kernel` or `sdfPen_kernel`.
3. `ff-hf-app.log` has no `draw with no pipeline` and no `refusing` line.
4. Frames `ff-stroke1`, `ff-stroke2` and `ff-stroke5` show the strokes, and `ff-grid` shows the new board's thumbnail with them.
5. Freeform's pid is the same at every step, with no `CRASH Freeform`, an empty `ff-hf-crash.ips` and no new kill switch.
6. Recoveries are 0, backboardd restarts are 0, the result is `selftest-done` with no reset, and the root is clean.

A run that reaches a board but finds no pen is inconclusive, not a failure. Read the frames, fix `FF_PENS` or the other points at the top of the script, and rerun.

## ast231: hands-free retry (2026-11-06)

ast231 ran `ff-daily-boot.img` (bb120a9d19b9...) with `freeform-handsfree.sh` 3bb4b893 (saved as `plans/freeform/tests/old/freeform-handsfree-ast231.sh`). dev.py called it `reset-unknown` in 460 s, with a clean root, 0 recoveries and 0 backboardd restarts. Freeform kept pid 437 the whole time and left no crash report. It opened an existing board with app Metal, which the daily driver cannot do. No stroke reached a canvas, so PencilKit never drew: `app dispatches 0, commits 95, no pipeline 0`.

### What the frames show

The check frames are 360x797, and sweettouch points are 375x812, so a point is (frame x * 375/360, frame y * 812/797).

| Frame | Tap before it (point, frame) | What it shows |
|---|---|---|
| 01 ff-open | none | Freeform opened on All Boards, not on its sidebar. There are two cards: Untitled 2 at frame x 18-173, y 140-283, and Untitled. The search bar is at the bottom, with its mic at 259,753, and new board is the compose button at 316,753 |
| 02 ff-row1 | 100,178 = 96,175 | The Untitled 2 board opened, an empty dot grid. Back is at 36,61. The bottom toolbar holds a shapes button (44,753), then pen (130,753), text (180,753) and attachments (230,753) in one bar, then new board (316,753) |
| 03 ff-existing | 100,230 = 96,226 | The tap hit the canvas, so Freeform shows "Select Objects" |
| 04 ff-back | 27,71 = 26,70 | All Boards again |
| 05 ff-new | 343,71 = 329,70 | The ... menu (Select, Icons, List, Sort). On the grid, top right is ..., not new board |
| 06 ff-pen-300 | 300,761 = 288,747 | That tap only closed the menu |
| 07 ff-pen-257 | 257,761 = 247,747 | The search bar's microphone opened the Enable Dictation alert |
| 08 ff-pen-187 | 187,761 = 180,747 | The search field, with the keyboard up |
| 09, 10 | 327 and 117 at y 761 | Taps on the keyboard area. The first stroke swipe after 117 typed into search |
| 11-14 | strokes and back | The strokes typed "Week", so the screen reads "No Results for Week". Back landed on the keyboard |
| 15 settle | home | The home screen |

ast231's single "PencilKit line" after pen 117 was `render pipeline uber_vertex+fa17fmha4` at 353 s, which Freeform built while the search screen was showing. It was not a stroke.

### Script fix

The new `freeform-handsfree.sh`, measured from the frames above, has SHA256 `b27c15db8d8ba11df1910432596606227b6343ad5f0b113a6c456ba32e9abb12`.

| Name | Point | Frame | Source |
|---|---|---|---|
| `FF_CARD1` | 100 178 | 96,175 | first card. It opened a board in frame 02 |
| `FF_BACK` | 37 62 | 36,61 | the centre of the back button in 02. The old 27 71 also worked, in 04 |
| `FF_NEW` | 329 767 | 316,753 | the compose button in 01 and 04 |
| `FF_PEN` | 135 767 | 130,753 | the pen icon in 02 |
| canvas | x 80-300, y 250-600 | x 77-288, y 245-589 | the empty dot grid in 02, between the navbar (y < 85) and the toolbar (y > 725) |
| `FF_CANCEL`, `FF_NOTNOW`, `FF_BLANK` | 343 480, 187 470, 104 509 | 329,471, 180,461, 100,500 | ways out of search (08), the dictation alert (07) and the ... menu (05) |

The script now checks the screen after every navigation tap. `ff_grab` reads `kern.sweet_fb_shot` raw with `/var/jb/usr/sbin/sysctl -b`. That is the same buffer `sweetshot` turns into a PNG, a 16-byte header and then BGRA rows. The script accepts it only if the header reads 360x797 with 1440 bytes per row. `ff_count` reads boxes with `od` and counts pixels by colour class. `ff_screen` uses those counts to call the screen grid, menu, board, keyboard, dimmed or unknown:

| Box (frame) | Class | Grid | Board | Menu | Keyboard | Alert |
|---|---|---|---|---|---|---|
| title "All" 18-60, 97-122 | cyan | 352 | 0 | 352 | 0 | 0 |
| ... dots 312-334, 56-66 | dark | 26 | 26 | 0 | 0 | 0 |
| search mic 252-266, 743-763 | dark | 68 | 0 | 68 | 0 | 71 |
| pen 120-140, 743-763 | dark | 0 | 99 | 0 | 0 | 0 |
| paperclip 220-240, 743-763 | dark | 0 | 89 | 0 | 0 | 0 |
| keyboard margin 0-8, 600-700 | 226,227,231 | 0 | 0 | 0 | 683 | 0 |
| centre 160-199, 380-399 | dimmed grey | 0 | 4 | 0 | 0 | 280 |

The flow:

1. Open Freeform, then reach the grid with at most four bounded actions. Board means back, keyboard means cancel, the alert means Not Now, and the menu means a blank tap. An unknown screen gets one reopen and one back, and is then refused.
2. Tap the first card and require a board. Tap back and require the grid. Tap new board and require a board.
3. Tap the pen once. Nobody has seen the pen's toolbar yet, so this check is negative. The screen must be board or unknown, not grid, menu, keyboard or the alert, and at least 150 bytes below the status bar must have changed. There is no second pen tap, because it would put the pen away again.
4. Before each stroke, Freeform's pid must equal the one at open and the screen must keep the pen check's class. If Freeform crashes, the home screen would also read as unknown, and the pid check stops strokes landing there. Stroke 1 logs the ink at its midpoint and the PencilKit line count before and after. That is evidence for the test, not a gate.
5. Tap back, and tap back again if that only closed the pen. Require the grid.

A failed check writes `REFUSED at STEP` to `ff-hf.txt` and the console, keeps a `refused-STEP` frame, skips the rest of the navigation and runs the report as before. A run without a refusal should take about 3 minutes of waits plus a few seconds per check, against ast231's 5 minutes. The check cost on the phone is not measured; on the Mac one check takes about 0.6 s.

`plans/freeform/tests/handsfree_frames.sh` takes the script's frame-check functions from between its markers and runs them under dash. It feeds them ast231's 15 frames, converted back to the raw sysctl layout, through a stub sysctl. All 15 classify as the table says, the three diff checks pass, and a bad header is refused. Its SHA256 is `18e45ebed880aee274a0ee6d9778f88cac80d323b4f317b5cedd3746173d0ba5`. A host dry run with the same stubs went through the happy path, a keyboard restored from search, ast231's menu tap (refused at `new`) and a pen tap that changes nothing (refused at `pen`). The composed `renegade-firstboot.sh` body passes `dash -n` at 14,626 bytes, up from 8,078.

Assumptions, all inferred:

- `sysctl -b` writes the opaque value raw. The root's binary is procursus system_cmds, and its usage lists `-b`.
- Freeform stays in light mode. ast231 ran at the same hour in light mode, and a dark screen would be refused, not mis-tapped.
- The compose button on the grid opens the new board directly.
- The pen tap changes the screen enough to pass. If it does not, the run refuses at `pen` and the `refused-pen` frame shows the toolbar for the next fix.

### The reset: after the test, not during GPU work

Proven:

- The script reached its last frame. `15-settle.png` exists and shows the home screen. hook.sh moves the old frames directory aside at start, so every frame in it is from this boot. The only commands after `shot settle` are `say DONE`, `sync`, `sleep 1` and `sweetreboot fastboot`.
- The console's last stats line is `up_s=442`, at line 15764, just after `SELFTEST: step settle` at line 15758. The next stats line, due at 452, is missing. `step settle` came before 442 and `shot settle` at least 10 s later, so the log stops in the seconds where the script was calling `finish`.
- The root was clean. dev.py reads the state in recovery straight after the run, with no replay boot. Clean means `06-hfs-put.py` found the volume's unmounted bit set and an empty journal, which needs an orderly unmount. ast100 and ast102, the two real hard resets, both left the root dirty.
- The deadman (900 s) did not fire. There is no RECOVERY, no panic, no SMMU fault, and no stalled-fence kick after line 14051 (about up_s 211). The last GPU lines are ordinary stats (`submit_us avg=372`, 6,427 fences). The last ~30 lines are routine launchd work: fairplayd, bluetoothd, wifip2pd and seputil.
- The phone was back in fastboot 18 s after the last stats line. In ast196 to ast233, runs that end with DONE show 3 to 23 s.

Inferred: the reset was the script's own `sweetreboot fastboot`, and the console ring lost its last seconds, `SELFTEST: DONE` included. That is the same end-of-run `reset-unknown` HANDOFF counts in 17 of 45 runs. In this log series it also appears in ast198, ast199, ast211 and ast215, each with a 10 to 16 s gap.

Unknown: why the ring drops the tail in some runs. That predates Freeform.

Against ast100 and ast102: those reset during or right after Freeform's first GPU use, with dirty roots, stats that stopped mid-run, and in ast102 a stalled-fence kick as the last A810 line. ast231 shows none of that. Freeform held a Chlorophyll device for about 5 minutes, opened a board through app Metal, and built PencilKit's uber pipeline, all without a reset. This is the first Freeform run on Chlorophyll that did not hard-reset. It still says nothing about strokes, because none was drawn.

Containment: nothing is warranted by ast231. The proposal under ast102 stands as written and is still not built.

### Rerun

The same image, files, pulls and keys as the original command, inside the /tmp/phone.lock protocol, with the new script:

```
python3 -I plans/devloop/dev.py plans/freeform/out/ff-daily-boot.img \
  /usr/local/lib/bgmtl/bgmtl-inject.dylib=plans/lag/next-ff3/out-metal/bgmtl-inject.dylib \
  /usr/local/lib/bgmtl/libchlorophyll.dylib=plans/lag/next-ff3/out-metal/libchlorophyll.dylib \
  /usr/local/lib/bergamota-gl/bgl-launch=plans/lag/next-ff3/out-metal/bgl-launch \
  --test ../../../freeform/selftest/freeform-handsfree --note ff-handsfree \
  --pull /private/var/tmp/bgmtl-app.log --pull /private/var/tmp/ff-hf.txt \
  --pull /private/var/tmp/ff-hf-app.log --pull /private/var/tmp/ff-hf-crash.ips \
  --key 'SWEET EXEC: appmetal' --key 'SELFTEST: FF ' --key 'CRASH Freeform'
python3 -u plans/smooth/run-smooth.py promote
```

Before it, check `shasum -a 256 plans/freeform/out/ff-daily-boot.img plans/freeform/selftest/freeform-handsfree.sh`. The answers must be `bb120a9d19b9...` and `b27c15db8d8b...`.

The pass list under "Hands-free retry" stands, with three changes:

- Item 2's pen position is now fixed. Look for `FF navigation complete, 5 strokes` instead.
- The stroke frames are `ff-stroke1`, `ff-stroke2` and `ff-stroke5`, as before, and the pen frame is `ff-pen`.
- A `REFUSED at` line means a navigation result, not a failure of the trial. Read its frame, and its counts in `ff-hf.txt`.

Do not treat a `reset-unknown` as a hard reset until the root and the last frame have been checked, as above.

### Audit brief: script delta 3bb4b893 to b27c15db

Diff `plans/freeform/tests/old/freeform-handsfree-ast231.sh` against `plans/freeform/selftest/freeform-handsfree.sh`. It is a dev.py test body only. The image, kernel and dylibs are unchanged.

1. Check the points against the ast231 frames in `ios-asteroids/logs/ast231-ff-handsfree/frames` (gitignored) and the table above.
2. Check that `ff_grab` can only read. `sysctl -w kern.sweet_fb_shot_scale=3` is the one write, and `sweetshot` already makes it at every `shot`. The kernel handler refuses a write to `kern.sweet_fb_shot` and needs root, which the self-test has.
3. Check that every loop is bounded: `ff_to_grid` makes at most 4 passes, `ff_count` covers fixed boxes, there are 5 strokes and at most 2 back taps. Every tool call goes through `timeout` or is a local file read.
4. Check that no tap is sent when a check fails. Each check is followed by `|| return 1`, and the report runs after `ff_run`.
5. New files in `/private/var/tmp`: `ff-hf.raw`, `ff-hf-board.raw` and their `.band` copies, about 4.5 MB, overwritten each run. Nothing on the phone is deleted.
6. `renegade-firstboot.sh` grows from 13,544 to about 20,000 bytes. `06-hfs-put.py` allocated 1 block for ast231's growth and refuses a file that needs overflow extents, so a refusal would stop before boot.
7. Run `sh plans/freeform/tests/handsfree_frames.sh` and `dash -n`, and use `dev.py --print-script ../../../freeform/selftest/freeform-handsfree`. That prints and touches no phone.

## ast236: the hard reset after the pen tap (2026-11-06)

ast236 ran `ff-daily-boot.img` (bb120a9d...) with `freeform-handsfree.sh` b27c15db (saved as `plans/freeform/tests/old/freeform-handsfree-ast236.sh`) and next-ff3's three files. The frame checks classed every screen right: grid, existing board, grid, new board. The pen tap worked, because frame `05-ff-pen` shows the pen palette with the pen raised. Then the phone hard-reset: `reset-unknown in 239 s`, dirty root, 0 recoveries. Line numbers below are in `ios-asteroids/logs/ast236-ff-handsfree3.txt`.

### What the logs prove

1. The tap came at about 188 s of uptime. `SELFTEST: step ff-pen` is line 12740, after the `up_s=185` stats line (12612). backboardd's log ends with 11 glass pipelines built from 187.99 to 188.29 s, two of them `variable_blur`. Those are routine, since the same pipelines were built in dozens of runs from ast100 on with no reset.
2. Two 2-IB submissions followed the tap: fences 1063 and 1100, first IB 8191 dwords (lines 12745 and 12748). The log does not say which process sent them.
3. The kernel ran for at least 4 s after that. The script took its pen frame, read `kern.sweet_fb_shot` twice and ran 68 of the about 244 `od` calls of the screen check. The `pen:` note never reached the log. The log ends at line 12825, just after a CPU governor line (about 190 s), with no stats line for 195 s.
4. There is no stalled-fence kick after line 10791 (boot), and no RECOVERY, `watch:`, SMMU FAULT, hang report or panic text.
5. The pulled app logs are stale, not short. `bgmtl-app.log` has 16 lines from this boot and stops at `alive for 15 s`. `ff-hf-app.log` and `ff-hf.txt` are older copies. dev.py read the root without replaying its journal, so it saw each file at its last journalled size.
6. The kernel log loses its last 10 s on any stop. `sweet_log_thread` writes the ring every `ufslogsecs` seconds, 10 by default (`sweet_ufs.c:1355`). Only a panic writes it at once, and a panic flush is skipped if the UFS lock is held.
7. A GPU hang that the kernel noticed would be on disk. `waitFence` (`SweetAdreno.cpp:440`) kicks the write pointer after 50 ms. After 1 s frozen with the hang bit up (`sweetgpufast=1000`), it calls `sweet_gpu_report` before any recovery. That runs `sweet_a810_hang_report`, which syncs the log (`sweet_a810.c:1467`). Nothing like that was written, so the recovery path did not start before the stop.
8. The phone stayed silent much longer than an orderly end. These are seconds from the last stats line to fastboot:

| Runs | Seconds |
|---|---|
| ast236, ast100, ast102 | 54, 53, 51 |
| Runs ending in `DONE` with stats to the end, ast196 to ast246 | 2 to 23 |
| End-of-run `reset-unknown` with a clean root (ast198, ast199, ast211, ast215, ast231, ast243) | 9 to 18 |
| ast208, the Wi-Fi start whose kernel stopped (`plans/wifi/audit-w2b-brief.md`) | about 60 |

9. This image arms no system watchdog. It has `wdt=-1` and no `sweetgwdt`. `sweetpanicfb=10` resets 10 s after a panic.
10. No 3a log in about 245 runs holds a kernel panic text, and the forced-panic check in `plan-usb.md` step 4 has not been run. So it is unproven that a panic's text reaches disk on this phone.

### The cause: not established, narrowed

Inferred, most likely first:

1. The whole system stopped within 10 s of the pen tap. That includes the log thread and every periodic kernel line, not just the GPU. Something below the kernel reset the SoC 30 to 45 s later, the same shape as ast208. A bus stall that wedges every CPU fits this. The only new work at that moment was Freeform's first real Metal work, through the palette and PencilKit getting ready to draw.
2. A kernel panic whose text never reached disk. Step 10 leaves this open.

Ruled out or not supported:

- A GPU-only hang. In the 75 hangs analysed in `hangs.md`, the kernel stayed up, kicked, reported and recovered. Here the report path never wrote, and the system died.
- A translator bug in PencilKit's compute. No stroke was drawn, so no paint or particle dispatch was due. Those kernels use no threadgroup memory, atomics or indirect dispatch (table above), and ast83 ran `sdfPen_kernel` correctly.
- The pen palette's glass. backboardd drew it, and frame 05 shows it.

Unknown: what Freeform submitted after the tap, which access stalled, and what reset the phone.

The A810 recovery path is not why the system went down, because it never started. The per-client kill under "Containment" would act at the same point, after the 1 s watch verdict, so it would not have helped here. ast100 also stopped with no GPU submission all boot, though its stock backboardd makes it a weaker match.

### Fix: see the last seconds next time

No kernel or translator change is justified yet. Two changes need no kernel build:

1. `plans/freeform/out/ff-daily-log1-boot.img` is ff-daily plus `ufslogsecs=1`, at 586 of 607 bytes. The ring then reaches disk every second, as in the A630 bring-up (`ios-asteroids/docs/GPU_A630_BRINGUP.md`). `build-images.sh --daily MANIFEST NAME EXTRA` builds it. The same recipe without EXTRA rebuilds bb120a9d byte for byte, and the two payloads differ only in the 13 bytes of the argument. The daily kernel f78dd695 has the argument, with the same `sweet_log_start` and `sweet_log_thread` as `xnu-local`.
2. `freeform-handsfree.sh` (SHA256 956e5e93...) runs `ff_mirror` in the background from `ff_start`. Once a second it copies the app log's new whole lines to the console as `SELFTEST: FFAPP`. It drops `present:` lines, keeps at most 60 lines a second and logs a skipped count. It stops after 1200 passes, and the report step kills it. `plans/freeform/tests/handsfree_mirror.sh` checks it under dash: nothing from before `ff_start`, partial lines held back, the 60-line cap, a missing log, and no repeats. The frame checks pass unchanged. The composed `renegade-firstboot.sh` is 21,495 bytes and passes `dash -n`.

The daily needs no containment. It has no `sweetappmetal`, and every reset so far needed it.

### Next run

Inside the /tmp/phone.lock protocol:

```
python3 -I plans/devloop/dev.py plans/freeform/out/ff-daily-log1-boot.img \
  /usr/local/lib/bgmtl/bgmtl-inject.dylib=plans/lag/next-ff3/out-metal/bgmtl-inject.dylib \
  /usr/local/lib/bgmtl/libchlorophyll.dylib=plans/lag/next-ff3/out-metal/libchlorophyll.dylib \
  /usr/local/lib/bergamota-gl/bgl-launch=plans/lag/next-ff3/out-metal/bgl-launch \
  --test ../../../freeform/selftest/freeform-handsfree --note ff-handsfree4 \
  --pull /private/var/tmp/bgmtl-app.log --pull /private/var/tmp/ff-hf.txt \
  --pull /private/var/tmp/ff-hf-app.log --pull /private/var/tmp/ff-hf-crash.ips \
  --key 'SWEET EXEC: appmetal' --key 'SELFTEST: FF ' --key 'CRASH Freeform' \
  --key 'SWEET A810: RECOVERY' --key 'SWEET GPU: (watch|A810 SMMU FAULT)'
python3 -u plans/smooth/run-smooth.py promote
```

Check first: `shasum -a 256 plans/freeform/out/ff-daily-log1-boot.img plans/freeform/selftest/freeform-handsfree.sh` must give `017f904c...` and `956e5e93...`. If it resets again, the root is dirty and the promote spends one replay boot first.

Read the last 2 s of the kernel log:

| See | Means | Next |
|---|---|---|
| `FFAPP` commit or dispatch lines, then one kick or none, then nothing | A Freeform command buffer stops the SoC | Dump that command buffer with `bgmtl.faildump` and decode it with `plans/gpu-next/tools/decode_hangs.py` |
| A kick or `watch:` line, then a hang report cut short | The CPU stopped inside the GPU report or recovery | Find the register the report or recovery touched last |
| `panic(cpu` | A panic | Read it |
| No `FFAPP` line after the tap, and the kernel stops | Freeform's Metal is not the trigger | Run the same steps without `sweetappmetal` |
| Strokes drawn and no reset | Timing changed, or the reset is rare | Read the stroke frames as under "Hands-free retry" |

### Audit brief: ast236 instrumentation (script 956e5e93, image 017f904c)

1. Diff `plans/freeform/tests/old/freeform-handsfree-ast236.sh` against `plans/freeform/selftest/freeform-handsfree.sh`. Expect only the mirror block, its start after `ff_start` and its kill in the report step.
2. Check that the mirror only reads `FF_LOG` and appends to `/dev/console`, that it is bounded (1200 passes of 1 s), and that no tap or check depends on it.
3. Run `sh plans/freeform/tests/handsfree_mirror.sh` and `sh plans/freeform/tests/handsfree_frames.sh`. Compose the body against `ios-asteroids/logs/ast236-ff-handsfree3/renegade-firstboot.before` with `dev.py`'s `compose_script` and run `dash -n` on it.
4. Run `dev.py --print-args` on both images. Expect `ufslogsecs=1` as the only difference. Check that `sweet_ufs.c:1539` accepts 1 (1 to 60), and that nothing else in the line changes. That keeps glass, `sweetfb_hz=60` and no `sweetpwrfast`.
5. Check the `build-images.sh` change: the NAME check keeps output under `out/`, and the default `--daily` call still gives bb120a9d.
6. Weigh the cost of a 1 s flush: one ring write, one header write and SYNCHRONIZE CACHE each second under `sweet_ufs_lock`. On the 3a it is untested.
7. Expect the run to reset again. That is the point of it.

## ast248: the reset again, and whether the frame check causes it (2026-11-06)

ast248 ran `ff-daily-log1-boot.img` (017f904c) with `freeform-handsfree.sh` 956e5e93 and next-ff3's three files. It hard-reset like ast236: `reset-unknown in 238 s`, dirty root, 0 recoveries. The frame checks classed grid, existing board, grid and new board, and frame `05-ff-pen` shows the pen palette. Line numbers below are in `ios-asteroids/logs/ast248-ff-handsfree4.txt`. Kernel sources are in `research/xnu-daily-b0/dxb/xnu`, the tree that built f78dd695 (`plans/daily-data/c/README.md`).

### What the 1 s log shows

1. The last `FFAPP` line is at 11562, from Freeform's launch. ast231 shows that Freeform logs one `app commit` line per command buffer once it draws through app Metal, and the mirror passes those on. None came, so Freeform's own device submitted nothing between launch and the stop, except perhaps in the last 1 to 2 s the log can lose.
2. There is no 2-IB submission after the tap. The last is fence 759 at boot (10706), so ast236's fences 1063 and 1100 are not needed for the reset.
3. backboardd built the same palette pipelines at 187.1 to 187.4 s that ast236 built at 188.0 to 188.3 s, `variable_blur` and `glass_foreground` included.
4. The pen tap is at 12646, between `up_s=184` and `up_s=194`. After it come `sweetshot`, the scale write and the raw read (12668 to 12672), then 257 toybox calls with the `up_s=194` stats line among them, and the log ends. The pen check makes 1 header `od`, 244 box `od` calls and 5 calls in `ff_changed`. The mirror adds one `wc` a second. The earlier checks of this boot show 252 to 255 calls between the read and their note. So the stop came at the end of the pen check, after `ff_changed` began and before the `pen:` note. That is about 11 s after the tap and 7 to 10 s after the last frame read.
5. The FB timing lines stay in their usual range up to the end: `handler_us max=32275` in the last window against 40377 at `up_s=174`.

### kern.sweet_fb_shot

The handler is `sweet_fb_shot_sysctl` (`bsd/dev/sweet_fb.c:81`). It is `CTLFLAG_LOCKED`, so no sysctl lock is taken. It refuses writes, needs root, takes a 1,453,528-byte buffer with `kalloc_data`, calls `sweet_fb_shot_copy` and copies out 1,147,696 bytes. It does not read 6 MB.

`sweet_fb_shot_copy` (`iokit/Kernel/arm/SweetFramebuffer.cpp:1364`) takes no lock and touches no register.

- In zero-copy or scan-out mode it reads a pooled IOSurface held by a reference. ast248 has `zc_frames=0`, so it took the other branch.
- In AST flip mode it reads `ast.va[ast.front]`. Those are permanent kernel mappings made in `astSetup` (`:2335` to `:2342`), `kIOMapWriteCombineCache` over `cont_splash_region`, 0x9dad00 bytes each. No code releases `fBufMap`.
- The copy is 360 x 797 strided 4-byte CPU loads, every 3rd pixel of every 3rd row. That touches about 3.4 MB of the 10.3 MB front buffer.
- A flip during the copy can only mix two frames, as its comment says (`:1357`). The plain load of `ast.front` races the same way.
- The CPU path does not go through the SMMU. The MDP's scan-out and the GPU's writes use the same DRAM through their own masters, and a CPU read of normal non-cacheable memory cannot stall them.

The compositor reads this memory far harder. Each copied frame is a 10.3 MB `memcpy` out of `ast.va[ast.front]` (`:4653`), at `copy_us avg=12919` over 617 copies by `up_s=194`. One shot is a fraction of one such frame.

Each check makes 2 handler calls (`shot NAME check` through sweetshot, then `ff_grab`'s `sysctl -b`) and one write of the scale, which is already 3. Before the stop ast248 made 10 calls in about 70 s.

### Conclusion

Proven:

- The handler holds no lock across the copy, maps nothing, writes no register and reads only memory that stays mapped.
- The same handler ran 2,173 times in 180 logged runs through sweetshot, 14 of them in ast231 while Freeform held a Chlorophyll device. None of those runs reset at a shot.
- In ast236 and ast248 the same check, with the same 2 reads and about 250 tool calls, passed 4 times in each boot before the pen check.
- ast248 stopped 7 to 10 s after its last read, while `od` and `cmp` were reading regular files.
- ast100 hard-reset with no sweetshot and no raw read.

Inferred: the fb_shot read is not the cause, and it needs no kernel fix. passcode-t1 uses only sweetshot, and cc-check uses the same raw read, so they keep running as they are.

Still open: the reset needs either something the pen tap starts, or the frame-check script as a whole. The script's process and file load includes `ff_changed`, which only the pen check runs: two 1.1 MB writes to `/private/var/tmp` and a `cmp -l`. Both are in ast236 and ast248, and both are absent from ast231. The two runs cannot separate them. ast248 makes a Freeform app-Metal command buffer unlikely as the trigger, by item 1.

### The blind arm

`plans/freeform/selftest/freeform-blind.sh`, SHA256 `cd6fc9a420378f781b451e0cd537d07864a304b4f8196153555f9a0444846d92`, uses the same image and files. It keeps 956e5e93's taps, waits, mirror, pid checks and report, and drops every frame check.

- It makes no frame read from Freeform's launch until `shot ff-stroke5`. The hook's `shot settle` on the home screen comes after that.
- It waits 15 s after the pen tap before stroke 1. That spans ast248's stop, about 11 s after the tap, with no tool running.
- From `step ff-new-board`, a background `ff_beat` writes `SELFTEST: FF beat N` once a second, for at most 150 passes. With `ufslogsecs=1` the last beat on disk dates the stop to about 2 s. The report step kills it with the mirror.
- A changed Freeform pid stops the strokes, with no swipe and no frame.

The navigation is blind. ast236 and ast248 both reached grid, board, grid and board with these points, so the risk is a stroke in the wrong place inside Freeform. A crash cannot send strokes to the home screen, because of the pid check.

On the host, a dash dry run with stub tools gave the taps in order: card, back, new, pen, 5 swipes and back. `shot ff-stroke5` was the only frame before `settle`, the beat stopped at the kill, and a crash after the pen tap stopped the strokes with no frame. The body composed onto `ast248-ff-handsfree4/renegade-firstboot.before` the way `compose_script` does it is 15,438 bytes, under ast248's 21,495, and passes `dash -n`. The same composition of 956e5e93 rebuilds ast248's `renegade-firstboot.sh` byte for byte.

Inside the /tmp/phone.lock protocol:

```
python3 -I plans/devloop/dev.py plans/freeform/out/ff-daily-log1-boot.img \
  /usr/local/lib/bgmtl/bgmtl-inject.dylib=plans/lag/next-ff3/out-metal/bgmtl-inject.dylib \
  /usr/local/lib/bgmtl/libchlorophyll.dylib=plans/lag/next-ff3/out-metal/libchlorophyll.dylib \
  /usr/local/lib/bergamota-gl/bgl-launch=plans/lag/next-ff3/out-metal/bgl-launch \
  --test ../../../freeform/selftest/freeform-blind --note ff-blind \
  --pull /private/var/tmp/bgmtl-app.log --pull /private/var/tmp/ff-hf.txt \
  --pull /private/var/tmp/ff-hf-app.log --pull /private/var/tmp/ff-hf-crash.ips \
  --key 'SWEET EXEC: appmetal' --key 'SELFTEST: FF (?!beat)' --key 'CRASH Freeform' \
  --key 'SWEET A810: RECOVERY' --key 'SWEET GPU: (watch|A810 SMMU FAULT)'
python3 -u plans/smooth/run-smooth.py promote
```

Check first: `shasum -a 256 plans/freeform/out/ff-daily-log1-boot.img plans/freeform/selftest/freeform-blind.sh` must give `017f904c...` and `cd6fc9a4...`. Afterwards, `grep 'FF beat' LOG | tail -1` gives the last second on disk.

| Result | Means | Next |
|---|---|---|
| Dirty root, beats stop within about 20 s of `pen tapped`, no frame read before | The frame check is not needed for the reset. The pen tap is | Run the same arm without `sweetappmetal`, to split Freeform's device from backboardd's palette (row 4 of the ast236 table) |
| `navigation complete, 5 strokes`, `selftest-done`, clean root | The check, or its timing, is needed. One clean run against two resets is weak | Run this arm once more. If it is clean again, run it with the pen check's `od` and `cmp` load on a copied file and no sysctl, to split the read from the load |
| Clean or end-of-run `reset-unknown`, `settle` frame present | The ring lost its tail, as in ast231 | Read it as a clean run |
| No strokes in `ff-stroke5`, no reset | Navigation drifted | Read the frame. Only the pen check has any info about this screen |

### Audit brief: blind arm (script cd6fc9a4, image 017f904c)

1. Diff `plans/freeform/selftest/freeform-handsfree.sh` against `freeform-blind.sh`. Expect the frame-check block, `ff_to_grid`, `ff_expect` and every `ff_grab` gone. Expect the taps, waits, stroke list, mirror block and report unchanged, apart from killing `$ff_beat_pid`.
2. Check that nothing reads `kern.sweet_fb_shot` before `shot ff-stroke5`. That covers hook.sh's preamble.
3. Check that `ff_beat` is bounded, writes only to the console, and that no tap depends on it.
4. Check `ff_same` without its frame: a pid change still returns 1 before any swipe.
5. Run `sh plans/freeform/tests/handsfree_mirror.sh` on the copied mirror block, or diff the block byte for byte. Compose the body and run `dash -n`.
6. Check the `--key 'SELFTEST: FF (?!beat)'` regex against `dev.py`'s use of `re.search`.
7. Expect a reset. That answers the question as well as a clean run does.

## ast254: the blind arm resets after stroke 1 (2026-11-06)

ast254 ran `ff-daily-log1-boot.img` (017f904c) with `freeform-blind.sh` cd6fc9a4 and next-ff3's three files. It made no frame read. The pen tap and the 15 s wait passed, stroke 1 drew (`PencilKit lines 0 -> 57`), and then the phone hard-reset: `reset-unknown in 250 s`, dirty root, 0 recoveries. Line numbers below are in `ios-asteroids/logs/ast254-ff-blind.txt`. Times are uptime. The app log's `at N ms` follows uptime to within about 1 s, because commit #7 (184871 ms) reached the console before the `up_s=185` stats line (13448).

### What the logs prove

1. The frame check is not needed for the reset. Nothing read `kern.sweet_fb_shot` in this boot before the stop.
2. Freeform's GPU work ran from 184.1 s (commit #1) to 191.07 s (commit #49). That covers 3 `paint_kernel` dispatches, the sixChannelBlend and uber pipelines, and the copied write-backs of surfaces 188, 204 and 205.
3. The GPU was idle and healthy for the last 15 s of the log. The last stalled-fence kick is line 13523, at about 188.5 s. No kick, `watch:`, RECOVERY, SMMU FAULT or hang report came after it. backboardd made 0 swaps between `up_s=195` and `up_s=205` (752 both times). The FB tick still ran at 60 Hz, with `repeated` going from 9305 to 9908.
4. The kernel log ends at about 207.5 s. Beat 50 follows the `up_s=205` line, then beats 51 and 52 come, then `stroke 1`, then `exec ps` (the pid check before stroke 2).
5. The system ran past that point. The pulled `bgmtl-app.log`, which next-ff3 fsyncs every line, ends with two lines the console never got: `slow: pipeline build A3Xghfc: 18736.4 ms`, then `render pipeline VfxXgh+A3Xghfc ... at 208978 ms`. Freeform's in-process CoreAnimation began that build at 190.24 s, logged as `pipeline path: newRenderPipelineState...options reflection (none)`, and it returned at 208.98 s. The stop came at or after 208.98 s, and before the kernel log's next flush, about 1 s later.
6. ast102 shows the same sequence before its reset. Freeform loaded QuartzCore's metallib, built `VfxXh+BclrA3Xhf` and `VfxU10Xh+TcimA3Xhf_Isrc`, dumped `VfxU10Xh`, and logged `pipeline path ... reflection (none)`. Its app log ends on that line. ast231, the one Freeform run with app Metal that did not reset, never loaded QuartzCore in Freeform. Its only reflection-path build was RenderBox's `fa17fmha4`.
7. backboardd builds `VfxXgh+A2Xghfc`, with the same binding masks (fragment buffers 0x1bf, textures 0x7ff8, samplers 0x3), 28,587 times across the logs. Its build times run from 137 ms to 44.4 s (`ast155`, `ast117`, `ast179`) with no reset. No process but Freeform in ast254 has built `A3Xghfc`.

### The other suspects

- `paint_kernel` cannot write past its buffers by rounding. `dispatchThreads` rounds up to whole groups (`next-ff3/Chlorophyll/src/bgmtl.m:2898`), but the AIR guards `gid + 1 < n` (table above). Its last dispatch was 18 s before the stop.
- The GPU SMMU enforces. Bank 0 reads back SCTLR 0x167, with M on, faults terminated and the fault interrupt on (line 3456). sCR0 0x2c0406 has USFCFG set, so an unmatched stream faults instead of bypassing (line 2753). `smmu=untested` in the FB stats is the display's bypass test, `fSmmuA` (`SweetFramebuffer.cpp:1781`), not the GPU's. The GPU reaches only pages the kernel mapped, and `releaseGem` and the 64-submission quarantine keep those pages until they are unmapped (`SweetAdreno.cpp:651` to `:766`). The limits stand as documented: one GPU address space for all clients, GPU-writable ring and memptrs, and an unprotected CP.
- The copied write-back is a userland `memcpy` under `IOSurfaceLock`, bounded by `bgCopiedPlain` (bytes per row at least width times 4, height within the surface) (`bgmtl.m:309`, `:3558`). The last one ran at 190.83 s. Freeform logged again at 199 s and 209 s, so it did not deadlock, and the kernel ran 18 s after it.
- The GPU wait is high but not a hang. ast254 has 62 stalled-fence kicks, the same as ast253 (passcode-t1, no reset), and the last 25 runs range from 23 to 62. The app's longest wait was 531 ms, under the 1 s watch. The FB copy maximum of 277,613 us during the stroke is the largest in those 25 runs (the others are 70,919 or less), and the system ran on.
- The stalled-fence lines are routine (above), and none came after 188.5 s.

### The cause: not established, narrowed again

Inferred: the first command buffer that draws with Freeform's in-process `VfxXgh+A3Xghfc`, or the end of its build, stops the SoC within about 1 s. That is too fast for the 50 ms kick and the 1 s watch to reach disk. ast254 fixes the time to within about 1 s of the build returning, and ast102 stops right after the same build began.

Against it: ast248 mirrored no Freeform Metal line after launch and stopped about 11 s after the pen tap. That fits only if the whole QuartzCore sequence took under the 1 to 2 s the log can lose. That sequence took 0.8 s in ast254 before its build, and backboardd's sibling build often takes under 300 ms. ast100 and ast236 have no app log from the end.

Unknown: what `A3Xghfc` is, whether its draw or its build is the trigger, and what stops the SoC.

### Fix

There is no fix yet. bgmtl already has the switch for this: `/private/var/tmp/bgmtl.refuse` lists fragment-function prefixes to refuse, and `bgmtl.refuse.build` still compiles and times them without drawing (`bgmtl.m:359`, `:1700`). It has never run on the phone. The reflection-free path that Freeform's CoreAnimation takes goes through that check (`bgmtl.m:1858`). The daily driver has no `sweetappmetal` and needs no containment.

### Next run: the refuse arm

`plans/freeform/selftest/freeform-refuse.sh`, SHA256 `ca4700a35ba4425f6fb9a862d7a7106ca2f0f5c29cba84e058eb0654395e4428`, is `freeform-blind.sh` with three changes.

1. Before Freeform opens, it writes `A3Xghfc` to `bgmtl.refuse` and creates `bgmtl.refuse.build`. backboardd read the absent list at its first pipeline of this boot and keeps it, since the list is read once per process.
2. The report notes the refused lines and the size of each AIR file in `/private/var/tmp/bgmtl-refused`.
3. The report moves both switch files aside with `mv`, as the script already does for the kill switch.

No dylib, kernel or image changes. A host dry run with stub tools, paths moved under /tmp, gave the switch before `openapp`, the same taps as the blind arm, then the refused count, the AIR list and both files moved aside. `dash -n` passes. The composed body should be about 16,100 bytes, against the blind arm's 15,438.

Inside the /tmp/phone.lock protocol:

```
python3 -I plans/devloop/dev.py plans/freeform/out/ff-daily-log1-boot.img \
  /usr/local/lib/bgmtl/bgmtl-inject.dylib=plans/lag/next-ff3/out-metal/bgmtl-inject.dylib \
  /usr/local/lib/bgmtl/libchlorophyll.dylib=plans/lag/next-ff3/out-metal/libchlorophyll.dylib \
  /usr/local/lib/bergamota-gl/bgl-launch=plans/lag/next-ff3/out-metal/bgl-launch \
  --test ../../../freeform/selftest/freeform-refuse --note ff-refuse \
  --pull /private/var/tmp/bgmtl-app.log --pull /private/var/tmp/ff-hf.txt \
  --pull /private/var/tmp/ff-hf-app.log --pull /private/var/tmp/ff-hf-crash.ips \
  --pull '/private/var/tmp/bgmtl-refused/VfxXgh+A3Xghfc.v.bc' \
  --pull '/private/var/tmp/bgmtl-refused/VfxXgh+A3Xghfc.f.bc' \
  --key 'SWEET EXEC: appmetal' --key 'SELFTEST: FF (?!beat)' --key 'CRASH Freeform' \
  --key 'SWEET A810: RECOVERY' --key 'SWEET GPU: (watch|A810 SMMU FAULT)' \
  --key 'refused pipeline|refused by bgmtl'
python3 -u plans/smooth/run-smooth.py promote
```

Check first: `shasum -a 256 plans/freeform/out/ff-daily-log1-boot.img plans/freeform/selftest/freeform-refuse.sh plans/lag/next-ff3/out-metal/bgmtl-inject.dylib` must give `017f904c...`, `ca4700a3...` and `d24d22af...`.

| Result | Means | Next |
|---|---|---|
| `refused pipeline ... built in N ms: ok`, 5 strokes, no reset (or an end-of-run `reset-unknown` with the settle frame) | Building `A3Xghfc` is harmless, and drawing it is needed for the reset | Read the pulled AIR on the Mac (`air2nir2 --report`, spills, bindings), then dump the first command buffer that draws it with `bgmtl.faildump` |
| Dirty root, stop within about 2 s of the `built in` line | The build, or what follows it, is enough | Rerun without `bgmtl.refuse.build` |
| Dirty root, no QuartzCore line from Freeform before the stop | `A3Xghfc` is not needed, as ast248 suggested | Run the blind arm without `sweetappmetal` (row 4 of the ast236 table) |
| Freeform's pid changes at the refusal, with a crash report | CoreAnimation does not take a nil pipeline | Read the crash, then rerun with the refusal for the draw only |
| No refused line and no reset | Freeform never asked for it this time | Run the arm once more |

Freeform's CoreAnimation can also wait for good on the nil pipeline, as backboardd's did when it was answered nil with reflection asked (`bgmtl.m:1850`). That stalls only Freeform's in-process renderer, and it still answers the question, because nothing draws `A3Xghfc`.

### Audit brief: refuse arm (script ca4700a3, image 017f904c, next-ff3 unchanged)

1. Diff `freeform-blind.sh` against `freeform-refuse.sh`. Expect the header, the five setup lines before `ff_run` and three report lines. Taps, waits, mirror and beat are unchanged.
2. Read `bg_refused`, `bg_refuse_dump` and the refuse block in `newRenderPipelineStateWithDescriptor:error:` (`bgmtl.m:359` to `:396`, `:1700` to `:1720`), and check the strings are in `out-metal/bgmtl-inject.dylib` (d24d22af). Confirm that the reflection-free method at `:1846` reaches it, and that the reflection-asked path backboardd uses does not.
3. Confirm the list is read once per process (`dispatch_once`), so backboardd keeps the list it read at boot. Check that no bgmtl process except Freeform starts after the switch in a normal run. If one did, check that `A3Xghfc` never matches a backboardd fragment name: there are 0 builds in any log.
4. Check that a leftover switch after a reset is harmless on the next boot (promote, daily). backboardd never builds `A3Xghfc`, and the files hold no other prefix.
5. Check the writes: `bgmtl.refuse`, `bgmtl.refuse.build`, their `.saved.<date>` copies, and bgmtl's own `bgmtl-refused/` (one copy per pipeline). All are in `/private/var/tmp`. Nothing is deleted.
6. Check the two `--pull` paths, which hold a `+`, against `sweet-hfs-pull.py`. It prints `not found` for a missing file and goes on.
7. Run `dash -n` and the dry run. Compose the body onto `ast254-ff-blind/renegade-firstboot.before` the way `compose_script` does, and check its size against the 1-block growth `06-hfs-put.py` allows.
8. Expect a reset in most outcomes. A clean run is the informative one.
