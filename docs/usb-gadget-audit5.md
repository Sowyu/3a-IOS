# USB gadget v5 safety audit

2026-11-03. GO for one controlled level-4 dev-session test boot on the known Mac, using the pinned v5 kernel and `out-v5/usb-dev-t-boot.img`. No kernel safety finding below blocks that experiment. The runner has an acceptance-reporting defect, so its exit status cannot establish success.

This decision retains audit 3's experimental risk boundary. Use RAM boot through plans/devloop/dev.py, armed `sweetfbarm=1`, `sweetpanicfb=10`, a running deadman, and the journaled root. It does not approve flashing, daily use, arbitrary hosts, or guaranteed recovery from an all-CPU bus stall. The previous hardware ownership and enumeration results are inherited evidence, not new v5 hardware results.

I read HANDOFF, usb-gadget.md, audit 3 and audit 4. I accessed no phone, adb or fastboot, ran no boot session, built no kernel or image, changed no research tree, and made no commit. The only dev.py invocations were the explicitly requested offline `--print-args` inspections. Scratch work is under `analysis/usb-gadget-audit5-scratch/` and retained `/tmp` directories.

Source references to `sweet_usb.c` below mean `plans/usb/gadget/work/b5/bsd/dev/sweet_usb.c`.

## Findings

### 1. The test runner returns success after a failed test

Medium severity. Nonblocking for boot safety. Blocks treating a zero runner exit as acceptance evidence.

`plans/usb/gadget/dev-v5-test.sh:19` captures the verdict text but discards its return code. Line 22 unconditionally exits zero, including after dev.py failed or usb-verdict reported FAIL. Line 17 also kills an unfinished checker, then ignores its result. Reusing the default note can retain an earlier result file if the checker dies before its first write, and line 18 selects a log by filename and modification time rather than a run identity.

The smallest fix is to clear the checker result by truncation before launch, use a fresh note per run, capture checker and verdict exit codes, and return nonzero unless dev.py, the checker and the verdict all succeed. A killed checker must fail acceptance. For this single experiment, inspect fresh same-run phone and checker files directly and require their explicit PASS plus dev.py's clean completion. Do not use the wrapper's exit zero as the result.

### 2. Host-read starvation is modeled but not exercised by the runner

Low severity evidence gap. Nonblocking for the first controlled test boot. Blocks claiming that the test validates the pending-IN recovery branch on hardware.

`plans/usb/gadget/host/test_kernel.c:1041` models a host that stops reading and resumes. `plans/usb/gadget/dev-v5-test.sh:13` only requests ordinary traffic and idle. Idle at `host/usb-check.py:228` follows completed SSH operations, so it need not leave a TX transfer pending. Stopping an application reader also need not stop the Mac's USB network driver from consuming IN packets.

The smallest validation addition is a controlled host fixture that actually leaves bulk IN pending for more than two seconds, then resumes IN service. Require an increment of `TX ended`, resumed checksummed traffic, and bounded command latency. The current 480-second soak is useful for the ast145 failure and idle recovery, but does not supply that specific proof. No fixture was run here.

## The ast145 diagnosis

The diagnosis is credible and reproduced in the host model. It is not proven retrospectively by the hardware log alone.

At `ios-asteroids/logs/ast145-usb-l4-v4d.txt:9299`, `0x0000c046` has endpoint 3, event type 1, and status 12. Those are bulk IN, XferComplete, and IOC plus LST. There is no error indication in that event word. Later lines 9551, 10098, 10188, 10344 and 10450 hold TX at 2226 as the queue fills to 32, RX advances and reset count remains 1.

`ud_if_output()` at `sweet_usb.c:833` holds `ud.lock` through `ud_tx_kick_locked()` and the STARTTRANSFER call. `ud_start_trb()` at line 535 publishes `xfer_active` only after the command completes. V4's event path checked that flag before taking the lock, so a fast completion on another CPU could see it false and discard the sole completion. The submitted frame had already set `tx_busy`, which prevented every later kick.

V5's `ua_event()` at line 2085 takes the same lock before checking ownership or HWO. It clears active and busy and kicks the next packet while still holding the lock. The event now waits until the start and resource publication finish. The regression generates the exact v4 stale message and passes on v5.

