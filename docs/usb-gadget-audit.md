# USB gadget audit before first phone boot

2026-11-03. Desk review of the exact 2160 and 2161 hashes supplied in the request. No phone, adb, fastboot, test loop, kernel build, or live-tree write was used. Scratch is `analysis/usb-gadget-audit-scratch/`. Nothing was deleted.

## Verdict

| Setting | Verdict for an isolated hands-free test boot | Reason |
| --- | --- | --- |
| `sweetusb=2` | NO-GO | Recovery is armed after USB startup, the DMA proof can fail open, and failed initial halting leaves ABL's DMA destination in place. |
| `sweetusb=3` or `19` | NO-GO | Level 2 blockers apply, and transfer-command failures and teardown completion are not handled reliably. |
| `sweetusb=4` or `20` | NO-GO | The earlier blockers apply, plus active notification TRBs can be overwritten by repeated host requests. |
| Unset or `sweetusb=0` | GO relative to the base kernel | The asteroids function returns before any USB access or allocation. These patches introduce no active DMA here. |
| `sweetusb=1` | Same read-only behavior as base | The previous probe still runs, with its existing MMIO risks. No new gadget code runs. |

GO here means no added risk from these patches in the disabled path. It does not certify the base kernel or the inherited ABL state.

Proven by reading means the source directly establishes the stated behavior. Proven by running means a desk command or extracted-function test established it. Inferred identifies possible hardware consequences that were not exercised. No USB behavior in this report was proven on this phone.

All `sweet_usb.c` and `sweet_usb_gadget.h` line references below use the result of applying both patches, identical to `plans/usb/gadget/work/b/bsd/dev/`. Base-kernel file references name their full relative path.

## Must fix before the indicated boot

### 1. Recovery safeguards are initialized after the risky USB work

Blocks levels 2, 3 and 4. Proven by reading the caller and recovery initialization.

`ios-asteroids/research/xnu-combined/dxb/xnu/bsd/dev/sweet_ufs.c:1309` calls `sweet_usb_probe()` before `sweet_reboot_asteroids_init()` at line 1315. That initializer calls `sa_handsfree_init()` in `bsd/dev/sweet_reboot.c:314`. Only then are the bootloader reason, panic delay, deadman thread and optional Gunyah watchdog set up, at lines 522 through 539.

Consequently a synchronous abort during `ua_gadget()`, or a stalled MMIO transaction before it returns, happens before the requested `sweetpanicfb=10` and `sweetdeadman=600` protection exists. The new level 2 QSCRATCH access at `sweet_usb.c:2023` is one concrete case. An endpoint-register access during level 2 is another. The generated boot arguments do not contain `sweetgwdt`, and a software deadman cannot recover a wedge of every CPU anyway.

Move recovery initialization ahead of the optional USB path, with a checked readiness result before levels 2 and higher. A hard-hang recovery claim also needs an independently validated watchdog and its bootloader-reason behavior. Do not enable that unproven watchdog merely to make this patch pass review.

The claim that a panic during gadget startup automatically reaches fastboot is false for this ordering. The claim that QSCRATCH is the only possible button-requiring hang is unsupported.

### 2. Missing event DMA proof is accepted as a successful ownership test

Blocks level 2's stated acceptance gate and advancement to levels 3 and 4. Proven by reading `sweet_usb.c:2098`, `2124`, `2133` and `2149`.

After 100 ms with no event, the code prints that DMA is unproven and still emits `L2: PASS`. Levels 3 and 4 then arm SETUP and set RUN_STOP. A refused or nonfunctional command-event path therefore passes the very gate intended to establish the event destination before connecting.

When an event does appear, the test accepts any nonzero word anywhere in the 4 KiB allocation, rather than the expected endpoint-command completion with the expected command and endpoint. `sweet_usb.c:2121` also derives the next cursor from the first nonzero word rather than an established producer position. This is weaker than validating the expected event at a known origin.

Require a real event of the expected form within the deadline. A timeout or unexpected event must stop the stage and prevent connection. If this command cannot generate an event while halted on this revision, redesign and document the proof stage instead of returning PASS.

This does not prove the controller writes outside the pool. It proves the intended evidence is optional in the current code.

### 3. Failure after the first register write does not consistently reach a safe stopped state

Blocks levels 2, 3 and 4. Proven by reading the branches. Continued unwanted DMA is inferred when hardware fails to halt.

At `sweet_usb.c:2029` the first write disables device events. If `ua_halt()` times out, line 2032 returns directly. ABL's event address remains programmed, its endpoints are still enabled, no owned buffer exists, and no further cleanup or immediate recovery runs. If ABL entered with an active transfer and the halt never completes, it can continue using ABL transfer memory while the kernel continues booting.

