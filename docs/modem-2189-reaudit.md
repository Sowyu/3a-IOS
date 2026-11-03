# 2189 source wiring re-audit

Reviewed 2026-11-03. What this change does: 2189 connects the modem worker to data2220, publishes selected-USIM readiness, and permits a guarded 600-second session with bounded data cleanup.

**Verdict: GO for the reviewed modem/data source wiring and guarded 600-second lifetime, with the conditions below.** Both must-fixes in `plans/modem/sol-2189-audit.md` are closed. I found no new blocking defect in this revision. This grants no ONLINE, IPA, integrated-image or phone GO. The v2 full build remains unconfirmed because its log has no `done building xnu` marker at review time.

## Reviewed identity and reproduction

I read `3a-IOS/HANDOFF.md` first and used the ponytail-review and unslop skills. I reviewed the prior audit, data2220 audit6, the separate 2221 audit, the ordered public modem patches, the connected production paths and the host fixtures.

The reviewed 2189 patch is byte-identical to commit `914f76d`, SHA256 `9b4b570d07d9a5b02288f006e500a733bfdea0876acaa938ef5b7395497ed58f`. Data2220 matches audit6, SHA256 `8a0b901c66eafe2e551a71e3ad93a2990d0b8d040c569ee695127c4f2ad6e311`.

I made a fresh isolated copy of the public patches and test tools under `tmp/sol-2189-new-review-mx8pqpl5`. Ordered extraction of 2170 through 2189, including the current 2187/2188/2189 revisions, and data2220 reproduced 35 source/header files, including the modem entry point `sweet_modem.c`. Every reproduced file matched `ios-asteroids/research/xnu-modem/dxb/xnu` byte for byte. The extractor checks existing hunk context while replaying each file. This is source reproduction, not a clean full-kernel checkout/build proof.

Resulting source SHA256 values are:

- `ModemRuntime.inc`: `be676ed77883a061b23dfdc48c4b419f3edc0abd4e3f63dd5f9fe85deed51d54`.
- `ModemGate.h`: `3d7d64f17d8dc1b6535fbf4ff21d304a4e6ac81136600754a2e0ff0da9e97ce1`.
- `sweet_data.c`: `ad78423232c3406e4d13b7d69d00f5ea666892242f0f99af7bd0c2ef316db863`.

All seven data2220 translation-unit entries occur exactly once in production `bsd/conf/files`. Production has no IPA implementation or 2221 adapter. References below are resulting production source paths under that XNU tree.

## Finding 1 closed: per-packet discovery

`bsd/dev/modem/ModemRuntime.inc:358-365` refreshes UIM before QRTR dispatch, checks the remembered UIM endpoint, processes the packet with `qcom_qrtr_receive`, checks the endpoint again, and then calls `sweet_data_discovery` whenever attached. This occurs for every successfully processed QRTR packet, before the next GLINK receive callback.

`QcomQrtr.c:144-151` updates NEW/DEL discovery and clears the table on BYE before returning. The data hook at `bsd/dev/data/sweet_data.c:501-509` synchronizes the service cache before evaluating readiness. `DataServices.c:14-23` clears presence and reports loss before reappearance, while `CellSession.c:587-596` makes required-service loss terminal for an active session.

DEL_DPM followed by its late success therefore cannot advance setup. DEL then NEW at the same endpoint also delivers a loss before reappearance. BYE cannot be hidden by subsequent announcements. The independent synchronization in `sd_receive` remains a useful additional guard.

The exported discovery hook can itself advance setup through service and readiness callbacks. Its position after the UIM post-control check matters. It is also a mandatory IPA freshness boundary in the later integration, as described below.

## Finding 2 closed: unconditional freshness

`ModemRuntime.inc:293-299` centralizes expiry, fatal and failed-state revocation. The worker calls it at line 593 before UIM/GLINK work and again at line 596 before data poll. Neither call depends on ready/handover bits, UIM polling cadence or incoming traffic.

The pre-dispatch call at line 360 remains, so expiry during a GLINK batch is handled before another QRTR callback. A GLINK-down notification also revokes immediately. The stopping latch in `m_uim_readiness` prevents a late valid card reply from reopening readiness during cleanup.

The fresh host run exercised the extracted worker fragment with real CellSession: readiness/handover states 0 through 5, time 14999 then 15000 ms, no incoming packets, and separate fatal/failed cases. Readiness was revoked before real session polling and no further setup send escaped. This fixture substitutes the transport and UIM poll functions. The enclosing loop and placement before transport were also checked directly in production source.

## Lifetime and teardown remain acceptable

Admission at `ModemRuntime.inc:554-562` retains exact default-off session/data agreement, the required modem/UIM/GLINK/RAM-rmtfs/pd-mapper/sysmon/handover dependencies, framebuffer/selftest gates and exactly one literal `sweetdeadman=900`. The 600000 ms post-authentication deadline at line 592 leaves the ordinary offline deadline at 120000 ms. ONLINE remains a separate gate.

