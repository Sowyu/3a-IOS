# Input and Freeform pre-boot audit

2026-11-02. Source review only. Verdicts authorize only a controlled test boot, not daily-use rollout.

- 2150 GO. Key polling defaults to 20 ms, but diagnostics and a PMIC read change even without `sweetkeys_ms`.
- 2151 GO. This polls a GPIO edge latch from a kernel thread. It adds no interrupt handler or interrupt-disabled SPI path.
- 2152 NO-GO. Physical rollback is not bounded by two seconds, and queued key events can misclassify a wake press.
- 2153 GO. The phase adjustment retains the existing latch wait, buffer ownership and scaler programming.
- 2130 NO-GO. It strips environment variables with the arg absent, does not identify exactly Freeform, and does not exclude restricted/other credential-transition targets.

## Scope and application checks

I read `3a-IOS/HANDOFF.md` first, then the five patches, input/Freeform design reports, input README, host-check sources, `analysis/next2-audit.md`, and the connected kernel paths. No phone commands, builds, commits, source edits, or removals were performed. The only file written was this report.

All 12 per-file patch sections passed actual `git apply --check` against temporary copies of the current source, with 2150 through 2153 stacked in order and 2130 checked afterward. To obey the single-writable-file restriction, this report's path temporarily held each copied source, patch headers were redirected to that absolute path, and Git ran with `--check --unsafe-paths -p0`. Post-patch text was assembled only in memory for the next check. No `git apply` mutation was run. Exact full-context matching also passed independently for every hunk.

Each input patch also passed `git apply --check` against its existing preceding staged snapshot, `plans/input/work/s0` through `s3`. The live framebuffer differs from s0 by removal of 2140/2141 tracing. Its 2120 early surface-hold release remains present. These differences cause no application conflict for the reviewed series. 2152 requires 2150, and its HID hunk also follows 2151. 2153 and 2130 add no conflicting framebuffer/exec edits to this sequence.

`K/` below means `ios-asteroids/research/xnu-local/dxb/xnu/`. References to `plans/input/work/s4/` are the existing fully patched snapshot, which still includes 2140/2141 trace lines. They identify the reviewed function text, not live-tree line numbers. References to patch files use their actual patch-file line numbers.

## Blockers

### 1. 2152 ends the speculative flag without guaranteeing the screen is restored

`plans/input/work/s4/iokit/Kernel/arm/SweetFbScale.h:77`, `plans/input/work/s4/iokit/Kernel/arm/SweetFramebuffer.cpp:1390`, `:1442`, `:1507`, `:2486`; `K/iokit/Kernel/arm/SweetFramebuffer.cpp:1457`; `K/bsd/dev/sweet_dsi_asteroids.c:181`.

The deadline clears `dark` and marks `fDirty`. `fastApply` has no SHOW implementation that restores either pixels or panel power. Recovery goes through the ordinary GPU gate and, with panel commands enabled, does not post ON until `dirty` is false.

Concrete path: a solo release blanks and posts OFF while iOS remains ON. A GPU recovery during the speculative interval changes the epoch. At expiry, the old pending record is marked dirty, but `gpuGateOpen` rejects its old epoch without a timeout override. With no subsequent swap, dirty stays true forever, ON is never posted, and the panel stays asleep while iOS thinks it is on. With `sweetpanel=0`, the black frame similarly persists. `sweetfb_keep=1` can show a completed previous record but does not clear an invalid pending record's dirty state, so it does not establish panel-power recovery.

There is also an independent physical-state failure: the panel thread assigns `panel_on = on` even when either DCS command times out. An OFF succeeds and its rollback ON fails, or speculative ON succeeds and rollback OFF fails. The software records the requested state, consumes the request, and never retries. Both dark-while-ON and lit-while-OFF can therefore outlive the window. This panel error handling predates 2152, but 2152 depends on it to undo new speculative transitions.

Fix before enabling: preserve a confirmed completed image for rollback, restore it without requiring unfinished/void GPU work, and reconcile panel commands against successful completion. Retain or retry failed transitions rather than treating a posted request as physical acknowledgement. The two-second value currently bounds a flag's lifetime only, with tick scheduling and DCS delays on top.

### 2. 2152 classifies the press after iOS can already have acted on it

`plans/input/patches/2152-asteroids-fast-power.patch:23`, `plans/input/work/s4/iokit/Kernel/arm/SweetFramebuffer.cpp:2482`, `plans/input/work/s4/iokit/Kernel/arm/SweetFbScale.h:85`.

The key thread calls `sweet_hid_key` before posting PRESS. The callback later reads `fPowerOff` when it obtains the framebuffer work-loop gate. The claim that `press_on` records the state when the physical press began is false.

