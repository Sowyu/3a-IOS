# Patch 2220 sixth audit

Reviewed on 2026-11-03. The bounded-trial GO from audits 4 and 5 carries over to this data revision with the per-control-packet discovery hook installed as documented. I found no new blocking defect in the delta. Finding 1 in `plans/modem/sol-2189-audit.md` is fixed on the data side, but the modem worker must call the new hook to close that finding in the integrated runtime.

This is a source decision about 2220. It does not approve an integrated image, a phone trial with the currently published worker, or the separately pending 2221/IPA integration.

## Revision and scope

I reviewed `git diff 1f91d89 aeceec8 -- patches/data tools/data docs/cellular-data.md`, audits 4 and 5, the original 2189 finding, the connected QRTR implementation and session paths, and the 2221 audit. The working copies of the requested delta paths match commit `aeceec8b03944897078adb18d0e993400282b717`.

The 2220 SHA-256 is `8a0b901c66eafe2e551a71e3ad93a2990d0b8d040c569ee695127c4f2ad6e311`. The baseline in `1f91d89` hashes to `adda4660cc553d741f7c36e627b974874c65cdb0adf6037a525a15457784ae48`. All fourteen added 2220 source and header files match the workspace's `plans/data/src` byte for byte.

Only `sweet_data.c` and `sweet_data.h` change among the old source files. `DataServices.c` and `DataServices.h` are new, and `bsd/conf/files` adds the new translation unit. CellSession, QMI parsing, QMAP, CellLink and raw schema code are unchanged from audit5.

The regenerated 2221 SHA-256 is `4bdc74ce4ad45000ba2efe18bf45454169dcb6464483d5722c918892875e1e49`. Its change in aeceec8 is only the conf/files hunk context and position. This review does not overturn `docs/data-2221-audit.md`.

Data source references below use line numbers under the workspace's `plans/data/src`. QRTR references use the current public-patch source extracted to `tools/data/tests/out/QcomQrtr.c`.

## Service-loss ordering

`DataServices.c:14-23` clears the cached presence bit before calling the loss callback and before notifying CellSession. A replacement endpoint produces loss first and presence second. `CellSession.c:587-596` sends an active session into terminal cleanup on loss of DMS, NAS, WDS, WDA or DPM. Reappearance cannot restart that session.

`sd_receive` at `sweet_data.c:393-398` now checks attachment and QRTR identity, synchronizes services, and only then looks up the sender. DEL_SERVER for DPM followed by its queued success therefore removes the stale endpoint and ends setup before routing the reply, even without an intervening discovery hook. A reply from another still-present service also reaches a session that has already observed the loss.

The per-packet hook preserves DEL followed by NEW at the same endpoint. The DEL hook delivers the loss while the QRTR table still lacks that service. The NEW hook restores routing but leaves the session terminal, so its late successful DPM reply cannot advance to WDA.

The audit5 send guard at `CellSession.c:114` still permits only WDS Stop Network during teardown. Non-WDS replies also meet the terminal receive guard at line 556. No session setup or automatic ONLINE request can escape after an active session observes a required-service loss.

Before setup, WAIT_READY can still wait for services and later proceed when all prerequisites hold. UIM remains an external readiness responsibility because CellSession does not treat it as one of its five required service bits. These behaviors are unchanged and do not weaken the fix for an active session.

## Required modem hook contract

The declaration at `sweet_data.h:9-21` and `docs/cellular-data.md:30` are clear and sufficient for this data-cache obligation. The precise worker sequence is:

1. Process the packet with `qcom_qrtr_receive` so its control update reaches the remote table.
2. After each NEW_SERVER, DEL_SERVER or BYE, call `sweet_data_discovery` with that same QRTR object before dispatching the next packet or doing further session work.
3. Keep the existing UIM, IPA, fatal and transport readiness revocations serialized before data-capable dispatch and readiness-triggered work.

Calling the hook before QRTR processing sees the old table. Calling it once after the GLINK batch can lose a DEL/NEW event. Calling it after every successfully processed QRTR packet is also sufficient, since unchanged-table synchronization does not manufacture a loss.

`QcomQrtr.c:144-151` updates NEW/DEL discovery itself and clears the remote table on BYE. The hook observes that result. It must not replace QRTR control handling or synthesize a service reappearance. After BYE, synchronization delivers the missing services to data before later announcements or replies can run.