Five-second card polling fits within the 180-request ceiling without transaction reuse during this bounded session. Exact transaction/endpoint checks, stale and duplicate suppression, suffix-only identifier output and RAM-only rmtfs writes remain intact. Raw-output restrictions from the earlier data audits still apply. This review grants no broader raw export permission.

Attach still happens once before load/authentication. Marking runtime attachment before calling the data attach function ensures a partial failure goes through cleanup. The data module disables admission on partial registration failure and the runtime has no retry path.

At `ModemRuntime.inc:471-487`, stop latches terminal state, revokes USIM, closes data admission, pumps replies for up to five seconds, queries cleanup and detaches once. Every nonzero cleanup result or failure to reach idle latches failure. `m_cleanup` still attempts owed PAS4 shutdown and returns failure for a failed data teardown.

An unverified PAS4 stop returns before reclaiming shared backing, firmware, mappings or required power. This preserves the owner's accepted bounded-run retention policy. M2 remains incomplete. No additional shutdown-only phone run is required by this review. The reported ast168 result is existing M3 evidence and does not prove this new data wiring on hardware.

## Host validation and limits

Fresh isolated runs passed with ASAN, UBSAN, warnings as errors and `-save-temps=obj`, with compiler temporary files under the workspace:

1. `check-data-hooks.sh` passed all eight PASS groups, including actual receive/discovery fragments, unconditional freshness with real CellSession, admission and bounded teardown.
2. `check-uim.sh` passed all five PASS groups.
3. The start suite passed its fourteen PASS groups after adapting the isolated copy's location-dependent touch-source path.
4. `tools/data/check.sh` passed the service-cache, data and IPA adapter stub suites.
5. A supplemental actual-hook probe passed BYE/reannouncement and loss/reappearance of each of the five required services at a seeded ONLINE stage, with a positive control that does issue ONLINE without loss.

The initial isolated start invocation stopped after twelve PASS groups because `check-bandwidth.py` resolves the production touch source relative to its installed directory. I changed only that source-location expression in the isolated test copy, then reran the suite. The repository test scripts and implementation were untouched. The updated shutdown extraction markers successfully compiled both joined shutdown fixtures.

Evidence is preserved in `tmp/sol-2189-new-review-mx8pqpl5`, including `check-data-hooks.sh.log`, `check-uim.sh.log`, `check-start-complete.log`, `data-check.log` and `audit-discovery.log`. The supplied v2 logs also contain the expected PASS groups.

The discovery fixture executes actual `m_greceive`, `sweet_data_discovery`, `sd_sync_services` and `sd_readiness` with real QRTR/DataServices/CellSession. Its data receive callback is equivalent test code, and its raw-loss callback is a stub. The freshness fixture forwards readiness to real CellSession through a replacement data API. Neither suite executes the full kernel raw waiter mutex/wakeup path, real concurrency, DMA or hardware timing. Seeded ONLINE cases test the send barrier, not a complete successful modem setup.

`plans/modem/logs/build-2189-v2-data.txt` was still producing build output and contained no completion marker when inspected. The earlier v1 build success does not establish v2 build completion. I did not start, stop or alter the running build.

## Conditions for the future IPA combination

The present source fails closed for a session without an installed IPA TX hook/readiness. The placeholders at runtime lines 364, 482 and 588 remain integration obligations. The separate 2221 integrated-trial NO-GO remains in force.

1. Prepare the audited IPA implementation, then bind 2221 once before data attach, QRTR start and MPSS authentication. Abort on any bind failure, including partial binding. Raw-only mode must follow its separately reviewed path.
2. Make IPA readiness authoritative and terminal on GSI, handshake, endpoint/service, transport or fatal failure. Synchronize/revoke it before every data-capable receive, discovery and normal or cleanup poll. Check IPA service identity after each QRTR control update before discovery can advance setup or another receive can run.
3. Keep USIM evidence current at the same boundaries, including callbacks that can assert readiness. Preserve one serialized worker owner and eight local QRTR slots, with no client announcements.
4. After bounded WDS cleanup, reject new AP submissions and drain/quiesce AP TX/RX before data detach and SSCTL/PAS4. A false readiness latch alone is not a channel drain. Propagate any quiesce failure.
5. Retain all reachable IPA firmware, rings, packet buffers, mappings and required power on uncertain PAS4 stop, even if AP quiescence succeeded. Release only after verified modem shutdown and the separately approved IPA stop proof.

The frozen integrated source and successful final build still need review with exact image arguments, independent deadman arming and a harness deadline below 20 minutes. The IPA owner's Gunyah/MMIO correction and the ast169 reset need their separate approval. This source verdict does not authorize an ONLINE request or a phone action.

No implementation edits, commits, pushes, phone access, partition writes, firmware publication or file removals occurred. Writes were limited to this report and isolated host-test artifacts under the workspace.
