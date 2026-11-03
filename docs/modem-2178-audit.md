# 2178 stop-handshake review

2026-11-03. Conditional GO for ONE offline 2178 RAM boot after matching build/image and device checks below. UIM remains NO-GO until that separate boot proves actual M2 with verified PAS shutdown and complete cleanup. Ast151 did not pass M2. No daily-use approval.

What this change does: 2178 clears the SMP2P stop request before PAS shutdown, then maps negative shutdown transport statuses into error classes so the existing bounded decrypt-shutdown retry handles unknown vendor failures. It also returns cleanup failure to the worker and requires both ready and handover for runtime success.

I read `3a-IOS/HANDOFF.md` first, then `plans/modem/sol-2177-audit.md` and `sol-ast149-audit.md`. Applied ponytail-review and unslop. Review only: no build, compilation, phone operation, delegation, upload or deletion. This report is the only file I wrote. Intended load is one bounded offline experiment on the known Mac.

## Exact reviewed source

Production is `ios-asteroids/research/xnu-modem/dxb/xnu`. I reversed 2178 through 2170 entirely in memory against production, requiring unique exact hunk matches and each added file to reverse to empty. All 65 hunks and 20 complete added files match. Integration hunks in existing base files match, but this does not certify every unrelated base file or prove the absence of all unrelated historical edits.

All nine modem patches match their public mirrors under `3a-IOS/patches/modem` byte for byte. Patches 2170 through 2177 retain the hashes recorded in `sol-2177-audit.md`. The 2178 SHA256 is `b64de02ba270f7e055a7d3407a4b47a3f53f1b01d225f834881a4b4e6fa0372e`.

Production SHA256 identities:

- `bsd/dev/modem/ModemRuntime.inc`: `c94de841755d05c250b5d2371c78efe10f06570d0aaab577678b3f7963ed43d7`.
- `bsd/dev/sweet_modem.c`: `b252b2bef43f7b4836d9329d0c6a2cd557293ffffc69519696e241968be95d5d`.
- `bsd/dev/modem/ModemUim.c`: `dfd0166976f9fd6b2480223da335ff8dcffd0d367ff473a254871423d7747d4a`.
- `bsd/dev/modem/QcomRpmh.c`: `1b8211d3ca3249cbd41d4868728daadcd57651a4e79f4b57ad9a3ebf088055d1`.

Production `sweet_usb.c`, `sweet_usb_gadget.h` and `sweet_reboot.c` match the audited USB v5 copies in `plans/usb/gadget/work/b5/bsd/dev`. USB C SHA256 remains `5695af3f994645c5e6597edac3995b3e1b0ff14d90adaf0ccd6b1eda47cd883e`. Comparing the two saved `baseline2178` files to production gives exactly the published 2178 changes. Approval covers this modem delta over the previously audited dedicated tree, with USB v5 and no additional kernel change.

## Evidence and shutdown logic

Ast150 closes the earlier completion gate. Its result records selftest-done in 245 seconds, root clean before and after, stale=false and one frame. The composed script checks bounded screenshot success and nonempty output, writes the matching nonce marker after the done step, syncs, and gives DONE fifteen seconds before reboot. Console lines 10584 and 10587 record the done step and DONE. Live power maps and no-PAS cleanup=ok are present. The active maps remain CX maximum 416, MSS maximum 320 and XO 0/32/80/128, with the prior table-padding limitations unchanged.

Ast151 proves PAS4 init/memory/auth success at console lines 7976, 7982 and 8753, ready plus handover at 8766, and GLINK up at 8771. Actual pd-mapper responses and RAM-backed rmtfs reads appear at 8955 through 8980, and UIM service discovery at 10775. Service discovery establishes availability only. UIM requests were off.

