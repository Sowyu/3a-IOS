# 2188 offline flow-control safety review

2026-11-03. NO-GO for the frozen 2188 boot. One blocking integration defect remains in the backpressure path. I do not extend the one-run PAS retention exception to this artifact. M2 remains incomplete.

What this change does: 2188 corrects QRTR RESUME_TX addresses and the local-client payload identity. It holds up to sixteen pending flow acknowledgments and retries them after the worker drains TX, with a sticky failure on capacity overflow.

## Review boundary

I read `3a-IOS/HANDOFF.md` first, then `plans/modem/README.md`, `3a-IOS/docs/modem.md`, `plans/modem/sol-2187-reaudit.md` and both 2185 UIM audit copies. The requested `docs/modem.md` is under `3a-IOS`. I applied ponytail-review and unslop and reviewed the production QRTR/worker/client/cleanup path, storage boundary, gates, runner, tests and packed image for one worker and one offline boot.

No phone operations, builds, compilation, test-binary execution, source edits, deletion or uploads occurred. Read-only Python used `-B` and reconstructed sources in memory. This report is the only file written. Prior ast165 hardware facts below are supplied run evidence, not a new live measurement or an independent replay of its raw log.

## Must fix

1. Temporary acknowledgment backpressure permanently fails the real worker.

`ios-asteroids/research/xnu-modem/dxb/xnu/bsd/dev/modem/ModemRuntime.inc:250-252` implements the actual QRTR send callback. It sets `m.failed=true` whenever the 32-entry TX queue is full. `QcomQrtr.c:52-70` preserves a refused acknowledgment and returns success for later retry, but cannot undo that sticky worker failure.

Concrete case: the TX queue has 32 packets while GLINK has no transmit space or usable remote intent. A confirm-requesting DATA packet arrives during `qcom_glink_poll`, or an existing pending acknowledgment is retried at the end of `m_ipc_poll`. `emit` calls `m_qsend_packet`, which sets `m.failed` and returns -1. QRTR correctly retains the acknowledgment. Even if TX drains and that acknowledgment queues successfully afterwards, `m.failed` remains true. The main loop ends with EIO, and `m_shutdown_request:376` skips SSCTL because it refuses an already failed worker. Cleanup still attempts PAS and preserves backing on failure, but the proposed recoverable backpressure behavior and accepted graceful-shutdown measurement are lost.

The saved worker regression does not exercise this connected path. `3a-IOS/tools/modem/check-qrtr-worker.py:22` substitutes a send callback that returns -1 on a full queue without setting `m.failed`. Its GLINK poll is also a no-op at line19. The generated unit contains the production `m_ipc_poll` body, but omits production `m_qsend_packet`. Its assertion that a full queue leaves the worker healthy therefore passes under different semantics. The standalone flow test has the same nonfatal mock refusal.

Fix acknowledgment queue refusal so temporary fullness retains the acknowledgment without setting worker failure. Preserve fatal handling for oversize packets and non-retryable ordinary reply loss. Do not globally suppress queue-full failure for rmtfs/pd-mapper replies, which have no retained retry ownership, and do not clear `m.failed` after a retry. Add a regression using the actual queue callback plus the actual poller and QRTR implementation. Cover a confirm-requesting receive while full, repeated GLINK Again, eventual drain, exactly-once receive delivery, exactly one queued resume, continued worker health and SSCTL eligibility. Preserve fatal tests for acknowledgment overflow and transport breakage. Rebuild/refreeze the changed artifacts and obtain a fresh Sol review before boot.

No second blocking finding was identified within this offline scope.

## Checks that passed

