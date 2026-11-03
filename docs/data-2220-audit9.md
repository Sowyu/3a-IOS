# Patch 2220 ninth audit

Reviewed on 2026-11-03. Conditional bounded-trial source GO carries over to this revision for a second integrated ONLINE experiment. I found no new blocker within the earlier supervised runtime and cleanup conditions. The author's account overstates acceptance checking and local termination on lost replies, as detailed below.

What this change does: after a valid QMI refusal of the first automatic ONLINE request, the session attempts LOW_POWER once and may attempt ONLINE once more. It preserves the numeric refusal code and adds the hardware-restricted value to operating-mode logs.

This report explicitly accepts the additional automatic LOW_POWER request within that bounded source configuration. It does not authorize a phone boot or approve a replacement image. The single-use approval for the exact 2218 image in plans/modem/sol-2218-data-image-v2.md cannot authorize a second boot with changed bytes.

## Revision and scope

The reviewed 2220 SHA-256 is eac1425092dc73f9463efd1bfb7cf60b8e63bc317da1d4cc98ca64ba0ac3469d. The baseline at 300c86c hashes to f331702b1bf1f5583a43bef11f76786f0da0000a543fbc987c2bf174af04359c. HEAD was 79d5dec7ca1949967ebdc2d3b0b4b0da0bf3b1af.

I reviewed git diff 300c86c HEAD -- patches/data tools/data docs/cellular-data.md, audits 4 through 8, the 2221 re-audit, the 2218 image review, the connected session send/receive/timeout/readiness/cleanup code, the raw gate and the private live log. The delta changes only CellSession.c, CellSession.h, the host data test and the cellular-data description. All fourteen added patch source/header files match plans/data/src byte for byte.

2221 remains byte-identical to the baseline, with SHA-256 4bdc74ce4ad45000ba2efe18bf45454169dcb6464483d5722c918892875e1e49. Source references below are relative to plans/data/src. I used the ponytail-review and unslop skills.

## Trial evidence and QMI meaning

The private log confirms completed IPA handshake and active endpoints, selected-USIM readiness, DPM, WDA, CS_ONLINE, mode 3 with logged offline reason 0, ONLINE refusal and terminal session failure. The failure occurs before registration and WDS call setup. It supports a modem operating-state obstacle, rather than an APN, WDS or packet-path diagnosis.

