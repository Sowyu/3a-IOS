# 2175 + 2176 with USB v5 review

2026-11-03. Verdict: GO for the read-only power probe plus USB level 4, conditional on a successful final remake of the exact source below and image verification. Conditional GO for the unchanged offline PAS/rmtfs stage after the power prerequisite passes. Conditional GO for read-only UIM only after a separate earlier boot establishes M2. None of those prerequisites is established by ast148. No daily-use approval.

I read `3a-IOS/HANDOFF.md` first, then `plans/modem/sol-2174-audit.md`, selected SWEET MODEM state lines from ast148, and its result fields. I used ponytail-review and unslop. I performed no phone operation, build, host-test compilation, delegation, implementation edit, or external upload. This report is the only file I wrote.

## Exact source reviewed

Production is `ios-asteroids/research/xnu-modem/dxb/xnu`. The 2170 through 2174 hashes still match the prior audit. I independently reconstructed all twenty added modem source/header files in memory from ordered 2170 through 2176 patches and compared their complete bytes with production. All match. Mirrored modem patches under `plans/modem/patches` match `3a-IOS/patches/modem`.

New patch SHA256 values:

- `2175-asteroids-modem-power-preflight.patch` is `72c97e0b8b7111d721539bad4af09a1fd83f7b3297848d3abe83c50df8ff4f50`.
- `2176-asteroids-modem-read-only-uim.patch` is `c05c9496cda37787456bf86a4823a8c9c1b25429211e9001dcd34022a4598cc1`.
- `2160-asteroids-usb-gadget-shared-v5.patch` is `ab2465de10e7db0cbf2d0e7cb23ac05984ce32d65b1832e03dfff75304335a59`.
- `2161-asteroids-usb-gadget-levels-v5.patch` is `81848fdd95b616b9a837dbdd19983505f2e77f3d3293137bd906f19bb2215f6b`.

Production `ModemRuntime.inc` is `b7214d41ddb66cc2fac85a88bd6946c39d2591b6425daa5bedbe25ef1c929f59`. `ModemUim.c` is `dfd0166976f9fd6b2480223da335ff8dcffd0d367ff473a254871423d7747d4a`. `QcomQrtr.c` is `74ea337936d84791462d4513287c60ab8bd0f7bd06d77579cac74ac2ecde84a9`.

USB `sweet_usb.c`, `sweet_usb_gadget.h` and `sweet_reboot.c` match the audited `plans/usb/gadget/work/b5` files byte for byte. `sweet_ufs.c` differs from b5 only by the modem start declaration and call. USB source SHA256 is `5695af3f994645c5e6597edac3995b3e1b0ff14d90adaf0ccd6b1eda47cd883e`.

## Actual first start and power refusal

Ast148 line 222 reports firmware preflight PASS. Line 4331 reports error 22 after 4 ms. Line 4332 reports no PAS activity, cleanup ok, ready 0, handover 0 and zero services. Line 4334's `preflight_error=0` refers to firmware preflight, not successful modem startup. The result is selftest-done in 353 seconds with root state clean.

The 2174 start order and absence of any vote/PAS activity locate this refusal at the CX/MSS/XO resource checks before SMP2P allocation. The log does not distinguish a failed lookup, missing/insufficient voltage levels, or invalid XO address. It proves neither PAS startup nor M2, and says nothing about a SIM.

2175 `ModemRuntime.inc:309` copies cmd-db into private kernel memory and reports each resource's lookup status, address, parsed count and levels. Its first failure does not prevent inspecting the remaining resources. `:320` retains the final voltage level >=384 requirement, then selects the highest parsed CX/MSS index. `:323` rejects duplicate CX/MSS/XO addresses.

