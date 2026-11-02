# 2152 and 2153 re-audit

2026-11-02. Desk review for an isolated test boot.

2152: NO-GO. The rewrite fixes the ordinary v1 rollback and event-order cases, but it still cannot meet the owner's bounded physical recovery requirement. The panel readiness check also releases frames before the driver's own sleep-out settling interval has elapsed.

2153: GO on the verified 2120 staged base. Its scheduling logic is unchanged. This does not clear 2152, or the documented upstream installation recipe, which is missing prerequisites.

I did not access the phone, adb, fastboot, or the running test loop. I did not edit either patch or the staged kernel sources. All application and experimental model changes were on scratch copies. Kernel source references below use `S/` for `plans/input/work/s4/`.

Reviewed patch SHA256 values:

- 2152: `98c90f83a88cc78711e0a49f9b3b99b42cecd5d703aa8ef354d8a6efc5aa7054`.
- 2153: `e199401666c882b7ddfc6a69d58836100e0dd8851e187972880cc5acb08066a0`.

## Blocking findings

### 1. A permanently failing restore command leaves the panel dark indefinitely

`S/bsd/dev/sweet_dsi_asteroids.c:231`, `:243`, `:249`; `S/iokit/Kernel/arm/SweetFramebuffer.cpp:2508`, `:2533`.

Concrete scenario: speculative DCS 28 succeeds, iOS stays ON, and every rollback DCS 29 times out. Expiry posts ON and clears the speculative state, but the worker only retries. It has no recovery deadline, reset path, or independent way to illuminate the panel. Retrying forever is not bounded recovery.

Proved by reading. Also reproduced in a scratch copy of the supplied model by changing the DCS 29 scenario's failure count from 1 to 100000. Darkness lasted 7891 ms in the nine-second simulation and had not recovered at its end. This is a simulation result, not a phone measurement.

The failure loop does not hold the framebuffer gate or `fLock`, so it does not directly deadlock the display. It does consume repeated command time and cannot restore physical visibility. With a permanently failing sleep-out command, the new 500 ms frame wait eventually expires and frames proceed against a still sleeping panel. If counters stop there, the existing latch fallback can stop or freeze scanout too.

This blocks 2152 under the stated hard requirement. A source-only guarantee against a permanently broken sole DCS control path needs an independent recovery mechanism or a narrower approved requirement. Increasing retries does not supply one.

### 2. A timed-out command is treated as definitely having had no physical effect

`S/bsd/dev/sweet_dsi_asteroids.c:156`, `:164`, `:231`, `:235`; `S/bsd/dev/sweet_dsi_asteroids.c:97`; `plans/input/host-check/fast_power_model.c:123`, `:136`.

Concrete scenario: DCS 28 reaches the panel, but the host never observes DMA completion. The worker retains recorded state ON. Before any retry completes, expiry changes the target back to ON. The planner sees recorded ON equal to target ON and sends no DCS 29. The physical panel can then remain dark even if later commands would work.

The driver reads the DSI host's DMA-done bit. It does not read panel power status. A timeout establishes uncertainty about delivery, not proof that the panel ignored the command. The current model uses one variable for physical and recorded state and explicitly prevents a failed command from changing it, so this counterexample is excluded by construction.

Proved by reading for the missing uncertain-state handling. Physical delivery despite a missing completion is an inferred fault case, not observed hardware behavior. A scratch model with separate recorded and physical state, and DCS 28 delivered despite timeout, stayed dark for 7849 ms through the end of the run after rollback became a planner no-op.

This blocks 2152. Keep an unknown state after timeout and actively reconcile the target through a safe command sequence or status check. The last successful completion alone is insufficient once a later command may have partially succeeded. This also affects ordinary panel power changes with `sweetpwrfast=0`.

### 3. Sleep-out completion is published before the required settling interval

`S/bsd/dev/sweet_dsi_asteroids.c:118`, `:234`, `:235`; `S/iokit/Kernel/arm/SweetFramebuffer.cpp:1434`, `:1438`; `S/iokit/Kernel/arm/SweetFramebuffer.cpp:1604`.

Concrete scenario: iOS requests ON while the panel sleeps. DCS 11 completes, and the worker immediately publishes DARK. The next framebuffer tick passes the ASLEEP check and calls `present`, even though fewer than 80 ms have elapsed since sleep-out. The planner enforces that 80 ms only before another DCS command. It does not enforce it before MDP flips.

Proved by reading. Instrumenting the supplied model to count flips during its own 80 ms sleep-out interval found three in `f7-flip-while-asleep`, where the original check reports zero. The original counter recognizes ASLEEP only and considers the panel ready immediately at DMA completion.

Whether this panel's counters remain stopped during that interval is inferred and needs hardware evidence. If they remain stopped, an early flip can hit the 25 ms latch timeout and fallback before the intended 500 ms readiness wait protects it. This is precisely the unverified interaction that the new readiness gate claims to prevent.

