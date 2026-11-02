# USB gadget v2 re-audit

2026-11-03. Desk review only. I did not use a phone, adb, fastboot, the running loop, or any image builder. Live kernel trees were read only. Patch application, compilation and added tests used `analysis/usb-gadget-audit2-scratch/`. Nothing was deleted.

## Verdict per level

| Setting | Isolated test boot | Decision |
| --- | --- | --- |
| `sweetusb=2` | GO | The new write path requires armed recovery, admission and an allocated pool. Missing or malformed proof never starts the controller. |
| `sweetusb=3` or `19` | GO after level 2 actually passes | Initial SETUP failure prevents connection. Command failures stop, and level 3 PASS requires a verified stop. |
| `sweetusb=4` or `20` | NO-GO | Alternate-setting teardown still permits TX to restart and can overwrite a controller-owned packet buffer. Finding 1 blocks this level. |
| Unset or `sweetusb=0` | GO relative to the base kernel | No new USB allocation, DMA or register access runs. |
| `sweetusb=1` | Existing read-only experiment | No new gadget path runs. Its inherited MMIO risks remain. |

GO means a single guarded experiment on the journaled root evidenced below, with `sweetfbarm`, `sweetpanicfb` and `sweetdeadman` armed. It does not guarantee recovery from an all-CPU bus stall. If “hands-free” requires recovery from that failure too, all three active levels remain NO-GO until an independent watchdog is validated. This limitation is shared by the new DWC3 accesses, not confined to QSCRATCH.

No live behavior is established by this audit. The source and host tests support the GO decisions, while the isolated boot supplies the missing hardware evidence. Do not advance past a failed or unproven level 2.

All short kernel filenames and line numbers below refer to `plans/usb/gadget/work/b2/bsd/dev/`. The scratch-applied files match those files byte for byte.

## Findings

### 1. Alternate 0 does not exclude network output during or after teardown

High severity. Blocks level 4. `sweet_usb.c:1829`, `:1839`, `:1845`, `:795`, `:804`, `:821` and `:508`.

`ua_data_down(false)` keeps `ud.configured` true, ends the data transfers without holding the TX lock, then clears `tx_busy`. It sets `data_alt` to zero afterward. Neither `ud_if_output()` nor `ud_tx_kick_locked()` tests `data_alt`, a stopping flag or `ud.gate` before accepting a packet.

There are two concrete failures. First, after an ordinary SET_INTERFACE to alt 0 completes, network output starts a new bulk IN transfer even though alt 0 advertises no endpoints. Pending network traffic is enough to trigger this without a hostile host.

Second, suppose TX is idle when alt-0 teardown checks EP_TX. Network output can start a transfer after that check and before teardown reacquires `ud.lock` at line 1839. Teardown then clears `tx_busy` on that new transfer. Another output copies a new packet into `UD_TX` at line 804 before the strict TRB guard notices that the slot is still active. The guard stops the next STARTTRANSFER, but the controller-owned payload has already changed.

I reproduced both cases using the supplied extracted real functions and the real `ud_tx_kick_locked()`, replacing its empty harness stub. For the race, a simulated network callback runs immediately before teardown's second lock acquisition. The first packet contains 0x22 and the second contains 0x77. The second packet replaces the active packet, and only afterward sets the gate to `start on an active or foreign slot`.

The smallest repair is to close a shared data-output admission gate under `ud.lock` before ending transfers, and keep it closed throughout alt 0. TX must check that gate and slot ownership before copying packet bytes. Reopen it under the same lock only when alt 1 is ready. Keep command cleanup outside the TX lock.

This finding proves a DMA ownership violation within the permanent pool. It does not demonstrate an out-of-pool write or HFS corruption.

### 2. Recovery refusal is before gadget MMIO, not before every USB register access

Medium severity. `sweet_usb.c:198`, `:212`, `:263` and `:2224`. This qualifies claim 1 and the daily-image refusal claim.

`sweet_usb_asteroids_probe()` runs the SMMU, SPMI, clock, DWC3 and PHY reads before calling `ua_gadget()`. The readiness check is inside `ua_gadget()`. Thus a daily image with `sweetusb=4` and no deadman still performs all the inherited read-only probe accesses before refusing the new gadget stage.

The supplied F1 test invokes `ua_gadget()` directly with a mocked readiness result. Its zero-MMIO result is true for that function and does not establish the same property for the public probe entry point. A fault during the preceding probe on an unarmed image remains outside this check.

Move the readiness rejection ahead of the probe when the requested level is 2 or higher to satisfy the stated before-any-access contract. This is not an additional new-register exposure on the properly armed test images, so it does not independently block the level 2 and 3 experiment.

