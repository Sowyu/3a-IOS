# USB gadget daily-use audit 4

2026-11-03. NO-GO for default daily use, including hours of RAM-booted use and a later boot_a installation. The successful first SSH transfer does not meet daily-use acceptance. The supplied daily image currently refuses to start the gadget, and the successful experimental boot later shows TX no longer progressing.

This review only read local source, images and recorded logs. I did not contact the phone, run a test cycle, change a kernel tree, build anything or commit. This report is the only file written. Tests below are proposed acceptance tests, not results.

## Scope and evidence

Paths are relative to `/Users/aniko/Documents/Vibes/EverIOS`. Short source references below mean files under `ios-asteroids/research/xnu-combined/dxb/xnu/bsd/dev/`, unless another path is given.

I read HANDOFF, the design and the three earlier audits. I compared the applied daily tree's `sweet_usb.c`, `sweet_usb_gadget.h`, `sweet_ufs.c` and `sweet_reboot.c` with `plans/usb/gadget/work/b4/bsd/dev/`. All four match byte for byte. The v4 patch hashes are `87a73ad2172f4c8750b73655e74ea57bcd14619ab86529afae403adc5083f41f` for 2160 and `95b25a365dcd69d09cfdc132c970b7665db3c62f8b9316d743ffb15916986a8f` for 2161.

The daily kernel SHA256 is `565dfd76303f90ecb27d29203d56560cf67a1c823b3e7c0681c8ba773f5d23fa`. Read-only `plans/devloop/dev.py --print-args` inspection of `next-combined/out-daily-usb/combo-ub-usb-boot.img` confirms `sweetusb=4`, `sweetthermal=3`, `sweetfbarm=1` and `sweetpanicfb=10`, with no `sweetdeadman`. It also confirms keep plus astwait, UBWC arguments, and no `sweetpwrfast`, `sweetamp_ui` or `sweetappmetal`.

Ast107 proves level 2 ownership at `ios-asteroids/logs/ast107-usb-l2.txt:302`. Ast141 proves configured enumeration and verified stopping at `ios-asteroids/logs/ast141-usb-l3-v4.txt:853` and `:3870`. Ast145 proves authenticated root SSH and a 4 MiB host-to-phone checksum in `plans/usb/gadget/runs/l4-v4d.txt:33` and `:39`. The Mac's en8 name and `02:00:00:00:00:00` address are recorded at `:15`.

I accept ast143 and ast144 as checker failures described in the request. Interface discovery and delayed SSH startup are not daily-kernel blockers. Ast145's boot arguments at `ios-asteroids/logs/ast145-usb-l4-v4d.txt:706` use `sweetthermal=0`, `sweetdeadman=600` and `sweetselftest=1`. They do not exercise the daily configuration or prove its combined behavior.

## Findings

### 1. The supplied daily image cannot enable level 4

High severity for deployment correctness. This is a safe refusal, not evidence of active DMA corruption.

`next-combined/build.sh:16` and `:24` construct the daily USB image without a deadman. `sweet_usb.c:208` refuses levels 2 through 4 before even the inherited probe unless `sweet_reboot_handsfree_ready()` succeeds. `sweet_reboot.c:555` requires `sa_deadman_running`, which only becomes true when a nonzero deadman creates its thread at `:537`.

Simply adding the test's `sweetdeadman=600` would force a reboot after ten minutes. The deadman is an absolute boot timer, not a progress watchdog, at `sweet_reboot.c:459`. Its accepted maximum is 86400 seconds at `:532`, so using a larger value does not provide indefinite daily readiness either.

Smallest immediate fix: keep USB absent or zero in the daily image. Enabling it needs a separately reviewed daily admission policy that preserves armed fastboot and panic recovery without treating an expiring test timer as a permanent health guarantee. Do not remove the existing check merely to obtain SSH.

Hands-free proof: first add a test image with the exact daily arguments plus the loop's required test instrumentation. Preserve and explicitly distinguish that instrumentation from production admission. Use dev.py to verify refusal without daily admission, then successful admission under the new policy, followed by a soak beyond the old 600-second timer. A short loop with the existing deadman cannot prove the proposed daily policy.

### 2. The passing boot contains a later persistent TX stall

High severity. The stalled counters are observed. Their hardware cause is unresolved.

`ios-asteroids/logs/ast145-usb-l4-v4d.txt:9299` records `stale completion 0x0000c046 on ep3 ignored`. At `:9551`, TX is 2226 with queue depth 29. At `:10098`, `:10188`, `:10344` and `:10450`, TX remains 2226, queue depth is 32, RX advances from 3679 to 3689, configuration and alt 1 remain selected, and the reset count stays 1. This continues across multiple status intervals after SSH passed.

