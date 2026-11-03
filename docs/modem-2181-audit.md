# 2181 read-only BCM survey review

2026-11-03. Conditional GO for ONE read-only BCM survey on the exact dedicated modem tree, subject to the five conditions below. No blocking source finding for this scope. No MPSS start, GLINK, rmtfs, pd-mapper, sysmon or UIM enablement is approved. M2 remains incomplete.

What this change does: 2181 inventories eight command-db resources from the existing bounded snapshot when `sweetmbcm=1` is present. It prints raw auxiliary metadata and adds no vote, MMIO mapping, SMC or storage path. 2180 preserves shutdown-time transport/fatal failures in the cleanup result.

I read `3a-IOS/HANDOFF.md` first, then the latest checkpoint/tail of `plans/modem/README.md` and `sol-2179-audit.md`. I applied ponytail-review and unslop. This was desk review within this workspace only, with no phone access, builds, compilation, test execution, delegation, uploads, deletion or code edits. This report is the only file I wrote. The intended load is one guarded survey.

## Exact source and equivalence

Production is `ios-asteroids/research/xnu-modem/dxb/xnu`. I reversed patches 2181 through 2170 in memory against production using unique exact text matches. All 76 hunks matched, and all 20 added files reversed to empty. All twelve patches match their `3a-IOS/patches/modem` mirrors byte for byte. This verifies the modem deltas and integration hunks, not every unrelated base-tree file.

Patch SHA256 values are:

- `2180-asteroids-modem-shutdown-error-result.patch`: `67ed839286c18ee5f2f0b6ce0929ebf83fd0a6b3ad53eb80eab54898b2fbc0bb`.
- `2181-asteroids-modem-bcm-metadata-probe.patch`: `2b1b454791ab6401e6fdc65040e77c09533f891320255ef3427cc6fbbaf24d0a`.

Production `ModemRuntime.inc` hashes to `894ec8acf510081d8f925fa77459ef6f3edae99009c2ff9e43a5824630f8e13a`. `baseline2180/ModemRuntime.inc` retains the audited 2179 hash `ae6c9b198092ce8105d4848875f8c858d835d2ca3e0fbd624a8fb0e398f3fdf5`. Production `QcomRpmh.c` hashes to `722fa2d49391db6b2bd6216e025519bf169229fe746096a711b1145f1d61b479`, and `QcomRpmh.h` to `664c30bb3ff1ecf5cd261e2bf39a6c1a6924f9cee1bc6518f5c35a4c9130d2b1`.

The production runtime, RPMh header/helper, gate, QRTR, RAM-rmtfs and UIM copies checked match the extracted host copies byte for byte. `ModemUim.c` retains its 2179 audit hash. USB C/header and reboot source match `plans/usb/gadget/work/b5/bsd/dev` byte for byte. The audited daily series is an inherited prerequisite, not a fresh whole-kernel audit here.

## Survey gates and bounds

`ModemRuntime.inc:409-428` initializes `m.bcm_probe` through the existing exact, single-token `modem_gate`. Absent, bare, duplicate, malformed or non-1 `sweetmbcm` cannot enable BCM logging. `sweetmbcm` alone does not create the worker. The requested `sweetmprobe=1` enables the existing inspection path, and absent `sweetmpss` exits before shared-memory allocation, QMP load-state messages, votes, firmware loading or PAS init/start.

The existing read-only prerequisites still map hardware and issue the previously reviewed SCM availability query. The claim established here is that 2181 adds none of those operations. Its eight lookups read the existing 128 KiB `dbcopy` populated by `m_power_inspect`.

`QcomRpmh.c:29-42` checks the full auxiliary range before reading it, zero-initializes the result and copies at most eight raw bytes. Slave ID and auxiliary version come from the bounded fixed slave record. Existing ARC decoding and level selection remain unchanged. All callers use the rebuilt local structure, so the added fields introduce no external structure ABI dependency.

`ModemRuntime.inc:394-406` surveys CE0, SN3, SN0, SH1, SH0, MC0, ACV and IP0. Missing entries or auxiliary lengths other than eight set EINVAL and skip metadata decoding. Valid records print little-endian unit/width and raw VCD/reserved values. Slave/version are evidence, not validation of a future voting implementation. Zero values or unfamiliar versions must remain raw observations. No BCM output is used for ARC levels or an RPMh command.

The public `build-bcm-probe.py` matches the local builder. Its argument expressions currently produce the prepared 565-byte line with unique names, `sweetmprobe=1 sweetmbcm=1 sweetusb=4`, and none of the MPSS/GLINK/rmtfs/pdm/sysmon/UIM gates. The 607-byte number is the packing limit, not this line's measured length. Required CPU, reset, selftest, thermal and daily graphics arguments are present.

## Prior findings and host evidence

2180 closes the previous error-propagation finding. `m_cleanup:370-371` logs `m.failed` and `m.fatal` and returns false for either flag, even after verified PAS and safe successful memory reclaim. `modem_run:443` converts cleanup failure to EIO. A failed PAS still returns before reclaim, unmapping, freeing or power release. Safe reclaim and a failed experiment result can coexist.

The joined test closes the previous separated-test gap at the extracted-function level. I reconstructed `tests/out/shutdown-joined.c` in memory from production functions and the current wrappers. It matches exactly, SHA256 `274d639a0078574dc0a4b8f6e45a8809a12346d0e3338bdfefec21702eac7f02`. The same check for `power.c` matches exactly, SHA256 `c3847455e5c4a0f166286487d3df88e98e0bd4c307d3571018c9af0362986d9a`.