At 11695 the offline interval ended after 122821 ms. Lines 11696 through 11700 show stop acknowledgment followed by PAS shutdown transport status `-3145800`, hexadecimal `0xffcfffb8`, result zero, and STOP UNVERIFIED. The old worker still printed preflight_error=0. Ast151's selftest-done in 366 seconds and clean root do not establish successful modem stop, M2 or SIM state. Its memory/power retention was the correct failure behavior.

Production `ModemRuntime.inc:302 to 323` now clears request bit 0 and rings SMP2P after acknowledgment or the existing three-second timeout, before PAS shutdown. `QcomSmp2p.c:59 to 67` preserves other bits and orders the shared-memory update. The newly initialized master entry is the first entry, so the clear uses its valid index. STOP acknowledgment alone never releases allocations.

The downloaded primary Linux v6.12 sources support the mechanism: `reference/linux-6.12-q6v5.c:196` clears the stop request after waiting, `linux-6.12-q6v5-pas.c:188` retries shutdown on -EINVAL up to fifty calls, and `linux-6.12-scm.h:160` maps unknown transport errors to -EINVAL. I inspected these local source contents. They support error classification and retry, and do not decode the vendor status or prove a successful SM7635 shutdown.

Production `m_pas` maps only negative transport failures for command 6. Raw status and result are logged first. Known failure classes remain nonzero, unknown negatives become -EINVAL, and nonzero PAS result values remain failures. Other PAS commands are unchanged. Exhausted busy/interrupted SCM retries return positive ETIMEDOUT and safely stop cleanup without being reclassified.

The outer shutdown loop makes at most fifty PAS calls and sleeps 100 ms after each retryable failure. This bounds retry count, not total wall time to exactly five seconds: each SCM call and log synchronization adds time. There is no new unbounded retry. Only actual zero permits reclaim, QMP-off and the existing MSS-only vote release. Unverified stop retains all modem allocations and power. Uncertain TCS state still retains votes and allocations. Shared XO=3 and the CX floor remain until reset even after successful cleanup.

`modem_run:381 to 382` requires ready and handover, returns EIO when cleanup is incomplete, and its sole caller in `sweet_modem.c:355` records that return as final_error. The earlier offline end line is emitted before cleanup, so end error=0 alone still cannot prove acceptance. Final_error=0 also requires corroborating service and storage evidence for M2.

## Host-check review and limits

I inspected `3a-IOS/tools/modem/check-start.sh`, the lifecycle prefix/tail, and the supplied passing start/preflight/UIM logs. The current generated public lifecycle C file matches the actual production `m_scm`, `m_pas` and `m_cleanup` bodies plus current test wrappers byte for byte. It does not substitute a mocked `m_pas`. The generated lifecycle SHA256 is `105e32e1f3d556d2c540401b7c37a47e41f15e4cbbe1f345fd4cf7875236ab26`.

Tests cover two raw vendor failures followed by actual zero, permanent vendor failure stopping at fifty calls with memory retained, known fatal transport failures, nonzero PAS results, clearing before PAS after acknowledgment and timeout, failed reclaim and uncertain votes. The raw-call stub asserts the stop request is clear before every shutdown call. The supplied log contains the expected 52 vendor failures across transient and permanent cases. The suite also checks existing transport resume/busy bounds, power votes, resource maps and read-only storage references.

These are host simulations with fake hardware and time. The suite does not execute the whole `modem_run` or its caller, so ready/handover acceptance and final-error propagation were source-reviewed rather than dynamically exercised. The older `plans/modem/tests/lifecycle-*` and its generated output are stale copies; the reviewed 2178 suite is the public `3a-IOS/tools/modem` suite above. I did not rerun or compile tests.

No new source finding blocks this bounded offline trial. Hardware efficacy of clearing the bit and retrying remains unproven. A repeated permanent status is diagnostic failure, never permission to treat it as success or widen commands.

## Build and one-boot conditions