The endpoint fix agrees with upstream Linux [qrtr_send_resume_tx](https://raw.githubusercontent.com/torvalds/linux/master/net/qrtr/af_qrtr.c), lines1007-1029 as read during this review. Linux sends from the original DATA destination to its source and places that local receiver node/port in the client payload. 2188 emits the same relationship, zeroes unused payload words and requests no acknowledgment of its own. It does not use the control broadcast as the resume destination.

QRTR checks the 32-byte header before field access, checks payload size against available bytes and rejects DATA addressed to another node before dispatch or acknowledgment. Outgoing lengths are bounded before padding into a 512-byte stack packet. Pending entries copy three integer endpoint fields, retain no borrowed packet pointer, coalesce identical flows and remain pending until queue acceptance. Sixteen entries are bounded, and a seventeenth distinct blocked flow latches failure. The worker observes that failure through its poller. BYE drops obsolete pending flows and discovery. All operations remain on the single modem owner.

The existing UIM path sends only empty seven-byte status requests 0x2f and 0x47. It pins the remote endpoint, retires matching transactions before parsing, rejects malformed/error/duplicate responses and checks service loss before cadence returns or reply admission. Selected readiness requires a PRESENT card and selected type2 USIM in state7, with fifteen-second freshness expiry. The card cap remains180 and slot cap3, within the unchanged120-second post-authentication worker. No provisioning, PIN/APDU, slot switch, ONLINE, raw-QMI, IPA or data hooks are present in the production runtime.

Modem storage references remain only `sweet_ufs_part_size` and `sweet_ufs_read_part`. Firmware is read from modem_a. Modemst1/2, fsc and fsg initialize RAM shadows. Rmtfs validates IDs, opened state, shared-window ranges and all vector extents before copying, and writes only those shadows. There is no modem-partition write callback reachable through these clients. Output remains suffix4 maximum, with full transport bytes private in RAM.

Strict hardware gates require one exact name=1 token and fail closed on absent, bare, duplicate or malformed values. Dependency checks precede hardware setup. The legacy read-only preflight gate is separate. Every path after PAS init becomes owed reaches `m_cleanup`, including transport/capacity failure. Owed PAS shutdown is attempted regardless of SSCTL eligibility or failure. Unverified stop returns before ownership reclaim, QMP-off, remaining vote release, backing unmap or RAM-shadow free. Subsequent ownership/QMP/vote uncertainty also prevents backing release. Shared XO/CX floors remain pinned. 2188 adds no PAS lifecycle or power transition.

The reviewed addresses match the supplied DT facts: MPSS PAS4 at0x8ac00000+0xe600000, SMEM0x81d00000+0x200000, rmtfs0xe1000000+0x280000, metadata0xe1400000 and arguments0xe1410000. Packed XNU RAM begins at0x8c0000000 with size0x134000000, above4GB.

## Artifact and test evidence

I independently matched all nineteen local/public patch pairs2170 through2188 and reconstructed all twenty modem-directory files plus sweet_modem.c in memory. Every reconstructed file matches production. Available saved extracted host-test inputs match production too. The 2187 public patch in that ordered chain is revision2. The unrelated private data-hook draft numbered2188 is outside this chain.

All six entries in `plans/modem/out/frozen-2188.txt` match current file contents. Frozen identities include:

- Patch2188 `727fae34cc4a3ec86bcba080c3b6895e70b76921780b1d3eb770494f1b2a741f`.
- Runtime `031e30b0116bb71781bd33574fc088317640b33e9e8daa3a1f2aac9d8bb066e9`.
- Kernel `d271eb0fea0af1c38c1b84a66a31f5d1f89e11cbe913724fdc9f5f304330b19a`.
- Image `1a5a9fc07d89079597f852d4125857a3263a9510e2d0a3bf9f2ea6caa6f01db0`.
- Script `6384312b1c6aac2594638df56023e66ad983ef29b77c44069cc73b0376c020ca`.

`python3 -B plans/modem/tools/verify-image.py uim-usb-2188` passes all ten rebased kernel segments and embedded-payload verification. The actual command line is601bytes, has unique keys and equals both saved2188 and21871 arguments. Required offline gates, fbarm1, selftest1, deadman600, USB4 and thermal3 are present. Bandwidth/IRQ gates are absent. The current hook/body composition equals the frozen script, and its matching private nonce is fresh against21871. I did not publish its value.

Saved `start-check-2188-final.txt` has fourteen PASS records, `uim-check-2188.txt` five, and `qrtr-worker-2188.txt` one. I found no ASAN/UBSAN error marker in these logs. Reviewed tests include the sixty-message peer model with a ten-packet ceiling, exact resume fields, refusal/coalescing/capacity, wrong destination/truncation, UIM transaction retirement, read-only rmtfs and joined SSCTL/PAS retention. These results do not cover finding1 because their queue callback differs from production. The build log reaches completed installation and `done building xnu`. Trash-safe --remake -j12 and exit0 are supplied execution evidence, not rerun here. Source-to-build provenance still relies on that record.

The runner pins the image, kernel, runtime, QRTR source/header and script, checks current hook/nonce after waiting, requires clean recovery, rechecks reservations/jobs and passes `--no-replay --boot-tries 1`. Its wants marker is cooperative reservation, with devloop taking the actual lock. It grants no right to interrupt IPA. IPA must finish and release both reservation/lock before any future approved launch. I made no phone contact or ownership change.

## Retention decision and future acceptance conditions

The supplied ast165 result supports a bounded diagnostic precedent: selected READY at10619/10621, nine card replies plus one slot reply, then silence and expiry11384. SSCTL accepted a fresh shutdown acknowledgment11686. All fifty PAS4 calls failed with raw -3145800, followed by retention11743 and final_error5 at11744. DONE12413 and normal365-second completion with fresh nonce and clean root show guarded return, not verified secure stop.

A flow-control-only status measurement could fit the same limited retention exception after finding1 is fixed. It adds bounded acknowledgment state without new persistent writes, ownership transfers, modem lifetime or power sequencing. The present artifact fails the claimed temporary-backpressure behavior and can skip the SSCTL stage required by the accepted known-failure diagnostic. The earlier approvals therefore do not authorize its boot.

A fresh GO for corrected, frozen artifacts must retain these conditions:

1. Wait for IPA release, acquire the modem reservation/lock through the reviewed launcher, wait for existing job PIDs and recheck clean recovery, frozen hashes, fresh hook/nonce and every guard immediately before exactly one RAM boot with no replay.
2. Require successful PAS authentication, ready/handover and MSS proxy release, healthy GLINK/QRTR, pd-mapper/RAM-rmtfs progress, more than ten fresh accepted UIM replies and selected-USIM readiness refreshed through the120-second worker. Queue refusal counters alone are not fatal if a corrected acknowledged retry drains successfully. Lost acknowledgment capacity, readiness expiry or transport failure fails acceptance.
3. Inspect the complete shutdown window. Require SSCTL success with fresh acknowledgment and every owed PAS attempt. For the known failure branch require the expected fifty raw -3145800 returns, explicit unverified-stop retention and final_error5, with no subsequent ownership reclaim, QMP/power drop or backing release. Any changed shutdown behavior needs review.
4. Require worker completion before normal selftest DONE under600seconds, matching fresh nonce, checked frame, stale=false, clean root before/after and recovery return. Reject panic, fired deadman, unexplained reset, missing guards/completion, unexpected cleanup or a new fatal condition. Stop progression without automatic replay.
5. Keep retained-stop diagnostic acceptance separate from strict checker PASS and from M2. No ONLINE, provisioning, IPA/data session, longer lifetime, partition writes, flash or slot operation follows. Firmware, raw logs and full identifiers remain private. Changed code or artifacts require a fresh review.

Verdict: fix finding1 and its regression first. Frozen2188 NO-GO. The limited PAS retention exception is not extended to this boot. M2 remains incomplete.
