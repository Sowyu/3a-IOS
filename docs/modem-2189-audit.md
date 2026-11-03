# 2189 source integration safety audit

2026-11-03. What this change does: 2189 connects the modem worker to data2220, forwards selected-USIM readiness and adds a guarded 600-second post-authentication lifetime with bounded data cleanup.

**Verdict: the 600-second lifetime is acceptable in source, but fix findings 1 and 2 before treating the wiring as approved.** Integration work and a later exact combined-image audit may proceed with those findings tracked. This report grants no phone GO, ONLINE approval, successful build claim or IPA approval.

## Scope and identities

I read `3a-IOS/HANDOFF.md` first, then `3a-IOS/docs/modem.md`, `3a-IOS/docs/cellular-data.md`, data audits4/5, `plans/modem/sol-2188-reaudit.md`, `plans/modem/README.md` and the public README. I used ponytail-review and unslop. References below are production paths relative to `ios-asteroids/research/xnu-modem/dxb/xnu`, unless otherwise stated.

The public2189 patch matches commit `c269f4f` byte for byte. Its SHA256 is `45db8e1d6939fa23793c2d42c855ff6390f89f922129918e73427907b2c51518`. Data2220 matches audit5's exact SHA256 `adda4660cc553d741f7c36e627b974874c65cdb0adf6037a525a15457784ae48`.

I reconstructed the ordered public2170..2189 series plus2220 in memory and compared all33 generated modem/data files and `sweet_modem.c` against production. All matched. Production `ModemRuntime.inc` hashes to `70ac53782a6be41461e132a80745562f856b92767b72f2a527af9b71c004e9f9`, `ModemGate.h` to `3d7d64f17d8dc1b6535fbf4ff21d304a4e6ac81136600754a2e0ff0da9e97ce1`, and `sweet_data.c` to `e1512a3e5d905a0b36302a70a061daa3ca2a5727f2d24d65624fb3fe95e72d32`.

No builds, compilers, test binaries, phone actions, delegates, deletions or source edits occurred. This report is the only file written. The running frozen21881 phone image excludes2189 and retains its separate approval. I did not interfere with it.

## Must fix

### 1. Data-service removal is observed too late inside a GLINK receive batch

`ModemRuntime.inc:352-357` validates UIM discovery before and after each QRTR packet, but does not synchronize the data module's other service endpoints there. `QcomQrtr.c:144-147` updates discovery on DEL_SERVER without notifying data. `sweet_data.c:494-519` notices those changes only from `sweet_data_poll`, called after the entire `m_ipc_poll` receive batch at runtime589. `QcomGlink.c:411-424` can deliver multiple packets during that batch.

Concrete sequence: a session awaits its DPM Open Port response, UIM and IPA are ready, and the FIFO contains DEL_SERVER for DPM followed by the already queued successful DPM response. QRTR removes DPM, but `sd.svc[DPM]` still contains the old endpoint. `sd_receive:404-410` accepts that response, and `CellSession.c:557-571` advances to WDA and can send Set Data Format before the later poll notices the missing required service and starts teardown. A DEL_SERVER followed by NEW_SERVER at the same endpoint within one batch can also disappear entirely from the poll's final-state comparison.

This violates audit4's receive/service-loss ordering condition. The current regression deletes UIM and substitutes a callback asserting the readiness flag. It does not test real `sweet_data.c` service-cache invalidation for DPM/WDA/WDS/NAS/DMS.

Fix the receive boundary so every discovery removal/replacement is delivered to data before any subsequent data callback or send can advance setup. Preserve an actual removal event even if the same endpoint reappears in that batch. A serialized discovery-change hook after QRTR control processing is one small solution. Add a connected regression with real data/session handling for DEL_DPM followed by its late success, and DEL/NEW at the same endpoint. No setup or ONLINE message may escape after loss.

### 2. Freshness expiry depends on ready/handover bits remaining asserted

`ModemRuntime.inc:332-335` checks the15-second freshness deadline in `m_uim_poll`, but runtime586 calls that function only while `(m.state & 6) == 6`. The additional check at354 runs only when an incoming QRTR packet invokes `m_greceive`.

