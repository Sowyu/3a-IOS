# 2187 offline UIM readiness audit

2026-11-03. NO-GO for the exact 2187 candidate. Fix findings 1 and 2, add the missing regressions, and obtain a fresh review of rebuilt artifacts before booting changed modem/rmtfs code. This review grants no phone operation or progression to data.

What this change does: 2187 expands local QRTR registrations from four to eight and adds selected-primary-GW USIM readiness reporting. Exact `sweetmready=1 sweetmuim=1` polls read-only GetCardStatus every five seconds during the unchanged 120-second worker.

## Scope and method

I read `3a-IOS/HANDOFF.md` first and applied ponytail-review and unslop. I reviewed the complete production UIM parser, sender/receiver, QRTR/QMI dispatch, registrations, worker gates, shutdown/retention, RAM-rmtfs, frozen script, runner, prior audits and saved tests. Expected use is one offline diagnostic owned by the existing single modem worker.

I used no delegates and performed no phone operations, SSH, builds, compilation, source edits, deletion or uploads. This report is the only file written. Read-only Python checks used `-B`. Daily/USBv5 qualification remains the inherited bounded development-session scope in HANDOFF and earlier audits, with its existing limitations.

## Must fix

1. A rejected current response leaves its transaction reusable. `ios-asteroids/research/xnu-modem/dxb/xnu/bsd/dev/modem/ModemRuntime.inc:284-298` clears `uim_pending` only after successful parsing. Queue transaction 3, receive its QMI error or malformed body, then receive a valid state7 response with the same transaction before the next poll. The first response revokes readiness, but leaves pending=true, so the second response is accepted and restores readiness. This contradicts the required one-consumption rule and allows an already rejected transaction to establish a fresh ready observation. The slot-status branch similarly leaves rejected matching transactions pending. Consume the matching response transaction before its parser result can return, keep readiness false on rejection, and require a newly queued transaction before success can restore it. Add error-then-success and malformed-then-success same-transaction regressions for both opcodes. Also cover a five/six-byte current reply: its transaction/message bytes are available, but the unconditional `len<7` return currently leaves existing readiness untouched.

2. Endpoint removal/change does not promptly invalidate readiness or reply admission. `ModemRuntime.inc:311-315` checks discovery only after the next-send deadline, while `m_uim_receive:283-289` checks only the cached source node/port. For example, accept readiness at t=0, remove the UIM server at t=1, and poll before t=5. The deadline return preserves readiness despite the absent endpoint. A delayed matching reply from the cached endpoint can also establish readiness after QRTR has processed DEL_SERVER/BYE, including later in the same receive batch. The saved removal test advances beyond the send deadline and misses this case. Validate endpoint continuity independently of request cadence and before admitting a readiness-producing reply. Revoke readiness and invalidate outstanding transactions on loss/change, and test removal/change before the next deadline plus a delayed reply after removal. This is observable diagnostic-state correctness even though no data hook currently consumes it.

These are source traces, not newly compiled reproductions. The existing saved tests confirm only successful-response duplicate consumption and endpoint removal checked after its deadline. No memory-safety defect or added persistent-write path was found in these findings.

## Verified behavior and limits

The selected index is decoded into card high byte and application low byte. The helper requires an in-range selected card in PRESENT state, an in-range selected type2 USIM and appstate7. Index ffff, unselected ready apps, absent/error cards and detected state1 cannot establish readiness. The HakiOS provisioning hypothesis supplies no authority to mutate provisioning or upgrade state1.

QMI validates response type, exact transaction/message, packet/TLV extents, a unique consistent standard result and a unique status body. Card/app counts, AIDs and variable fields are bounded, with exact body consumption. Slot ICCID decoding exports only suffix4 and skips AIDs. Returned identifiers still pass through private transport RAM. No raw dump or full-identifier output was added.

Only empty seven-byte service11 requests 0x2f and 0x47 are sent. Card-status monitoring stops at 180 successfully queued requests, with separate slot-status maximum three. The total absolute ceiling is therefore 183 requests, not 180 across both opcodes. The current 120-second worker cannot reach that card-query cap. Slot polling stops on its first valid success. Successful responses consume pending once, old transaction IDs are ignored, and the ordinary poll path expires readiness after 15 seconds without accepted status. Findings above qualify the stronger fail-closed claims.

QRTR retains its service-zero client behavior. Only nonzero services announce, including HELLO replay. The larger static table admits eight callbacks and refuses the ninth. The actual diagnostic still registers only rmtfs, pd-mapper, SSCTL and UIM. No IPA/data hooks, online-mode commands, PIN/APDU operations, slot switching or provisioning are present in this runtime.

The exact-single-token gate rejects missing, duplicate, bare, malformed and non1 values. Readiness requires UIM, and UIM requires GLINK before hardware setup. With the modem/probe gates absent the worker is not started. With readiness off the existing bounded UIM snapshot behavior remains.

Every path after modem activity reaches cleanup. Owed PAS shutdown is attempted even after start/init failure. Failed PAS returns before ownership reclaim, QMP-off, remaining power/vote release, backing unmap or RAM-shadow free. Firmware, metadata, arguments and rmtfs backing remain allocated until reset. The ready/handover MSS proxy release is the previously measured power sequence. Firmware and initial shadows come from read-only UFS operations, while rmtfs mutations copy between RAM buffers only.

