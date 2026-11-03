# Patch 2220 seventh audit

Reviewed on 2026-11-03. The bounded-trial GO from audits 4 through 6 carries over to this revision under their remaining conditions, including audit6's per-control-packet discovery hook. The reported sync-before-revoke edge is closed in all four session entry paths. I found no new blocking defect in this delta.

This is a decision about the data patch source. It does not approve an integrated image, phone trial, modem lifetime extension or the separate 2221/IPA integration.

## Revision and scope

The reviewed 2220 SHA-256 is `410ee41d9269ab7e6f97b1beb4c4550af28464b5503c5719790094bedc2ebdf8`. The baseline patch in `8c92e4f` hashes to `8a0b901c66eafe2e551a71e3ad93a2990d0b8d040c569ee695127c4f2ad6e311`, as requested. HEAD was `833961a` during this review, and the requested delta paths had no working-tree changes before this report.

I reviewed `git diff 8c92e4f HEAD -- patches/data tools/data docs/cellular-data.md`, audits 4 through 6, the complete service helper and its callers, and the connected session readiness, service, receive, send and cleanup paths. All fourteen added 2220 source and header files match `plans/data/src` byte for byte. Source line references below are relative to that directory.

2221 still hashes to `4bdc74ce4ad45000ba2efe18bf45454169dcb6464483d5722c918892875e1e49` and is unchanged in the requested delta. The ancillary en2 preference tool and proof instructions do not affect this ordering fix. I did not run that tool or approve its phone proof procedure.

## Ordering fix

`DataServices.c:52-63` implements the author's claimed order. If readiness was delivered and current evidence is false, it first calls `cell_session_ready(session, 0)` and clears `ready_given`. It then synchronizes service presence and only afterward delivers a new positive readiness record.

In the reported case, `CellSession.c:599-604` clears `is_ready` while leaving a waiting session in WAIT_READY. When the last service subsequently arrives, `cell_session_service` can call `advance`, but the readiness check at `CellSession.c:293` stops setup. Read-only information requests can still be sent once all services exist, as earlier audits allowed. NAS Register Indications and DPM Open Port remain behind readiness.

A later positive evidence sample grants readiness after the cache is current and starts setup normally. The grant flag is set after the readiness call, just as the old owner flag was. Moving the flag does not create a second grant route.

The four paths all use `sd_step`, which passes the current USIM and IPA evidence, immutable limits, service-loss callback and optional session to `data_services_step` at `sweet_data.c:510-513`:

1. `sd_receive` calls it at line 396 before endpoint lookup, raw response handling, indication capture or session receive.
2. `sweet_data_discovery` calls it at line 498 after checking attachment and QRTR identity.
3. `sweet_data_poll` calls it at line 546 before raw poll and session poll.
4. `sweet_data_usim` records the new flag at line 559 and calls it at line 561 when attached with a session.

There are no remaining production calls that bypass the helper to synchronize services or deliver session readiness. Stop and detach cannot advance setup and retain their existing terminal and lifetime contracts. `sweet_data_ipa_ready` still only stores evidence, so worker serialization before subsequent session work remains necessary.

## Behavior retained

Initial grant remains conditional on the same evidence and limits. Zero-initialized `DataServices.ready_given` replaces the zero-initialized owner field. Granting while a required service is absent still records readiness in WAIT_READY, and a later service can start setup only while that record remains valid. A USIM-positive notification now synchronizes the cache before granting, which also prevents that notification from granting against an outdated service view.

During CALLS, readiness loss still clears `is_ready` and invokes terminal cleanup through `cell_session_ready`. The unchanged send guard permits only WDS Stop Network during teardown. Restoring evidence cannot restart the session because `cell_session_ready` accepts positive readiness only in WAIT_READY. `ready_given` can become set after a rejected terminal-state grant, just as the old field could, without enabling setup.

USIM revocation still takes effect synchronously on its worker callback. It now also reconciles service changes after revocation. The raw-only case passes a null session, skips both readiness branches and continues cache synchronization and raw service-loss callbacks. The unchanged USIM callback skips the step in raw-only mode, which has no session readiness to revoke.

Detach calls `data_services_clear` at `sweet_data.c:624`. That now clears both service presence and the relocated grant flag, preserving the previous combined reset. It does not clear session ownership or the cleanup result. The successful-attach once latch at lines 437-439 and 474 is unchanged, as is the restriction against retrying partial registration within that QRTR lifecycle.

Revocation can initiate Stop Network before cache reconciliation. If readiness and a WDS endpoint are lost together, such a cleanup attempt can use the previously cached endpoint. The ordering guarantee concerns setup sends. It does not guarantee that cleanup never attempts a stale endpoint. The session retains ownership until confirmed cleanup or an unresolved deadline, and cleanup failure must still propagate. This does not weaken the bounded-trial decision, but the poll comment's blanket claim about never sending through a stale endpoint should be read with that limitation.

## Validation

`sh tools/data/check.sh` passed with AddressSanitizer, UndefinedBehaviorSanitizer and warnings treated as errors. The service-cache suite, data suite and IPA adapter stub suite passed against patch-extracted sources.

The new service regression uses real QRTR, cache and session code. Both its discovery path and its receive path first grant readiness with DPM missing, then turn evidence false and announce DPM. They assert WAIT_READY with both readiness flags clear, no DPM Open Port and no NAS registration, followed by successful setup when evidence returns.

I independently verified the author's negative-control claim. Two copies under `tools/data/tests/out` use a helper changed only to sync before revocation. The discovery-only and receive-only regression variants both fail the expected readiness/setup assertion. No implementation file was changed for those controls.

I also compiled and ran `tools/data/tests/out/audit7-probe.c` with the same sanitizers. It reruns the supplied service suite and checks unchanged-evidence grant suppression, CALLS revocation with a tracked handle, Stop Network cleanup, refusal to revive setup, raw-only null-session synchronization and clearing both presence and grant state. These checks passed. The CALLS state and handle are deliberately seeded to isolate the revocation boundary.

The host suite does not compile `sweet_data.c` or execute its actual kernel hooks. Poll and USIM wiring, attachment guards, detach and the once latch were checked statically. These tests do not establish real worker hook installation, kernel concurrency, DMA safety or modem behavior.

## Conditions and decision

Audit6's hook condition remains essential. Process each NEW_SERVER, DEL_SERVER or BYE with QRTR first, then call `sweet_data_discovery` with that object before dispatching the next packet or doing more session work. A same-endpoint DEL/NEW can still disappear from a final cache snapshot if the per-control hook is omitted.

The owner must update and revoke UIM and IPA evidence on the worker before data-capable work. The helper orders actions around the evidence it samples. It cannot detect an unreported readiness loss or close an asynchronous loss after that sample. Keep the stopping latch and prevent readiness revival during teardown.

GO carries over for the bounded raw-only probe and both bounded session configurations from audits 4 through 6. Keep their raw-output restrictions, eight local QRTR slots, one successful attach per boot, immutable IPA callback lifetime, serialized ownership, bounded cleanup with failure propagation, IPA quiescence and shutdown ordering. Audit5's transaction-exhaustion fix is unchanged, and any longer modem runtime still needs separate lifetime approval and the independent deadman.

I edited no implementation files, built no kernel and did not touch the phone. Writes were limited to this report and host-test artifacts under `tools/data/tests/out`. No files were removed.
