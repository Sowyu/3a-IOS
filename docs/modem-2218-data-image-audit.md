# Combined 2218 ONLINE image review

**NO-GO for the exact combined ONLINE trial.** The accepted offline I3 prerequisite is satisfied, and I found no new blocking DMA lifecycle defect in the reviewed retained teardown. The runner's restoration path can overwrite an unrecognized preferences file, which violates the required protection of the owner's profile changes.

This decision covers the submitted source, exact ONLINE image, runner, profile swap/restoration and live en2-bound proof procedure together. Do not launch this candidate or set its manifest to approved=true on the strength of this report. No phone operation was performed.

## Must fix

1. Preserve unrecognized profile changes during restoration. In `plans/modem/tools/run-data-usb.py:82-92`, `restore()` reads the current preferences and writes the approved original whenever those bytes differ from the original. It never compares the current bytes with the approved temporary en2 profile. A profile edited during the trial, or an unexpected profile present after an early launch failure, therefore gets replaced too.

   I reproduced this with the actual pinned runner imported on the host. Device discovery, clean-root checks, profile reads and the write command were mocked. Feeding a third file containing owner changes as the pre-restoration read caused a PUT of the original and then successful return when the readback supplied original bytes. No device command ran. The existing runner fixture also explicitly expects an arbitrary `changed` file to be overwritten, so its passing result does not establish conflict protection.

   The smallest safe change is to pass the pinned trial profile into restoration, leave an already-original file alone, restore only a recognized trial profile, and retain ownership without writing on a third state. If configd legitimately rewrites the trial bytes, define and test the precise allowed transformation rather than accepting every difference. Add the conflict case to the runner check, update its frozen hash and request a new exact-artifact decision. This review makes no implementation changes.

The initial profile check at `run-data-usb.py:144-146` does correctly stop before the swap when the live original differs. The defect above is in restoration after `launched` becomes true. Clean recovery, same-device selection and post-write byte equality do not identify whether a differing pre-restoration profile belongs to this trial.

## Frozen source and image

I independently SHA256-checked all 143 manifest artifacts and all 56 source-hash entries against their actual files. Every entry matched. The manifest remains approved=false, contains no final audit hash yet, and its one-attempt file is absent. Those are appropriate admission barriers while review is unresolved.

The saved kernel and live built kernel both hash to:

```
e6bc2136e5a87302331271164f7ac11c6c4725e3b1cae63143200337c12b2f84
```

The actual `plans/modem/out/data-usb-2218-boot.img` hashes to:

```
4e1409ee78555833882db2823e7c3ffefb77b7c738da9b57d2d7e580fad77bd0
```

I reran `verify-image.py data-usb-2218`. It verified payload containment in the decompressed image and exact equality of all ten packed segments against the live kernel after chained fixups. I independently extracted the image arguments, matched them with the pinned argument file, checked the 607-byte length and unique argument keys, and verified the reset/selftest/deadman gates.

I reran ordered source reconstruction through 2218. All 56 reconstructed production files matched the actual combined tree. The new reconstruction is `plans/modem/series-check/1793706422463625000`. The supplied full build log ends with successful kernel installation and `done building xnu`. I did not rebuild the full kernel or claim a fresh build.

The reviewed argument line preserves the required modem, IPA and ONLINE data gates, USB4, cpus8, panel0, thermal3, fetchsync1, reset protection, selftest1 and deadman900. Raw QMI export and probe-only gates are absent. The private APN and PAP selection are supplied to Start Network requests. The session uses no profile create/modify request. The kernel starts the 600-second session deadline after successful MPSS authentication.

## Accepted hardware evidence

I read the four accepted records and independently matched their actual result and raw-log hashes. For records with markers, I also matched the actual marker hashes. I reran their host acceptance predicates against the actual captures. For the clean checker I suppressed its acceptance-record write in memory, preserving the existing accepted file.

- ast174 completed normally in 214 seconds with clean roots and stale=false, verified SMMU-only setup and parking, and no PAS start.
- ast176 completed normally in 214 seconds with a clean final root and stale=false, with benign journal replay and no IPA/modem work.
- ast177 completed normally in 214 seconds with clean roots and stale=false, PAS15 commands 1/2/5/6 successful, intact guards, parking and backing release.
- ast182 completed normally in 363 seconds with clean roots and stale=false, and the actual checker returns `I3 complete; PAS4 unverified; IPA backing retained until reset`.

For ast182 I independently checked its image, checker and source-audit hashes against the accepted record, alongside its result/raw/marker identities. The manifest also matches the actual captured root script. Raw lines 8150/8151 record tables/SRAM PASS and ready endpoints, 9149 records PAS4 authentication, 9458 records every required QMI flag, and 9461 records I3 after queue drain. Offline error=0 at 12176 precedes AP quiescence at 12177 and SSCTL at 12178. Modem/IPA retention follows at 12235/12236, then normal DONE at 12722.

The accepted checker explicitly permits this retained branch, requires final_error=5, and forbids PAS15 shutdown or backing release there. The older source audit's statement that I3 was unproven describes its pre-run state. The accepted ast182 capture now establishes offline I3. It establishes neither ONLINE registration nor WDS calls nor packets. Earlier selected-USIM READY evidence remains M3 evidence only.

## Retained teardown and ONLINE calls

I traced the actual `ModemRuntime.inc` worker exit, `m_data_stop`, `m_cleanup`, transport synchronization, `IpaRuntime.inc` cleanup, hardware quiescence/final stop, GSI completion checks, data adapter, readiness/service cache and `CellSession.c` stop/late-reply paths.