Concrete path: iOS is OFF, the key-down HID event wakes it, and RequestPowerChange sets ON before fastAction consumes PRESS. PRESS now records `press_on=true`. If the release arrives in a later batch, SHORT darkens the newly woken screen even though this was a wake press. A delayed lock press has the opposite failure and can be interpreted as a wake. OR-coalescing PRESS and SHORT suppresses some occurrences but does not fix separately consumed batches.

Fix before enabling: capture an ordered press record and the relevant display state before HID delivery, or maintain an event sequence with display-power generation numbers. A later callback must not reconstruct the press's starting state from current iOS state. Add a test with iOS ON arriving between HID dispatch and PRESS consumption.

### 3. 2130 changes exec environments even when both injection args are absent

`plans/freeform/2130-asteroids-app-metal-exec.patch:43`, resulting `K/bsd/kern/kern_exec.c:6996` after 2130.

The app-only and combined exact-string comparisons have no `sweetappmetal` gate. Any exec, including an unrelated arm64 process, loses an inherited variable equal to either string even with `sweetappmetal` absent and `sweetsb=0`.

Concrete path: a launcher deliberately sets `DYLD_INSERT_LIBRARIES=/usr/local/lib/bgmtl/bgmtl-inject.dylib`, then execs an app on an otherwise nonexperimental boot. 2130 strips the variable and injects nothing. The combined sweetsb/Chlorophyll string is also removed even when no feature is enabled. This violates the off-by-default requirement and changes existing explicit injection behavior.

Fix: gate the new stripping on the app-injection feature being enabled. Within enabled mode, keep stripping globally across child execs so a child cannot inherit the experimental injection unintentionally.

### 4. 2130's basename check admits other bundles and symlink targets

`plans/freeform/2130-asteroids-app-metal-exec.patch:19`, `:23`; `K/bsd/kern/kern_exec.c:6780`; `analysis/next2-audit.md`, finding 6.

With `sweetappmetal=Freeform`, `/Applications/Other.app/Freeform` matches. A direct helper with that basename matches too. The code verifies a lexical `.app` parent and executable basename, not `/Applications/Freeform.app/Freeform` or the executable vnode's canonical identity. An eligible-looking symlink can name a binary elsewhere.

Fix for this experiment: require the exact Freeform executable path, and verify resolved identity if the scope is intended as an executable security boundary. The existing helper correctly rejects nested extensions, frameworks, XPC paths, literal traversal and container-app paths for appmetal. The host matcher test omits the other-bundle and symlink counterexamples.

### 5. 2130 retains restricted-target and non-mode-bit credential-transition exposure

`plans/freeform/2130-asteroids-app-metal-exec.patch:56`, `:58`; `K/bsd/kern/kern_exec.c:2069`, `:2201`, `:2403`, `:7705`; `K/bsd/kern/sweet_amfi.c:592`.

The current baseline already excludes `VSUID | VSGID` before appending. That part of earlier finding 3 is fixed and 2130 retains it. It does not exclude restricted/hardened targets or MAC credential transitions, which are determined elsewhere. Extraction runs before later code-signing-flag publication and `exec_handle_sugid`, so adding a test of incomplete flags here alone is insufficient.

Concrete eligibility case: an arm64e restricted executable named Freeform directly under an accepted bundle has no set-id bits and receives the new variable. A MAC transition without those bits is also outside this guard. The local AMFI dyld-policy path returns 0x3ff for eligible arm64e processes without testing these restrictions, so normal dyld stripping is not an established protection on this port. Actual privileged library execution depends on the installed loader, which was not run.

Fix: make automatic app injection conditional on the final executable and credential policy, or explicitly exclude these cases at a stage where they are known. No such target was identified as the installed Freeform binary. This is a containment blocker under the requested launch-policy checks, not evidence that ordinary Freeform is setuid or restricted.

## Should-fixes

### 6. 2152 does not cancel speculative wake on touch or another key

`plans/input/work/s4/iokit/Kernel/arm/SweetFbScale.h:76`, `:81`; `analysis/input.md`, fixes table.

OTHER and PRESS cancel `dark`, but `awake` tests only iOS state and expiry. Wake while OFF, followed by a touch or volume key while iOS stays OFF, leaves the panel awake until the original deadline. This contradicts the reported input-cancellation rule. Include the intended cancellation events in the awake branch and ensure the action actually posts sleep from fastAction, where SLEEP currently only logs and relies on a later tick.

### 7. Panel/flip coexistence and two-second guarantees need integration coverage