`ua_trb_done()` at `sweet_usb.c:2024` rejects a completion while its slot is inactive or HWO remains set. `ua_event()` at `:2065` logs and ignores it. There is no transfer-progress deadline in `ua_thread()` at `:2124`. A stuck TX can therefore leave SSH unavailable for the remainder of the boot without tripping a gate. An unserviced host transfer is also possible. These logs alone do not establish which condition caused the stall.

The host checker stops after the first upload checksum at `plans/usb/gadget/host/usb-check.py:179`. The combined verdict at `host/usb-verdict.py:29` checks success and explicit error strings, not later progress. Neither rejects the observed persistent full queue.

Smallest fix: diagnose this exact signature before widening approval. Add bounded progress detection and a safe recovery path that ends and verifies an owned transfer before any reuse. Do not clear `tx_busy`, HWO or `xfer_active` to make a timeout disappear. Add post-transfer bidirectional probes to the checker, including after idle.

Hands-free proof: run a dev.py level-4 test that repeats uploads and downloads with hashes for at least an hour, with small SSH commands between them and after a long idle. Log active/resource/HWO state and queue age when progress stops. Require no sustained full queue, bounded probe latency and no unexplained stale completion followed by lost progress. Deliberately stop host reads, then resume them, and require recovery without a phone reboot.

### 3. USB exports console logs without authentication by default

High severity for an untrusted computer. This exposure is independent of SSH authentication.

`sweet_usb.c:2441` defaults `sweetusblog` to 1 and starts `ud_log_thread`. At `:1263` it starts from the last 64 KiB of console output. At `:1269` and `:1289` it sends that output to `192.168.7.1:5140` using plain UDP. The daily image does not disable this behavior. A connected host can assign itself that IPv4 address and receive the stream without an authorized SSH key.

These logs contain kernel addresses, process activity and diagnostic paths. Sensitive future log contents would use the same unauthenticated channel. A Mac lacking IPv4 configuration today does not constrain another host. The code checks configuration, not host identity or phone lock state.

Smallest fix: default live logging off for daily use and put `sweetusblog=0` in the daily configuration. Read logs over authenticated SSH when explicitly enabled. Flush queued packets during host changes so an earlier host's pending diagnostic traffic cannot resume to another host. `ua_data_down()` at `sweet_usb.c:1844` currently preserves the TX queue.

Hands-free proof: in a dev.py test, have an unauthorized test host claim `192.168.7.1` and capture UDP 5140 before and after enumeration, lock, traffic and reconnect. Require zero console datagrams with logging disabled. A controlled opt-in logging run should demonstrate the capture fixture can detect them.

### 4. The actual root setup listens on IPv4 and IPv6 and has no USB trust gate

High severity for default exposure to arbitrary computers. No unauthenticated root-login bypass is established by this review.

The script named in the request is not the whole root-build path. `ios-asteroids/tools/sweet-root-tweaks.py:268` does generate `-F -E -s -p 192.168.7.2:22`. However, `ios-asteroids/tools/renegade/root_tweaks18.py:525` explicitly documents dual-stack listening, and `:529` generates `-F -E -s -p 22`, with an explicit host key or `-R`. At `:531` it sets root, RunAtLoad and KeepAlive. At `:535` and `:539` it installs both the standalone plist and the entry inside the launchd cache.

`ios-asteroids/research/root-build-gpu17.log:184` records `org.sweet.dropbear (port 22 v4+v6, key-only)`. Ast145 records launchd's `XPC_SERVICE_NAME=org.sweet.dropbear` at `:1399`, executes dropbear at `:1401`, and demonstrates root login over IPv6 in the Mac record. This supports the dual-stack generated job as the explanation. The exact current phone plist and running argv were not collected, so their present contents remain unverified. An IPv4-only bind cannot explain the observed IPv6 session by itself.

The expected job retains `-s`, so plugging in an unknown computer does not itself grant root. It does expose the USB parser, network stack and preauthentication SSH handling, even while locked. I found no owner approval or lock-state check in the gadget admission or job generation. macOS accessory approval protects the Mac and does not authenticate that Mac to the phone. A wildcard listener can also become reachable through future network interfaces.

Smallest fix: keep daily data access disabled until the owner explicitly enables it for a session, and disable it on lock or host change unless the owner deliberately accepts locked access. Preserve key-only authentication, pin host identity in the daily client, and restrict listening or firewall policy to the intended USB addresses/interface. Apply job changes to both launchd representations. Changing only the old helper or only the standalone plist is insufficient.

Hands-free proof: a dev.py self-test must collect the loaded job, process argv, both plist representations and listening sockets. An independent host must fail password, keyboard-interactive and unknown-key login over IPv4 and IPv6. The authorized key must work only in the intended enabled state. Repeat while locked and after a host switch. Negative authentication tests are required because the successful keyed login does not prove password rejection.

