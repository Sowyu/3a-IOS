# 2174 first offline start review

Verdict: GO for one guarded first PAS4 + GLINK + QRTR + RAM-only rmtfs + pd-mapper boot of the exact series below, subject to the image and recovery conditions in this report. No blocking source finding remains from the 2173 review. This is first-start permission within the existing task scope, not hardware qualification or approval for daily use.

Review only. I performed no phone operation, kernel build, host-test compilation, partition write or implementation edit. I read HANDOFF.md first, then sol-2173-audit.md and docs/modem.md. I used the ponytail-review and unslop skills and wrote only this report.

## Exact ordered revision

Apply these files from `3a-IOS/patches/modem/` in this order on the previously reviewed dedicated-tree base. SHA256 values are pinned below.

1. `2170-asteroids-modem-preflight.patch`
   `2eeb835d8710b71cc33ed6af8dc0d45bc6daf5db4188b42b5185ac484ec9baf2`
2. `2171-asteroids-modem-fat-names-and-chains.patch`
   `880052a579262fa352968cfa3f1823531d4590e3dd9e16129ab4853782d0a750`
3. `2172-asteroids-modem-smem-glink-core.patch`
   `aa898aa02865333c5f8bb8fe70d2467ecb655c71e6efdff3b5bacaa6d824dbb0`
4. `2173-asteroids-modem-pas-offline.patch`
   `3b0f4f1c13f30ab85316bec6d9024c0b614e1d4448134c93dcbfcab9b90ea7ba`
5. `2174-asteroids-modem-xo-and-glink-completion.patch`
   `7c6d9adac20cc0aaf89438d680f68d258f9a8eb4582cb75557c10e524df04bbf`

I independently reconstructed all eighteen added source/header files in memory using the ordered public patches and compared their full bytes with production under `ios-asteroids/research/xnu-modem/dxb/xnu`. All matched, including `sweet_modem.c`, SMEM, GLINK and all thirteen 2173 files. I also verified every resulting integration hunk in `bsd/conf/files`, `sweet_ufs.c` and `sweet_touch_asteroids.c` against production.

Production hashes:

- `bsd/dev/modem/ModemRuntime.inc`: `76786f10967f2f9ea47c088d3a9a1b5fb26c785ae9ca18e7c09b77ee7f00d72a`
- `bsd/dev/modem/QcomGlink.c`: `a58d9578e297234a619770b3472a2768998545af275839b2283bc275eb0c402f`
- `bsd/dev/sweet_modem.c`: `f0812ab4aedb103dd769c8fada8592a9fcb56991a7c22c268d223a9b81d41034`

## XO and CX rationale

I explicitly examined `bsd/dev/sweet_rpmh.c:522-590`, including the hard lesson at lines 569 onward. Its APPS vote replaces the inherited ABL vote. CX/MX feed display, UFS and memory, so that helper refuses their off votes and only releases MSS. The helper is gated by `sweet_board_beryllium()` and supplies policy evidence, not the SM7635 implementation.

2174 follows that policy in its actual SM7635 path. `ModemRuntime.inc:281` selects the highest cmd-db CX/MSS level index after validating the level table. It does not assume the inherited APPS baseline is zero. Cleanup sends zero only to MSS, after PAS shutdown has succeeded or when PAS was never attempted. CX remains at the conservative shared floor until reset.

`ModemRuntime.inc:95-131` adds cmd-db `xo.lvl` as a third acknowledged command with data 3. The value is the enable mask and never passes through the voltage-index lookup. The start path waits for all three command completions before `m_load`, PAS init, memory setup or auth/reset. No idle TCS means refusal without submitting a vote.

The explicit XO floor is a kernel-lifetime dependency of this bounded bring-up image. It does not restore an invented zero baseline or claim XO/CX are powered off. With the supplied XO enable encoding, mask 3 preserves both enable bits. I found no other XO writer in the production kernel search. Touch uses separate VRM/BCM resources and shares the same RSC mutex around selection, programming and IRQ updates. The legacy ARC helper is board-gated away on asteroids.

This is acceptable for the requested reboot-bounded experiment. It is not a suspend or general clock-client implementation. Any new XO/suspend consumer requires shared aggregation before the floor policy changes. Reset ends this kernel's floor lease. Hardware acceptance of these requests and thermal behavior remain unmeasured.

## GLINK completion fix

`QcomGlink.c:196-211,225-244` now keeps a delivered local intent busy until progress successfully queues RX_DONE or RX_DONE_W_REUSE. FIFO backpressure leaves the completion owed. Later progress retries without calling the receive callback again, and a premature packet using that busy intent breaks the stream instead of overwriting it.

Without reuse, successful completion clears advertisement before readvertising the intent. With reuse, advertisement remains valid. Both clear busy only after the completion enters TX. Production callbacks queue QRTR output and do not recursively poll GLINK, so the exactly-once reasoning applies to the reachable runtime.

The two-peer test fills the receiver's TX FIFO, delivers one packet, polls repeatedly while blocked, drains TX and verifies completion plus a second delivery. It runs with and without negotiated reuse. This directly covers the previous permanent-intent-loss case.