I found no remaining unsynchronized TX completion path in the asteroids event handler. An event lost in delivery, an anomalous HWO observation, or an old completion arriving after recovery can still be ignored. These cases no longer imply permanent busy ownership while the event thread runs, because the progress check settles the current slot. A failed STARTTRANSFER instead reserves the slot and gates the gadget. A permanently blocked event thread or controller bus remains outside that progress guarantee.

## Timeout ownership and DWC3 behavior

`ua_tx_check()` at `sweet_usb.c:2161` closes admission under `ud.lock` before inspecting the synchronized TRB. It only acts on busy, active TX with an age of at least 2000 ms. The timestamp is elapsed mach time, although checks every 100 polls are not a hard real-time scheduling guarantee.

For this single normal terminal LST|IOC TRB, HWO clear returns ownership of the descriptor and buffer. There is no chained successor or live ring to continue fetching. Reclaiming that completed terminal transfer without ENDTRANSFER is reasonable for this experiment. It is not a general rule for nonterminal TRBs, and does not prove successful delivery merely from ownership return. The checker must establish data correctness.

When HWO remains set, `ua_end_transfer()` at line 1761 issues ENDTRANSFER with the saved resource and FORCERM. `ud_epcmd()` at line 475 actually polls CMDACT and checks command status. Ownership initialization at line 2417 sets and reads back GUCTL2 bit 14 on the identified DWC_usb3 3.30a core. A failed end leaves active and busy reserved with admission closed. The next event-thread iteration takes the gated stop path.