Mode 3 is OFFLINE, with RF deactivated and partial shutdown. Mode 1 is ordinary temporary RF low power. These definitions come from the upstream [libqmi DMS enums](https://raw.githubusercontent.com/linux-mobile-broadband/libqmi/main/src/libqmi-glib/qmi-enums-dms.h).

The likely interpretation is that firmware remained in an offline startup or policy state despite host-side USIM and IPA readiness. The available evidence cannot identify its cause. Offline reason 0 does not prove normal firmware initialization, valid provisioning or absence of restrictions. QmiData.c:163-174 zero-initializes the optional offline-reason field, so the old log cannot even distinguish an absent reason TLV from an explicit zero bitmap.

The first trial did not retain the QMI refusal code. It therefore did not establish error 52, error 60 or any other specific cause. In upstream [libqmi error definitions](https://raw.githubusercontent.com/linux-mobile-broadband/libqmi/main/src/libqmi-glib/qmi-errors.h), decimal 52 is DeviceNotReady and decimal 60 is InvalidTransition. The new test's comment calling 0x34 an invalid transition is incorrect. Its numeric refusal fixture still exercises the intended branch.

HakiOS hakios.mpss.c:5311-5322 really tries ONLINE, then LOW_POWER and ONLINE only after error 52, with a two-second delay. That code and its comment are precedent for an experiment, not proof of a successful OFFLINE escape on this modem. This patch deliberately broadens the trigger to every valid positive QMI error and replaces the fixed delay with mode reads.

I could not establish a universal Qualcomm guarantee permitting OFFLINE to LOW_POWER. A firsthand [ModemManager development report](https://mail.gnome.org/archives/networkmanager-list/2012-July/msg00194.html) documents an MC7710 that required reset to leave OFFLINE and rejected other transitions. That establishes a real reset-only case, not a rule covering this firmware.

Treat LOW_POWER as an unproven transition here. If this firmware permits it, readback can support the next ONLINE attempt. If this firmware requires reset, a valid LOW_POWER refusal will log its code and end the session. That outcome is acceptable for the trial. Do not add automatic reset, OFFLINE, persistent low power, factory-test or provisioning requests to overcome it.

## Correctness and write bounds

CellSession.c:325-351 checks the existing ONLINE gate before automatic mode writes. The first ONLINE charges online_sent before sending. The refusal handler at lines 535-553 sets lowpower_sent before its one LOW_POWER attempt. A subsequent ONLINE charges online_sent from 1 to 2.

These fields are zeroed only at session initialization and are not reset by polls or replies. At most two ONLINE attempts and one LOW_POWER attempt can occur in one session. Transport refusal consumes the attempt too. No response, duplicate response, retry poll or mode oscillation creates another LOW_POWER or third ONLINE.

The complete possible write sequence is a prefix of 0,1,0. Without a positive QMI refusal of the first ONLINE, it is at most the single initial 0. The shared builder at QmiData.c:124-126 additionally refuses every mode other than 0 and 1. Raw admission is unchanged and is separate from this session budget.

A valid first ONLINE refusal logs the numeric error and cached starting mode, emits a recoverable CE_ERROR and attempts LOW_POWER. CE_ERROR alone does not latch session failure. A valid LOW_POWER refusal, a valid second ONLINE refusal or a matched malformed Set Operating Mode response calls fail and enters terminal cleanup.

A successful Set response invalidates the cached mode and requests readback. A valid mode 0 read advances to CS_REGISTER. After LOW_POWER, a valid mode 1 read permits ONLINE2. If readback is already mode 0, registration proceeds directly and no second ONLINE is needed.

The attempt count is bounded independently of response delivery. Completion time is not bounded by this ONLINE branch in every case. The owner's finite runtime, independent deadman and terminal stop remain required.

## Non-blocking findings for this bounded trial

1. A lost LOW_POWER reply can bypass the claimed acceptance/readback condition. In CellSession.c:334 and :543-545, lowpower_sent means attempted, not accepted. Start in mode 1, refuse ONLINE1, lose the LOW_POWER reply, then poll after its five-second timeout. The pending entry disappears and the cached initial mode 1 immediately permits ONLINE2 without a successful LOW_POWER reply or a fresh mode read. I reproduced this with the real patch-extracted session. The smallest correction is to distinguish LOW_POWER acceptance from its attempt and fail on its unanswered timeout, or explicitly require fresh readback before ONLINE2. This does not exceed the approved 0,1,0 write budget, so it does not block this supervised trial, but the author's exact sequencing claim is false.

2. Lost Set replies do not universally fail the session. CellSession.c:652-661 retires expired requests with a timeout event and then advances. Lost ONLINE1 does not trigger LOW_POWER. It leaves the session reading mode until online, owner stop or transaction exhaustion. Lost ONLINE2 can likewise leave repeated successful nonzero mode reads running until the owner stops. Successful Set acceptance followed by indefinitely nonzero readback has the same existing limitation. I reproduced both lost-ONLINE waits through 66 seconds with no extra mode writes. Add an ONLINE-stage deadline or explicit Set-timeout failure before claiming local termination in all cases. This limitation is acceptable only with the inherited runtime bound.

3. The LOW_POWER read count and cadence differ from the prose. CellSession.c:551-553 sets next_poll to zero after acceptance. The immediate read returning mode 3 then schedules another read immediately through lines 340-342, followed by two more scheduled reads at five-second intervals. There are an initial read and up to three additional read cycles, with the first additional read having no five-second delay. I reproduced four readbacks and terminal failure with no ONLINE2. Each unanswered or unusable read cycle can itself consume up to three Get attempts. Set next_poll after the initial read if five-second spacing is required, and describe the total accurately. The current count is finite and the extra immediate request is read-only.

Malformed handling also needs precise wording. A matched malformed Set reply fails immediately with parser return -1, which is not a modem QMI error code. Malformed or refused Get replies are retried through the existing three-attempt budget and do not retain their QMI error in the final generic failure. Replies too short to match a transaction become timeouts instead. The new hardware-restricted log is present, but the decoder defaults that optional field to zero when absent, so zero is not affirmative evidence that firmware supplied an unrestricted flag.

The new maintained session_online test passes its success, second-ONLINE refusal, LOW_POWER refusal and stuck-OFFLINE cases. Its final modes-only assertion checks only the last case's collected sequence. The builder allow list and exact success sequence provide additional protection, but this test does not cover timeout, transport refusal, malformed reply or readiness loss for the new path.

## Readiness, stopping and inherited guarantees

The new receive-time LOW_POWER send does not bypass the existing terminal barrier under the audited worker contract. sweet_data.c:397 synchronizes current readiness/services before routing the reply. cell_session_receive drops non-WDS processing when ending at CellSession.c:592. Readiness or required-service loss enters terminal cleanup before the refusal callback can execute.

The independent central issue guard at CellSession.c:114 permits only WDS Stop Network while ending. Transaction exhaustion can change the state during the new SEND, but its transaction allocation returns zero and no mode write escapes. Positive readiness is still accepted only in WAIT_READY, so late evidence and replies cannot restart setup.

I tested readiness revocation and explicit stop while ONLINE1 was outstanding, then delivered its refusal and restored readiness. Neither case sent LOW_POWER or revived the session. The same receive/send guards cover LOW_POWER and ONLINE2 outstanding at revocation. An already transmitted request can still take effect after stop. Cleanup confirms WDS ownership, not restoration of DMS operating mode, so modem shutdown and retention-on-uncertainty remain essential.

The delta preserves the earlier transaction reserve, singleton result validation, late WDS start adoption, unknown-start ownership, two-call cleanup and permanently latched unresolved outcome. It does not change APN/authentication validation, request-only carrier settings, IPv4-before-IPv6 Start Network, DataServices revoke-sync-grant ordering, per-control-packet discovery, QRTR slot use, once-per-boot attachment, QMAP limits, callback lifetime or the 2221 adapter interfaces.

The earlier raw-export/free-form-text restrictions and settings-parser limitations remain. No provisioning, profile modification, partition write, new raw schema or DMS reset is added. The 2221 re-audit's worker ordering, IPA revocation, AP-channel quiescence and shutdown requirements remain applicable. I did not re-audit changed modem or IPA implementation outside this delta.

## Approval conditions and validation

The owner rule requires GO for any boot sending state-changing QMI. LOW_POWER is state-changing even though it disables RF. This report supplies conditional data-source GO for that new automatic request under sweetdataonline=1 and the maximum 0,1,0 attempt sequence. The earlier modes-0-and-1 condition is satisfied, but that condition alone never constituted boot approval.

Before the second integrated trial, record this exact replacement hash and report in the relevant cutoff, source snapshot and new image admission. Reproduce and review the actual integrated image and its arguments. The legacy plans/ipa/cutoff-2206.txt still names audit7's 410ee41d revision, so it cannot establish reproduction of these new bytes. Do not reuse the consumed 2218 one-trial approval.

Keep the guarded 600-second data runtime, exact independent sweetdeadman=900, required gates and harness deadline below 20 minutes unless separately reviewed owner limits are stricter. Keep active supervision, one successful attach per boot, eight QRTR local slots, serialized fresh selected-USIM and IPA evidence, per-control discovery, immutable callbacks, QMAP v1 limits, bounded WDS cleanup with failure propagation, IPA quiescence before detach, conservative retained backing and RAM-only EFS. This review grants no runtime extension.

sh 3a-IOS/tools/data/check.sh passed freshly against patch-extracted sources with warnings as errors, AddressSanitizer and UndefinedBehaviorSanitizer. The data, service-cache and IPA adapter suites passed. The supplemental tools/data/tests/out/audit9-probe.c and its sanitized executable also passed, rerunning the maintained data suite and exercising the lost-reply, read-cadence, malformed Set, readiness and stopping cases described above.

I edited no implementation file, built no kernel and did not touch the phone. Writes were limited to this report and host artifacts under tools/data/tests/out. No files were removed. Host models do not prove kernel concurrency, actual firmware transitions, physical DMA cessation, successful connectivity or a final integrated image.

Verdict: conditional bounded-trial source GO for the exact new 2220 hash. The bounded write sequence is safe within the retained owner conditions. Acceptance checking and timeout termination have the stated limitations. A fresh, separately approved integrated image and second-trial admission are still required before boot.
