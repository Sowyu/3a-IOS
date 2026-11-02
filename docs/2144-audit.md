# 2144 kernel audit

Verdict: GO for daily inclusion with the default idle steering enabled. Keep `sweetamp_ui` absent and the thermal guard active at its default mode 3. The idle steering does not need a new opt-in boot arg. `sweetamp=0` already restores stock placement.

This GO does not clear a daily configuration using `sweetthermal=0` or `sweetthermal=1`. The successful measurement runs explicitly disabled the guard. Do not copy those boot arguments into the guarded daily configuration.

What this change does: 2144 activates the existing core-class preference in the compiled Clutch SMP processor chooser. It also adds optional selection of a busy big core for higher-priority user work. The default enables only the idle preference.

Reviewed on 2026-11-03. This was a desk audit. No phone access, adb, fastboot, dev.py, kernel-tree writes, file removals, commits, or hardware tests were performed. Scratch files remain in `analysis/scratch/2144-audit/`.

## Findings

### Should fix before using UI preemption more widely

1. An idle little-core hint bypasses `sweetamp_ui`.

`osfmk/kern/sched_prim.c:5240`, `:5247`, `:5255`, `:5338` in the patched scratch tree.

Proven by control flow. With `sweetamp_ui=47`, let the render thread have an idle A520 as its previous-processor hint, all big cores be busy, and one big core run lower-priority work. The hint is retained because there is no idle preferred-class core, so the function returns the A520 before reaching the UI branch.

The advertised busy-big preference therefore depends on the hint. The same runnable thread with no idle hint can take the busy big core. This explains a remaining placement path without claiming it explains a particular measured frame.

The smallest fix is to evaluate UI eligibility before accepting the wrong-class idle hint, while preserving that hint as fallback if no eligible busy big core exists. This does not block the default, where UI preemption is disabled.

2. Selecting a busy big core does not guarantee immediate execution there.

`osfmk/kern/sched_prim.c:5340`, `:5349`, and unchanged `:4564` through `:4587`. `osfmk/kern/sched_clutch.c:3426` through `:3442`.

Proven by control flow. The UI branch returns a busy processor even when `options` does not contain `SCHED_PREEMPT`. For a nonurgent thread whose target is not eager-preemptible, `processor_setrun` can enqueue it without sending a preemption signal, despite idle little cores being available. A timeshare thread whose scheduling priority has decayed below its base priority has another path that suppresses preemption.

Clutch inserts eligible unbound work into the shared pset queue. Selecting a big core requests a wakeup or reschedule there. It neither binds the new thread there nor reserves that core for it. Another CPU can take queued work first.

If UI mode is meant to promise busy-big execution, its enqueue and preemption contract needs an explicit decision and a targeted check. Otherwise document it as a best-effort reschedule preference. Keep this mode opt-in.

### Daily configuration requirement

3. The performance evidence does not establish guarded daily heat or battery behavior.

`ios-asteroids/logs/ast133-night-UBSa.txt:149`, `:153`, `:412`, and `ast135-night-UBSb.txt:149`, `:153`, `:412`.

Proven from the raw logs. Both best runs use `sweetthermal=0`, `sweetcpufreq=fixed`, and `sweetcpufreq_little=100`. The driver reports all three domains at 100 percent. Those runs demonstrate useful placement and pacing with no reported GPU recoveries, but do not test interaction with active thermal throttling.

Concrete scenario: reusing that boot line for a daily kernel leaves sustained big-core activity without this software thermal guard. Retain mode 3 by omitting `sweetthermal` or explicitly setting it to 3. This is a configuration requirement, not a defect introduced into the thermal limiter by 2144.

## Scheduler correctness

Source references in this report use `analysis/scratch/2144-audit/xnu-combined/` after applying 2144. Unchanged driver references use the corresponding supplied combined-tree files. The compiled configuration defines `CONFIG_SCHED_CLUTCH=1` and `MAX_CPU_CLUSTERS=1`. Neither `CONFIG_SCHED_SMT` nor `CONFIG_SCHED_EDGE` is defined in the captured preprocessor macros. Clutch's dispatch table uses `choose_processor` at `osfmk/kern/sched_clutch.c:3240`.

The thread lock and starting pset lock are caller requirements at `sched_prim.c:5184`. `thread_setrun` acquires the pset lock before invoking the chooser. Every new return selects from the currently locked pset's state maps. The patch does not unlock it, sleep, allocate, or recurse.

The idle selection at `:5332` intersects the preferred class with `PROCESSOR_IDLE` and `recommended_bitmask`. The optional busy selection at `:5340` intersects `PROCESSOR_RUNNING` or `PROCESSOR_DISPATCHING`, recommendation, and the complement of pending urgent reschedules. Offline, pending-offline, and starting processors cannot enter these new selections through a correctly maintained state map.