Worker exit terminally revokes IPA readiness and drops queued TX before cleanup. `m_data_stop` marks stopping, revokes USIM readiness, starts data cleanup, and pumps SMP2P/GLINK/QRTR replies plus session polling for at most five seconds. It checks both idle and the cleanup result, quiesces AP DMA before detach, and records failure if either cleanup or quiescence is unverified. A failure remains visible through `m.failed` and the final error.

During STOPPING, the session sends only WDS Stop Network. A matching late valid Start success can still supply its handle, which is then stopped. A malformed Start keeps the outcome unknown. Unresolved starts or handles remain cleanup failures even when the session reaches a terminal state. Retention never converts them into successful WDS cleanup.

SSCTL/SMP2P/PAS4 stop follows AP quiescence attempts. When PAS4 stop is unverified, `m.stop_verified` stays false, final GSI reset/deallocation is skipped, and `ipa_cleanup(false)` goes directly to retention before PAS15 shutdown or SMMU release. Modem and IPA backing, mappings and required power remain retained until guarded reset. Failed AP quiescence sets `ipa.retained`, so backing cannot be released even if PAS4 later stops.

Final IPA release requires verified PAS4, successful GSI final stop and PAS15 stop, followed by SMMU release and guard checks. A failed final GSI stop sets retention. In the verified-PAS4 branch PAS15 shutdown can still be attempted before the retained flag prevents release. This is distinct from the unverified-PAS4 branch, which issues no PAS15 shutdown. I found no path that frees the IPA backing after failed AP quiescence.

This contract is sufficient for an owner-accepted bounded ONLINE experiment once the runner blocker is fixed. The online-specific unknown is real active-call teardown and the actual data-channel completion format. Command completions require measured 0x23, while data completions require strict 0x22. Unexpected type, producer, pointer, length, channel or error status fails closed and revokes readiness. No broader event acceptance is approved. M2 remains incomplete even after a successful packet proof.

## Runner, root swap and packet proof

The runner correctly refuses approved=false before ownership or phone mutation. It checks pinned artifacts, the future pinned audit, image arguments, exact standalone hook composition and actual recovered-root composition, then checks frozen identities again before consuming one exclusive attempt. It uses the modem hold, atomic phone lock, same original profile, clean recovery, `--no-replay` and `--boot-tries 1`. Restoration failures retain ownership. The remaining conflict bug blocks approval of the combined launch path.

I recomposed both selftest files from the actual hook/body and the ast182 captured root script. Both matched byte for byte. The body waits 660 relative seconds and uses a 15-second finish delay. Its completion marker identifies body completion only. It is not evidence of WDS success, packets or clean modem shutdown. I regenerated the en2 preferences from the pinned original and obtained the exact approved trial-profile bytes. The profile keeps ue0 available for SSH and gives en2 DHCP service priority.

The en2 procedure requires the existing key over bridge100, unchanged Sharing, and an exact-image live PID check before each SSH operation. It withdraws the boot job's competing ue0 route/DNS state once, creates a fresh private directory exclusively, uploads the pinned helper without overwrite, and compares full readback bytes and SHA256 before chmod and one execution. The helper remains an inert archive afterward. These steps have been reviewed only, not performed.

I checked the helper's actual source and binary hash, verified its code signature with `codesign --verify --strict`, and reran the localhost-only probe tests. The arm64e executable has an ad hoc signature. Its production entry point accepts no caller-selected interface or destination, binds IP_BOUND_IF to en2, reads back that binding, binds and verifies the en2 source address, and applies one monotonic ten-second deadline to connect/send/read. It requires an HTTP status line and has no unbound fallback.

For a future trial, retain private before/after interfaces, routes and counters alongside the helper output and exit status. Only the successful bound exchange counts as M6 packet evidence. DNS, TLS, Safari, iOS primary-route use and full M2 shutdown remain separate claims.

The runner and devloop have individual operation timeouts but no aggregate ownership deadline. Any future GO must retain active supervision and a strict less-than-20-minute budget including recovery, collection and restoration. A 900-second kernel deadman alone does not enforce that host-side limit. Do not begin SSH work near the end of that budget or reconnect after the selected PID exits.

## Independent host checks and limits

Fresh test copies and logs are in `/tmp/sol-2218-ju0zbzlq`. I ran the copied IPA datapath and lifecycle suites, the data suite against actual combined production data sources, the actual helper's localhost suite and runner validation/restoration fixtures against the pinned runner. They passed with ASan/UBSan where supplied. I also confirmed the reconstructed IPA headers/runtime used by the tests equal the actual combined production bytes.

The checks cover command/data completion separation, malformed producers and completions, terminal runtime fault revocation, queued TX/pending RX stopping, teardown failures, real 2221 partial binding, SMMU parking/retention, PAS15 failures, session late Starts, unresolved WDS cleanup, service replacement, QMAP framing, profile/auth request layout and the bound socket deadline. Runner checks cover approval refusal, hash changes, clean same-device restoration and failed readback. The additional mocked restoration-conflict case exposes finding 1 despite those passing fixtures.

Reconstruction, compiler and test subprocesses ran with TMPDIR=/tmp, and review-created tempfile calls explicitly selected dir=/tmp. Writes stayed in the workspace or /tmp. I did not change implementation, frozen fixtures, the manifest, accepted records or another agent's test outputs. No phone/SSH operation, commit, delegation, upload or file removal occurred.

The exact ONLINE image has not been booted or measured by this review. Live recovery state, exclusive ownership and available trial budget must still be established at launch. Fix finding 1, repin the changed runner and obtain a new combined decision before approval.