This blocks 2152's panel path until readiness includes settling, or evidence establishes that flips are safe earlier. It affects panel modes 1 and 2 even with fast power disabled. The 500 ms escape also deliberately allows flips while still ASLEEP after repeated DCS 11 failures, so the stronger claim that sleeping-panel flips never occur is false.

### 4. Pixel rollback abandons its retained image if the new blank or restore flip fails

`S/iokit/Kernel/arm/SweetFramebuffer.cpp:246`, `:258`, `:276`, `:2521`, `:2526`, `:2530`.

Concrete scenario: `sweetpanel=0`, hardware scaling enabled, a speculative blank latches successfully, and the flip back times out. `astFallback` seeds the boot buffer from the current black front. With hardware scaling, the seed is explicitly black. It stops asteroids flipping. `fastRun` then clears `fFastKept` and its timer, with no independent recovery attempt. With no valid new GPU frame, iOS remains ON and the black screen persists.

A failed speculative blank can also enter fallback before rollback. The eventual restore guard rejects `ast.stopped`, so retaining the old buffer does not establish recovery on that path either.

Proved by reading the control flow. The occurrence of a latch failure is a fault assumption, not a measured event. The host model's `flip` always succeeds, has no stopped or frozen state, and cannot exercise either case.

This blocks 2152 under the hard requirement. An existing fallback does not cover a new speculative transition if it preserves black and discards the only rollback route. Recovery must preserve a usable image and account for flip failure before clearing the rollback obligation.

### 5. The stated upstream apply order omits the local 2043 through 2049 prerequisites

`plans/input/README.md:25`; `3a-IOS/patches/kernel/2051-asteroids-vsync-tick-deadline.patch:9`; `ios-asteroids/xnu/patches-12377/2047-asteroids-bilinear-scale.patch:176`.

Concrete scenario: start from the named upstream `asteroids-ios26` branch, then apply 2051 through 2120 as documented. The current upstream branch is `1537cf52ea32587c89f834ce77e1716cb9926f44`, ending at kernel patch 2042. It has no `SweetFbScale.h`. Patch 2051 modifies that file, which local patch 2047 creates.

Proved by running `git ls-remote`, inspecting the matching local commit, reconstructing its framebuffer files from its committed patches on a scratch directory, and running actual `git apply --check` for 2051. It fails with `SweetFbScale.h: No such file or directory`.

This blocks the documented clean installation recipe for both patches. It does not invalidate 2153's GO on the separately verified staged 2120 base. Specify the missing prerequisite series or an exact prepared base revision. The full corrected upstream bootstrap has not been verified here.

## Nonblocking integration finding

### 6. Pixel darkness extends CoreAnimation surface holds across the speculative window

`S/iokit/Kernel/arm/SweetFramebuffer.cpp:1423`, `:1457`, `:1462`, `:1524`, `:1604`, `:1864`, `:1872`, `:4533`.

Concrete scenario: `sweetfb_keep=1`, a retained previous swap and pending swap hold the two surfaces that CoreAnimation wants to reuse, then speculative pixel darkness begins while iOS keeps rendering. The tick returns before consuming either record and releasing its holds. The early swap path also returns. The normal 2120 release after copying cannot run until that window ends or input cancels it.

Proved by reading for the deferred holds. A hold-wait timeout or resulting bars depends on the user's renderer and surface reuse timing. The handoff already records a 50 ms hold-wait failure with keep. A 300 to 3000 ms speculative pause exceeds that timeout substantially. The model represents frames as IMAGE or BLACK and has no surface ownership or hold-wait behavior.

This is nonblocking for the isolated source-level verdict, but keep and hold-wait are not cleared by these host checks. Test them explicitly after the blocking fixes. Retaining framebuffer pixels itself needs no CoreAnimation surface hold, so rollback ownership and pending-render ownership should be handled separately.

## What the rewrite actually fixes

By reading, the ordinary no-fault rollback no longer needs a new GPU frame. Panel speculation targets DARK with DCS 28 and restores with DCS 29. Pixel speculation keeps `ast.front`, flips another slot black, and skips tick presentation, HUD work and early swap processing while dark. In the normal successful-flip path, that retained slot is not reused by those paths, including after a GPU epoch change.

The queue captures display state and generation before delivering power events to HID. Release eligibility uses the press generation. This addresses previous finding 2. Touch and other-key cancellation clear early wake and post ASLEEP from `fastRun`, addressing previous finding 6. Early wake targets DARK through DCS 11 and does not itself request DCS 29.

The second thread call is armed at the speculative deadline. Values outside 300 to 3000 ms reset to 2000 ms, rather than saturating to the nearest endpoint. The permitted window is bounded, but the physical result is not bounded by that timer. Both calls still acquire the framebuffer work-loop gate. Neither the code nor model proves the documentation's approximate 50 ms maximum gate delay.

