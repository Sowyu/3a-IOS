# 2182 MSS proxy handover review

2026-11-03. GO for ONE guarded offline RAM boot of the exact 2170 through 2182 candidate below, under the launch and acceptance conditions in this report. No blocking source finding for this scope. M2 remains incomplete. UIM remains OFF and the prepared UIM image is not approved.

What this change does: default-off `sweetmho=1` releases the modem-specific MSS ARC proxy request once MPSS has started and both ready and handover are observed. Shared CX/XO requests stay pinned for the remainder of this bounded kernel session.

I read `3a-IOS/HANDOFF.md` first, then the modem checkpoint and prior 2179/2181 audits. I applied ponytail-review and unslop. This was desk review only: no phone actions, builds, compilation, host-suite execution, production edits, uploads or deletion. This report is the only file written. Read-only Python checks verified source equivalence, packed segments, arguments and script composition.

## Source and artifact identity

I reversed all thirteen numbered modem patches in memory against production. All 80 hunks matched uniquely and all 20 added files reversed to empty. All public and local patch copies match byte for byte. This establishes the modem deltas and integration hunks, not every unrelated base-tree file.

Applying 2182 in memory to `baseline2182/ModemRuntime.inc` reproduces production exactly. The baseline retains the audited 2181 runtime hash. Extracted host-source copies match production, and generated vote/handover, PAS/cleanup and SSCTL test translation units match current production extraction plus their wrappers. USB C/header and reboot source remain byte-identical to the previously reviewed v5 copies.

Checked SHA256 identities:

- Patch 2182: `4e7e1f44c418ec32f8d5dfca45190b35d9b9a89aad740133c0d6b85e5baa4247`.
- Production runtime: `8e9c0407fdeb4bc0dbfcdf6ea5f01f04c81872214ed740265decc066d29be039`.
- Kernel: `1db3c6ccb71dcfe6db23a2005955fa7b5665efe78c634a9a16ecc391003c141f`.
- Offline image: `558a03d19400f09e5e551371ff36635452431778c2dfeca6f96146f38c49ca24`.
- Composed script: `926c89dfd395389428251e83f059a1d84fbd5a7b33f203e2b44bb115b8c06d1e`.

I ran the read-only `plans/modem/tools/verify-image.py offline-usb-2182`: all ten rebased segments match the exact kernel, and the saved payload is embedded in the image. The payload's actual command line matches the snapshot, is 593 bytes, has unique keys, and includes all six modem stage gates plus USB4, selftest, framebuffer arming, deadman600 and thermal3. UIM, probe, BCM survey and USB live-log gates are absent. The current hook/body compose exactly to the saved script and match its private nonce snapshot. Collision-safe archives, checked screenshot and fifteen-second DONE flush are present.

The supplied build log ends with `done building xnu`; the requester reports safe `--remake -j12` exit 0. I did not independently recover that process exit status or rebuild. The supplied sanitizer-backed start log contains the actual handover vote PASS, lifecycle/SSCTL checks, joined QRTR/RAM-rmtfs/PAS PASS and read-only storage assertion. I did not rerun those suites.

## Why this candidate is justified

The local primary vendor source, basis commit `d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7b`, supports the resource and timing. `asteroids.dts:953–963` maps phandle 0x77 to `mss.lvl`; the modem node at 7045 onward uses it as mx-supply. `nothing-drivers_remoteproc_qcom_q6v5_pas.c:1091–1109` releases proxy regulators during handover. Its helper at 436 onward sets voltage/load to zero and disables them. `nothing-drivers_remoteproc_qcom_q6v5.c:227–234` invokes that callback from the handover interrupt.

The candidate releases only MSS after both ready and handover, with successful PAS start also required. This is a conservative subset of the vendor proxy release: the shared CX/XO floors remain. No BCM vote or bandwidth encoder is introduced. Acknowledged zeroing of the APPS MSS request relinquishes its proxy floor; it does not establish that the physical regulator turns off, since other voters remain hardware-controlled.

Ast155's persistent log at 10526 confirms SSCTL response, indication and fresh bit-7 acknowledgment, followed by fifty unsuccessful PAS calls, retention at 10585 and final_error=5 at 10586. Its result records selftest-done in 361 seconds, clean root and stale=false. Ast157 records all eight read-only BCM metadata records, no PAS activity, cleanup=ok, final_error=0 and selftest-done in 244 seconds with clean root and stale=false. Neither establishes M2. The proxy lifetime discrepancy is a specific source-backed experiment; it does not decode the secure-world error or prove the cause of failed shutdown. Missing vendor shutdown bandwidth remains a separate hypothesis.