At `:335` SMEM partition discovery is read-only. The SCM operation at `:336` queries PAS support. RSC version and cmd-db accesses are reads. `:339` exits whenever MPSS is absent, including when inspection succeeds, before the first `m_alloc` at `:347`, IPCC ringing, QMP load_state, rail voting, firmware copying or PAS init/auth. Cleanup has no owed operations on this path. USB level 4 still performs its separately approved USB work, so “read-only” describes the modem stage.

The inherited parser in `QcomRpmh.c:34` stops after sixteen levels and returns success. Current [Linux rpmhpd.c](https://github.com/torvalds/linux/blob/master/drivers/pmdomain/qcom/rpmhpd.c) defines a 32-level capacity. This is a possible truncation mechanism, not a diagnosis of ast148. No actual cmd-db contents were supplied, and a printed count of sixteen cannot establish that the complete table ended there.

Do not lower 384, substitute an address, or reinterpret a refusal as a pass. If a resource saturates sixteen entries, establish its complete table before the offline stage so “highest floor” remains true. A parser change or newly discovered hardware layout needs review of that change. The unchanged probe is allowed to collect the diagnostic evidence even when inspection fails.

## UIM boundaries

`ModemRuntime.inc:328` uses the existing unique complete `name=1` token parser. UIM requires GLINK, and `:339` requires MPSS before any IPC preparation or UIM sender becomes reachable. `:357` polls UIM only after ready and handover are both set. This is a runtime readiness gate. The requirement for M2 in a prior boot remains an operational condition, not persisted kernel state.

The only UIM request constructor emits seven-byte requests for service 11, GetCardStatus 0x2f and GetSlotStatus 0x47. Client port 0x4002 has service zero, so QRTR binds it without announcing a server. The 128-entry remote table remains bounded, and the three local endpoints fit the existing four-entry table.

Each message gets at most three successfully queued requests, separated by at least ten seconds. Retries use fresh transaction IDs. The receiver rejects unsolicited replies before any request, mismatched remote node/port, wrong response type, wrong opcode and stale transactions. A selected endpoint change while polling marks the worker failed. Missing service discovery or exhausted retries supplies no UIM success evidence.

The [libqmi UIM schema](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-uim.json) confirms the two query IDs and the card/application and physical-slot field layouts. The parser validates QMI framing, result TLV, nested lengths, card/application caps and exact consumed subarray lengths. It skips AID bytes and emits no AID or raw packet. ICCID BCD decoding copies only four final digits into the output structure. PIN1 output is a numeric state enum, not a PIN value. No slot switch, provisioning, PIN submission, SIM power operation, APDU, online/NAS/WDS command or generic raw QMI interface is introduced.

## Integration and findings

No new source finding blocks these bounded stages. The modem worker starts after reboot safeguards, USB initialization and thermal setup in `sweet_ufs.c`. USB and modem do not introduce a shared transfer queue or register writer. The existing touch/modem RSC mutex remains in use. The physical reservations, DT and RAM-only EFS model inherit the 2174 audit unchanged.

1. Acceptance must inspect stage-specific evidence. `ModemRuntime.inc:359` permits an overall zero result with ready alone, and neither checks UIM completion nor propagates cleanup failure through the firmware preflight summary. This is nonblocking for a diagnostic boot, but zero `preflight_error`, selftest-done or a wrapper exit status cannot establish M2/M3. Require ready plus handover, GLINK/QRTR, actual scoped rmtfs/pd-mapper response evidence and verified PAS shutdown with cleanup ok for M2. Require valid matched UIM card/application status for M3, accepting an explicit absent-card response. Slot-query errors or missing ICCID must remain visible.
2. The final unsolicited-reply guard has no explicit pre-request receive regression case in the inspected sender test. Source inspection verifies the guard. Add that small case before relying on the host suite to catch its removal. This evidence gap does not widen request scope or block the read-only experiment.
3. Hardware qualification remains limited. I read `3a-IOS/docs/usb-gadget-audit5.md` and the end of `usb-gadget.md`; ast147 supplies the known-Mac SSH/transfer/idle-soak evidence recorded in HANDOFF. Combined modem load, the USB ENDTRANSFER recovery branch, replug, suspend, reset quiescence and daily operation remain unqualified. The v5 runner's zero exit is not acceptance proof. Use fresh same-run evidence.

PAS cleanup remains owed before its first init attempt. Unverified PAS shutdown retains memory and power. Uncertain TCS state retains its programmed TCS, allocations and rail votes. As in 2174, verified PAS shutdown may still permit ownership reclaim and QMP-off while a vote remains uncertain. Normal verified cleanup releases only MSS and retains XO enable mask 3 and the highest supported CX floor until reset.

## Validation and build condition

I inspected the existing passing `uim-check-2176.txt`, `start-check-2176.txt`, `protocol-check-2176.txt` and `preflight-check-2176.txt` plus their scripts and test bodies. These cover the actual extracted UIM sender/receiver, power inspection, SCM, cleanup and vote functions, parser truncation/BCD cases, read-only storage references and GLINK backpressure. They are host-model evidence. I did not run them.

Host check scripts now put TMPDIR inside their workspace output directories and use `-save-temps=obj`. The image builder uses TMPDIR `/tmp`. Neither change is a device test.

`plans/modem/logs/build-2176-usb-v5.txt` had no completed-build marker when inspected. The final source guard changed during that build. Require that build to finish, then a successful final `--remake` compiling the pinned final source. Record the resulting kernel hash and verify that the proposed RAM image embeds it. This audit does not certify an existing boot artifact or stale object.

## Allowed sequence

1. GO, after the final build/image checks, for one known-Mac USB4 power-probe RAM boot with `sweetmprobe=1` and all of `sweetmpss`, `sweetmglink`, `sweetmrmtfs`, `sweetmpdm`, `sweetmuim` absent. Inspection may refuse. No PAS/SMEM-write/vote activity is permitted in that probe.
2. Conditional GO for a subsequent unchanged offline boot with one each of `sweetmpss=1 sweetmglink=1 sweetmrmtfs=1 sweetmpdm=1 sweetusb=4`, with probe and UIM absent. First require the diagnostic boot's firmware, SMEM, PAS-support, RSC and complete power-resource checks to pass with clean completion. A refused or truncated/ambiguous power inspection leaves this stage NO-GO pending evidence or a separately reviewed fix.
3. Conditional GO for a later M3 boot adding only `sweetmuim=1` to stage 2 after a prior boot proves M2 and verified clean shutdown. No UIM boot is authorized from ast148 alone. This permits only the two read queries above.

The inspected `build-offline-usb.py` produces 591 argument bytes. Appending UIM gives 603. Replacing the four modem-stage flags with the probe gives 553. The builder itself currently makes stage 2, so verify the actual probe image separately before use. Preserve cpus=8, `sweetfbarm=1 sweetpanicfb=10 sweetdeadman=600 sweetselftest=1 sweetthermal=3`, keep plus astwait, UBWC and fetchsync. Removing fixed cpufreq arguments selects the normal governor. Omit `sweetusblog` so the audited asteroids UDP logger stays off.

All boots remain future operations after the owner returns the phone to fastboot/recovery. Before every run verify fastboot devices or adb recovery, even when no dev.py process exists. Permit no concurrent dev.py and keep each whole run under twenty minutes. A connected known Mac and USB SSH do not authorize a different QMI command surface or access to private raw logs.

No partition write, flash, stock boot, Android restore, slot change, persistent EFS change, sudo, or write outside the workspace or `/tmp`. EFS access remains read-only snapshots with RAM-only writes. Never permanently delete files. Any removal must use `/usr/bin/trash` with an absolute path. Keep firmware, dumps, raw logs and identifiers private, with identifier suffixes limited to four digits.

Not checked: final remake, packed image identity, hardware power tables, combined modem/USB behavior, M2 or SIM detection. Verdict: scoped conditional GO as above.