Concrete sequence: selected USIM becomes ready, then a nonfatal SMP2P state change clears ready or handover and incoming QRTR traffic ceases. `m_smp_poll` updates the bits without setting fatal for that case. Every subsequent iteration skips `m_uim_poll`, while `sweet_data_poll` continues seeing the old true USIM flag even after15 seconds. It can continue session work using expired evidence until a receive callback or another failure occurs.

Move the freshness/fatal/failure revocation check into an unconditional worker step before transport/data processing, including iterations without receive traffic. Keep the pre-dispatch check for expiry arising during a receive batch. Add a regression that clears a state bit after READY, advances past15 seconds with no incoming packets, and verifies revocation before the next real session poll. This does not require more phone readiness runs.

## Lifetime and admission

`modem_run:547-555` checks strict default-off gates before mappings or hardware setup. `sweetmsession=1` must match at least one exact `sweetdata=1` or `sweetqmiraw=1`; MPSS, GLINK, RAM-rmtfs, pd-mapper, UIM, readiness polling, sysmon, handover, framebuffer arming and selftest must all be enabled. `modem_arg_is` requires exactly one literal `sweetdeadman=900` and rejects duplicates, bare tokens and numeric aliases. Data ONLINE remains a separate default-off gate.

The post-authentication deadline at584 is600000ms for a data session and120000ms otherwise. Both use monotonic absolute time. UIM polling is no faster than every5000ms, so at most about120 card requests fit inside600seconds, below cap180. Accepted card transactions are odd numbers1..359, slot requests are even2/4/6, and counters advance only on accepted sends. There is no wrap or overlap with the data client ports' transaction spaces. Latest-transaction matching and one-time pending retirement remain intact.

The independent reboot code accepts900seconds within its30..86400 range and starts its own worker. The900-second guard is15minutes, under the owner's20-minute limit, and leaves nominal margin beyond the600-second session,30-second pre-hardware delay and bounded cleanup. Boot, firmware I/O, scheduling and SMC execution are not proven wall-time bounds by this source inspection. The image audit must verify actual recovery arming and the deadman-start log, plus a normal harness deadline below20minutes. A deadman firing is recovery evidence, never session success.

Audit5 removes transaction-exhaustion as a reason to retain the data120-second cap. Its reserve and central send guard remain present. A600-second bounded session is acceptable once the revocation gaps above and the future integration conditions below are closed.

## Attach, concurrency and cleanup

`m_glink_prepare:372-376` initializes QRTR and registers runtime ports. Attach at581 precedes `m_load`, PAS authentication and QRTR start. The worker marks `data_attached` before calling attach, so an error still owes stop/query/detach. Data's partial-registration failure disables its gates and never starts a session. Registered callbacks remain in the static QRTR object, so failed attach must remain terminal for that lifecycle. No retry is present.

QRTR receive, USIM transitions, session callbacks, polling and runtime teardown run on the same modem worker. Data's sysctl only queues bounded requests under its static mutex. Raw sends occur on worker poll. CellLink state is mutex protected, while packet output calls the immutable IPA TX callback outside that lock. The ifnet and callback/context must remain valid for the kernel lifetime. A future IPA driver must make its own TX gate and queue synchronization authoritative because an ifnet thread can already have passed the atomic ready check when the worker begins teardown.

`m_data_stop:463-479` latches `data_stopping`, revokes USIM, calls stop, pumps SMP/GLINK/data poll for at most5seconds and accepts only idle AND cleanup0. All nonzero cleanup results, including pending EINPROGRESS, latch `m.failed`; detach happens once. The real data APIs close raw admission, wake a queued/sent caller and preserve the terminal cleanup result. Delayed RAW_COPYING notices closed state before enqueue. The terminal session guard prevents readiness or late setup replies from restarting the session. Late valid Start replies remain tracked for Stop Network.