The allocation and address-check failures at lines 2037 through 2052 also return directly after hardware writes. They are safer when halt has actually succeeded, but contradict the unconditional teardown claim. `DEVTEN=0` does not itself set `GEVNTSIZ.INTMASK`, so the allocation-failure log also assumes ABL's event interrupt was masked.

Even `ua_stop()` at line 1683 ignores ENDTRANSFER results, continues after a failed halt, restores QSCRATCH, and permanently sets `ud.stopped`. It cannot retry later. On event-address readback failure at line 2081, its log still claims that the address stays on our pool, even though the check has just established that this may be false. Level 3 emits PASS after `ua_stop()` without checking whether it actually halted, at line 1939.

For the first test, require the known Phase 0 halted state as an admission condition and refuse unexpected running state before changing it. Make cleanup return a checked result, preserve the owned pool, stop reporting PASS on cleanup failure, and define a bounded recovery path for failure to quiesce. A rejected address write cannot be described as safely owned without verified readback.

### 4. ENDTRANSFER completion is assumed without establishing the polling mode

Blocks levels 3 and 4 for reliable reset, reuse and disconnect. Proven by reading the implementation. Late DMA or stale completions affecting reused slots are inferred.

`ud_epcmd()` at `sweet_usb.c:472` treats a cleared CMDACT as completion for every command. `ud_bus_reset()` at line 1074 and `ua_stop()` at line 1698 issue forced ENDTRANSFER without CmdIOC, ignore errors, and immediately clear software ownership. The patch neither reads nor enables `GUCTL2[14]` to establish the DWC_usb3 ENDTRANSFER polling mode.

