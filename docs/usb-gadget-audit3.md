# USB gadget v3 third audit

2026-11-03. Level 4 is a conditional GO for one isolated test boot after levels 2 and 3 pass on the candidate image. The v2 alt-0 TX ownership race is fixed. I found no remaining DMA ownership blocker in the requested teardown variants.

This was desk work. I did not access the phone, adb, fastboot, the running loop or an image builder. All patch application, test extensions and compiler outputs used `analysis/usb-gadget-audit3-scratch/` or retained temporary directories. I made no writes to live kernel trees and deleted nothing.

## Verdict per level

1. `sweetusb=2` is GO for the guarded ownership experiment. Recovery refusal now precedes the inherited probe too. Admission, allocation, event-buffer ownership and the exact CmdIOC proof retain their v2 checks.
2. `sweetusb=3` or `19` is GO after an actual level 2 PASS. The normal enumeration and timed verified-stop tests pass. The pending SETUP stop change improves this path.
3. `sweetusb=4` or `20` is GO after actual levels 2 and 3 PASS on the candidate image. This approves a controlled CDC-ECM and SSH experiment with the known Mac, not daily deployment or arbitrary-host USB conformance. The remaining findings below do not block that experiment.
4. Unset or `sweetusb=0` is GO relative to the base kernel. The public asteroids probe returns before recovery checking, allocation, thread creation or MMIO. The scratch test confirms zero modeled accesses.
5. `sweetusb=1` remains the inherited read-only experiment. It does not enter the new gadget path and retains the inherited probe risks.

These GO decisions retain audit 2's conditions. Recovery must actually be armed through `sweetfbarm`, `sweetpanicfb` and the running deadman. The writable root must be the journaled configuration assessed there. Neither the root's current state nor hardware recovery was revalidated here.

There is still no recovery guarantee for an all-CPU bus stall. If hands-free means recovery even from that failure, active levels 2, 3 and 4 are all NO-GO until an independent watchdog is validated. Under the same limited experimental meaning used in audit 2, the level 4 source blocker is closed. Nothing here authorizes interrupting the running v2 tests.

Short kernel filenames and line numbers below refer to `plans/usb/gadget/work/b3/bsd/dev/`. The scratch-applied files match those files byte for byte. Host filenames refer to `plans/usb/gadget/host/`.

## Findings

### 1. Full control-request validation is still incomplete

Medium severity. Nonblocking for the isolated known-Mac experiment. `sweet_usb_gadget.h:332`, `:348`, `:350`, `:352`, `:357` and `:366`.

The dangerous malformed SET_CONFIGURATION case from audit 2 is fixed. SET_CONFIGURATION and SET_INTERFACE now check their lengths, interface indices are bounded, Address state is tracked, and arbitrary class requests no longer succeed. However, the claim that every accepted request is validated in full is too broad.

I compiled a separate caller of the actual `ug_setup_plan()` and confirmed these examples. GET_STATUS addressed to interface 65535 in Default state returns a two-byte IN plan. CLEAR_FEATURE with selector 65535 and endpoint index 65535 in Default state returns success. GET_DESCRIPTOR for the device descriptor with nonzero wIndex also returns data, and SET_SEL is accepted at high speed in Default state.

The GET_STATUS and feature branches do not validate the addressed object or legal state. Features are acknowledged without changing anything, and GET_STATUS always returns zero. The ECM filter branch also accepts reserved filter bits because it does not check wValue.

The known CLEAR_FEATURE(ENDPOINT_HALT) omission belongs here. If an endpoint is actually halted, the host can receive successful completion of its clear request while the hardware remains stalled. That can prevent networking or SSH from recovering. SET_FEATURE(ENDPOINT_HALT) similarly acknowledges a halt it never applies.

The smallest repair is to reject unsupported feature selectors and nonexistent recipients, validate the state and reserved fields for supported requests, and implement checked CLEARSTALL for an accepted endpoint-halt clear. Endpoint status should reflect the implemented state. This is a protocol and recovery problem. The reproduced inputs did not escape the permanent DMA pool or recreate the TX corruption race.

Evidence is `analysis/usb-gadget-audit3-scratch/claims.c` and `claims.txt`.

### 2. Verdict isolation selects the last start of the requested level

Low severity. Nonblocking when each verdict receives fresh evidence for its own run. `host/usb-verdict.py:25`, `:28` and `:40`.