### 3. Request validation remains incomplete

Medium severity. `sweet_usb_gadget.h:340`, `:352`, `:395` and `:415`.

The class-request branch accepts any class request with zero length at level 4, regardless of direction, recipient, interface or configuration state. For example, an IN class request addressed to the device before configuration receives success. Standard SET_CONFIGURATION and SET_INTERFACE validate their request type but still accept a nonzero data length, and GET_INTERFACE accepts an invalid interface index.

For example, SET_CONFIGURATION with type 0x00, value 1 and length 1 changes configuration and then arms an OUT data stage. That is not a valid SET_CONFIGURATION request. No address state is tracked, so the parser also cannot reject configuration in the Default state.

These cases remain bounded by the pool and packet-size checks. I found no independent memory escape from them. Validate the supported request's complete shape before returning its action, including length, index and legal device state. Claim 5 is only partially fixed.

## Disposition of the eight v1 findings

1. Recovery initialization now precedes `sweet_usb_probe()` at `sweet_ufs.c:1314` and `:1319`. `sweet_reboot_handsfree_ready()` at `sweet_reboot.c:555` checks asteroids, a usable reason channel, successful reason arming, a nonzero panic timeout and successful deadman-thread creation. The inherited probe exception is finding 2.

2. `ug_cmdioc_proof()` at `sweet_usb_gadget.h:117` requires count 4, the expected event at word zero and zero in every other word of the 4 KiB buffer. The mask deliberately ignores bits 23:16 and 31:28, as its comment says. Zero count, wrong form and misplaced events all stop without RUN_STOP. This closes the fail-open proof bug.

3. `ua_admit()` at `sweet_usb.c:2187` refuses running, unmasked, pending-event or command-active state before writes. The DMA allocation and address checks precede the first write at line 2292. `ua_stop()` at line 1757 checks ENDTRANSFER results, verifies halt, retains the allocation and panics if halt fails. It returns false on other verification failures, and level 3 no longer prints PASS in those cases. This closes the unsafe early-return and false-PASS paths.

4. GUCTL2 bit 14 is set and read back at `sweet_usb.c:2295` before any transfer can need ending. Failed ENDTRANSFER preserves software ownership and sets the gate at line 1744. `ua_trb_done()` at line 1993 rejects a completion when the current TRB is still hardware-owned. These repairs close the v1 mechanisms, with the TX teardown race in finding 1 still outstanding.

5. Repeated selection of the current alternate setting returns without restarting a notification at `sweet_usb.c:1944`. Unconfiguration closes TX admission before teardown, and active TRB slots are refused. Alternate 0 remains unsafe because its teardown does not close admission, and request checking is incomplete.

6. STARTTRANSFER failure reserves the slot and sets `ud.gate` at `sweet_usb.c:530`. Initial SETUP is checked before QSCRATCH and RUN_STOP at line 2374. Configuration, connect-done, SETSTALL and failure to leave halt all reach checked stopping. Runtime cleanup runs from the event thread, rather than recursively acquiring the TX lock. The source establishes that lock ordering, but the supplied mock locks are no-ops and cannot test deadlock behavior.

7. The host checker now requires an IOUSBHostInterface observation before disappearance. `usb-verdict.py` requires the kernel configuration line, the successful timed-stop record, verified-stop text and the Mac PASS. The previous descriptor-only false positive is closed for a single-run log. IORegistry interface keys remain hand-authored mock assumptions, so actual Mac detection is still unverified. Use a fresh phone log and checker result for the same run because the verdict script matches substrings and does not bind evidence across concatenated boots.

8. Level 2 has no QSCRATCH or FIFO register reads. The new device, endpoint-command, GUCTL2 and event-register groups get synced first-access markers. QSCRATCH is first accessed only after level 2 proof and first SETUP at levels 3 and 4. The MMIO table is materially accurate for the new gadget path, subject to finding 2's preceding probe.

## GUCTL2 on the recorded 3.30a core