`plans/input/work/s4/iokit/Kernel/arm/SweetFramebuffer.cpp:2495`, `:2528`; `K/bsd/dev/sweet_dsi_asteroids.c:164`, `:174`; `K/iokit/Kernel/arm/SweetFramebuffer.cpp:113`, `:242`; `plans/input/host-check/fast_power_check.c:100`.

The host random walk checks abstract flags. It does not model `fDirty`, epochs, retained frames, asynchronous panel requests, failed DCS commands, or callback delay. All timer/fast-action framebuffer flips share the work-loop gate, but panel DCS commands run independently on the panel thread. The panel worker serializes DCS sequences with each other and coalesces pending requests. It does not coordinate them with MDP flips.

If the panel's sleep stops the frame counter or flush completion, an iOS ON tick can enter `astWait` before the delayed panel ON is posted. After 25 ms it falls back, restores scaler registers and may freeze copies if restoration also fails. This is a conditional hardware failure path, not a proven counter-stop on this panel. Gate flips on confirmed panel readiness or establish that counters/flushes continue during sleep before relying on this pairing.

The earlier `push_surface` failure was associated with mode 2's brightness service. 2152 does not add that service and mode 1 avoids that particular trigger. Existing mode-1 evidence predates asteroids flips and hardware scaling. It cannot certify the new combination, and this review does not claim that 2152 reproduces the old assertion.

### 8. 2150's first-frame log can claim success without a displayed frame

`plans/input/patches/2150-asteroids-input-timing-keys.patch:115`; `K/iokit/Kernel/arm/SweetFramebuffer.cpp:4306`.

`fOnAbs` is consumed after any `present` return. An epoch change can skip `astFlip`, or a latch can fall back. The log can also precede panel sleep-out/display-on by roughly 90 ms plus panel-worker queueing. Another ON request during an in-flight present can be consumed by that older present. Log a confirmed successful latch associated with the ON generation, and distinguish latch completion from panel illumination.

### 9. 2151 timestamps observation time, not controller sample time

`plans/input/work/s2/bsd/dev/sweet_touch_asteroids.c:1147`, `:1160`; `plans/input/work/s2/iokit/Kernel/arm/hid/SweetHID.cpp:530`.

The stamp is taken before GPIO checks and the SPI read. It is neither a hardware edge timestamp nor a controller scan timestamp. With bit 2 alone, an arbitrary poll is stamped this way. Idle detection can be about 4 ms late, and a missed edge uses the fallback schedule. Describe it as read-start/edge-observation time and verify velocity behavior on the phone before treating simulated timestamp improvements as measured.

## Verified paths and absent-argument behavior

### 2150

The poll default remains 20 ms and values outside 2..100 reset to 20. Logs, absolute-time sampling, timestamp storage, framebuffer timing and the additional PON debounce-register read are unconditional once the relevant drivers run. Thus the patch is suitable as a diagnostic test baseline, but it is not literally behavior-identical with the arg absent. No new PMIC writes or unbounded loops were added. Gate diagnostics separately if strict no-arg equivalence is required.

### 2151

`K/bsd/dev/sweet_touch_asteroids.c:20`, `:518`, `:642`, `:913`; staged s2 `bsd/dev/sweet_touch_asteroids.c:1144` and `:1183`; staged s2 `iokit/Kernel/arm/hid/SweetHID.cpp:544`.

There is no new touch IRQ handler. GPIO status is polled by the existing single priority-81 kernel thread. Pace mode requires a successfully configured latch. No new `ml_set_interrupts_enabled`, spinlock, or SPI operation under a HID lock appears. The poll thread owns the touch/SPI state. The HID lock still protects contact state and is released before the 2152 callback.

An edge storm produces at most one read per loop and the loop sleeps between passes. Active contacts without edges get a read after 12 ms since the last read began, plus SPI/scheduling time. Idle input retains low-level and roughly 100 ms periodic fallback checks. A lost edge can lose a short gesture whose entire lifetime fits between reads, but does not introduce an indefinite wait. SPI timeout/dead-ring handling predates 2151 and stops the touch driver, then releases contacts on thread exit. It is not a new kernel IRQ livelock.

Without the new args, read eligibility and waits stay as before. HID calls use the new wrapper with stamp zero and preserve dispatch-time timestamps. Extra bookkeeping, clock reads and startup logging still execute. `sweethid_touch_us` independently changes the advertised interval when set, even if `sweettouchirq` is absent.

### 2152 concurrency

Keys and touch only OR events and schedule a thread call. They never hold the framebuffer lock while doing SPI or DCS. HID touch releases its lock before calling the event hook. `IOWorkLoop::runAction` closes the same recursive gate used by the display event loop, so fastApply and timer presentation cannot concurrently modify the asteroids flip buffers. The lock order is work-loop gate then `fLock`; the reviewed key/touch callers add no reverse edge.

