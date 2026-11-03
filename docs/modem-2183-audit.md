# 2183 guarded offline modem review

2026-11-03. NO-GO for the requested exact 2170 through 2183 offline experiment. One blocking integration finding: touch can replace the new SN3 maximum request with a keepalive request before PAS shutdown. Fix finding 1 and re-audit the resulting source and image before the one run. No phone or build action was taken.

What this change does: default-off `sweetmbw=1` submits the seven measured crypto-to-DDR BCM requests before PAS initialization. It intends to retain those shared requests throughout the bounded offline modem lifecycle.

## Must fix

1. Touch can overwrite the supposedly pinned SN3 floor.

Production `bsd/dev/modem/ModemRuntime.inc:489–491` votes bandwidth once before loading/PAS initialization. Production `bsd/dev/sweet_touch_asteroids.c:469–473` writes SN3 with x=y=1, and `at_power:982` calls it during the independently delayed touch startup. Both paths use the same APPS RSC and resource address `0x5001c`. Their shared mutex serializes transactions but does not combine their requested floors. A later write replaces this APPS request. Hardware taking the maximum across voters does not combine successive requests from this same voter.

This ordering occurs in the actual baseline boot. `ios-asteroids/logs/ast158-modem-offline-2182.txt` records modem ARC vote at 5050, PAS initialization at 5118, successful handover release at 7173, then touch SN3 at 9474, and PAS shutdown starting at 10637. The touch worker defaults to 45 seconds at `sweet_touch_asteroids.c:1111`, while the modem runtime waits 30 seconds. The 2183 diff adds no coordination with touch and never refreshes the bandwidth floor. With the same ordering, SN3 falls from x=y=0x3fff to x=y=1 while `bandwidth_pinned` stays true. Touch's MC0/SH0 writes use maximum values and do not have this problem.

The smallest bounded fix must keep touch's SN3 request at least the modem floor whenever `sweetmbw=1`, including if touch executes after the bandwidth transaction. A narrowly gated maximum SN3 touch request is one possible fix. A fixed delay alone does not prove ordering. Add a host regression that performs the bandwidth transaction, then the actual touch SN3 write, and checks that the effective SN3 APPS request still satisfies the floor before shutdown. No such cross-client test exists in the submitted suite.

Skipping this fix invalidates the claimed retained crypto-to-DDR shutdown experiment. A failed PAS shutdown would not test the full vendor bandwidth hypothesis. This is a concrete integration and hypothesis-efficacy blocker. I found no evidence that this write introduces a new permanent-storage hazard.

## Source and artifact congruence

I read `3a-IOS/HANDOFF.md` first and applied ponytail-review and unslop. This was read-only desk review except for this report. No compilation, suite execution, production edits, phone operation, upload or deletion occurred.

I reversed all fourteen modem patches 2170 through 2183 against production in memory. All 86 hunks matched uniquely and all 21 added files reversed to empty. Public and local numbered patch copies match byte for byte. Reversing 2183 alone reproduces `baseline2183/ModemRuntime.inc`, whose SHA256 is the exact previously audited 2182 runtime, `8e9c0407fdeb4bc0dbfcdf6ea5f01f04c81872214ed740265decc066d29be039`.

Checked SHA256 identities:

- Patch 2183 `a2e375701ea956e92ab4516c6f26cf67f53e67e0c684b56efe4014b17f72a9dc`.
- Production runtime `e815266454fbb1407c46159454014728b8aff7a55c07840495d5728108ccb842`.
- Kernel `91bd6d09d3e4d5c182fafc1e136640eb7c3720b0e44db5ec32602c80ce8cfece`.
- Offline image `a757bbc095332e2861be01c275276f87221db719d0f7cecb3411a0ba0d3f0147`.
- Composed script `3d752b3363f792e9b08e260f02814f92a3ac719c3c61bacb8decb3dd4b80af71`.

I independently ran the read-only `python3 -B plans/modem/tools/verify-image.py offline-usb-2183`. All ten rebased kernel segments match the current kernel and the saved payload is embedded in the image. Actual payload arguments equal the snapshot, have unique keys and occupy 586 bytes. All seven modem gates are 1, USB4, fbarm1, selftest1, deadman600, panicfb10, thermal3, keep, astwait and fetchsync are present. UIM, BCM survey, IPA and sweetgpufast gates are absent.

Current hook/body composition equals the saved script and its private nonce matches the snapshot. Collision-safe archives, checked screenshot and fifteen-second DONE flush are present. The runner still names 2182 as disclosed. It must be updated to the newly reviewed exact image and note only after a later GO, preserving no-replay, recovery/clean-root, process and shared-hold guards.

`plans/modem/src/ModemBcm.h` is the discarded older draft with `vcd>=10` rejection. It is not the production header, public patch or extracted host header. Those three carry the exact measured map and explicitly accept ACV VCD12. Do not build or regenerate from the discarded draft.