The caller carries the same pset lock into `processor_setrun`, which enqueues the thread and handles idle exit or preemption before unlocking. Thus the patch introduces no unlocked interval between choosing a CPU and enqueueing. CPU identities and processor-array entries are established before masks are published. Mask publication uses a release store of the big mask, and classification first acquires that mask at `:4740`.

The extra relaxed big-mask read is safe under the existing one-time setup contract. `cf_started` prevents repeated setup and the masks remain fixed. This would need revisiting if runtime reconfiguration were added. Processor current-priority fields remain scheduling hints, as in the existing chooser. They do not reserve execution or replace runqueue priority checks.

A powered-down idle CPU remains a valid scheduler wakeup target. A CPU removed from the scheduler's available states is excluded. Frequency-domain power state is not independently checked here, which matches the existing idle-selection contract. This is source reasoning, not a tested hotplug or power-collapse result.

When no preferred idle CPU exists, the original preferred, non-avoided, any-idle, and busy search remains reachable. With no usable CPUs, the existing bounded retries and last-resort enqueue remain intact. No recursion or lost fallback was found.

The new early idle return precedes perfcontrol preference and migration-avoidance ranking. A hypothetical avoided big CPU can therefore beat a non-avoided little CPU. Recommendation is still obeyed. The migration/preference setter is in the Edge-only section of `osfmk/kern/thread_group.c:1505`, while this target builds SMP Clutch, so I found no active rotation-policy conflict on this target. Recheck this ordering if Edge or a rotation provider is enabled later.

## Fairness, realtime, and thread selection

The default is a preference among idle CPUs. It never waits for a big core while an idle little core remains available. It changes neither priority, quantum, binding, Clutch bucket accounting, nor queue eligibility. I found no new default-mode starvation mechanism or hard pinning.

Realtime scheduling still delegates to `SCHED(rt_choose_processor)` before entering the new idle-search block. The added hint test can reject a realtime thread's idle hint, after which realtime selection runs normally. It does not route realtime work through UI preemption. UI work must also have a scheduling priority strictly greater than the proposed victim, so ordinary UI work cannot displace a running realtime thread by this branch.

Bound threads bypass `choose_processor` in `thread_setrun`. Their binding is unchanged. With optional UI mode, sustained higher-priority work can repeatedly displace a lower-priority thread bound to a big CPU while little CPUs are idle. That bound thread cannot migrate away. This is an inferred workload risk of the optional policy, not evidence of a default-mode starvation bug.

Classification at `sched_prim.c:4738` uses `base_pri`, not process name, thread name, an entitlement, or a CoreAnimation marker. With all steering arguments absent, every non-kernel-task thread at base priority 31 or higher prefers a big core. User threads below 31 and unmarked kernel-task threads prefer little cores. Priority inheritance that changes only `sched_pri` does not itself change this classification. Fallback still allows either class.

The render thread at base priority 54 and SpringBoard main at 47 qualify. So do unrelated user threads at or above 31. This is broad default-QoS-and-above steering, not UI-only steering. Exact QoS-to-priority changes continue to follow XNU's existing priority policy.

The framebuffer work loop is marked big at `iokit/Kernel/arm/SweetFramebuffer.cpp:999`. Row helpers are marked at `:4236`, and the touch thread at `bsd/dev/sweet_touch_asteroids.c:1244`. They retain big preference despite belonging to `kernel_task`. They are excluded from initiating optional UI preemption by its separate kernel-task test.

Other kernel workers, including thermal and cpufreq workers, get little preference with big fallback. GPU submission code executing in a user task follows that task's thread classification. Interrupt handling follows interrupt routing, not this processor chooser. I found no new kernel-only affinity or path that traps display, touch, GPU, or guard work on one class.

The existing eight-entry marked-thread registry remains a bounded, never-cleared pointer list. Default framebuffer helpers plus the work loop and touch thread fit. Overflow would lose a preference and fall back to normal class behavior, not lose runnable work. This patch does not add lifecycle management to that existing registry.

## Boot arguments

`bsd/dev/sweet_cpufreq.c:232`, `:233`, `:898`, `:899`, `:737`, and patched `sched_prim.c:4708` define these behaviors.

1. All arguments absent enable idle steering after SMP bring-up, with threshold 31 and UI preemption disabled.
2. `sweetamp=0` prevents mask setup and disables both steering modes, even with `sweetamp_ui` present.
3. `sweetamp_prio=N` changes the user-thread base-priority threshold and is not range-validated by the existing driver.
4. `sweetamp_ui=N` enables optional busy-big selection only for positive values below `BASEPRI_RTQUEUES` and threads satisfying both priority thresholds.
5. Missing, zero, negative, or realtime-range UI values leave UI mode disabled on normal one-time initialization.

Setting a low UI threshold does not override a higher `sweetamp_prio`. Setting `sweetamp_prio` above normal priorities effectively removes user big preference. Negative values make essentially all user threads prefer big. These are tunable policy choices, with safe fallback, rather than array indexes.

