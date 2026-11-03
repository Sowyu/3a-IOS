# 2170 to 2177 and USB v5 review

2026-11-03. Verdict: scoped conditional GO. The first permitted run is the read-only modem power probe with USB4, after matching build/image and device checks. Offline PAS remains conditional on a successful live probe and complete power tables. UIM remains conditional on a separate successful M2 boot and verified shutdown. No daily-use approval.

Review only. I read `3a-IOS/HANDOFF.md` first, then `plans/modem/sol-2175-2176-audit.md` and `sol-2174-audit.md`. I applied ponytail-review and unslop. I performed no phone operation, build, host-test compilation, delegation, upload or implementation edit. I ran read-only source/patch comparisons and the read-only local AOP analysis. This report is the only file I wrote.

## Exact revision and production match

Production is `ios-asteroids/research/xnu-modem/dxb/xnu`. I reversed the ordered modem series in memory against production, checking every resulting hunk and requiring each added file to reverse to empty. All 60 hunks and all 20 complete added source/header files match. The three existing integration files are `bsd/conf/files`, `bsd/dev/sweet_touch_asteroids.c` and `bsd/dev/sweet_ufs.c`. This checks integration hunks, not unrelated contents of the existing base.

Patch SHA256 values, in application order:

1. `2170-asteroids-modem-preflight.patch`: `2eeb835d8710b71cc33ed6af8dc0d45bc6daf5db4188b42b5185ac484ec9baf2`.
2. `2171-asteroids-modem-fat-names-and-chains.patch`: `880052a579262fa352968cfa3f1823531d4590e3dd9e16129ab4853782d0a750`.
3. `2172-asteroids-modem-smem-glink-core.patch`: `aa898aa02865333c5f8bb8fe70d2467ecb655c71e6efdff3b5bacaa6d824dbb0`.
4. `2173-asteroids-modem-pas-offline.patch`: `3b0f4f1c13f30ab85316bec6d9024c0b614e1d4448134c93dcbfcab9b90ea7ba`.
5. `2174-asteroids-modem-xo-and-glink-completion.patch`: `7c6d9adac20cc0aaf89438d680f68d258f9a8eb4582cb75557c10e524df04bbf`.

The remaining patches are:

1. `2175-asteroids-modem-power-preflight.patch`: `72c97e0b8b7111d721539bad4af09a1fd83f7b3297848d3abe83c50df8ff4f50`.
2. `2176-asteroids-modem-read-only-uim.patch`: `c05c9496cda37787456bf86a4823a8c9c1b25429211e9001dcd34022a4598cc1`.
3. `2177-asteroids-modem-arc-level-clamp.patch`: `8bbb3687195820be2ead125f186075d75167b91282b87e722925aa6bedee4f6e`.
4. `2160-asteroids-usb-gadget-shared-v5.patch`: `ab2465de10e7db0cbf2d0e7cb23ac05984ce32d65b1832e03dfff75304335a59`.
5. `2161-asteroids-usb-gadget-levels-v5.patch`: `81848fdd95b616b9a837dbdd19983505f2e77f3d3293137bd906f19bb2215f6b`.

Every modem patch mirror under `plans/modem/patches` matches `3a-IOS/patches/modem`. Production `sweet_usb.c`, `sweet_usb_gadget.h` and `sweet_reboot.c` match the audited `plans/usb/gadget/work/b5/bsd/dev` files byte for byte. `sweet_ufs.c` differs only by the modem start declaration and call, after reboot safeguards, USB and thermal initialization.

Final production identities:

- `bsd/dev/modem/ModemRuntime.inc`: `26488d9dfb38df62f058ba495cbba0af4288112aded7abde70a9f4a43b26f1bf`.
- `bsd/dev/modem/QcomRpmh.c`: `1b8211d3ca3249cbd41d4868728daadcd57651a4e79f4b57ad9a3ebf088055d1`.
- `bsd/dev/modem/QcomRpmh.h`: `061e19a9d272475b2db664cc297a7fa0325dedbe469813a6e9c2bfedc1f28d24`.
- `bsd/dev/sweet_modem.c`: `9db4454af3a43f31cc15ab3cb2d46f31473739d27f7acee65f0de42c03eb6e88`.
- `bsd/dev/sweet_usb.c`: `5695af3f994645c5e6597edac3995b3e1b0ff14d90adaf0ccd6b1eda47cd883e`.

`ModemUim.c` remains `dfd0166976f9fd6b2480223da335ff8dcffd0d367ff473a254871423d7747d4a`, and `QcomQrtr.c` remains `74ea337936d84791462d4513287c60ab8bd0f7bd06d77579cac74ac2ecde84a9`.

## Desk inference and 2177

I read `plans/modem/tools/inspect-aop-table.py` and reproduced `plans/modem/logs/aop-table-desk.txt` exactly using the LOCAL stock backup. No firmware bytes or dump were published. The candidate starts at header offset `0x201c0`, with 28 consecutive lvl/tmr headers for cx, mx, ebi, lcx, lmx, gfx, mss, ddr, mmcx, nsp, mxc, lnoc, nsp2 and xo. The maximum aux offset plus length is 448, yielding the candidate preceding aux base `0x20000`.

The derived CX resource is `0x30000`, with maximum level 416. MSS is `0x30060`, with levels 0, 56, 64, 128, 192, 256 and 320 followed by zeros. XO is `0x300d0`, with aux levels 0, 32, 80 and 128 followed by zeros. These candidate resources have 32 aux bytes each and fit within sixteen entries.

This base derives from the plausible contiguous layout. The script does not prove live cmd-db headers, placement or identity. Its output is desk inference and does not diagnose ast148 conclusively. The next live probe must confirm the cmd-db lookups and complete maps before PAS. Never substitute these candidate addresses into runtime.