Linux documents GUCTL2 bit 14 as the DWC_usb3 3.10a-and-later polling mechanism. Current Linux uses a different no-IOC path that skips CMDACT polling and waits 1 ms. Its unconditional-delay fix addresses that skipped-poll path, which v5 does not use. Linux also warns that FORCERM can leave HWO set after an end, so successful end completion, rather than software clearing HWO, is the relevant release condition. See [Linux gadget.c](https://github.com/torvalds/linux/blob/master/drivers/usb/dwc3/gadget.c) and the [upstream delay-fix submission](https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2405.0/01077.html).

Linux's cancellation path ends an active request and later permits transfer restart without a separate selected-TX-FIFO flush in that path. This supports v5's reuse after verified ENDTRANSFER. It does not prove the absence of controller-specific FIFO residue or partial ECM frames on this phone. An aborted frame may be lost, and resumed network traffic must validate framing and delivery. I found no basis to require an unreviewed FIFO-flush command before this one experiment.

The newer ForceRM fault report specifically concerns DWC_usb31 2.00a and 2.10a, not this DWC_usb3 core. It is not evidence of the same fault here. See the [upstream version-specific ForceRM submission](https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2608.0/12269.html).

Teardown, completion dispatch and `ua_tx_check()` all execute on the event thread. No teardown runs concurrently while the progress checker releases the mutex to end TX. Network output can run then, but sees `tx_closed` and cannot copy or start. Reopening at line 2201 requires no fatal gate, configuration present and alt 1. A queued reset or disconnect can be handled afterward and will end any newly started transfer through the same closed-admission path.

The rule against hiding a timeout holds. Busy is cleared only after observed hardware ownership return or a verified end. HWO is never cleared by the progress checker. Active is cleared on hardware return or successful ENDTRANSFER, and remains reserved on failure. Late old events can be ignored while a new slot is hardware-owned. If that new terminal TRB is already complete, accepting an old event merely reaps a completed current slot. A scratch test covers the hardware-owned case.

## Logging and queue teardown

`ua_data_down()` at `sweet_usb.c:1856` closes output before any end. After all three data endpoint ends succeed, lines 1876 through 1880 free every queued mbuf under the mutex. Alt 0, unconfiguration, reset and disconnect all use this path. Failure returns with admission closed and the gate set, so retained frames cannot resume to another host.

The asteroids logger default at line 2534 is now zero. Omitting `sweetusblog` from these images therefore leaves plain UDP console streaming off. The Poco F1 logger path is unchanged. This closes audit 4 finding 3's default exposure in the reviewed source. This audit did not capture traffic on hardware, and explicit `sweetusblog=1` still exports unauthenticated logs.

## Independent checks

1. The supplied b5 harness passes 38 of 38 grouped tests plus boot order. B4 fails exactly six of those tests. The scratch race trace reproduces `stale completion 0x0000c046 on ep3 ignored` on b4.
2. The scratch extension passes 43 of 43 grouped tests on b5. It adds queue flush and closed-admission checks for all four teardown actions and a late old TX event after HWO-based recovery. Its source and results are retained in `analysis/usb-gadget-audit5-scratch/`.
3. `python3 -I plans/usb/gadget/host/test_usb_check.py` passes all supplied checker, verdict and interface cases. A separate verdict invocation rejects the actual ast145 log for unchanged TX with a full queue. The status parser accepts the new stale/lost/ended suffix.
4. The requested strict C11 header test passes 13,741 checks. Reversing v4 2161 then 2160 on a scratch copy of the combined dev files reproduces work/a. Applying v5 2160 then 2161 reproduces all four work/b5 files byte for byte.
5. The two XNU source trees have the same file inventory and differ only in `bsd/dev/sweet_usb.c`, which matches work/b5. Build products outside the source tree differ as expected. The pinned kernel hash matches, and all ten payload segments in each supplied boot image match that kernel after the packer's chained-pointer rebasing.

The harness models register completion, cache synchronization and lock scheduling. Its race hook abandons a blocked event turn and retries it after unlocking. This is appropriate for the tested lock-before-mutation race, but is not a real multicore execution or a hardware model of FIFO state. Neither the tests nor the canaries establish DMA confinement.

## Image and session checks

The kernel SHA256 is `a1bca511017b253efece8136b82bcf895692e182f823eccae84e5ae27a35375a`.

The test image SHA256 is `95d381335d7c03b676a4303ce2ed9ea3e2728286a2a2e7723cc8b9b75b591722`. Its argument line is 596 bytes. The longer session image SHA256 is `d0e31cf5486fcd678bc9b375e8e5d357a3a755023dae79f82090d35ec047e792`, with 597 bytes of arguments.

Offline inspection confirms both images contain `sweetusb=4`, `sweetfbarm=1`, `sweetpanicfb=10`, `sweetselftest=1`, `sweetthermal=3`, UBWC, and keep plus astwait. The deadmen are respectively 960 and 7200 seconds. Neither includes keepsyms, sweettouchirq, sweetkeys_ms, sweetusblog, sweetpwrfast, sweetamp_ui or sweetappmetal. Admission still checks that the deadman thread actually started, rather than trusting the argument alone.

`plans/devloop/selftest/scripts/usbdev.sh:8` targets deadman minus 300 seconds, so the test normally exits near 660 seconds of uptime and the longer session near 6900 seconds. Its ten-second polling and later hook work make these approximate normal exits. The hook's `finish()` at `plans/devloop/selftest/hook.sh:33` syncs and requests fastboot. The kernel deadman remains the fallback.

The 480-second soak starts after enumeration, SSH startup and the initial checksum. Slow startup can leave insufficient time before the self-test exits. That must produce failed or incomplete acceptance, not an inferred pass. The 7200-second image is not qualified for an owner's two-hour session by this first short experiment.

## Audit 4 findings still present

1. Finding 1's missing-deadman admission failure is absent from these dev images. They deliberately retain an expiring session timer, which is still unsuitable for indefinite daily use.
2. Finding 4 remains. Root SSH has no phone-side host trust or lock-state gate, and wildcard dual-stack exposure is unchanged. Restriction to the known Mac is an experiment condition.
3. Finding 5 remains for physical replug, suspend, charger attachment and endpoint halt recovery. V5 improves it only by discarding queued old-host frames after successful teardown.
4. Finding 6 remains. Orderly reboot, panic and thermal reset still lack explicit gadget quiescence before reset. The new progress path does not add such a hook or validate firmware DMA reset.
5. Finding 7 remains as a hardware qualification gap for long uptime and combined load. V5 adds bounded TX progress handling and counters, but does not add event-overflow recovery, an independent watchdog or DMA confinement.

Audit 4 finding 2 has a reproduced software cause and a bounded recovery design suitable for testing. Its hardware closure remains to be established by the proposed run. Finding 3's default streaming and queued-frame issues are fixed in source. None of the out-of-scope daily findings was found to become a new test-boot blocker through v5.

For acceptance, require fresh phone and Mac evidence, an explicit completed soak PASS including post-idle rounds, no persistent full TX queue, no gate or canary failure, and clean dev.py completion back through recovery. Record stale, TX lost and TX ended counts. A recovery count alone is diagnostic, not a pass or a failure. Do not extrapolate this single run to daily readiness.

Verdict: GO