The same-level concatenated-boot bug is fixed. An old level 3 PASS followed by another level 3 start that goes quiet now fails, as the supplied test demonstrates.

The code searches for the last start of the requested level, rather than the last start of any level. I reproduced a log containing a passing level 2 followed by a new level 3 start with no further output. Calling `verdict(2, log, None)` still returns PASS from the earlier boot. A verdict for the latest boot must reject that level mismatch.

Select the last start across all gadget levels, then require its level to match the requested level. A boot that dies before emitting any new start still cannot be distinguished without an independent boot boundary. The Mac result also has no shared run identifier, so a stale Mac PASS remains usable with a later phone log. Continue requiring fresh phone and Mac files from the same run.

Evidence is `analysis/usb-gadget-audit3-scratch/check-claims.py` and `claims.txt`.

### 3. Ten of the advertised twelve alt-0 injection positions never execute

Low severity. Test coverage claim only. `host/test_kernel.c:885`, `:892` and `:895`.

I counted actual hook firings during the supplied SET_INTERFACE alt-0 sweep. Hook positions 1 and 2 each execute once. Positions 3 through 12 never fire because the operation only takes two relevant locks. The injected network output itself adds one nested mock lock acquisition when it fires.

On v2, those ten cases still fail because the unconditional output after teardown starts TX. They do not demonstrate ten additional teardown interleavings. Position 2 does execute and reproduces the previous ownership race, so this overstatement does not invalidate the regression test that matters.

Record hook firings and require each intended injection to execute. Describe this as two lock-boundary interleavings plus output after teardown. Additional timing coverage needs separate hooks or concurrent execution, rather than unused position numbers.

Evidence is the first thirteen lines of `analysis/usb-gadget-audit3-scratch/extra-v3.txt` and the retained scratch `host/test_kernel.c`.

## Why the TX blocker is closed

`ua_data_down()` closes `ud.tx_closed` while holding `ud.lock` at `sweet_usb.c:1849`, before any endpoint transfer is ended. An output that wins the lock before this point completes its start before teardown proceeds, so teardown sees and ends that transfer. An output that loses the lock sees the closed gate and drops its packet at `:833`.

`ud_tx_kick_locked()` checks the closed gate, fatal gate, stopped state and strict TX-slot ownership before dequeuing or copying bytes at `:804`. This closes the second part of the old failure, where copying preceded the active-slot guard. `tx_busy` is cleared after successful ending with admission still closed at `:1862`.

Alt 0 leaves admission closed. The only runtime reopen is under the same lock in the alt-1 action at `:1981`, after RX and notification starts, and only with configuration present and no fatal gate. Queued packets can then resume. A failed transfer end leaves ownership reserved and causes the event thread to stop.

`ua_stop()` closes admission and sets the terminal stopped flag under the lock at `:1776`. Reset and disconnect both pass through `ua_data_down(true)` at `:1875`. Endpoint command cleanup remains outside the TX lock. I found no alternate asteroids caller that bypasses those teardown rules.

The event thread serializes RX completion handling with SET_INTERFACE, reset and disconnect. An RX completion handled before teardown can rearm, and teardown then ends that transfer. A completion handled after teardown finds its software slot inactive and is ignored at `:2062`. There is no separate RX worker that can reopen the slot behind teardown.

## Independent checks

Both supplied patch hashes match the requested SHA256 values. Every entry in `SHA256SUMS-v3` matches. Applying 2160 then 2161 to scratch copies of `work/a` succeeds and reproduces all four `work/b3` files byte for byte.

I reran the unmodified supplied harness. V3 passes 31 of 31 tests plus boot order. V2 passes 25 and fails the six new regression checks, including the alt-0 race at position 2. The alt-1 liveness check passes on both versions. The header test passes 13,916 checks, the Mac checker passes nine canned scenarios, and the verdict script passes eight canned cases.

My scratch harness extension uses the actual `ud_if_output()`, `ud_tx_kick_locked()` and `ud_rx_done()`. It passes 46 of 46 grouped checks, including the original 31. The added cases cover:

1. Alt 0, unconfiguration, reset, disconnect and stop with idle TX or an active TX plus a queued packet, sweeping the lock-hook positions and submitting another packet afterward.
2. RX completion before alt 0, and a stale RX completion after alt 0, checking that no bulk OUT transfer remains active.
3. Disconnect with both a control IN data stage and bulk TX active, checking that both end and ep0 returns to SETUP.
4. A queued packet across alt 0 and alt-1 reopen, including output interleaved at reopen, followed by completion and another packet.
5. Failed TX ending during reset, plus unset and unarmed levels 2 through 4 through the public probe entry.

All modeled ownership checks pass on v3. The expanded alt-0 and queued-reopen assertions fail on v2 as expected. The sweep bounds are not a claim that all twelve positions fire. Only the instrumented, reachable boundaries provide interleaving evidence.

The real RX completion function runs with no attached network interface in this model. Its network-stack delivery branch is compiled with stubs, and its rearm logic executes. This checks RX ownership and teardown, not packet delivery through XNU. Locks are scheduling hooks rather than real mutexes, cache synchronization is mocked, and MMIO completion follows the author's controller model.

The scratch-applied USB, UFS and reboot sources also pass `-fsyntax-only` using the recorded kernel compile flags and existing headers. Compiler outputs and temporary files stay in scratch. I did not build or link a kernel.

Reproduce the extended checks with:

```sh
TMPDIR="$PWD/analysis/usb-gadget-audit3-scratch" python3 -I analysis/usb-gadget-audit3-scratch/host/kernel-harness.py b3
```

The retained logs are `harness-v3.txt`, `harness-v2.txt`, `extra-v3.txt`, `extra-v2.txt`, `host-tests.txt` and `claims.txt` under that scratch directory. `extra-v2.txt` records the earlier 45-check extension, before the final reopen-hook check was added. `extra-v3.txt` records the final 46-check version.

## Stop and the remaining open items

Skipping ENDTRANSFER for ep0 while software is in SETUP removes the reported pending-SETUP timeout mechanism. The supplied model with a pending SETUP that never completes ENDTRANSFER now reaches verified halt. The DMA allocation remains permanent, and stopped state prevents subsequent reuse even though the skipped software slot remains marked active.

The implementation is still simpler than Linux's full disconnect sequence. Linux waits for control processing to reach SETUP or forces recovery, and explicitly notes that pending event counts must be acknowledged for halt. V3 performs synchronous ending and halt polling without draining new events inside that stop routine. A host arriving during stop can therefore still cause a failed halt and panic fallback rather than a clean level 3 PASS. See [Linux DWC3 disconnect and transfer-stop handling](https://raw.githubusercontent.com/torvalds/linux/master/drivers/usb/dwc3/gadget.c).

That is a recoverable failed experiment under the already required panic-reset assumption. The hardware halt handshake, GUCTL2 polling semantics and panic reset are not established by mock success. I found no new path that frees DMA memory or resumes network output after a failed stop.

No reconnect after `ua_stop()` is acceptable for this single-boot experiment. The stopped flag is final, level 3 intentionally disconnects once, and level 4 errors require another boot. Normal bus-reset or disconnect handling through `ua_bus_reset()` is a different path and does not set that terminal flag.

Missing CLEARSTALL limits endpoint recovery as finding 1 describes. It does not block trying first enumeration and SSH on the known Mac. If that run stalls, it must fail rather than count partial enumeration as success.

The all-CPU stall watchdog remains the only open item that prevents an unconditional hands-free verdict. A software deadman and panic handler cannot run if every CPU is stuck on the bus.

## Level 2, level 3 and unset regression assessment

The v3 UFS and reboot source files are byte-identical to v2. Recovery initialization order, deadman readiness and storage paths therefore retain audit 2's assessment. I did not repeat its journal analysis or inspect the live root.

The USB delta adds the earlier readiness refusal, the TX gate, Address-state tracking, stricter request checks and the pending-SETUP stop exception. Level 2 does not open the TX gate or process host control requests. Its normal ownership proof is unchanged. Level 3 never creates the ECM data endpoints, and ordinary host enumeration still passes the stricter parser and verified stop tests.

With sweetusb unset, the early return precedes the new readiness test. The existing unconditional boot-order change is the same as v2. No v3 allocation, MMIO or recovery action is added to that disabled path. On Poco F1, `tx_closed` remains false and strict ownership remains disabled, so the new TX admission check is inert in the normal legacy path. No physical Poco F1 test was performed.
