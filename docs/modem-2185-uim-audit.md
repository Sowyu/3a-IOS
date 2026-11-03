# 2185 offline UIM diagnostic review

2026-11-03. GO for ONE bounded, offline, read-only UIM RAM boot of the frozen 2185 image, with the conditions below. PAS shutdown is KNOWN unresolved. M2 remains blocked. This is a fresh scope decision and grants no automatic progression to ONLINE, IPA or another boot.

What this change does: `sweetmuim=1` enables the previously unbooted 2176 card-status and slot-status clients. 2185 makes their successful responses reject duplicate result/status TLVs. The proposed image omits `sweetmbw`, restoring the measured 2182 modem power path.

## Review boundary

I read `3a-IOS/HANDOFF.md` first and applied ponytail-review and unslop. I reviewed the complete production `ModemUim.c/.h`, `m_uim_receive`, `m_uim_poll`, QRTR/QMI helpers, their runtime callers, shutdown/retention path, RAM rmtfs, boot gates, selftest and runner. This was a desk review with no phone operation, build, production edit or deletion. The requested report is the only file written.

The earlier M2-before-UIM comments and audit conditions are acknowledged. This approval explicitly permits the separately bounded diagnostic despite failed secure stop. It neither changes those historical results nor asserts successful secure shutdown.

## Why known PAS failure does not block these reads