The old final-level >=384 guard rejects this plausible valid MSS map. [Linux rpmhpd_set_performance_state](https://raw.githubusercontent.com/torvalds/linux/master/drivers/pmdomain/qcom/rpmhpd.c), lines 1020 to 1048 as inspected, searches for the first supported level at least as large as the request and clamps an oversized request to the maximum index. Lines 1053 to 1090 bound the aux mapping and recognize zero padding. This supports replacing the minimum-384 assumption with bounded selection.

Production `ModemRuntime.inc:309 to 331` uses the existing bounded helper, reports request index and final floor index, then still selects the highest parsed CX/MSS index. Thus a maximum-320 MSS map selects index 6, while the candidate CX floor remains index 11. Cleanup still releases only MSS. XO still uses enable mask 3, independently of its aux voltage indices.

`QcomRpmh.c:33` now records the complete declared aux byte length before parsing. For CX/MSS, inspection rejects odd lengths, lengths above 32, zero/single-level maps, a nonzero off entry and descending parsed levels. Lookup also rejects extents outside the copied database. Keeping sixteen entries is safe against capacity truncation here because any larger declared CX/MSS aux block refuses, even if its parsed prefix would fit. No address is guessed or substituted. The new struct field has no additional production consumers beyond this path.

Two precision limits remain. Zero padding terminates parsing, so the inspector validates and prints the active prefix, not every trailing aux word. A malformed sequence such as 0, 16, 0, 320 would be treated as a terminated prefix, as in the upstream padding convention. Also, XO bypasses CX/MSS voltage validation because its vote is a mask. Therefore do not describe this as validation of every aux byte or rejection of every malformed XO table. This does not block the expected zero-padded maps, but an ambiguous live map is NO-GO for PAS pending complete-table evidence or a reviewed parser change.

## Gates, cleanup and inherited safety

The read-only probe still exits at `ModemRuntime.inc:345` whenever MPSS is absent, before the first SMEM allocation/write, IPCC ring, QMP load-state write, rail vote, firmware copy or PAS init/auth. Earlier SMEM discovery, cmd-db/RSC reads and the SCM PAS-support query are prerequisite inspection. USB4 performs its separately audited USB work, so read-only describes the modem stage.

All modem stages retain strict unique complete `name=1` gates and default off. GLINK requires MPSS, and rmtfs/pd-mapper/UIM require GLINK. The storage path uses only fixed modem_a and EFS names through size/read APIs. Rmtfs modifies heap snapshots and the bounded shared RAM window, with no persistent EFS write callback. GLINK still retains a delivered intent until its completion can queue, including FIFO backpressure and reuse.

The inherited board reservations remain PAS4 MPSS `0x8ac00000/0xe600000`, firmware span `0xd700000`, SMEM `0x81d00000/0x200000`, mutex `0x1f43000`, SMP2P 428/435 and APPS/modem 0/1 with fatal/ready/handover/stop-ack bits 1/2/4/8. IPCC uses client 2 signals 0/2. QMP is `0xc300000`, RSC `0x17a20000` with `0xd00/0x2a0/24` layout, and cmd-db `0x81c60000`. Rmtfs `0xe1000000/0x280000`, metadata `0xe1400000/0x10000` and arguments `0xe1410000/0x4000` remain disjoint. The supplied low-bank reservation is an inherited assumption.

PAS shutdown is owed before the first init attempt. Every ordinary exit reaches cleanup. Unverified PAS shutdown retains memory and power. An uncertain TCS retains its programmed state, rail votes and allocations. After verified PAS shutdown, ownership reclaim and QMP-off can still occur while votes remain uncertain. Normal verified cleanup sends zero only to MSS and retains XO mask 3 and the highest CX floor until reset. All three start commands must acknowledge before loading/authenticating the modem. The touch path retains the shared RSC mutex.

## UIM and evidence

UIM still polls only with ready and handover both set. Prior-boot M2 remains an operational gate, not persistent kernel state. Only service 11 GetCardStatus `0x2f` and GetSlotStatus `0x47` requests are constructed. Local client port `0x4002` announces no server. Each query permits at most three successfully queued requests, with fresh transactions and at least ten seconds between retry rounds. Endpoint changes fail the worker.

The receiver checks the selected endpoint and requires a previous request before accepting a matching response. The parser checks framing, response type, opcode, transaction, result TLV and bounded nested lengths. It skips AID bytes and stores/logs only the last four ICCID digits. PIN1 is a state enum. There is no provisioning, slot switch, PIN submission, SIM power operation, APDU, online/NAS/WDS command or raw QMI bridge.

I read the passing `start-check-2177.txt` and `uim-check-2177.txt`, their extraction scripts and test bodies. The actual power inspector tests maximum-320 clamping, descending levels, aux lengths 64/7/2, duplicate resource addresses and bad magic. Actual vote and cleanup tests cover three-command acknowledgment, XO=3, MSS-only release, IRQ preservation, retained timed-out TCS, failed shutdown and failed ownership reclaim. These are host models, not live power evidence. They do not explicitly test every rejection branch, including nonzero off or nonzero words after padding.

The new UIM regression supplies an otherwise matched valid reply before any request, with the endpoint and transaction already selected, and requires no completion. This closes prior audit finding 2. No additional kernel UIM change occurred. I did not compile or rerun the host suites.

## Findings and build condition

No new source finding blocks the three bounded stages under their prerequisites. Prior audit finding 1 remains: firmware `preflight_error=0`, overall return zero, wrapper exit zero or selftest completion cannot establish M2/M3 or successful cleanup. Runtime can return zero with ready alone and does not propagate cleanup failure through the firmware summary. Acceptance must inspect the stage evidence below.

USB qualification remains limited to the known-Mac experiment. HANDOFF records ast147's SSH/checksum and 480-second idle/transfer soak, with no stale TX and a clean root. Combined modem load, the ENDTRANSFER recovery branch, replug, suspend, reset quiescence and daily operation remain unqualified. Root SSH lacks a phone-side host trust gate. Keep `sweetusblog` absent so the audited asteroids UDP logger remains off. Use fresh same-run USB evidence and reject gates, canary failures or persistent stalled TX.

`plans/modem/logs/build-2177-final.txt` now contains `CC sweet_modem.o` at line 2343, `CC QcomRpmh.o` at line 2353 and `done building xnu` at line 5857. The installed kernel timestamp, 14:44:00 local, follows the reviewed runtime/RPMh source timestamp, 14:42:24. The installed kernel SHA256 is `051eaefd44f33eace03ba6d9488bfb18a1d1ebb0005ec7724f9b34c4aa2d917a`. This is existing completion evidence. I did not launch or supervise that build or independently establish its process exit status.

Before any boot, the parent must confirm successful final build exit, unchanged pinned source, and that the proposed RAM image actually embeds this kernel with the scoped arguments. Existing image identity is not certified by this review. Current argument files contain 553 probe bytes, 591 offline/USB bytes and 603 UIM/USB bytes, with no duplicate argument names, within the 607-byte ceiling.

## Exact permitted sequence

1. GO after the matching build/image checks for one known-Mac RAM boot with `sweetmprobe=1 sweetusb=4`, with `sweetmpss`, `sweetmglink`, `sweetmrmtfs`, `sweetmpdm` and `sweetmuim` absent. Refusal is permitted diagnostic evidence. Require no PAS/shared-memory-write/vote activity, clean selftest and recovery. Require firmware, SMEM, PAS-support, RSC and complete live CX/MSS/XO resource evidence before advancing. A 32-byte table is now within capacity, but any ambiguity, refusal or unexpected map keeps stage 2 NO-GO.
2. Conditional GO for a later offline boot with exactly one each of `sweetmpss=1 sweetmglink=1 sweetmrmtfs=1 sweetmpdm=1 sweetusb=4`, with probe and UIM absent, only after stage 1 passes. M2 requires ready plus handover, GLINK/QRTR, actual scoped rmtfs/pd-mapper response evidence and verified PAS shutdown with cleanup=ok. Service discovery alone is insufficient. A timeout or incomplete stop ends advancement.
3. Conditional GO for a still later boot adding only `sweetmuim=1` to stage 2, after that earlier boot proves M2 and verified shutdown. M3 requires valid matched card/application status, including an explicit absent-card response if applicable. Preserve slot-query errors and unavailable ICCID as visible results. No SIM evidence can be inferred from ast148 or an overall success code.

Every run must independently verify `fastboot devices` OR adb recovery and no existing dev.py immediately before starting. Never interrupt owner iOS. The lifted reservation does not replace this check. Use dev.py only, keep each whole run below twenty minutes, and preserve `cpus=8 sweetfbarm=1 sweetpanicfb=10 sweetdeadman=600 sweetselftest=1 sweetthermal=3`, keep plus astwait, UBWC and fetchsync. The older `run-offline.py` only waits for dev.py and names the older non-USB image, so it is not a sufficient standalone launcher for these scopes.

Continue through these gates toward M3 without routine owner approval. A failed gate requires evidence review or a scoped fix, not automatic widening of commands. Stop for an owner-only blocker. Unverified shutdown must retain buffers and power until guarded recovery/reset.

Never partition-write, flash, boot stock, restore Android, change active slots, persist EFS changes, use sudo, or write outside the workspace or `/tmp`. No TFTP, online mode, IPA, data call, NAS/WDS or raw QMI bridge. No permanent deletion. Removals require `/usr/bin/trash` with an absolute path. Keep firmware, dumps, raw logs, serials and full identifiers private, with identifier output limited to suffix four.

Ast148 remains refusal before hardware writes, no PAS, selftest 353 seconds and root clean. It establishes neither M2 nor SIM state. Not checked here: packed image identity, live cmd-db tables, combined hardware behavior, actual PAS startup/shutdown, M2 or M3. Verdict: conditional GO for the sequence above.