### 5. Replug, suspend and endpoint recovery remain experimental

Medium severity for routine availability. Potential forced-stop failures raise the consequence when the owner is relying on the phone.

Normal reset and disconnect do have a reusable path through `ua_bus_reset()` at `sweet_usb.c:1876`. It closes TX admission, ends data transfers and prepares ep0. This is different from terminal `ua_stop()`, whose stopped flag prevents restart for that boot at `:1774` and `:1782`. It would be incorrect to claim every unplug is intentionally terminal.

However, only disconnect, reset and connect-done events are enabled at `sweet_usb.c:2405`. The gadget forces VBUS/session-valid bits at `:2415`, and restores them only in `ua_stop()` at `:1806`. There is no demonstrated charger attach/detach or host sleep/resume state machine. A wall charger or power bank need not send SET_CONFIGURATION, and level 4 keeps waiting at `:2176`. The 1 ms poll continues during idle at `:2183`.

The earlier control-request finding also remains. `sweet_usb_gadget.h:357` and `:359` incompletely validate status and feature recipients. At `:378`, CLEAR_FEATURE and SET_FEATURE acknowledge success without changing endpoint halt or remote-wakeup state. A host's accepted CLEAR_FEATURE(ENDPOINT_HALT) cannot clear a real stall through this implementation. This is a concrete recovery gap, not proof of out-of-pool DMA.

Smallest fix: implement supported halt/status behavior and reject unsupported feature requests. Define VBUS loss, disconnect, suspend and resume behavior without reusing active DMA slots. Keep data output closed until a newly configured host selects alt 1. Remove pending old-host frames on teardown.

Hands-free proof: drive 100 physical data/VBUS disconnect/reconnect cycles using an automated USB switch during a dev.py test. Include active upload, active download, ep0 requests, alt changes and idle. Add a scripted host sleep/wake cycle and simulated endpoint-stall recovery. Switch among the Mac, another computer and non-data power sources with an automated fixture. Require recovered SSH after each data-host return, no stale payload delivery and no button recovery. Software interface down/up alone does not prove physical replug behavior. The current dev.py command allowlist does not operate such a switch, so a separate host-side fixture is needed.

### 6. Shutdown and panic do not quiesce the gadget before reset

High severity as a daily-release evidence gap. Actual persistent controller damage or next-boot failure is not proven.

The verified stop routine is private to `sweet_usb.c:1769`. There is no call to it from `SweetPlatformExpert::haltRestart` in `iokit/Kernel/arm/SweetPlatformExpert.cpp:55`, `sweet_reboot_asteroids_panic()` at `sweet_reboot.c:428`, or `sweet_reboot_asteroids_halt()` at `:562`. Panic flushes logs and storage, waits, then resets. Hard thermal shutdown directly enters the halt path at `sweet_thermal_asteroids.c:152`. These paths can reach reset while RUN_STOP and the VBUS override remain set.

Permanent pool ownership protects the allocation until this kernel ends. It says nothing about a new kernel reclaiming that physical memory before firmware has reset or halted the old DMA engine. ABL probably resets/reinitializes USB, but that is an inference requiring evidence for each reset path. Restoring the stale ABL event address would not be a safe substitute.

Ast145 is useful positive evidence. Its log shows orderly HFS unmount at `:10911`, and its summary records `root clean` after a 263-second cycle. That establishes one ordinary return through recovery. It does not establish repeated panic, thermal hard reset or reset with actively pending bulk traffic. The earlier level-3 verified stop does not cover a level-4 reboot that never calls that stop.

Smallest fix: expose a serialized bounded quiesce operation for orderly shutdown while the event thread and its locks can still run. Establish that emergency reset stops controller DMA before any new memory owner runs. Panic must not acquire an ordinary mutex that another stopped CPU might hold. Retain journaled HFS and the armed fastboot reason.

Hands-free proof: use separate dev.py runs for ordinary restart, requested power off, controlled panic and injected thermal soft/hard actions while both data directions are active. Inject thermal policy inputs, not physical overheating. Each run must reach fastboot, then OrangeFox, pull a readable log and check journal recovery. RAM-boot the known-good image afterward and repeat USB enumeration. Verify charging separately with measured current. Do not flash boot_a as the test mechanism. Recovery-chain flashing needs its own image, slot and successful-mark review under HANDOFF.

### 7. Long-uptime DMA behavior and combined load are not qualified

Medium severity as a qualification gap. I found no arithmetic ring-wrap bug or per-transfer unbounded allocation in the reviewed source.

The DMA allocation is contiguous, permanent and bounded at `sweet_usb.c:2281`. The event ring is 4 KiB and wraps with `(pos + 4) % UD_EVT_SIZE` at `sweet_usb_gadget.h:134`. Bulk endpoints use individual terminal TRBs, not linked TRB rings. TX has a bounded 32-entry queue at `sweet_usb.c:411`. TRB slot ownership and the pre-copy TX gate retain audit 3's protections at `:517` and `:805`.