## Failure handling and test limits

`ModemRuntime.inc:148–158` gates release on explicit opt-in, started, owed vote, ready plus handover and no failed/fatal/uncertain state. The exact single-token gate rejects missing, duplicate, bare or non-1 values. A successful release clears votes_owed, preventing repeated release in subsequent loop iterations or cleanup.

Failure sets m.failed and exits the runtime loop. PAS shutdown remains owed and is still attempted. A timed-out vote leaves its TCS unchanged and vote_uncertain set; cleanup never reuses that TCS or frees buffers on incomplete cleanup. ENODEV means no TCS was programmed and preserves the owed vote. Cleanup may retry that owed release only after verified PAS. 2180 keeps failure/fatal state visible in the final worker result.

After a successful handover release, failed PAS retains memory and the remaining shared power requests. MSS has already been released and is not reasserted. The old generic retention message must not be read as proof that the original MSS proxy floor remains. Vendor handover semantics justify this behavior for this one bounded trial.

1. Host vote tests exercise successful release, once-only behavior, opt-out, missing ready/handover/start, fatal refusal and an actual release timeout retaining the TCS. They do not explicitly exercise handover-time ENODEV, preexisting failed/uncertain guards, or successful selection of the second/third TCS. Source inspection shows the guards and failure result handling, but those cases remain untested.

2. The joined shutdown suite runs real QRTR dispatch, RAM-rmtfs, SSCTL, PAS and cleanup, but substitutes m_vote. The actual handover/vote suite does not call cleanup or the full modem_run loop. Consequently no single dynamic test proves handover timeout -> PAS attempt -> retained buffers, or successful handover -> no second vote during cleanup. Separate tests and source flow cover those connections for this manual trial. Add those joined regression cases before treating this as a qualified or automatically repeatable lifecycle.

3. Host register mocks cannot prove physical rail handoff, remote ownership, RSC concurrency or firmware behavior. The hardware trial must establish those outcomes through the required logs and normal guarded recovery.

These are coverage limits, not blockers to this single diagnostic run.

## Conditions for the one run

1. Freeze the exact source/kernel/image above and retain the parent's successful build-exit evidence. Recheck hashes if anything changes. Verify the actual script supplied by dev.py retains the reviewed hook/body and fresh nonce; an appended original firstboot body is expected. Do not substitute the prepared UIM image.

2. Acquire `/tmp/phone-modem-wants`, wait on existing phone-job PIDs, and independently recheck recovery mode, no active dev.py and clean root immediately before launch. Never interrupt owner iOS. Use the existing runner and dev.py with `--no-replay` for exactly one RAM boot, under twenty minutes. The runner refuses fastboot as its launch state. Its mode/process checks are observations rather than an atomic device lock, so the established shared hold protocol remains required.

3. Require same-run PAS start success, ready plus handover, one successful MSS release record, GLINK/QRTR NEW_SERVER, scoped RAM-rmtfs/pd-mapper progress and successful SSCTL response plus fresh acknowledgment or valid indication. Missing release or service evidence fails this experiment even if the shell reaches DONE.

4. Require actual PAS shutdown status=0/result=0, verified PAS, successful owed reclaim/QMP-off, cleanup=ok, transport_failed=0, fatal=0 and final_error=0. Require no uncertain vote, canary/transport failure or unhealthy USB. After successful handover there is no owed MSS release at cleanup. Failed PAS must retain buffers and return only through the guarded reset/recovery path.

5. Require recorded done step and DONE, matching fresh completion nonce/frame, root clean before and after, stale=false and no FAIL/SKIP, panic or fired deadman. Any failed or missing measurement leaves M2 incomplete. Stop for evidence review; no unchanged repeat and no automatic UIM progression.

No stock boot, flashing, plain reboot, slot changes, partition writes, online mode, TFTP, IPA or raw QMI bridge is approved. Never write modemst1/2, fsg, fsc, persist or modem_a. Storage shadows remain RAM-only. Writes stay in the workspace or /tmp; removals require `/usr/bin/trash` with absolute paths. Keep raw logs, firmware and full device/SIM identifiers private.

Verdict: GO for the one exact guarded offline 2182 run above. Concrete blockers: none within this scope. Hardware shutdown success and M2 remain unproven; UIM remains NO-GO.