## Failure paths and storage boundary

The stage gates require a unique complete `name=1` token and default off. MPSS gates the worker, GLINK requires MPSS, and rmtfs/pd-mapper require GLINK. The preflight still validates the signed split-image layout before the hardware worker runs.

PAS shutdown is owed before the first PAS init call. Cleanup attempts it on success and ordinary error exits. Busy/interrupted SCM handling is bounded, and shutdown retries -22 at most fifty times. Unverified PAS shutdown returns before ownership reclamation, QMP-off, rail release or allocation release.

An uncertain TCS retains its programmed state, inhibits all subsequent rail-release commands and makes cleanup INCOMPLETE. Allocations remain retained. Precisely, after verified PAS shutdown, cleanup can still reclaim shared ownership and send QMP load_state off even when a vote is uncertain. The code retains the rail votes and memory allocations, not every ownership/load_state setting. I found no unsafe memory release or uncertain-TCS reprogramming in that path. This distinction must remain visible when interpreting the tests.

After a verified normal stop, releasing MSS while retaining known XO/CX floors is deliberate successful cleanup. Do not require XO/CX off as a success condition. An unverified stop or INCOMPLETE cleanup is not a successful run and must terminate through guarded recovery/reset.

The reachable storage chain uses only fixed-name `sweet_ufs_part_size` and `sweet_ufs_read_part` for modem_a and the four EFS snapshots. `ModemRmtfs.c` has no storage callback or storage API. Its write direction changes heap snapshots only, and both directions validate sector extents and the low shared window before copying. No UIM, online-mode, IPA, data-call or TFTP sender is present in this production series.

The mappings remain consistent with the supplied board reservations: PAS4 MPSS at 0x8ac00000 of size 0xe600000, firmware span 0xd700000, SMEM 0x81d00000/0x200000, mutex 0x1f43000, SMP2P 428/435, APPS/modem 0/1 and fatal/ready/handover/stop-ack bits 1/2/4/8. IPCC client 2 signals 0/2, QMP 0xc300000, RSC 0x17a20000 with 0xd00/0x2a0/24 layout and cmd-db 0x81c60000 match the runtime. The rmtfs window 0xe1000000/0x280000, metadata 0xe1400000/0x10000 and arguments 0xe1410000/0x4000 are disjoint. Low-bank availability is the supplied owner reservation assumption.

## Existing validation inspected

`logs/start-check-2174.txt` reports PASS for QRTR/QMI, RAM-only rmtfs, strict gates, QMP, SMP2P, cmd-db, extracted SCM/cleanup and extracted MMIO voting. I read the extraction script and test bodies. The actual vote test checks XO=3, MSS-only release, preserved IRQ bits, missing third-command acknowledgment retaining the TCS, and refusal when no TCS is idle. The cleanup test checks failed shutdown, fifty -22 attempts, failed ownership reclaim and uncertain votes retaining allocations. MMIO and SCM are mocks, so these establish code behavior rather than hardware readiness.

`logs/protocol-check-2174.txt` reports PASS for SMEM and the GLINK backpressure/reuse cases. I inspected those tests but did not rerun them, honoring the no-build instruction.

The final build is now complete. `plans/modem/logs/build-2174-final.txt:5774` says `done building xnu`, followed by the final timestamp operation. Its compile diagnostics reference the reviewed runtime and its object list includes all modem modules. The installed kernel timestamp follows the final patch/source update. The current installed kernel SHA256 is `ac2caa20213cf74cf0bd698019160069e8da68607dd3b3576a23d7f5c4c8d24b`. This is existing build evidence, not a build performed by this review.

## Allowed first-boot scope

Use one RAM-boot image built from that exact successful kernel with one each of `sweetmpss=1 sweetmglink=1 sweetmrmtfs=1 sweetmpdm=1`. Preserve `sweetfbarm=1 sweetdeadman=600 sweetselftest=1`, the normal guarded recovery path and the under-twenty-minute whole-run bound. No concurrent dev.py is allowed. The inspected `plans/modem/tools/build-offline.py` selects the dedicated kernel, checks duplicate arguments and the 607-byte ceiling, and the existing args file has 582 bytes excluding its newline. The offline selftest waits 240 seconds before its normal frame/recovery sequence.

Before using an image, confirm it embeds this kernel and these exact arguments. A changed kernel, patch, gate, address or recovery harness invalidates this GO. The report approves scope only and performs no phone operation.

Require ready plus handover, GLINK/QRTR service evidence, the scoped rmtfs/pd-mapper responses and a verified PAS stop with cleanup=ok to call the offline run successful. Service discovery alone does not establish working rmtfs, and ready alone does not establish M2. Log the retained shared XO/CX floors as intentional dependencies. Missing services or a timeout is evidence to review, not permission to add TFTP or send UIM/online commands.

No UIM, online mode, TFTP, IPA, data calls, persistent EFS changes, partition writes, flashes, slot changes, stock boot or Android restore. Firmware, raw logs, dumps and identifiers remain local. ast146 read-only preflight remains the only phone modem evidence at review time. No actual PAS start or SIM detection has yet been measured.