`ModemRuntime.inc:281-292` sends only empty seven-byte QMI requests for service 11 messages 0x2f and 0x47. Neither message selects online mode, changes provisioning, verifies a PIN, switches a slot, reads an EF or sends an APDU. The message IDs and layouts agree with the [upstream libqmi UIM schema](https://raw.githubusercontent.com/linux-mobile-broadband/libqmi/main/data/qmi-service-uim.json). The expected card body has eight index bytes, a one-byte card count, six fixed bytes per card and 14 fixed bytes plus AID bytes per app. The slot body has a one-byte count and ten fixed bytes plus ICCID bytes per slot.

The additional risk is exercising an unmeasured firmware request/response path and handling its returned data. It adds at most six small queued requests during the existing 120-second runtime. It adds no new shared allocation, ownership transfer, power transition, persistent write interface or extended modem lifetime. A firmware fault or malformed response remains possible and makes the diagnostic fail.

The concrete safeguard against a live modem accessing released backing is already present. `m_cleanup:401-429` attempts SSCTL, falls back to the stop bit when needed, and always attempts owed PAS shutdown. A nonzero PAS result returns before ownership reclaim, QMP off, remaining vote release, unmapping or freeing. Firmware, metadata, SCM arguments, shared rmtfs window and RAM shadows remain backed until the guarded reset. Shared CX/XO floors stay pinned. The earlier MSS proxy release occurs at ready/handover and is the measured 2182 behavior, not a post-failure power drop.

I found no concrete additional teardown safeguard missing specifically for these two status queries. Requiring verified PAS before issuing them would restate the old milestone order without identifying another resource they make unsafe. This GO accepts the existing bounded retention/reset risk for this single experiment. It does not establish secure-stop correctness or suitability for a persistent data session.

## Complete client review

The worker calls the sender only after ready and handover, with GLINK/QRTR enabled. It binds an unannounced local client at 0x4002. With rmtfs, pd-mapper and SSCTL, this exactly fills the four local QRTR entries. All work runs on the existing single modem worker.

Discovery selects the first service-11 advertisement. The selected source node/port is pinned, and a changed discovered endpoint marks transport failure. QRTR routes the local destination port, then the UIM callback checks source node/port. The parser additionally requires response type, exact message ID and the latest outstanding transaction. Transactions are distinct across messages and attempts: 1/3/5 and 2/4/6. A late previous response can be rejected without creating another request.

There are at most three successfully queued requests per opcode, spaced by ten-second polling deadlines. Queue refusal sets `m.failed` in the actual runtime and ends the loop. Shutdown does not call `m_uim_poll`, so it adds no new UIM requests. Already queued traffic can still drain while SSCTL runs, within the existing shutdown deadline. Replies only print status and set completion bits.

QMI parsing validates exact packet length and every TLV extent before `unique_tlv` walks them. Success requires one four-byte standard result with consistent result/error fields and one status body. Duplicate relevant singleton TLVs cannot establish success. Unknown optional TLVs are skipped. Counts are bounded at four cards and eight apps per card, AID at 32 bytes and ICCID at ten bytes. Every variable-length advance is checked before access, final body consumption must be exact, and outputs remain zero on rejected responses.

ICCID decoding accepts decimal BCD with optional final high-nibble F and exports only four digits plus NUL. AIDs are skipped. Runtime logs contain status values and suffix4, with no packet dump or full ICCID formatting. Full reply bytes necessarily exist in borrowed transport buffers, so this is suffix-only output, not proof that full identifiers never enter RAM. Raw transport/log artifacts remain private.

No blocking client finding remains for this scope. Numeric app/PIN/slot states are diagnostic output, not permission to start data. Empty counts, unknown values, inconsistent card/slot reports, an unsupported 0x47 or oversized replies require an explicit incomplete/unsupported result. They must not be turned into proof of no SIM or a usable SIM. Discovery does not enforce a particular UIM instance or fail immediately on advertisement removal; stale-endpoint reads remain bounded and cannot establish success without matching valid replies. These limits do not authorize a general-purpose QMI client.

## Independently checked evidence

All sixteen local/public patches 2170 through 2185 match byte for byte. I reconstructed ModemUim.c/.h, QcomQrtr.c/.h and ModemRuntime.inc from the full ordered public series in memory. They match production and the saved host-test inputs. The saved runtime unit exactly composes the current test prefix, actual production sender/receiver functions and test tail.

The sanitizer test source covers status/apps, absent/error responses, truncation, transaction mismatch, AID overflow, BCD rejection, duplicate result/status and optional TLVs. The runtime unit checks its gate, actual encoded opcodes, wrong source node/transaction, completed-request suppression, retry exhaustion and endpoint change. `logs/uim-check-2185.txt` records both PASS results. The runtime test mocks time/transport and does not prove actual firmware compatibility. I did not rebuild or rerun these suites under the no-build scope.

`logs/build-2185.txt` includes the completed full install and ends with `done building xnu`. Exit 0 and safe remake execution are supplied process evidence. The unchanged runtime SHA matches the previously joined-tested 2184 runtime. I inspected the saved joined/bandwidth evidence, not a new hardware result.

I independently ran `python3 -B plans/modem/tools/verify-image.py uim-usb-2185`. All ten rebased kernel segments match and the saved payload is embedded in the boot image. I decoded the actual payload command line and verified equality with `out/uim-usb-2185-args.txt`, unique keys and 587 bytes.

The payload enables mpss/glink/rmtfs/pdm/mstop/mho/muim at 1, USB4, fbarm1, selftest1, deadman600, panicfb10, thermal3, keep1, astwait1 and fetchsync1. It has no mbw, online, IPA, TFTP or raw-QMI gate. Touch's 2184 maximum-SN3 branch uses the same strict `sweetmbw` gate and is inert here.

The current hook/body composition exactly equals `out/uim-script-2185.sh`. Its private nonce matches `out/modem-uim-nonce.txt`. Collision-safe archive names, checked nonempty screenshot and 15-second DONE flush are present. The script waits 240 seconds before its frame, while the modem runtime is bounded at 120 seconds after authentication. Acceptance must still check that the worker actually finished before DONE.

Frozen SHA256 identities:

- Production UIM `107541d6e2eb4997aa57ec8109400876329ce614001aeb29f8df32a7073d6869`.
- Runtime `e815266454fbb1407c46159454014728b8aff7a55c07840495d5728108ccb842`.
- Touch `7b5cf717cbc553a36a5bcfe19b996fc6866b05b091d6b2425734f14f74ad8534`.
- Kernel `e9a429a09108bee92c1adec73afb6f931bb9829950bbe63e2ab633ad78d2a86a`.
- Image `ebdc89eea67d716452a62c8af5e83669116e7ad5028167727760118f02a70884`.

Frozen script SHA256 is `1a4d202a491b643b3c90ca4c86fc54fd2f4ffa6eaafc3c35de0ff695068dc6c0`. Source-to-build execution relies on the supplied build record. Segment equality establishes packed-image/kernel congruence.

## Actual prior results

I checked ast158's raw log. MSS proxy release is at 7173, accepted SSCTL response plus fresh shutdown acknowledgment at 10636, fifty PAS shutdown calls with raw status -3145800 at 10637 through 10703, retention at 10704 and final_error=5 at 10705. DONE is at 11443. Its result records 364 seconds, clean root before/after, one frame and stale=false. This proves observed guarded return after failed PAS, not successful secure stop.

ast160's raw log and result show bandwidth timeout before PAS initialization, cleanup=INCOMPLETE at 9740, final_error=5 at 9743, DONE at 12557 and a 366-second clean-root return. It supplies no MPSS/UIM result and is not a reason to retry the unchanged bandwidth configuration. These records contain no evidence of a modem-caused reset. They do not guarantee a future run cannot reset.

## Launch and acceptance conditions

1. The operator must change only the runner's image/note from its current 2182 names to exact 2185, keep `--no-replay`, coordinate the shared modem hold, wait on active phone-job PIDs, and recheck actual recovery plus clean root immediately before launch. The wants marker alone is not a lock. Recheck frozen hashes, payload arguments, hook and private nonce. One RAM boot only, under twenty minutes, with no unchanged repeat or queued progression.
2. Require successful PAS authentication, ready/handover, successful MSS proxy release, healthy GLINK, UIM advertisement, pd-mapper and RAM-rmtfs progress. Record each UIM request and require valid card/app status plus valid slot status/suffix4 for the installed card, or unambiguous absent-card evidence for plain no SIM. No valid response is an incomplete measurement, not no SIM. PIN-required or app-not-ready status is an observation, not usable-data approval.
3. Run all existing shutdown steps and log every actual PAS attempt. For the known failure branch require accepted SSCTL with fresh acknowledgment, the same raw PAS failure behavior, explicit STOP UNVERIFIED retention and final_error=5 before DONE. Require no subsequent reclaim, QMP-off, vote/power drop or free/unmap of unverified modem backing. Review the whole shutdown window because the early retention return does not print the final teardown transport/fatal summary. Any transport fatality, uncertain vote or unexpected PAS failure behavior fails acceptance and stops further runs.
4. Require normal selftest DONE, matching fresh nonce, checked frame, stale=false and clean root before/after. Reject panic, fired deadman, reset-unknown, FAIL/SKIP, missing worker completion or dirty root. Panic/deadman remain emergency guards, not acceptable outcomes. Keep firmware, raw logs and full identifiers private.
5. Report SIM observations and retained-stop outcome separately. The known PAS-failure outcome can measure M3 only and leaves M2 blocked. Actual PAS status=0/result=0 plus cleanup=ok, healthy transport and all lifecycle/completion checks would supply the missing M2 evidence. The existing strict `check-run.py` deliberately cannot PASS retained-stop diagnostics and must remain strict. Do not label a diagnostic-only acceptance as its PASS or as normal cleanup.

No partition writes to modemst1/2, fsg, fsc, persist or modem_a, flash, stock boot, plain reboot or slot changes are authorized. rmtfs mutations remain RAM-only. No ONLINE, IPA, TFTP, raw QMI, SIM mutation or longer-lived session is authorized. Changed source/image, broader integration or an unexpected result requires fresh review.

Verdict: GO for this one guarded offline UIM diagnostic with known unresolved PAS shutdown. No phone run was performed by this reviewer.
