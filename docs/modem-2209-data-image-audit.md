# Independent review of the exact 2209 ONLINE data image

Reviewed 2026-11-03. Verdict: NO-GO for the current integration dependency set. The mandatory SMMU-only prerequisite has a concrete acceptance-checker defect. No new blocking defect was found in the frozen combined kernel delta or data runner, but this report grants no launch approval and no active conditional image GO while that dependency remains defective. Keep `integration-2209/manifest.json` at `approved=false`.

What this change does: IPA teardown parks verified private context banks instead of replaying UNKNOWN reset fields. Data audit8 adds request-only authentication and delays IPv6 Start Network until IPv4 has usable settings.

## Blocking finding

1. The required S trial cannot pass its actual success checker.

`plans/ipa/tools/check-smmu-result.py:9` rejects any `SWEET IPA: PAS15` text. Successful cleanup at integrated `bsd/dev/modem/IpaRuntime.inc:184` emits `SWEET IPA: PAS15 not started; DMA guards intact; SMMU parked; backing released`. The checker's line 21 requires that cleanup evidence, making its success conditions contradictory.

I independently reproduced the failure with the actual checker copied to `/tmp/sol2209-zwai9ga5/checker`. The synthetic capture supplies the correct S image name, clean root before and after, fresh script nonce, DONE, matched write traces, setup completion and final_error=0. It exits 1 at line 9. The fixture and rejection are retained there. This confirms the finding in `plans/ipa/sol-2209-audit.md`, whose source GO and supervised C GO do not lift its explicit S NO-GO.

The smallest correction is to reject actual PAS15 command records while allowing the explicit not-started cleanup line. The IPA owner must update the live/frozen checker and dependency pins, add positive and forbidden-PAS/incomplete-write fixtures, and obtain the required independent re-review. I made no correction. Consuming a one-shot S attempt before this is resolved would leave even a successful hardware capture unable to meet the required acceptance gate.

## Scope and frozen identity

I read `3a-IOS/HANDOFF.md` first, then `plans/modem/README.md` and `plans/modem/sol-2208-data-image.md`. I used ponytail-review and unslop, reviewed the additive 2209 patch and connected production callers, data audit8, the separate new IPA report, runner, proof procedure and imported root helpers. Source references below are under `ios-asteroids/research/xnu-modem/dxb/xnu`.

All 77 manifest artifact hashes, 56 relative source hashes and 32 cutoff patch hashes matched freshly. The saved kernel also matches the installed kernel. Fresh ordered reconstruction with explicit cutoff2209 reproduced all 56 production files exactly. Its log is `/tmp/sol2209-reproduce.txt`, with reconstruction output retained under `plans/modem/series-check/1793695992422585000`.

The frozen identities are:

- Kernel SHA256 `bfa56dd9f1198ff099ebe7a62df26d9d5ae8897498687c3298e39b74b400738b`.
- Image SHA256 `f2360ae9a17f538a3452f48c5301809647bf4299942e2c73533c9a2d66120a50`.
- IPA2209 SHA256 `888ede093e7445a701cb6a6d8f05a2a923f4d859c819cadf26a4ad6f893ac87b`.
- Data2220 audit8 SHA256 `f331702b1bf1f5583a43bef11f76786f0da0000a543fbc987c2bf174af04359c`.
- Data2221 SHA256 `4bdc74ce4ad45000ba2efe18bf45454169dcb6464483d5722c918892875e1e49`.

`verify-image.py data-usb-2209` freshly passed all ten rebased kernel segments. Additional checks verified unique payload containment, segment descriptor bounds, exact 607-byte arguments and unique argument keys. The supplied build log ends with `done building xnu`. I did not rebuild the kernel.

## Kernel integration findings

`IpaMmu.h:94-122` verifies disabled control, private CBAR/CBA2R identity and explicit TCR/TCR2 before translation enable and selector publication. TCR2 includes both observed TBI bits with exact readback. The implementation does not hide their difference with a blind mask. Distinct nonzero VMIDs, non-global descriptors, the fixed 36-bit geometry guard and shared allocator claim remain in place.

`IpaRuntime.inc:110-112` marks every touched installation failure retained. Cleanup checks retention before release. `ipa_mmu_release` also rejects partial configuration before any repair write. Partial or unproven configuration therefore keeps all backing until reset.

For verified owned contexts, release checks mode and identity, routes the owned published SID to fault and verifies it, disables and verifies the bank, performs private invalidation and synchronization, zeros both TTBRs and verifies the known format. It invalidates and reads back the owned SMR before restoring the dormant S2CR. It leaves the known TCR/TCR2/MAIR/private VMID installed and never replays sampled UNKNOWN SCTLR/TTBR/TCR/MAIR fields.

The generic APPS allocator now excludes nonzero CBAR or CBA2R. IPA allocation and VMID leasing also exclude parked banks and identities. Parking consumes two banks until reset and is suitable only for this bounded, single-owner lifecycle. Completion/status observation remains the evidence for write-only maintenance commands. Host models cannot establish physical acceptance of those commands.