iOS RequestPowerChange runs separately and updates `fPowerOff` under `fLock`. It can change the requested state after sfs_fast_step selects an action and before fastApply posts it. A later tick normally reconciles this, but it is not an atomic state/action transaction. Findings 1 and 2 explain why reconciliation is insufficient as a general guarantee.

Absent/zero `sweetpwrfast` leaves the global framebuffer pointer null and allocates no thread call. Added hooks do an atomic pointer load and return. The zeroed state leaves the original power path in effect. No new speculative display transition was found with the arg absent.

### 2153, hardware scaler and retained frames

`plans/input/patches/2153-asteroids-latch-lead.patch:40`, `:64`, `:80`; `K/iokit/Kernel/arm/SweetFramebuffer.cpp:113`, `:252`, `:1350`, `:4299`; `plans/input/work/s4/iokit/Kernel/arm/SweetFbScale.h:20`.

The new sample is written only after an asteroids flip succeeds. Feedback requires a current-tick successful latch, a taken record and a previous schedule deadline. The movement is bounded to 1000 us per taken frame, with a 250 us dead band. `sfs_tick_next` still advances by a period and enforces a minimum future wait instead of a catch-up burst. Moving a negative offset through unsigned addition is defined modular arithmetic and does not underflow the realistic uptime deadline here.

The patch does not move copying after CTL flush, skip astWait, write the confirmed front buffer, change QSEED/stride programming, or release extra surface holds. A retained completed frame goes through the same present/flip route and may validly supply feedback. The 2120 hold release stays after the full surface copy and before the latch wait. No new tear, latch starvation, scaler or keep-path failure was established.

The fixed 8333 us period assumes 120 Hz. A long copy or a late observed latch can defeat convergence or add jitter, particularly at the 1500 us minimum lead. Existing bounded latch/fallback handling remains. These are performance uncertainties, not established memory or ownership failures. With `sweetfb_lead` absent, the new phase branch is inactive. An extra latch-time read/store and expanded stats log remain unconditional.

### 2130 capacity, composition and failure behavior

The strings occupy 51, 62 and 91 bytes including NUL for sweetsb-only, app-only and combined insertion. The append reserves string bytes plus three pointer widths in argument space, charges one environment pointer, and leaves room for the environment terminator and alignment. The separate string-space test retains alignment slack. envc and strendp advance once. No append overflow or string/pointer overlap was found for either pointer width.

With both `sweetsb=1` and matching appmetal, this mechanism appends one `DYLD_INSERT_LIBRARIES` variable containing a colon-separated list, sweetsb first. Other eligible UI apps receive sweetsb only. Inherited exact app/combined values are refunded before envc/pointer charging. An arbitrary existing insertion value, including an empty one or a different list, is retained and suppresses automatic insertion. Copying and pointer preflight still occur before stripping, so near-capacity input may return E2BIG even for a subsequently removable variable. That limitation already existed in 2058.

The older `sweetexecenv` mechanism appends after the inherited-variable scan and does not update `sweet_existing`. If it inserts DYLD_INSERT_LIBRARIES, combining it with these automatic mechanisms can still create duplicate variables. The automatic 2058/2130 combination alone does not create two variables. Avoid overlapping `sweetexecenv` insertion or account for it in the shared existing-variable flag.

Insufficient room skips the new append and permits ordinary exec. This is not protection from a broken installed library. The local AMFI policy enables insertion-failure allowance, but dependency, fixup and constructor failures can still terminate the process after a library is accepted. Freeform's later watchdog cannot contain loader-time failures. Exact phone-loader behavior was not established here. The existing noinline attribute stays attached to exec_extract_strings in 2130, unlike the earlier displaced-attribute version discussed in next2 finding 6.

## Could not verify

No host checks were executed because their scripts compile programs and create additional files. I reviewed their source and their stated prior results only. No new syntax check, emitted-code check, kernel link, boot image/source correspondence or actual installed Freeform executable/credential inspection was performed.

No hardware evidence establishes IRQ rate, SPI report stability, real timestamp jitter, storm CPU cost, DCS success during concurrent flips, panel counters while asleep, scaler/keep behavior under the phase feedback, or physical recovery within two seconds. The host simulations do not cover those boundaries. Runtime exec negative tests, exact dyld behavior, symlink identity and concurrent first-use parsing were not exercised. GO verdicts are source-level clearance for isolated test boots, with 2152 and 2130 excluded until their blockers are fixed.