During review, `logs/build-2178.txt` reached `CC sweet_modem.o` at 2343 and `done building xnu` at 5774. The installed kernel presently hashes to `ad1167ae5e3d071bbd58f179cda6bc03ed6aaa0d7be4a7aa15ad88d9afb1f636`. This is observed existing build evidence. I did not supervise the build or establish its process exit status, and have not verified a newly packed 2178 image.

1. Before boot, the parent must confirm the matching --remake build exited successfully, the pinned reviewed sources remain unchanged, and the new offline RAM image embeds this exact resulting kernel. Verify rebased kernel segments and packed arguments, record image/kernel hashes, and refuse any stale 2177 image or additional kernel change.
2. Permit exactly one each of `sweetmpss=1 sweetmglink=1 sweetmrmtfs=1 sweetmpdm=1 sweetusb=4`, with `sweetmprobe`, `sweetmuim` and `sweetusblog` absent. Preserve cpus=8, sweetfbarm=1, sweetpanicfb=10, sweetdeadman=600, sweetselftest=1, sweetthermal=3, keep plus astwait, UBWC and fetchsync. Confirm unique argument names and the 607-byte ceiling in the actual image.
3. The parent must restore `/tmp/phone-modem-wants` before waiting for the next phone-job PID, then independently confirm actual recovery or fastboot and no active dev.py immediately before the run. The current runner requires checked clean recovery and refuses an automatic replay boot. Use dev.py only, never interrupt owner iOS, and keep the entire run under twenty minutes.
4. Inspect the composed selftest before launch. Preserve prior frames, check the actual screenshot exit and nonempty same-run output, use a fresh run-specific completion marker, retain the done step/DONE and fifteen-second flush interval, and collect the marker and frame. A reused marker such as ast151's fixed `2177-offline-first` text alone cannot prove the next run completed.
5. Accept M2 only with same-run ready plus handover, GLINK/QRTR, actual scoped RAM rmtfs reads and pd-mapper responses, cleared stop request, actual PAS shutdown status=0/result=0, STOP verified PAS with cleanup=ok, successful ownership reclaim/QMP-off/MSS release where owed, and final_error=0. Require normal unattended completion, recorded DONE and done step, matching fresh marker/frame, root clean and stale=false, healthy USB, no SELFTEST FAIL/SKIP, panic, fired deadman or unresolved transport/canary failure.

A stop timeout alone does not prove failed PAS shutdown, since PAS may still return zero. Preserve timeout evidence and review it before accepting advancement. STOP UNVERIFIED, cleanup=INCOMPLETE, missing stage evidence, nonzero final error or ambiguous completion makes M2 NO-GO. Retain memory and rails on unverified shutdown and return only through guarded recovery/reset. A failed run needs evidence review and fresh scoped approval for any subsequent modem boot.

## Separate UIM stage and safety

UIM remains off for the 2178 offline trial. Only after actual M2 and verified shutdown are reviewed may a separate later boot add `sweetmuim=1`, with a fresh Sol GO and independently matching image/device checks. M3 needs valid endpoint/transaction-matched card/application status, including an explicit absent-card result where applicable. Discovery of service 0xb, an overall zero return or selftest DONE cannot establish M3. Preserve slot-query errors and unavailable ICCID visibly, and emit at most identifier suffix four.

Continue toward M3 within these gates without routine owner confirmation. This report authorizes no phone action by this reviewer. No partition writes to EFS/modemst1/modemst2/fsg/fsc/persist/modem_a, no flash, stock boot, plain fastboot reboot, Android, set_active, sudo or permanent deletion. Modem storage uses read APIs and RAM shadows only. Local writes stay in the workspace or /tmp. Removals require `/usr/bin/trash` with an absolute path. No TFTP, IPA, online mode, NAS/WDS, data call or raw QMI bridge. Public material must exclude firmware, dumps, raw logs and identifiers beyond suffix four.

Not checked live: packed 2178 image identity, successful hardware shutdown, M2 or M3. Verdict: conditional GO for the single offline boot above, UIM NO-GO pending actual M2 and fresh review.