The Phase 0 record in `3a-IOS/docs/plan-usb.md:81` gives GSNPSID 0x5533330A. This identifies DWC_usb3 3.30a. Linux documents GUCTL2[14] as enabling ENDTRANSFER completion polling on DWC_usb3 starting at 3.10a, and explicitly excludes the separate DWC_usb31 family. Thus the claimed mode applies to the recorded core, and checking CMDACT after enabling and reading back the bit is supported by the primary source. See [Linux dwc3_stop_active_transfer](https://raw.githubusercontent.com/torvalds/linux/master/drivers/usb/dwc3/gadget.c) and the register address in [Linux core.h](https://raw.githubusercontent.com/torvalds/linux/master/drivers/usb/dwc3/core.h).

Linux also warns about pending SETUP delaying ENDTRANSFER and avoids ending ep0 outside its data phase. The patch attempts to end every software-active transfer, including pending SETUP. This can produce a command timeout during stop. Its halt verification and panic fallback contain that failure rather than justifying guaranteed successful ENDTRANSFER on every control phase. The mock does not model that hardware restriction.

## HFS root and panic reset

The comments at `sweet_ufs.c:17` and `:1697` describe an older non-journaled root. They are not sufficient evidence of the current root format. The existing recorded boot `ios-asteroids/logs/ast100-ff-owner.txt:437` explicitly mounts `/dev/ufsroot0` as journaled HFS, with UFS FUA disabled at line 130. I did not inspect or change the live root.

For that journaled configuration, panic reset is a reasonable crash-consistency fallback. The HFS journal code at `ios-asteroids/research/xnu-combined/dxb/xnu/bsd/sweet_hfs/core/hfs_journal.c:464` issues DKIOCSYNCHRONIZE before committing its header when FUA is not enabled. The UFS ioctl implements this as a mutex-serialized SYNCHRONIZE CACHE at `sweet_ufs.c:791`, so completed journal writes are flushed rather than silently treating a barrier as a no-op.

`sweet_reboot_asteroids_panic()` at `sweet_reboot.c:436` flushes the raw log, attempts a final device-cache flush, delays and resets to fastboot. `sweet_ufs_flush_final()` at `sweet_ufs.c:1789` can skip when its lock is busy and does not perform VFS sync or unmount. Safety therefore comes from journaling and storage ordering, not from that final best-effort flush. An incomplete transaction or dirty journal after reset is expected. Recent file data can still be lost.

I found no new UFS write-path change or journal-ordering regression in v2. I found no source reason that this fallback would corrupt a healthy journaled root under normal UFS semantics. This is not a power-loss test of the device. A non-journaled writable root would not meet the user's corruption constraint and would make these panic-based active experiments NO-GO until the root is journaled or mounted read-only.

## Effects with sweetusb unset

The added `sa_deadman_running` flag records a successful thread creation that already occurred in the base implementation. The getter has no side effects and is called only from the gadget path. It does not arm recovery, start a new deadman or reset anything by itself.

The UFS change unconditionally reorders the USB call after touch, keys and recovery initialization on asteroids. It is therefore not literally conditional on `sweetusb>=2`. With sweetusb unset, the USB function immediately returns and the existing touch, keys and recovery actions remain the same. I found no new active hardware behavior in that disabled path. Level 1's probe now runs later as well.

## Host checks and their limits

Both supplied v2 SHA256 values match. Every entry in SHA256SUMS also matches. Applying 2160 and 2161 to scratch copies of `work/a` succeeds and reproduces all four `work/b2` files byte for byte.

I ran the supplied kernel harness on both stages. v2 passes all 24 tests plus the boot-order check. v1 passes the three baselines and fails all 21 finding checks plus boot order. The supplied header test passes 13,895 checks. The Mac checker passes nine canned scenarios and the verdict script passes five cases. Syntax checking the scratch-applied USB, UFS and reboot files with the recorded build flags exits zero for all three. No linked kernel was built.

The harness really extracts and compiles the kernel's `ua_*` functions and selected `ud_*` helpers. It is not merely a second implementation of their decisions. However, it replaces `ud_tx_kick_locked()` with an empty stub, replaces `ud_rx_done()` with a rearming stub, stubs readiness and network setup, omits the outer probe, and uses no-op locks and cache synchronization. Its GUCTL2 completion behavior is an assumed model. Consequently the 24 passing tests do not cover real TX payload ownership, scheduling, recovery execution, cache behavior or physical MMIO.

The extra test compiles the real TX helper alongside the same extracted gadget functions. Both new safety assertions fail while all original 24 tests still pass. Evidence is `analysis/usb-gadget-audit2-scratch/extra.txt`, and the retained source is `extra.c`. Run the existing binary with:

```sh
analysis/usb-gadget-audit2-scratch/extra
```

Exit 1 is expected because it asserts the missing safety properties. The race test uses one explicit scheduling hook before teardown's second lock acquisition, rather than claiming real host-thread concurrency.

## Known open items

No software deadman handles an all-CPU bus stall. Synced address markers help diagnose the last attempted group, but a button remains necessary in that case. No watchdog was enabled or assumed in the GO verdicts.

If the halted core produces no CmdIOC event, level 2 now fails closed. That is an acceptable failed experiment, and levels 3 and 4 must not follow it.

The daily image without a deadman refuses the gadget stage. It still executes the read-only probe first, as finding 2 explains. It is not approved here for active level 4 even if that readiness requirement is later relaxed.
