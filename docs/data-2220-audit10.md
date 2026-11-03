# Patch 2220 tenth audit

Reviewed on 2026-11-03. Conditional bounded-trial source GO carries over to the exact 2220 SHA-256 ea6bd5991f5be9c48265b06806a2181c7bc974eaee77218ce9385803a2ea00e5 for the second integrated ONLINE trial, subject to audit9's retained conditions and separate image admission. Both requested audit9 findings are fixed. I found one non-blocking defect in the new deadline's placement. It does not enforce an exact 60-second cutoff.

What this change does: it requires a successful LOW_POWER reply before ONLINE2, ends the session when a Set Operating Mode reply times out, and adds an ONLINE-stage timeout. It adds host tests for lost replies and accepted ONLINE that stays offline.

## Revision and scope

HEAD is bbf6834568bfeb875629a7d69782c23978b48bcb. I reviewed git diff 79d5dec bbf6834 -- patches/data tools/data and the connected session, readiness, receive, send and cleanup paths. The working patch and tools have no tracked difference from that commit.

The baseline 2220 hash is eac1425092dc73f9463efd1bfb7cf60b8e63bc317da1d4cc98ca64ba0ac3469d, matching audit9. The unchanged 2221 hash is 4bdc74ce4ad45000ba2efe18bf45454169dcb6464483d5722c918892875e1e49. There is no 2221 delta.

Source references below refer to bsd/dev/data files reconstructed from the public patches under tools/data/tests/out/data. The prior plans/data/src directory is absent in this checkout. I used ponytail-review and unslop. This is a narrow source review of one serialized session under the inherited worker contract, not a new audit of the kernel, modem or IPA implementation.

## Audit9 findings

The stale-mode bypass is fixed. CellSession.h:88 adds lowpower_ok, initialized to zero by CellSession.c:617. Its only assignment to one is CellSession.c:554, after a matched LOW_POWER Set reply passes qmi_response with success.

CellSession.c:337 requires lowpower_ok, online_sent equal to 1 and mode equal to 1 for ONLINE2. The LOW_POWER success handler clears mode_known at line 555. advance then requires a successful Get Operating Mode before the cached mode can be used again.

The normal session flow has no older Get Operating Mode request pending across LOW_POWER success. The initial read was consumed before ONLINE1, and the pending Set guard prevents starting another read during either Set request. A duplicate or retired reply cannot supply acceptance or readback because answered matches a live transaction and transaction numbers are not reused.

The stuck-not-low-power branch also uses lowpower_ok at line 341. Merely attempting LOW_POWER no longer activates either branch. Losing LOW_POWER with cached mode 1 now ends the session without ONLINE2, as the maintained regression test confirms.

The lost-Set-reply and accepted-but-never-online termination finding is also fixed in substance. CellSession.c:657-661 retires an unanswered DMS Set Operating Mode request at its five-second reply timeout and calls fail. The guard covers ONLINE1, LOW_POWER and ONLINE2. A reply too short to match eventually takes this timeout path too.

A successful Set followed by repeated valid nonzero mode reads now reaches local failure through the stage deadline. A missing or unusable Get answer still has the existing finite step-attempt limit. Local failure therefore no longer depends solely on owner stop or transaction exhaustion for these cases, though the stage cutoff has the edge case below.

## Non-blocking finding

1. The new 60-second check is skipped while mode is unknown and when readback reports mode 0. CellSession.c:327-333 handles both cases before checking ONLINE_MS. This makes the stated whole-stage deadline inaccurate.

I reproduced this with the real extracted session and normal setup. CS_ONLINE starts at time 1000 ms. ONLINE1 succeeds, repeated reads return mode 3, and a new Get is sent at 56200 ms. Hold that read until time 61100 ms, which is 60100 ms after stage entry and only 4900 ms after the request. Polling at 61100 leaves CS_ONLINE active because mode_known is zero. Answering that same request with mode 3 fails the session, but answering with mode 0 enters CS_REGISTER after the deadline.

This does not reopen indefinite nonzero-mode waiting or increase the write budget. Unanswered reads still exhaust their attempt limit, and a valid nonzero read triggers the deadline when advance processes it. It does allow the state to outlive 60 seconds and accept late success.

The smallest correction, if 60 seconds is intended as a strict stage ceiling, is to check elapsed time at the start of CS_ONLINE when cfg.allow_online is true, before the unknown-mode and mode-0 branches. Keep the passive WAIT_ONLINE_ALLOWED path exempt. Add a regression with a read outstanding across the boundary and a mode-0 reply after it. The present behavior remains acceptable for this supervised trial under the independent runtime bounds.

## Write budget and state interactions

The maximum logical mode-attempt sequence remains 0,1,0. CellSession.c:338 charges each ONLINE attempt before sending. The only fallback LOW_POWER send at lines 544-548 charges lowpower_sent first and requires a valid positive refusal of ONLINE1. ONLINE2 requires online_sent equal to 1, and its attempt increments that field to 2.