`m_cleanup:482-506` still attempts every owed PAS4 shutdown regardless of data cleanup failure. Data failure makes the existing SSCTL eligibility predicate return false, so an SSCTL call is not a guarantee of a transmitted request on that branch. Stop-bit fallback and PAS still run. Sticky failure reaches final EIO even if PAS subsequently succeeds. No data cleanup failure becomes hidden success.

On failed PAS4 the early return at493 precedes shared-memory reclaim, QMP-off, remaining vote release and firmware/metadata/shared/args/shadow unmap/free. Existing mappings and power remain backed until guarded reset. This follows the owner's accepted retention policy. Raw-3145800 is still an unverified secure stop, and M2 remains incomplete. No further shutdown-only phone trial is required by this review.

## Required future combined IPA contract

Production currently contains no IPA prepare, binding, refresh or AP-channel-quiesce implementation. The comment at runtime474 is an integration obligation, not an implemented drain. Thus the current source cannot establish an ONLINE-ready barrier or safe IPA teardown.

1. Prepare the audited IPA implementation and its two QRTR ports, then install immutable TX/RX binding before data attach. Keep all eight local ports bounded and service-zero clients unannounced.
2. Supply QMAPv1 without checksum offload,8192-byte completed RX buffers and at most16 datagrams. TX must copy before return, and RX must run on the serialized worker outside the IPA TX queue lock.
3. Refresh/revoke IPA readiness before QRTR callbacks and session work whenever handshake or endpoint health changes. Require modem completion0x35, running endpoints and the installed TX callback before the data barrier opens.
4. After data stop and bounded reply pumping, make IPA unavailable, reject new AP submissions and drain/quiesce in-flight AP TX/RX before data detach and SSCTL/PAS4. A failed drain must retain reachable backing and remain a failure.
5. Release PAS15/GSI firmware, mappings, rings and backing only after verified PAS4 stop and the separately reviewed IPA stop proof. Known PAS4 failure must retain them through guarded reset.

The fifth point is essential under the explicitly accepted PAS4-retention case. Retaining modem firmware alone cannot make a future freed IPA DMA buffer safe.

## Storage, privacy and existing evidence

The connected storage APIs remain only `sweet_ufs_part_size` and `sweet_ufs_read_part`. Firmware is read from modem_a. EFS snapshots are read once into RAM, and `ModemRmtfs.c` validates opened IDs and all vector extents before copying between RAM shadows and the shared window. There is no rmtfs UFS write callback. No PIN/APDU, provisioning, slot switching, calls or SMS path was added.

DT constants remain MPSS PAS4 at0x8ac00000+0xe600000, SMEM0x81d00000+0x200000, rmtfs0xe1000000+0x280000, metadata0xe1400000 and args0xe1410000. XNU RAM is above4GB. No private firmware, raw logs or full identifiers were copied into3a-IOS or this report. UIM logs retain suffix4 only.

Data audit5's N5 remains relevant. Raw response/indication exports and free-form modem text are not comprehensively filtered. Keep raw disabled in the first integrated session unless the audit4 read-schema/output restrictions are enforced. No opaque response dumps or unrestricted identifier-bearing text may be logged or published. This report does not expand raw approval.

Saved logs contain14 start-suite PASS records,5 UIM-suite PASS records and6 hook-suite PASS records. I found no sanitizer error marker in those logs. The hook script compiles extracted runtime functions with sanitizer flags, but substitutes the data APIs in its teardown test and uses a replacement data callback in its receive-order test. The lifecycle prefix stubs stop separately. Those records support gate/order/bounded-loop checks, not real kernel concurrency, combined-service removal ordering, hardware delivery or DMA safety.

The integration syntax-check success with inherited header warnings is supplied evidence only. I did not rerun it. The running `build-2189-data.txt` did not contain `done building xnu` when inspected. There is no completed2189 build or2189 image approval in this report.

Verdict: accept the bounded600-second lifetime design, fix1 and2 before wiring GO, and carry the explicit IPA obligations into the exact final combined-image audit. Phone GO still requires the successful final combined build, frozen image/arguments/harness review and IPA safety approval. The earlier frozen21881 run remains separate.