Ast145's thousands of RX/TX frames already exercise event-buffer wrap. That is stronger than one enumeration, but does not establish sustained reuse over hours. `ua_thread()` validates whole-event counts within the buffer at `:2127`; it has no explicit overflow-event recovery and no transfer-progress deadline. Canary checks run every 1000 loop iterations at `:2151`, not on a strict elapsed-time deadline. The SMMU check requires pass-through at `:1681`, so the canary pages detect adjacent damage after it happens and do not confine DMA to the pool.

The 32-bit `ticks` and event counters eventually wrap. Cursor arithmetic does not depend on their total and remains bounded. At the nominal 1 ms sleep, tick wrap takes roughly 50 days or longer. It is not a demonstrated hours-scale overflow defect. CPU scheduling delay and lost progress are the more immediate unresolved limits.

Smallest fix: retain ownership checks, add progress and overflow diagnostics with a safe stop/recovery policy, and qualify the actual daily combination. Do not infer DMA containment from canary success or add a speculative ring rewrite.

Hands-free proof: use a 6-hour dev.py soak with `sweetthermal=3`, the daily scheduler/display/driver combination, logging off, a test-only deadman longer than the run and repeated bidirectional checksummed transfers. Include 2 hours of screen-off idle and an active UI phase. Require no stuck queue, gate, canary error, SMMU fault, reset or unexplained lost event. Collect rates, memory use and temperature throughout. A separate model test must cover cursor wrap, count overflow, stale completions and integer-counter wrap. Six hours is a release gate, not proof for every possible uptime.

## Interaction with the daily patches

2144 steers eligible user threads toward big cores and kernel threads toward the little-core preference in `osfmk/kern/sched_prim.c:4738`. Selection among idle CPUs still falls back to the ordinary scheduler at `:5330`. The optional busy-core UI-preemption branch at `:5338` is off in this image. I found no new direct USB lock dependency, but SSH user work and the 1 ms USB poll share CPU and memory bandwidth with rendering. Delay of the event thread under combined UI and network load needs the soak above.

2142's honest SwapWait remains paired with keep in `next-combined/build.sh:24`. It does not share the USB DMA pool. 2152 v4 initializes mode to zero and reads `sweetpwrfast` at `iokit/Kernel/arm/SweetFramebuffer.cpp:1267`. Since the daily image omits that argument, its fast-blank path is off. Ordinary lock and screen-off do not stop USB through the reviewed gadget code. Screen-off is not proof of deep hardware suspend or of a trusted-host boundary.

2130 injection is off without `sweetappmetal`. The input patches do not directly change USB ownership. The PMIC charging survey is opt-in at `sweet_power_survey.c:134`. Leaving the power path off avoids a new charger-programming interaction, but does not prove inherited charging limits, charger negotiation or battery current. The thermal guard throttles and resets at mode 3 in `sweet_thermal_asteroids.c:132` and `:150`. It is not a charger controller and cannot act when every CPU is stuck.

The all-CPU bus-stall limitation from audit 3 remains. The image has no validated independent watchdog. A progress watchdog can address a running system with stuck USB, but cannot recover every CPU wedged on the bus. Enabling unvalidated `sweetgwdt` is not an acceptance fix.

## How to run the proposed acceptance tests

No phone test was run for this report. The current `usb.sh` only waits 150 seconds and prints interface/process information at `plans/usb/gadget/selftest/usb.sh:4`. It cannot prove any of the long-duration or failure-path requirements above.

Implement dedicated self-test bodies before running the proposed cases. This is the command shape for an instrumented, RAM-only candidate, not a command ready to run against the current daily image:

```sh
python3 -I plans/devloop/dev.py <instrumented-daily-usb-loop.img> \
  --test <implemented-case> --note usb-daily-<case> \
  --key 'ASTEROIDS USB|SWEET REBOOT|SWEET THERMAL|SELFTEST'
```

Dev.py requires `sweetfbarm`, a bounded `sweetdeadman` and `sweetselftest` at `plans/devloop/dev.py:96`. The fixture and host checker run alongside it. Give each case fresh phone and host evidence and require the final post-idle checks to pass. Use existing replacement paths for root-side collection scripts and pull their reports through recovery. Do not weaken dirty-journal checks or use plain fastboot reboot, slot changes or a flash to recover a test.

The runtime admission issue, stalled TX, unauthenticated logging and arbitrary-host exposure need resolution before daily approval. Replug, suspend, charging and all shutdown variants need recorded qualification on the combined daily configuration. Until then, retain the known-good daily image with USB off and keep active level 4 restricted to separately authorized controlled test boots.

Verdict: NO-GO.