The panel planner walks one successful command at a time and rechecks targets between commands. Backoff is 20, 40, 80, 160, 320, 640, then 1280 ms, plus command time and scheduling. New posts can wake that wait early, so it is not a strict minimum retry interval. Completion accounting is improved, subject to finding 2.

## Queue, locking and lifetime

By reading, the 16-entry queue is fixed-size and lock-protected. A full queue sets `lost` without advancing the head. The next drain cancels dark, awake and press eligibility. Unsigned head/tail subtraction remains valid across wrap because occupancy stays at most 16. A press storm does not allocate an unbounded queue or repeatedly extend an already active window. No out-of-bounds queue access was found.

Queue draining releases the simple lock before taking `fLock`. Producers do not take `fLock` or perform DCS work. Fast callbacks share the work-loop gate with presentation, and the panel worker performs DCS outside that gate. I found no new lock-order cycle in those paths.

The raw global framebuffer pointer and thread-call parameters do not retain the object. There is no new stop/free cleanup, pointer withdrawal, call cancellation with draining, or simple-lock disposal. The current AppleCLCD implementation has no provider teardown path in this file, and closing a user client terminates the client, not the provider. I did not establish a reachable provider-free sequence during this isolated built-in-driver boot, so I am not reporting a proven use-after-free blocker. Supporting provider stop, failed-start disposal or restart would require lifetime handling first. Partial thread-call allocation also leaks allocations while leaving the feature disabled.

## Checks run and their limits

I ran the supplied `host-check/run.sh`. Touch pacing, fast-power checks, latch lead and panel-plan checks passed. All nine v1 failures and v2 successes reproduced. The 3000-run walk reported zero violations, worst darkness 2070 ms and worst stale-image exposure 16 ms.

The Python model script uses `TemporaryDirectory`, whose normal cleanup permanently removes files. I ran the unchanged shell script through a Python launcher that redirects that cleanup to `/usr/bin/trash` with absolute paths. It did not change the model or assertions. Experimental sources and binaries were retained in scratch directories.

The real header state machine and extracted `panel_plan` are compiled. The framebuffer glue, timer scheduling, DSI worker, queue, latches, GPU and iOS are modeled. In particular, the model has a 64-entry event array with no real 16-entry overflow behavior, successful instantaneous flips, no scaler fallback, no surface holds, no 80 ms frame-readiness state, and no distinction between uncertain physical state and recorded command state. Its 5% independent command failures do not establish behavior under permanent link failure.

I independently undid touch cancellation and the generation comparison in scratch headers. `fast_power_check.c` failed both mutations as expected. I did not reproduce every claimed author mutation. No complete mutation runner or result ledger was supplied in `host-check`, so that broader claim remains unverified.

Additional fault checks and outputs are in `analysis/2152-extra-check.log`. The 7891 ms permanent-DCS result and three premature-settle flips show that passing the supplied scenarios does not establish the claimed bound. The separate-state timeout experiment is conditional fault modeling, not hardware proof.

## Apply checks and 2153 comparison

Actual `git apply --check` and application succeeded for 2150 through 2153 on a copy of `work/s0`. The result matched all staged s4 files exactly. On copies of the current affected source files, I reversed the lag patches and reconstructed the local kernel-series base, then checked and applied every available kernel patch from 2051 through 2120 in order. The input series applied afterward.

On the copied 2120 base, 2140, 2141 and 2142 each checked and applied, followed by 2150 through 2153. Thus both optional-lag combinations pass for that prepared base. An initial attempt using s0 alone lacked a separate file needed by 2140. The later full affected-file copy resolved that test-fixture omission. These successes do not prove the incorrect clean-upstream recipe in finding 5.

Application details are in `analysis/2152-apply-check.log`. The independent upstream failure is in `analysis/2152-upstream-check.log`. No application command targeted the running kernel tree. During scratch baseline reconstruction, reverse application of the local series removed patch-added scratch files through Git rather than Trash. That violated the Trash-only policy. The original files and live tree were untouched. The independent upstream reconstruction used forward application only.

I diffed 2153 against `work-v1/2153-v1.patch`. The two placements moved, the temporary `wait` variable was added, and the timing-line counter moved to its own sampled log. The counter still increments once for each nonzero move. The phase function, 1000 us maximum movement, 250 us dead band, successful-latch sampling, scheduling guard and deadline update are unchanged. The 2120 surface release remains after copying and before latch wait. No new scaler, buffer ownership or hold-release change was introduced by 2153.

No kernel build, cross-compile, boot-image correspondence check, panel status measurement or physical rollback measurement was performed. 2153's GO covers an isolated test on the verified prepared base. 2152 remains excluded until the blocking recovery and readiness findings are resolved.