Neither counter is reset during polling, response handling, state transitions or cleanup. LOW_POWER success latches acceptance but cannot rearm the counters. Lost replies, malformed replies, duplicate replies, transport refusal and transaction exhaustion cannot create a fourth mode attempt. Transport refusal can consume an attempt without emitting a packet, so observed writes can be a shorter prefix. The shared builder still allows only modes 0 and 1.

state_since is assigned on an actual state change at CellSession.c:54-60. Repeated reads, ONLINE1 acceptance, LOW_POWER acceptance and ONLINE2 do not reset it. Entering STOPPING records a new cleanup start time, so time already spent in ONLINE does not consume the five-second WDS cleanup interval.

WAIT_ONLINE_ALLOWED remains the passive path for cfg.allow_online equal to zero. It can read past 60 seconds without automatic mode writes and can advance when readback becomes mode 0. The configuration is copied at initialization and the production owner does not toggle it to restart ONLINE or refresh the stage clock. Time remains driven by cell_session_poll, so these are session-clock bounds under the existing regular polling contract.

The stopping barrier remains intact. fail enters terminal cleanup, advance dispatches STOPPING, issue permits only WDS Stop Network while ending, and receive drops non-WDS processing after the barrier. Failing inside the pending-timeout scan does not restart ONLINE or reset an existing cleanup deadline. Late Set replies cannot send LOW_POWER after failure.

Readiness and required-service loss still fail the active session before another setup step. sweet_data.c calls sd_step before routing replies, and DataServices revokes readiness before synchronizing services or granting new evidence. Supplemental tests revoked readiness, removed DMS or stopped explicitly at each of the three Set phases. Delivering a refusal afterward and restoring readiness/services emitted no further setup request and did not revive the session.

An already transmitted DMS request can still take effect after local failure or stop. Session cleanup settles WDS ownership, not restoration of modem operating mode. The inherited modem shutdown and conservative retention conditions remain necessary.

## Validation

sh tools/data/check.sh passed freshly. It reconstructed the public patch sources and ran the data, service-cache and IPA adapter host suites with warnings as errors, AddressSanitizer and UndefinedBehaviorSanitizer.

The new maintained session_online cases cover lost ONLINE1, lost LOW_POWER from mode 3, lost LOW_POWER from cached mode 1, and accepted ONLINE with continuously answered nonzero reads through the stage timeout. They pass. They do not cover a pending read across the deadline or late mode-0 success.

The supplemental tools/data/tests/out/audit10-probe.c and sanitized executable also passed. They reran the maintained data suite, checked all three Set timeouts and late replies, verified acceptance before fresh mode-1 readback, tested stopping/readiness/service-loss barriers at all three Set phases, exercised passive waiting past 60 seconds, and reproduced both outcomes of the delayed-read case above. Each supplemental scenario checked the mode-write prefix bound. This is targeted coverage, not exhaustive state-space verification.

Audit9's existing LOW_POWER read cadence observation remains unchanged. Its extra immediate read is bounded and read-only. The old test comment labeling 0x34 as InvalidTransition also remains unchanged. Neither issue is introduced or fixed by this delta.

## Carryover conditions

This report supplies conditional source GO for this exact hash and the bounded automatic 0,1,0 sequence with sweetdataonline=1. It supplies no phone-boot authorization or approval of an unreviewed integrated image. The consumed 2218 one-trial approval cannot admit a changed image or a second boot.

Record this hash and report in the replacement source snapshot, cutoff and second-trial image admission. Reproduce and review the actual integrated image and its arguments before the trial. The 60-second local check does not replace the guarded 600-second data runtime, exact independent sweetdeadman=900, active supervision or harness deadline below 20 minutes, and it grants no runtime extension.

All other audit9 conditions carry over, including one successful attach per boot, eight QRTR local slots, fresh selected-USIM and IPA evidence, per-control discovery, immutable callbacks, QMAP v1 limits, bounded WDS cleanup with failure propagation, IPA quiescence before detach, conservative retained backing and RAM-only EFS. No reset, OFFLINE, provisioning, profile modification or partition-write permission is added.

I edited no implementation file, built no kernel and did not touch the phone. Writes were limited to this report and host artifacts under tools/data/tests/out. No files were removed. Host tests do not establish firmware transition support, kernel concurrency safety, physical DMA cessation, connectivity or correctness of a final integrated image.

Verdict: conditional bounded-trial source GO carries over to ea6bd5991f5be9c48265b06806a2181c7bc974eaee77218ce9385803a2ea00e5. Both requested audit9 findings are fixed. The new deadline is a soft cutoff with the documented late-read exception. No new blocker was found within the retained trial conditions.