`check-shutdown-joined.py` extracts actual SCM/PAS, RAM-rmtfs callback, SSCTL receiver/request and cleanup functions. `shutdown-joined-tail.c` routes packets through actual QRTR and actual RAM-rmtfs request handling. Its success case proves a read into the window and a write back to the shadow during SSCTL, then one PAS shutdown and reclaim. Other cases assert ten-second timeout fallback, fifty-call permanent PAS retention with no reclaim/free/unmap, and shutdown-only transport/fatal flags that fail cleanup despite safe reclaim.

Hardware time/SMP signaling, SMC results, ownership/power helpers and packet delivery remain mocked. Host extraction alone removes the two ARM barriers in the RAM-rmtfs callback. The production barriers remain. This does not prove hardware ordering, real PAS success or the whole GLINK execution path.

I inspected supplied `logs/start-check-2181.txt`, including its final joined PASS at line 5456. Existing ARC checks, opted-in missing BCM refusal, eight valid metadata records, seven-byte auxiliary refusal and off-path inertness are covered. Helper checks assert slave/raw-prefix copying and short-prefix zero fill. These are supplied sanitizer-backed results, not tests rerun by this reviewer.

## Hardware evidence and hypothesis

`logs/live-offline-usb-20261103-161025.txt:38-43` records a zero SSCTL response, two RAM-only writes, a new slave value 0x86 and accepted response/indication/bit-7 acknowledgment. The following PAS calls still return raw status -3145800. Lines 94-95 record unverified shutdown with retention and final_error=5. Successful SSCTL does not establish M2. No UIM result is established.

The local primary vendor `nothing-drivers_remoteproc_qcom_q6v5_pas.c:46-47,320-356,1112-1150` defines UINT_MAX average/peak bandwidth, enables it before shutdown and disables it afterward. `nothing-drivers_interconnect_qcom_icc-rpmh.h:51-56` defines the eight-byte BCM metadata layout. Its C implementation reads those fields and clamps unit/width for voting. This survey deliberately reports raw fields without that voting transformation.

The local volcano interconnect source associates CE0 with crypto and the selected SN/SH/MC/ACV resources with the downstream interconnect/memory path. The bcm-voter source uses unit, width and VCD to construct votes. These sources justify inventorying metadata. They do not decode -3145800 or prove omitted bandwidth caused it. IP0 inventory authorizes no IPA action.

## Conditions for the single survey

1. Establish exit 0 for the exact safe `--remake` build and freeze the reviewed source. The current log contains `done building xnu`, but I did not establish process exit status. Parent must verify the installed kernel, every rebased kernel segment, embedded payload and exact packed argument line in the final image, recording hashes. No unrelated kernel delta is approved.

2. Require one each of `sweetmprobe=1 sweetmbcm=1 sweetusb=4`, unique names and at most 607 packed bytes. Require `sweetmodem`, `sweetmpss`, `sweetmglink`, `sweetmrmtfs`, `sweetmpdm`, `sweetmstop`, `sweetmuim` and `sweetusblog` absent. Preserve `cpus=8 sweetfbarm=1 sweetdeadman=600 sweetselftest=1 sweetpanicfb=10 sweetthermal=3`, daily graphics, fetchsync, keep plus astwait and the reviewed canvas/panel settings.

3. Acquire the modem hold, respect active dev.py processes and independently verify actual mode immediately before launch. Ignore the battery hold. Require checked clean recovery and no other dev.py, never interrupt owner iOS, and use dev.py for exactly one RAM survey. The runner does not autonomously move fastboot into recovery and refuses to launch there. Do not replay a failed run automatically.

4. Compare the actual dev.py script against the reviewed hook/body and fresh nonce. `out/bcm-script-2181.sh` matches current composition exactly, SHA256 `8420aff3afee9a0b18f21da3c0cd9006e0b7a9a6259c5efc96ded5dca18736c8`. Require collision-safe archives, checked fresh screenshot, matching completion nonce, recorded done step and DONE, fifteen-second flush, clean root and stale=false. FAIL/SKIP, panic, fired deadman, unhealthy USB or missing evidence prevents acceptance.

5. Require same-run command-db and eight BCM lookup/metadata records, hardware preflight error=0, STOP no PAS activity with cleanup=ok, teardown flags zero and final_error=0. Any missing/truncated resource is a failed survey requiring desk review. Require no modem start, SSCTL, GLINK/rmtfs/pdm/UIM activity, load-state message or modem vote. The result permits metadata analysis only. Any vote or new MPSS boot needs a separate review.

Observed artifact hashes at review time are kernel `93f7555614ce18a9a2077caf92ed0cb282ece47b39eef99e9ad89418338c5f6a`, BCM image `1dc152b77e3bc705717fbfeb072c96d5f543e40072a3ad123905cdaed32fcfe4` and payload `54adbf3e5dbfb2a2317755572221b4468ae322dbe6b07a2de30aced18bb19a38`. These observations do not replace the parent's build-exit and segment/packed-image checks.

No partition writes, including modemst1/2, fsg, fsc, persist or modem_a. Existing snapshots stay read-only and any future separately authorized rmtfs writes stay in RAM. No flashing, stock kernel, plain fastboot reboot, set_active, Android, PAS expansion, IPA, online mode or WDS. No sudo, outside-workspace or outside-/tmp writes, permanent deletion, public firmware, raw logs or identifiers.

Not checked live: final build exit, packed image equivalence, phone mode/root, hardware survey results or M2/M3. Verdict: conditional GO for the one read-only BCM survey above. New modem boot and UIM remain NO-GO.