`sweetcpufreq=off` still permits steering setup if `sweetamp` is enabled. Read-only frequency mode also permits steering. A failed SMP wait or missing core class leaves steering off. There is no new boot arg besides `sweetamp_ui`.

## Power and heat

2144 writes no frequency, voltage, thermal, power-collapse, or hardware registers. More time on big cores can increase instantaneous power and reduce idle residency. Faster completion can reduce execution time. Net energy and sustained temperature changes are unmeasured.

The guard defaults to mode 3 at `bsd/dev/sweet_thermal_asteroids.c:45`. Its initial read is delayed 20 seconds at `:118`, followed by one-second polling. The policy begins stepped limiting after five hot polls at 80 C, steps each poll at 90 C, requests clean shutdown after three polls at 100 C, and hard shutdown after two polls at 108 C. Invalid sensor readings eventually impose a 55 percent CPU ceiling, rather than forcing shutdown.

`bsd/dev/sweet_cpufreq.c:302` applies the thermal ceiling to every driver frequency request. Governor floors and touch boost pass through that function. Fixed mode explicitly follows ceiling changes at `:802` through `:812`, with a 250 ms accounting period. Therefore a configured performance floor does not override the guard's requested ceiling. Clock selection uses available LUT states, so percentages are requested operating-point approximations.

Steering does not turn off as temperature rises. It can continue choosing big cores at reduced clocks. That can reduce its performance benefit and retain some energy cost, but it does not bypass the frequency cap. The guard depends on functioning sensor mapping, scheduled workers, and hardware clock control. In `sweetcpufreq=off` or read-only mode, the driver cannot enforce its CPU frequency ceiling, even though steering can remain active. The daily GO assumes governor or fixed mode with applied domains. It is not proof of battery life, comfortable surface temperature, or an absolute thermal bound.

Judgment: default idle steering is a reasonable daily policy with the existing active guard. There is no source-level reason to require opt-in for that part. Keeping the more aggressive busy-big mode opt-in is warranted by its behavioral gaps and lack of guarded combined-series evidence.

## Combined-series compatibility and checks

I copied the supplied combined XNU source with copy-on-write into scratch. The 2144 patch dry-run and real application both succeeded without reported offsets or fuzz. A subsequent `git apply --check --reverse` succeeded. Only `osfmk/kern/sched_prim.c` is changed by 2144.

The resulting scheduler file is byte-identical to `xnu-local/dxb/xnu/osfmk/kern/sched_prim.c`, which contains the tested patch. The eight combined driver files affected by 2140 through 2143 and 2150 through 2153 match the earlier retained `analysis/2152-audit4-scratch/with-lag` reconstruction byte for byte. Reverse dry checks also confirm 2130 v4, 2141 through 2143, 2151, and 2152 v4 in the supplied checkout.

Independent reverse checks for 2140, 2150, and 2153 encounter context changed by later patches. These are not 2144 application failures. The matched reconstructed files provide stronger evidence of their final integrated state. I did not rebuild the entire upstream-through-2049 and 2051-through-2120 history from a clean upstream checkout in this audit. Compatibility is directly proven against the supplied daily checkout, with the retained prior reconstruction supporting its input/lag series identity.

2130 v4 changes exec behavior, while the input patches change display and touch timing. They do not modify the scheduler file or its new symbols. Display/touch big markings survive their final integrated sources. USB 2160 and 2161 were not applied, are not required by 2144, and are outside this clearance.

The patched scheduler passed `clang -fsyntax-only` using the existing combined build's compilation arguments and read-only generated/exported headers. Exit status was 0, with 1866 warnings and no errors. The macro dump confirms the compiled scheduler selection. No full kernel build or link was run, and matching source does not prove the supplied Mach-O was built exclusively from that source.

The raw run logs establish the steering masks as `big=0xf0`, `little=0x0f`. The run summaries in `analysis/lag.md:383` and `:455` report the placement improvement and 73.9 percent one-vsync share. The UBWC comparison also changes kernel composition because the 2144 image lacks 2130 and 2150 through 2153. Consequently, attributing the entire pacing improvement to 2144 alone remains inferred. Zero observed GPU recoveries is useful evidence, not a scheduler fairness or sustained-heat stress test.

Evidence files are `analysis/scratch/2144-audit/apply-check.log`, `applied.diff`, `series-check.json`, `syntax.log`, `compile-entry.json`, `macros.txt`, and `sha256.txt`.

Patch SHA256: `69e6f5d6dad6b1faa4f05293a76159f6339f59103e0b72505f5ab94ada606285`.

Supplied kernel SHA256: `9100bd802ca944a7c1a2a0733b3a721d32f2dbfd6a7f80e9cf1276bd2178f809`.

Not checked: physical hotplug/power collapse, sustained thermals, battery drain, full combined-kernel linkage, or runtime behavior of the final daily combination.