The supplied full build log ends with `done building xnu`; exit 0 and Trash-safe remake execution remain requester-supplied process evidence. The supplied start log has bandwidth and joined lifecycle PASS records plus the read-only storage assertion. I recomposed bandwidth.c, shutdown-joined.c and handover-joined.c entirely in memory and verified exact equality with the saved generated translation units. Their extracted runtime, ModemBcm and QcomRpmh sources match production. I did not rerun or compile these suites.

## Other review findings and limits

The local vendor sources at Nothing commit `d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7` justify the bandwidth hypothesis. `qcom_q6v5_pas.c:1124` enables bandwidth immediately before shutdown, and the helper at 321 uses UINT_MAX average and peak. Volcano, icc-rpmh and bcm-voter support the selected path, node widths/channels, scaling, rate saturation and enable masks. This does not decode SCM error -3145800 or prove bandwidth is its cause.

The production encoder validates all seven addresses, slave 5, auxiliary version 0x1000, exact eight-byte metadata, units, widths, VCDs and reserved byte before any BCM write. The ast157 records at 6659 through 6672 match. Rate arithmetic uses uint64_t with sufficient range and requires both fields to saturate before sending maximum x/y. SH1 uses enable bit 1 and ACV preserves both documented enable/performance bits with value 3. IP0 is excluded. Raising ACV performance and holding saturated requests has a power/thermal cost, limited here by thermal3 and the bounded reset.

Sorted order is MC0, SH1, SH0, SN3, SN0, CE0, ACV. Commit and wait apply at group ends, yielding wait mask 0x75 and enable mask 0x7f. Vendor bcm-voter groups by VCD and sets commit/wait at group ends. Vendor synchronous RSC requests ask for a response on every command, matching 0x10108 here even for commands without a wait bit. VCD12 is metadata, not a TCS slot index.

The transaction requires hardware capacity 7 through 16, completely idle active TCS state and clear completion IRQ before selection. It holds the same touch mutex, disables only its owned IRQ-enable bit, and requires fresh completion status, idle and all seven issued/complete command statuses within 100 ms. Success clears only owned TCS/IRQ state and restores the owned enable bit. Timeout leaves the TCS and disabled owned IRQ bit intact, marks uncertain/failed, and refuses another bandwidth submission.

The completion assumption remains unproven on hardware: the vendor source normally enables the interrupt for synchronous requests, while this polling transaction masks delivery and expects IRQ_STATUS to latch anyway. The mock synthesizes that status and cannot prove mask-independent latching. If it does not latch, the code fails closed before m_load/PAS initialization, retains the uncertain TCS and backing, and reports failure. Do not weaken fresh-completion admission to make that case pass. This limitation alone is acceptable for one guarded diagnostic run after finding 1 is fixed.

Cleanup retains buffers when PAS cannot be verified and retains uncertain backing even if PAS is unnecessary or later succeeds. No new mapping, storage write, IP0 request, IPA start or UIM transaction is introduced. The actual joined handover/RSC/SSCTL/QRTR/RAM-rmtfs/PAS tests now close the previous audit's joined handover gap, including success without a second MSS vote, timeout retention and ENODEV. They do not cover touch overriding BCM floors or prove physical hardware completion.

Ast158 confirms MSS release, successful SSCTL response/fresh acknowledgment, fifty failed PAS calls, retention, final_error=5 and fresh DONE at 11443. Its clean-root result does not establish successful modem shutdown. M2 remains incomplete.

## Conditions after correction and re-audit

1. Freeze the corrected source, kernel, image, actual arguments and fresh script/nonce, then acquire the shared hold and recheck no active phone job, recovery and clean root immediately before one no-replay RAM boot under twenty minutes.
2. Require bandwidth error=0/pinned=1 with no later lower shared request, successful PAS start, ready plus handover and MSS release, GLINK, QRTR NEW_SERVER, UIM advertisement only, RAM-rmtfs progress and accepted SSCTL shutdown.
3. Require actual PAS shutdown status=0/result=0, verified cleanup=ok, transport_failed=0, fatal=0 and final_error=0.
4. Require fresh DONE/nonce/frame, root clean before and after, stale=false and no FAIL/SKIP, fired deadman, panic or transport failure.
5. Stop after any failure for evidence review, with no unchanged repeat or automatic UIM progression.

Safety judgment: the new bandwidth transaction's fail-closed and retention behavior supports a future bounded offline trial after the shared-floor defect is fixed. Efficacy judgment: the exact submitted image does not guarantee the requested floor through shutdown, so the requested run is NO-GO. No stock boot, flash, plain reboot, partition write, online mode, UIM, TFTP, IPA or raw QMI bridge is approved. Keep firmware, raw logs and identifiers private.