The hook at `sweet_data.c:501-509` synchronizes services before evaluating readiness and ignores calls before attachment, after detach, or for a different QRTR object. Poll has the same service-before-readiness order at lines 554-563. The owner must keep IPA and USIM evidence current before calling this hook because presence and readiness callbacks can themselves advance setup.

This contract does not fix the separate UIM freshness finding 2 or the IPA readiness and quiescence findings. The published 2189 worker still has no call to `sweet_data_discovery`. Its integration remains a required caller change, not an implemented property of 2220.

## Refactor behavior

`sd_send` at `sweet_data.c:116-123` preserves service selection, client ports, attachment-related routing state and refusal rules. The cache uses the same service IDs, order and `qcom_qrtr_find_remote` selection as before. Lookup now returns the first matching endpoint instead of the old loop's last match, but QRTR's remote table is keyed by node and port and cannot supply two cached services at one endpoint under the reviewed serialized flow.

`sd_service_lost` at lines 484-491 finishes a matching RAW_SENT request with ENOENT under the same mutex as before. The callback runs before CellSession hears of loss, and the presence bit is already clear. A late raw reply cannot change RAW_DONE into success. RAW_QUEUED still goes through the worker send path and fails with ENOENT if the endpoint remains absent. This review grants no broader raw-output permission.

Detach at lines 625-639 still closes raw admission, wakes queued or sent callers, clears link configuration, drops attachment and QRTR ownership, clears service presence and resets readiness delivery. `data_services_clear` only clears presence, exactly as the old loop did. The successful-attach once latch and partial-registration failure behavior are unchanged.

Late WDS-start ownership, bounded Stop Network cleanup, unresolved cleanup reporting and transaction exhaustion protection remain in the unchanged session code. Earlier privacy restrictions and settings-TLV limitations also remain.

## Validation and limits

`sh tools/data/check.sh` passed from `3a-IOS` with AddressSanitizer, UndefinedBehaviorSanitizer and warnings treated as errors. The connected service-cache suite, existing data suite and IPA adapter stub suite all passed against patch-extracted sources.

The supplied service test uses real QcomQrtr, DataServices and CellSession with complete QRTR packets. It covers DEL_DPM plus late success with and without the hook, same-endpoint DEL/NEW with the hook, successful progression without loss, and endpoint movement. Its receive callback preserves the relevant sync, lookup and session-receive order.

The author's callback-equivalence claim has a limited scope. The test does not compile `sweet_data.c`, execute its raw mutex/wakeup path, or model `sd_readiness`. Its discovery helper calls the pure sync function rather than the exported kernel hook. These omissions do not invalidate the demonstrated service-loss ordering, but the suite is not proof of actual runtime hook installation or kernel concurrency.

I also wrote and ran `tools/data/tests/out/audit6-probe.c` with the same sanitizers. It checks BYE followed by reannouncement, moved-endpoint loss callback ordering, endpoint lookup and clearing, and a negative control showing that same-endpoint DEL/NEW still advances if the required hook is omitted. A synthetic ONLINE-stage test confirms that removal and reannouncement of each of the five required services prevents an ONLINE send, with a no-loss positive control that sends ONLINE.

The supplemental callback checks the pure callback boundary, not the kernel raw waiter implementation. ONLINE-stage fields are deliberately seeded to isolate the send barrier. Host checks do not establish modem responses, DMA safety, actual worker timing or a successful combined kernel build.

## Decision

GO carries over for the raw-only bounded probe and both bounded session configurations approved by audits 4 and 5, subject to their remaining conditions and the new per-control-packet hook ordering. Audit5's transaction-exhaustion fix remains valid. A longer runtime still needs the modem owner's separate lifetime approval and independent deadman.

Keep one successful attach per boot, eight local QRTR slots, serialized ownership, current USIM and IPA evidence, raw-output restrictions, bounded cleanup with failure propagation, and IPA quiescence before detach and modem shutdown. The 2221 integrated-trial NO-GO remains separate and in force pending its integration review. No integrated phone GO is granted here.

I edited no implementation files, built no kernel and did not touch the phone. Writes were limited to this report and host-test artifacts under `tools/data/tests/out`. No files were removed.