The strict SMMU-only gate is absent from this exact image. Full mode still prepares IPA, binds the real2221 adapter, attaches data and only then loads/authenticates MPSS. The serial worker maintains fresh USIM/IPA evidence and terminal data revocation. Partial binding failure remains terminal before attach or MPSS authentication.

`m_data_stop` revokes admission, pumps bounded cleanup, checks its outcome, quiesces AP channels and detaches once. The outer path calls modem cleanup first, then hardware stop only after verified PAS4 stop, then IPA cleanup. An uncertain PAS4 stop returns before modem reclaim and causes IPA cleanup to retain all IPA backing, mappings and power without parking or release. With verified PAS4 stop, failed IPA/GSI quiescence or PAS15 shutdown still prevents release. Owner acceptance of PAS4 retention remains necessary and M2 remains incomplete.

Data changes remain confined to `CellSession.c`, `CellSession.h` and `sweet_data.c`. APN length/authentication validation precedes data-port registration. The operator-supplied request-only APN and selected PAP authentication enter Start Network TLVs 0x14 and 0x16, with empty credentials omitted and no Create/Modify Profile path added. IPv6 Start waits for usable IPv4 settings without spending its start budget. Existing unresolved-start, handle ownership, late-reply and teardown rules remain intact.

## Runner, storage and proof boundary

The runner remains byte-identical to the prior reviewed runner. The new manifest additionally pins imported HFS/devloop dependencies. Its approval/report-pin checks currently fail closed as intended. The same original profile, clean recovery, actual root-hook composition, exclusive attempt, one boot, no replay and zero button wait remain mandatory. Restoration requires byte-for-byte original-profile readback and clean recovery before releasing the hold/lock. Uncertain restoration retains ownership.

The exact arguments retain framebuffer arming, thermal3, fetchsync1, USB4, every required modem/IPA/data gate and independent deadman900. They omit SMMU-only, firmware-only and raw gates. Removing the optional SpringBoard/GPU arguments and shortening panic display to nine seconds does not relax those guards. The worker remains 600 seconds, script wait 660 relative seconds, finish delay 15 seconds and devloop return deadline 1020 seconds. Supervise ownership and collection within the separate under-20-minute limit.

MPSS remains PAS4 at the DT remoteproc reservation, reading stock `modem_a` through `sweet_ufs_read_part`. IPA remains PAS15 at the DT GSI reservation. Rmtfs writes affect RAM shadows only. No write to modem_a, modemst1/2, fsg/fsc or persist is authorized. Root hook/preferences/evidence updates remain the separate normal-devloop scope.

The pinned sweeten2 binary and proof procedure preserve en2 interface binding, IPv4 source binding and one HTTP HEAD to 1.1.1.1 with a ten-second monotonic deadline. Use only the original exact-image PID's lifetime, private exclusive nonce directory, full-byte helper readback before execution and one route withdrawal. Keep Mac Sharing unchanged and the helper archive inert afterward. Ordinary USB-unbound requests prove no cellular traffic. This review establishes neither actual packet connectivity nor DNS/TLS/Safari operation.

## Host validation and limits

Fresh isolated ASan/UBSan lifecycle, shared-TLB, datapath, real worker/binding, strict map-gate and generic allocator checks passed under `/tmp/sol2209-zwai9ga5`. All 26 extracted IPA/modem inputs corresponding to production files matched byte for byte. The data protocol, service-cache and adapter suites passed, as did the runner's mocked restoration/identity checks and standalone/root selftest composition checks.

The frozen audit8 hook suite passed after supplying its missing copied data-test fixtures from the public tree in the isolated hierarchy. SMEM/GLINK protocol checks also passed. Initial incomplete/mixed test-copy attempts failed on fixture layout/version differences and were not counted as production failures. These checks exercise host models and extracted code, not physical DMA cessation, kernel concurrency or actual modem authentication. The signed helper's socket tests were not rerun here.

No phone operation, upload, sudo, commit, delegation, implementation change, manifest edit or deletion occurred. Writes were confined to this report, retained reconstruction artifacts in the workspace and isolated host artifacts in /tmp. No carrier/APN value or full device/subscriber identifier is included.

## Required path to a future image GO

1. Resolve the S-checker finding through its owner and obtain a new independent S trial GO with updated frozen dependencies.
2. Complete and accept a separately reviewed benign clean C trial, then a separately reviewed successful SMMU-only S trial, with fresh complete evidence and clean root.
3. Complete a separately reviewed successful PAS15-only trial, then a separately reviewed successful offline IPA/GSI/QMI handshake trial on the accepted dependency chain.
4. Obtain a fresh exact-data-image decision against the resulting dependency evidence and unchanged frozen bytes before recording approval and the real report hash.
5. Preserve one exclusive attempt, fresh nonce/frame/DONE, stale=false, clean root before/after, guarded restoration and the two-equivalent-unexpected-resets stop line.

Ast171 reset after 63 seconds before PAS15 and left the root dirty. It satisfies none of these prerequisites. The suspended exact2208 decision cannot authorize this replacement. Verdict: NO-GO until the concrete prerequisite defect and measured trial requirements are resolved.
