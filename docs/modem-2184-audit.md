# 2184 guarded offline modem review

2026-11-03. GO for ONE exact offline 2170 through 2184 RAM boot, subject to the launch and acceptance conditions below. No blocking finding remains from 2183. This approval does not authorize this reviewer to operate the phone or interrupt IPA/data.

What this change does: 2184 makes touch preserve SN3 x=y=16383 when the exact single `sweetmbw=1` token is present. It closes the later-touch overwrite found in 2183 without changing the modem runtime, bandwidth encoder or lifecycle.

## Source and integration

I read `3a-IOS/HANDOFF.md` first, then `sol-2183-audit.md`, and applied ponytail-review and unslop. This was desk review only. I made no production edit, build, phone operation, upload or deletion. Writes were this requested report and a verification script under workspace `tmp/modem-2184-audit/`.

All fifteen public/local modem patches 2170 through 2184 match byte for byte. I reversed them against production entirely in memory. All 88 hunks matched uniquely, and all 21 added files reversed to empty. Reversing 2184 alone restores the exact `baseline2184/sweet_touch_asteroids.c` snapshot. The modem runtime still has the previously audited 2183 SHA.

Production `bsd/dev/sweet_touch_asteroids.c:473` uses the existing `modem_gate` parser from `modem/ModemGate.h`. Exact resource name SN3 and exactly one valid `sweetmbw=1` select maximum x/y. Absent, bare, duplicate and noncanonical numeric values select keepalive 1. Other resource names keep their old behavior. The call at touch power-up uses the exact literal SN3, and command-db lookup still requires a unique, aligned BCM address.

Both clients retain the same `sweet_apps_rsc_mutex`. If modem votes first, later touch writes maximum SN3. If touch votes first, the modem transaction writes maximum SN3 again. MC0 and SH0 already use maximum requests. I found no other SN3-lowering writer in the reviewed production device sources. The new message records floor selection before submission, so acceptance also requires the subsequent touch transaction to report success.

2184 adds no storage API, allocation, hardware mapping, modem command, bandwidth encoding or shutdown change. Existing runtime storage references remain partition reads/size queries, and rmtfs mutations remain in RAM. This statement concerns the modem change, not all kernel or root-filesystem writes.

## Test evidence

I recomposed `bandwidth.c`, `shutdown-joined.c` and `handover-joined.c` in memory from their current generators and verified exact equality with the saved generated units. Extracted modem headers/sources in the host output match production. I did not compile or rerun the suites under the no-build restriction.

The bandwidth unit contains actual touch `at_bcm`, `at_amc`, `at_amc_locked`, TCS helpers/constants and actual modem `m_bandwidth_vote`. Both clients share one register mock, which records the last SN3 APPS request when a transaction triggers. Both startup-order assertions check the effective x/y maximum. Off/bare/duplicate/non-1 gates and another resource retain keepalive behavior. Existing metadata, saturation, fresh completion, partial acknowledgment, uncertain-state retention and TCS-selection assertions remain present. Command-db touch lookup and hardware completion are mocked.

The saved negative unit substitutes the exact pre2184 `at_bcm` function. Its only other change is an assertion wrapper that exits 97 instead of aborting. The supplied regression log identifies line 240, which is the modem-then-touch retained-floor assertion. This is the regression that 2183 lacked.

`logs/start-check-2184.txt` reports PASS for bandwidth, joined handover/RSC/SSCTL/QRTR/RAM-rmtfs/PAS cleanup and the earlier control/lifecycle/storage checks. The supplied full build log includes touch compilation and ends with `done building xnu`. Trash-safe remake execution and exit 0 remain requester-supplied process evidence.

## Frozen artifacts

Verified SHA256 identities:

- Patch 2184 `a817d9de64fd7a0a1e5433339993d1cc3beb60908c138335adfff1b18a8f4a64`.
- Production touch `7b5cf717cbc553a36a5bcfe19b996fc6866b05b091d6b2425734f14f74ad8534`.
- Production runtime `e815266454fbb1407c46159454014728b8aff7a55c07840495d5728108ccb842`.
- Kernel `2a8e152cb1339647e77da6fa2f424b36750d78a5fdd43588545e999508919766`.
- Offline image `f8ad0fe53de62936b66d508445cd43258de43f42fb059532599f30fe58ab9315`.

I independently ran `python3 -B plans/modem/tools/verify-image.py offline-usb-2184`. All ten rebased kernel segments match the current kernel, and the saved payload is embedded in the image. The matching kernel contains the new touch floor message.

The decoded payload command line equals both the 2184 snapshot and the 2183 command line, has unique keys and occupies 586 bytes. It enables mpss/glink/rmtfs/pdm/mstop/mho/mbw at 1, USB4, fbarm1, selftest1, deadman600, panicfb10, thermal3, keep1, astwait1 and fetchsync1. It contains no UIM, BCM survey, online, IPA, TFTP or raw bridge gate. Image/kernel congruence is independently checked. Source-to-build execution relies on the supplied build evidence.

Current hook/body composition exactly equals `out/offline-script-2184.sh`, SHA256 `91c62eb604157692ffa8b87fbe93cf652d7ba4232809d28b6fdedeb5834295a9`. Its private completion nonce equals `out/modem-offline-nonce.txt`. Collision-safe archives, checked screenshot and fifteen-second DONE flush remain present. I have not published the nonce.

The runner still names 2182. Before launch, the operator must change only its image/note to exact 2184, retain `--no-replay`, and perform the external shared-hold/process checks. The runner's wants marker alone is not an atomic phone reservation. `check-run.py` also does not enforce every bandwidth/handover/SSCTL condition below, so its PASS alone cannot establish M2.

## Launch and acceptance conditions

1. Coordinate release from IPA/data, acquire the shared phone hold, and recheck no active phone job plus actual recovery and clean root immediately before launch. Recheck frozen hashes, arguments and script/nonce. Use one no-replay RAM boot with the existing bounded selftest and 600-second deadman. Do not queue an unchanged repeat.
2. Require bandwidth `error=0 pinned=1 commands=7`, successful PAS start, ready/handover, MSS release, GLINK, QRTR services including UIM advertisement only, pd-mapper and RAM-rmtfs progress. Require the later touch SN3 maximum message and successful SN3 transaction, with no subsequent lowering request. Missing touch evidence leaves this experiment incomplete.
3. Require accepted SSCTL response and fresh acknowledgment, actual PAS shutdown `status=0 result=0`, `cleanup=ok`, `transport_failed=0`, `fatal=0` and worker `final_error=0`.
4. Require fresh DONE, matching nonce, checked frame, clean root before and after, `stale=false`, and no FAIL/SKIP, fired deadman, panic or transport failure.
5. Stop after any failure or missing measurement for evidence review. M2 remains incomplete until every required result is measured. No automatic UIM progression is approved.

## Limits and prohibitions

2183 was never booted. The last actual modem run is ast158 on 2182, with PAS failure, retained resources, normal DONE at about 364 seconds and clean root. It supplies no successful-shutdown measurement. The reported ast159 IPA read-only PASS and clean recovery are separate evidence, and do not establish modem M2 or current phone availability.

Polling IRQ_STATUS while its delivery bit is masked remains a hardware unknown. The host mock cannot prove latching. Missing fresh completion fails closed before firmware load/PAS initialization and retains uncertain backing/TCS state. Do not weaken this admission condition to obtain a passing run. The bandwidth hypothesis may still fail to explain the vendor PAS error.

No partition writes, modemst/fsg/fsc/persist/modem_a modifications, stock boot, flash, plain reboot, online mode, UIM requests, IPA startup, TFTP or raw QMI bridge are authorized. Keep firmware, raw logs and identifiers private. Any changed source/kernel/image or broader integration requires another review.

Verdict: GO for the one guarded offline 2184 experiment only. Hardware completion, retained bandwidth and successful PAS shutdown remain unmeasured.