Known PAS failure alone does not identify an additional retention safeguard missing for these status reads. The earlier 2185 diagnostic exception remains a reasonable narrow scope precedent. It does not waive the new readiness correctness requirements or prove secure stop.

## Evidence checked independently

All 18 patch pairs 2170 through 2187 match between `plans/modem/patches` and `3a-IOS/patches/modem`. I reconstructed all 20 modem-directory files touched by that series in memory and matched production and available saved host-test inputs. The saved UIM runtime unit exactly composes the current test prefix, actual production receiver/poller and current test tail.

`logs/uim-check-2187.txt` records parser/selected-index, actual runtime and eight-port PASS results. `logs/start-check-2187.txt` records strict gates, read-only storage, lifecycle, joined SSCTL/QRTR/RAM-rmtfs/PAS, handover and bandwidth PASS results. The test scripts use ASAN/UBSAN. I inspected these records and assertions without rerunning or compiling tests. They do not cover findings 1 and 2.

The saved full build log includes completed installation. Trash-safe remake and exit0 are supplied process evidence, not a build I reran. I independently ran `python3 -B plans/modem/tools/verify-image.py uim-usb-2187`, which matched all ten rebased segments and embedded payload.

Frozen SHA256 identities:

- Patch 2187 `10e7398fe5f02bc7739a86db9fb305c9dc4dc17d006f7c9251c2022fdfe53465`.
- Runtime `f602d10ddb0173486794bdc6a883c933020ab7be376a3c2cd5031326b8c3c5be`.
- Kernel `dfdd2e118d4b8f3da51ce5a8a6089b8b442372469c9424512fce3b3c5e860802`.
- Image `27fa830063c01899ab0cf082da895342a51043d14df8f56c30391c42dda3cb51`.
- Script `1a92bb31a3d4514c46013c2844ca682c8a44d49e3394a3a74dfd4dfae245fe88`.

The embedded command line equals the saved arguments and has unique keys. I measure 601 bytes before NUL, rather than the supplied 600, within the 607-byte limit. It enables mpss/glink/rmtfs/pdm/mstop/mho/muim/mready, USB4, fbarm1, selftest1, deadman600, panicfb10, thermal3, keep/astwait and fetchsync. Bandwidth/IRQ, data, IPA, ONLINE, raw-QMI and TFTP gates are absent.

The frozen script exactly matches the current hook/body composition with its private nonce. It preserves collision-safe frame archives, requires a nonempty screenshot, waits 240 seconds and flushes DONE for 15 seconds. That wait alone does not prove worker completion.

I checked the actual ast161/162 local result and console records. Ast161 returned normally in 363 seconds with clean root before/after, stale=false and one frame. It measured detected USIM state1 and ICCID suffix0660. SSCTL acceptance at line11784 precedes fifty raw -3145800 PAS failures, retention at11847, final_error=5 at11848 and DONE at12577.

Ast162 returned normally in 362 seconds with clean root before/after and stale=false. IRQ37 was disabled/inactive at7766, all seven bandwidth commands succeeded at7767, and touch preserved maximum SN3 at10608. SSCTL acceptance at11711 precedes fifty identical PAS failures, retention at11778, final_error=5 at11779 and DONE at12623. Completion reporting worked, while PAS remained unresolved. These records supply no modem-caused-reset evidence or M2 success.

## Conditions for any later reviewed diagnostic

The current runner still names 2185 and lacks the requested data-reservation wait and modem-hold acquisition. Those are outstanding launch prerequisites, not edits made by this review. No launch is authorized by this report.

1. After a fresh GO, wait until both `/tmp/phone-data-wants` and `/tmp/phone.lock` are absent, acquire the coordinated modem hold, wait for active phone-job PIDs, and require actual clean recovery with no job immediately before launch. Respect the DATA exception without direct phone/SSH access during its reservation. Use only `dev.py --no-replay`, the newly reviewed exact artifact and one bounded run.
2. Require authentication, ready/handover, MSS release, healthy GLINK/QRTR, pd-mapper and RAM-rmtfs progress. Report selected primary-GW index and accepted status replies without upgrading detected/none/incomplete results to readiness. Readiness requires a fresh correctly selected present type2 USIM response at state7.
3. Inspect the entire shutdown window. Require accepted SSCTL, the known PAS-failure sequence, explicit retained backing/power and final_error=5 before fresh normal DONE. No subsequent reclaim, QMP-off, power drop or backing release is acceptable while PAS remains unverified. Unexpected failure behavior or transport fatality stops progression.
4. Require matching fresh nonce, checked frame, stale=false and clean root before/after. Reject panic, fired deadman, reset-unknown, FAIL/SKIP or missing worker completion. Keep the strict run checker strict and report retained-stop diagnostic acceptance separately.
5. Keep M2 blocked. No ONLINE, IPA/data, provisioning, calls/SMS, raw QMI, extended session, partition write, flash or slot operation follows from this review. Keep firmware, raw logs and identifiers private. Any changed modem/rmtfs code or image needs GO before boot.

Verdict: fix 1 and 2 first. Exact 2187 is NO-GO for the requested readiness diagnostic. No phone run was performed.