The DWC3 3.30a core supports this mode, but its ABL value was not probed. Linux documents that polling mode explicitly and otherwise uses completion events or a delay. See `dwc3_stop_active_transfer()` in [Linux gadget.c](https://raw.githubusercontent.com/torvalds/linux/master/drivers/usb/dwc3/gadget.c).

A host reset during bulk OUT can therefore cause software to reuse the RX TRB and buffer before it has established that the old transfer ended. The permanent allocation prevents a free-to-another-owner error within this boot. It does not prevent concurrent use of the same slot or stale completions advancing the new transfer.

Use the controller's supported completion mechanism and check its result before reuse. Ensure reset and disconnect tests exercise pending SETUP and bulk transfers, since command timeout is already a real branch in this code.

### 5. Repeated SET_INTERFACE overwrites a notification that the controller still owns

Blocks level 4, including a confused or hostile host. Proven by reading and by running extracted `ua_setup()` and `ud_start_trb()` with register commands stubbed.

Each `SET_INTERFACE(interface 1, alt 1)` at `sweet_usb.c:1795` invokes `ud_notify_connected()`, including when alt 1 is already selected. That function writes `UD_NOTIFY` at line 602 and rewrites `UD_TRB_INT` at line 608. `ud_start_trb()` at line 498 does not refuse an already active endpoint or a TRB with hardware ownership still set.

A host can send two such control requests without polling the interrupt endpoint between them. The second request rewrites the notification and its live TRB. A completion from the first request can then trigger `ud_notify_speed()` against the second transfer. Pool bounds remain valid, but DMA ownership and notification state are broken.

Serialize notification ownership. Repeated selection of the same alternate setting should not restart an outstanding notification. Wait for successful completion or a confirmed end before changing either its TRB or its buffer.

The same request handling leaves ECM configured after `SET_CONFIGURATION(0)` at line 1783 and leaves data transfers active after alt 0 at line 1795. I reproduced the unchanged `ud.configured` and `data_alt` after configuration zero. Implement unconfiguration and alternate-setting teardown before accepting those requests.

### 6. Command failures do not stop the live gadget

Blocks levels 3 and 4. Proven by reading and by an extracted-function run with STARTTRANSFER returning failure.

At `sweet_usb.c:517`, STARTTRANSFER failure leaves neither `ud.gate` nor `ud.stopped` set and propagates no error to the caller. Thus failure to arm the first SETUP at line 2150 still proceeds through QSCRATCH and RUN_STOP. A command timeout is also ambiguous about whether hardware later accepted the operation, while software records no active resource to end.

`ua_configure()` at line 1755 prints failure and returns void. `ua_setup()` still records the configuration timestamp and proceeds with its status stage. Connect-done endpoint reconfiguration at lines 1840 and 1841 ignores both command results. The startup wait at lines 2164 through 2168 also continues after all 500 polls still report halted.

Return and propagate command status, fail closed on timeout, and stop before connecting when initial SETUP cannot be armed. During operation, defer cleanup to a context that does not already hold `ud.lock`. A direct `ua_stop()` call from the TX path under that lock would deadlock.

## Test and diagnostic findings

### 7. Host disappearance does not prove a successful level 3 timed disconnect

Blocks trusting the host PASS as acceptance evidence. It is not independently a DMA blocker. Proven by reading `host/usb-check.py:88` and `selftest/usb.sh:9`.

The checker accepts any disappearance within 300 seconds after seeing descriptors. A device that never reaches SET_CONFIGURATION and stops at the 120-second timeout also passes this host test. The self-test's eventual reboot can produce the same observation.

Require the kernel's successful configuration and successful halt records in the combined verdict. Add a mock case where descriptor discovery succeeds but configuration never happens. Existing eight mock scenarios pass without covering this false positive.

### 8. The new MMIO exposure is larger than the QSCRATCH claim

Blocks the exclusive hands-free risk claim. Proven by reading the probe register list at `sweet_usb.c:230` and the new reads at lines 2023 through 2025.

Level 2 newly reads GUCTL1, four TX FIFO sizing registers and GRXFIFOSIZ0, in addition to QSCRATCH. None is in the preceding level 1 DWC3 register list. Command registers are also newly accessed. Successfully reading GSNPSID does not prove that every such address or write is permitted by Gunyah.

The plain inline-assembly accessors are correct for avoiding compiler-generated writeback MMIO loads and stores. They cannot guarantee that a forbidden, powered-down or stalled access returns. The last sync before the grouped new reads is at line 2000, so the ABL-state and exact pending diagnostic read are not individually persisted before QSCRATCH.

Flush an explicit address marker before each first access to an unproven register group. Keep the distinction between an external abort with CPU progress and an unrecoverable bus stall. Fix finding 1 before treating either as an isolated hands-free experiment.

## DMA and concurrency assessment

The allocation at `sweet_usb.c:2037` is a sound starting point. `kmem_alloc_contig()` obtains contiguous wired storage, the caller verifies each page's physical address, checks the SMMU output-width ceiling, and rejects overlap with the assumed 4 KiB ABL event buffer. `KMA_PERMANENT` and the absence of a free keep that storage assigned throughout this kernel boot. I read the allocator in base `osfmk/vm/vm_kern.c:1189`, the permanent mapping flag at line 1030, and wired-count setup at line 1059.

All newly supplied event and packet addresses derive from `ud.dma_pa`, obtained through `pmap_find_phys()`. The event address is at offset 0, the five TRBs are at 0x1000 through 0x1100, SETUP is 0x1140, notifications 0x1180, control data 0x1200, RX 0x2000 and TX 0x2800. Buffer lengths are checked against separate regions, and STARTTRANSFER receives the checked TRB address, including its upper 32 bits. I found no host-supplied DMA address and no direct DMA into an mbuf.

These are single terminal TRBs with LST set. There is no linked TRB ring and no link TRB to audit. The event buffer is the only circular ring, and the poller bounds its count and wraps its software cursor inside 4 KiB. The missing protection is ownership during reuse, particularly findings 4 and 5, rather than ordinary offset arithmetic.

The pages before and after the pool are writable canary pages, not protected guard pages. They detect adjacent overwrites after they happen. They cannot prevent a wrongly addressed transfer from jumping directly into kernel or UFS memory. Stage 1 pass-through and the lack of a pool-only mapping make that limitation real, as the plan already acknowledges.

`ud_sync()` reaches the real `dc civac` helper in `bsd/dev/sweet_board.h:28`, with barriers. TRBs and small buffers have separate 64-byte slots, which avoids the previous neighbor-TRB cache-line sharing on a 64-byte cache-line machine. RX buffers are synchronized before copying, and the event allocation is initialized and synchronized before programming it. Device cache coherency and all DMA ordering remain unproven on this phone, but I found no missing ordinary buffer-sync call in the reviewed paths.

The current route gate is read-only and checks exactly one matching SID route and a nontranslating destination. It accepts the Phase 0 SMR3, bank 3, SCTLR.M=0 state. No SMMU mapping is changed while old transfers may exist. It does not constrain DMA to the allocation and does not log a post-DMA sGFSR, despite that Phase 3 diagnostic gate.

The recorded Phase 0 halted state is good evidence for that boot. The code checks halt before changing the event address and programs the new event address before any new STARTTRANSFER or RUN_STOP. It acknowledges the old count, then resets endpoint resource allocation with DEPSTARTCFG index 0 and restricts DALEPENA to ep0. I did not establish from a 3.30a programming manual that this alone purges every retained ABL transfer context or establishes the event producer cursor for arbitrary pending state. Do not infer that from one clean ABL snapshot. Refusing unexpected running or pending state is the smaller first-boot policy.

There is no new IRQ handler. GEVNTSIZ masks the event interrupt and one thread polls. TX output and TX queue changes use `ud.lock`; endpoint command registers for different physical endpoints are separate. I found no immediate double-free or mbuf ownership error in the bounded TX queue. RX residual arithmetic rejects impossible lengths, and only bounded Ethernet frames are copied into a checked cluster before passing to the network stack. Overlength OUT transfers remain bounded by the submitted 2 KiB DMA region, assuming normal DWC3 behavior.

Request parsing bounds control OUT data to one packet and a 512-byte region. It does not fully validate request direction, recipient or device state, so malformed standard requests can still mutate configuration. That combines with the ownership defects above. Full-speed ECM is explicitly refused, while default SuperSpeed operation broadens the first experiment beyond the plan's high-speed-only gate. High-speed override alone does not fix the blockers.

The fixed IPv6 bytes and `SIOCLL_START_64` call are consistent with the claimed fe80::5eff:fe77:ee02 address. I traced the ioctl to base `bsd/netinet6/in6.c:704`. Interface attachment, duplicate-address detection, Mac ECM matching and Dropbear IPv6 reachability remain end-to-end unknowns. These patches do not establish SSH merely by logging that the interface is up.

## Checks actually run

1. Both patch SHA256 values matched the request, and applying them with `git apply` to a scratch copy of the combined kernel USB file succeeded.
2. Both resulting files matched the author's `work/b` byte for byte.
3. The supplied C host tests passed all 13,856 checks, and the supplied Mac checker mocks passed all eight scenarios in scratch with TMPDIR redirected there.
4. Syntax checking and ARM64 assembly generation with the combined build's recorded compiler arguments succeeded. The six diagnostics attributed to this C file are in inherited code. The complete compilation emits 552 warnings, mostly in included headers.
5. Extracted unchanged `ud_start_trb()` and `ua_setup()` functions reproduced silent STARTTRANSFER failure, an active-slot restart, ignored configuration zero, and duplicate alternate-setting notifications with MMIO and surrounding services stubbed.

The additional check is `analysis/usb-gadget-audit-scratch/audit-regressions.c`. Compile and run it locally with:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I analysis/usb-gadget-audit-scratch/apply/bsd/dev \
  analysis/usb-gadget-audit-scratch/audit-regressions.c \
  -o analysis/usb-gadget-audit-scratch/audit-regressions
analysis/usb-gadget-audit-scratch/audit-regressions
```

It asserts the buggy behaviors to demonstrate them. It is not a passing safety test for the gadget.

Generated assembly preserves the explicit plain `ldr` and `str` MMIO templates. A writeback load elsewhere in the function can be a normal RAM access, so a blanket search for writeback instructions is not a valid MMIO audit. No linked kernel or boot image was produced or disassembled.

The build wrapper, self-test body and host checker were read. I did not execute the image builder or real host checker because they are unnecessary for a desk safety review. An attempted read-only download of Linux source to scratch returned HTTP 429. The primary sources used for the hardware cross-checks were read through web retrieval instead.

## Author claims disposition

Claim 1 is partially established. The route check, owned permanent allocation, halt-before-reprogram order and absence of RUN_STOP in level 2 are present. The event proof is optional, and the surrounding pages are canaries.

Claim 2 describes the intended normal path. Level 3 has no advertised data endpoints and a 20-second timer after configuration. It can report success despite failed halting, and host disappearance alone does not establish that path.

Claim 3 is unproven end to end and unsafe to boot as submitted. The code supplies ECM descriptors, interface creation and the fixed IPv6 address, but has active-transfer ownership defects.

Claim 4 is false. Early returns bypass teardown, command errors do not consistently trigger it, and a failed event-address readback cannot support the retained-owned-buffer claim.

Claim 5 is established by reading the asteroids dispatch and comparing the base probe. Unset, zero and one do not enter new DMA code. Patch 2160 alone adds no active asteroids behavior. This conclusion does not extend to Poco F1 and sweet gadget behavior, whose shared layout and accessors do change.

Claim 6 is false as an exclusive claim. More registers are newly accessed, recovery is armed too late, and no enabled hard-hang watchdog protects this startup path.
