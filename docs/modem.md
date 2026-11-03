# Modem and SIM: desk findings (2026-11-03)

M3 passed in ast161. ast168 now confirms sustained selected-USIM READY with19 card replies, one slot reply and no freshness expiry. Patch2188 fixed the ten-response QRTR stall. Patch2189 adds600-second data sessions, but its source review found two ordering gaps being fixed before integration approval. PAS shutdown still uses the accepted safe retention path, so M2 remains incomplete. ONLINE is off pending IPA and the combined-image audit.

Labels as in `docs/input.md`: **Measured**, **Code**, **Inferred**, **Missing**.

## Facts

- **Code, firmware.** The `modem_a` partition (FAT16, 4096-byte sectors) holds `image/modem.mdt` and `modem.b01` to `b30`. That is 29 relocatable load segments plus a hash segment, about 109 MB, spanning 0xd700000. It fits in `mpss_mem` (0x8ac00000 + 0xe600000). There is no `modem_dtb.mdt`. Mainline Linux runs this SoC family's modem as PAS id 4 without a DTB image. `plans/modem/tools/fatls.py` lists and extracts it read-only. The firmware stays out of this repo.
- **Code, DT.** `remoteproc-mss@04080000`, compatible `qcom,volcano-modem-pas`. Its regions are `mpss_mem`, `q6_mpss_dtb_mem` (0x99200000 + 0x80000) and a third (phandle 0x7a). Its interrupts are wdog, fatal, handover, ready, stop-ack and shutdown-ack. It has a "stop" SMP2P state, GLINK edge "mpss" (remote pid 1, IPCC mailbox) and AOP QMP signalling.
- **Code, memory.** XNU's RAM is 0x8c0000000 + 0x134000000, so the kernel cannot allocate below 4 GB. rmtfs needs a 2.5 MiB buffer below 4 GB. The bank at 0xe0a00000 is the only free low RAM, and 0xe1000000 to 0xe2000000 is unused.
- **Code, storage.** `sweet_ufs_read_part()` reads `modem_a` and the EFS partitions read-only. The kernel's UFS write path refuses anything outside `userdata`. The modem's EFS (`modemst1`, `modemst2`, `fsg`) holds the IMEI and RF calibration. It must never be written: serve it read-only, and keep any writes in RAM.
- **Code.** The modem series now includes an apps-RSC RPMh client coordinated with touch and an AOSS QMP sender. ast151 exercised them before PAS authentication.
- **Code, reuse.** The Poco F1 port booted SDM845's modem on this same xnu-12377 (`ios-asteroids/xnu/patches-12377/1104` to `1116`: SMP2P, rmtfs over QRTR and GLINK, tftp, pd-mapper). SDM845 used MBA, while this phone uses PAS. rootstock's `ref/UniXNU-asteroids/package/` has SMEM, SMP2P, GLINK, QRTR, QMP and an ADSP start for this phone.
- **Inferred, from HakiOS (`ref/hakios-xnu-kernel`, SC7280).** The modem announced UIM about 30 s after rmtfs was served, while still offline. Going online without IPA crashed the modem. So reading SIM status needs rmtfs, and likely tftp and pd-mapper too, but not IPA.

## State

- `plans/modem/src/lift/` holds unfinished drafts that do not compile. The dedicated `ios-asteroids/research/xnu-modem` tree now has the requested daily series, USB v5 and modem patches 2170 through 2183. Other kernel trees were not edited.
- The owner wants mobile data only. Calls, SMS and status-bar changes are excluded. A wired-interface approach may avoid Apple baseband integration for data, but still needs a working modem and IPA path.

## Patch 2170, read-only preflight

**Code.** `patches/modem/2170-asteroids-modem-preflight.patch` adds `sweetmodem=1` after UFS initialization on the Nothing board. Absent or zero is inert. Other levels are refused. It validates FAT16 geometry against partition size, bounds every read to modem_a, and checks split ELF32 segment sizes, overlap and the MPSS reservation span. It reads the last byte of each load segment through its FAT chain. It does not authenticate firmware or start hardware.

**Measured, host only.** `tools/modem/check.sh` compiles the source extracted from the public patch with AddressSanitizer and UndefinedBehaviorSanitizer. Synthetic tests pass for boot gates, FAT bounds/cycles, MDT truncation/overflow/span and partition restrictions. Local evidence is `plans/modem/logs/host-check.txt:9`. No phone identifiers or firmware are test fixtures.

**Code.** No UFS write, SMC, MMIO mapping or memory-ownership call exists in the new source. The only storage reader calls `sweet_ufs_read_part("modem_a", ...)` after checking offset and length. This describes the modem patch, not other kernel drivers.

**Missing.** Full kernel build, real firmware acceptance, a Sol review and phone preflight. The build's MakeInc.cmd defines `/bin/rm -f`; install uses it for temporary files and symlinks. Configure recoverable Trash cleanup before the required --remake build. PAS, QMP, coordinated RPMh, SMP2P, GLINK, QRTR and rmtfs remain to implement. The old draft's exclusive DRV2 assumption conflicts with the touch driver's idle-TCS selection and shared IRQ register updates.

## Revised milestones for mobile data

M1 remains the guarded PAS/IPC/rmtfs implementation and host checks. M2 requires ready and handover bits, announced QRTR services and a verified stop in phone logs. M3 requires UIM card status, application state and at most the last four ICCID digits. A modem response saying no SIM satisfies M3.

### M4, IPA prerequisites and online NAS registration

**Code, DT.** `asteroids.dts:23195` contains a disabled generic `qcom,ipa_fws` node naming `ipa_fws` and PAS id 0x0f. The actual `qcom,ipa@3E00000` node also names that firmware and PAS id. Its `memory-regions` points to phandle 0x26d, the GSI reservation at 0x9ea90000 of size 0xa000. The adjacent IPA reservation is 0x9ea80000 of size 0x10000. Do not choose the adjacent region merely from its name.

**Code, DT.** IPA base is 0x03e00000 with length 0x84000; GSI base is 0x03e04000 with length 0xfc000. The configuration offset is 0x140000. The hardware version property is 0x17, which the matching vendor header identifies as IPA 5.2. EE is zero. Core clock, three interconnect paths, IPA/GSI interrupts and IPA SMP2P mappings are specified. AP and microcontroller SMMU streams are 0x4a0 and 0x4a2, with IOVA pools at 0x20000000 of size 0x20000000. The 64-bit DMA property does not remove SMMU mapping requirements.

**Code, reference.** HakiOS `drivers/modem/hakios.ipa.c` targets IPA 4.11/GSI 2.11 and describes command, LAN RX, modem TX and modem RX endpoints, GSI rings and shared routing/filter tables. These are requirements to investigate, not verified SM7635 register offsets or endpoint IDs. Its PAS loader explicitly warns that loading the adjacent wrong reservation caused unreliable modem behavior.

**Code, matching vendor source.** Nothing's `sm7635/v/mr` source at commit `d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7` defines `IPA_HW_v5_2 = 23` in [msm_ipa.h](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/vendor/qcom/opensource/dataipa/drivers/platform/msm/include/uapi/linux/msm_ipa.h#L1073). Its [ipa3_get_gsi_ver](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/vendor/qcom/opensource/dataipa/drivers/platform/msm/ipa/ipa_v3/ipa.c#L7719) selects GSI_VER_5_2. Use these tables rather than copying HakiOS's IPA 4.11/GSI 2.11 register assumptions. This identifies the software configuration, not a live hardware version read.

**Measured, local firmware inventory.** The stock modem_a backup contains `ipa_fws.mdt` and b00 through b04. The MDT is 4204 bytes. Its three relocatable load segments total 17824 file bytes and occupy a page-aligned span of 0x7000, fitting the DT's 0xa000 GSI reservation. The load addresses begin at zero and require relocation to the selected reservation. The hash segment is metadata, not an executable load. No IPA authentication or MMIO operation has run.

**Missing.** Read the live IPA/GSI version, verify the complete load path and matching endpoint/register tables, arrange clock/interconnect votes, isolated SMMU mappings, bounded DMA buffers and a stop/reset sequence. Implement the modem's IPA QMI setup handshake and tables before online mode. Firmware authentication alone is insufficient. Fresh Sol GO is required before booting these changes. Only then request DMS online and record NAS registration and serving system. Two matching resets stop that attempt.

**Code, clock prerequisite.** DT clock index 12 is RPMH_IPA_CLK. The matching [volcano clock table](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/drivers/clk/qcom/clk-rpmh.c#L903) selects the command-db BCM resource `IP0`. Its rate vote uses command-db unit metadata and BCM encoding, not an ARC voltage index. The vendor IPA probe enables this clock before mapping and reading IPA registers. First inspect command-db metadata, then audit bounded clock/interconnect setup before live IPA register reads.

**Code, IPA handshake.** The vendor [IPA QMI service](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/vendor/qcom/opensource/dataipa/drivers/platform/msm/ipa/ipa_v3/ipa_qmi_service.c#L27) uses service 0x31, version 1, AP instance 1 and modem instance 2. ast151 already announced the modem endpoint as packed instance 0x201. The setup passes real header, route, filter and shared-memory ranges to the modem. The generated protocol defines indication registration 0x20, modem-driver initialization 0x21, master initialization indication 0x22 and modem completion 0x35. Do not advertise completion before those tables and DMA mappings exist.

### M5, WDS data call

**Missing.** Read provisioned profiles without changing persistent profiles, obtain the SIM/carrier APN, negotiate WDA framing and bind the correct endpoint and mux before WDS start. Request the IP family and parse assigned IP/prefix, gateway and DNS from runtime settings. Use transient call parameters only. Record the call handle for teardown. Do not guess an APN or treat a WDS success response as packet delivery.

### M6, IPA/rmnet packets

**Missing.** Implement the SM7635 GSI command/TX/RX/event rings, IPA routing/filter/header tables and QMAP mux framing. Bound DMA lengths and aggregated packet parsing, maintain cache coherency, and reject unknown mux IDs. Prove a returned packet with counters and a bounded network test, then verify stop drains DMA before freeing memory. WDS stop must run on success and failure exits before modem teardown.

**Code, endpoint reference.** The matching vendor [IPA 5.2 endpoint table](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/vendor/qcom/opensource/dataipa/drivers/platform/msm/ipa/ipa_v3/ipa_utils.c#L5203) and `ipa_gsi_ep_config` identify these endpoint/channel pairs. These are source values awaiting live verification.

| Client | IPA endpoint | GSI channel | Execution environment |
| --- | ---: | ---: | --- |
| AP command producer | 9 | 6 | AP |
| AP LAN producer | 6 | 4 | AP |
| AP LAN consumer | 11 | 7 | AP |
| AP WAN producer | 2 | 5 | AP |
| AP WAN consumer | 18 | 9 | AP |
| Modem WAN producer | 7 | 0 | Modem |
| Modem LAN consumer | 12 | 3 | Modem |

### M7, iOS interface

**Code.** `iokit/Kernel/arm/SweetNetwork.cpp` already exposes `sweet_net_publish(bsd_name, prefix, unit, mac, ip, mask, router, dns1, dns2)`. It publishes the IOKit Ethernet properties that configd uses to adopt the USB BSD ifnet. Its network configuration overrides already accept manual IPv4, router and DNS values.

**Inferred.** Reuse that facade for a distinct modem ifnet after M6, supplying the measured WDS configuration. This is the smallest first test of the owner's wired-Ethernet idea. DHCP is a fallback if configd will not adopt the WDS settings directly. Keep USB separate and verify interface withdrawal or reconfiguration when the call ends or its address changes.

**Missing.** IPA carries IP/QMAP, not Ethernet. The adapter must translate Ethernet/ARP or provide a private routed Ethernet link with DHCP and a matching gateway; IPv6 needs its own routing/RA design. A virtual Ethernet label alone does not configure the WDS address or DNS. Test configd adoption, route selection, DNS and app traffic on this iOS build. CommCenter emulation is not planned. No status-bar sysctl is planned.

## Follow-up 2171

**Measured, host.** The original preflight fails against the existing modem_a backup with MDT lookup error 2. VFAT long names map to unrelated short aliases. Patch 2171 adds one-slot VFAT lookup with sequence, ASCII and checksum validation. The real-image candidate now reports 29 load segments, 109371307 bytes, span 0xd700000. Local evidence is `plans/modem/logs/real-image-check-candidate.txt:1`. This is not a live phone result or firmware authentication.

**Code.** 2171 also rejects repeated file clusters, free terminal clusters, inconsistent zero-memory loads and numeric boot-argument aliases. The boot gate accepts the exact text `1`. Synthetic checks reconstruct and compile both patches, including bad VFAT checksums and the file-chain cases identified by Sol.

**Measured, source review.** Sol approved exact 2170 read-only inspection conditionally on a successful full kernel build, with no approval for modem start. The audit is `docs/modem-2170-audit.md`. 2171 needs its own review. The full 2170 build is running in the dedicated tree with recoverable cleanup. Its known file-lookup failure makes it unsuitable for the phone test; apply 2171 and rebuild first.

**Code, build safety.** The dedicated tree's local MakeInc.cmd redirects RM/RMDIR to a wrapper that invokes only `/usr/bin/trash` on absolute build-tree paths. PATH routes unqualified shell cleanup to it. Make preserves intermediate and failed targets. Host checks confirmed that trashing a symlink retains its target and that outside paths are rejected. Do not use MAKEFILES to insert these rules: the build derives its top-level file from MAKEFILE_LIST.

**Measured, build.** Both 2170 and the corrected 2170+2171 kernel installs completed with exit 0. The latter log is `plans/modem/logs/build-2171.txt`, ending in `done building xnu`. `tools/modem/build-probe.py` built `plans/modem/out/preflight-boot.img`, 8044544 bytes, with 598 bytes of boot arguments. It includes sweetmodem=1, sweetfbarm=1, sweetdeadman=600 and sweetselftest=1. The corrected-patch audit and phone run are pending. The USB test still owns the phone; do not start a competing devloop.

**Measured, review and build.** Sol approved 2170+2171 for read-only inspection, conditional on a successful full build; `docs/modem-2171-audit.md` records the exact source hashes. The full build passed. A separate build-tool finding was fixed: kextsymboltool now retains failed output instead of calling unlink, and the rebuilt helper's error-path test retains the existing output. No unlink import remains in that helper. The subsequent full install passed and the RAM image hash stayed unchanged. A retained failed output must be Trashed before retrying a failed build.

**Code, boot parser correction.** The platform parser turns bare `sweetmodem` into `1` and uses the last duplicate value. The image builder emits one explicit `sweetmodem=1`; the source-level claim of requiring an equals sign was too strong. Approval remains confined to read-only inspection. No hardware stage is enabled.

## Phone preflight, ast146

**Measured.** `ios-asteroids/logs/ast146-modem-preflight.txt:222` reports PASS with 29 load segments, 109371307 bytes and span 0xd700000. Line 223 reports error=0 and MPSS untouched. The run used the audited 2170+2171 image, returned after 130 seconds, and left OrangeFox recovery with a clean root. Logs and firmware remain local.

**Measured, harness limit.** The loop verdict is selftest-no-frames because the minimal script took no screenshots. This does not negate the logged firmware checks. It does not prove MPSS start, QRTR services or SIM detection. The root retains the inert-without-sweetselftest devloop hook in `/usr/local/libexec/renegade-firstboot.sh` and normal selftest output. No firmware or modem binary was pushed.

## IPC groundwork, 2172

**Code.** `patches/modem/2172-asteroids-modem-smem-glink-core.patch` adds the existing portable UniXNU SMEM/GLINK core with extent and length fixes. SMEM revalidates partition/free markers, bounds cached and uncached data, and rejects allocation overflow or corrupt lookup results. GLINK rejects impossible RX frames instead of waiting indefinitely, and bounds TX length before arithmetic. These modules have no production caller or hardware mapping yet, so they do not start IPC.

**Measured, host only.** `sh tools/modem/check-protocol.sh` reconstructs the public patch sources and runs sanitizer-backed tests. They cover corrupt SMEM extents/allocation overflow, GLINK version/channel/intent exchange with two simulated peers, IPCRTR payload delivery and malformed RX/TX lengths. Local evidence is `plans/modem/logs/protocol-check.txt:1-2`. The 2172 kernel build is still running. It was not used for ast146.

**Missing.** PAS, power/QMP arbitration, SMP2P, QRTR integration and RAM-only rmtfs remain required for M1. No phone modem start or UIM request has occurred. The protocol tests are not M2 evidence.

## Offline MPSS candidate, 2173

**Code.** `patches/modem/2173-asteroids-modem-pas-offline.patch` adds PAS4 start with stock signed firmware read from modem_a, QMP, coordinated RPMh votes, SMP2P, the mpss GLINK edge over IPCC, QRTR and optional pd-mapper. The gates are `sweetmpss=1`, `sweetmglink=1`, `sweetmrmtfs=1` and `sweetmpdm=1`. Each rejects bare, duplicate or nonexact values. IPC requires MPSS. rmtfs and pd-mapper require IPC.

**Code.** rmtfs snapshots modemst1, modemst2, fsc and fsg through the existing read-only partition API before startup. Its protocol handler has no storage callback. Reads and writes copy only between the snapshots and the low shared RAM window. Shutdown requests stop-ack, calls PAS shutdown, reclaims shared memory and releases power. An unverified stop retains buffers and power until reboot. Cleanup errors retain buffers. The offline run is bounded to 120 seconds after authentication. No online QMI command exists.

**Measured, host only.** `sh tools/modem/check-start.sh` passes sanitizer-backed QRTR/QMI, rmtfs, gate, QMP, SMP2P and command-db checks. It extracts the actual SCM retry and cleanup functions from the patch for fault tests, including failed shutdown and failed memory reclamation. `check.sh` and `check-protocol.sh` still pass. Full 2173 kernel build and final remake completed with exit 0. Local evidence is `plans/modem/logs/start-check.txt:1-10` and `plans/modem/logs/build-2173-final.txt`.

**Missing.** Sol approval for this candidate is pending. The inherited XO state and shared power votes require review. TFTP will be added if requested by the modem. UIM is staged separately and has not run. The ast146 preflight is still the only modem-related phone evidence.

## 2174, reviewed first-start candidate

**Code.** `2174-asteroids-modem-xo-and-glink-completion.patch` fixes the two blocking 2173 review findings. GLINK keeps a delivered intent busy until its completion can enter TX. It retries after backpressure without delivering the packet twice. XO is explicitly requested with enable mask 3 before PAS. CX/MSS use their highest supported cmd-db indices. Cleanup releases only MSS after verified PAS shutdown. Shared XO/CX floors remain until reset, preserving inherited dependencies. This is a bounded bring-up policy, not suspend support.

**Measured, review and host.** `docs/modem-2173-audit.md` records the original NO-GO. `docs/modem-2174-audit.md` gives GO for one guarded offline test of exact 2170 through 2174. The final full build passed at local `plans/modem/logs/build-2174-final.txt:5774`. Host tests cover both GLINK completion modes and the actual vote function's XO acknowledgment, MSS-only release, IRQ preservation and timeout retention. No UIM, online-mode or TFTP approval is included.

**Code.** The first-start image has 599 bytes of arguments, the four reviewed stage gates, sweetfbarm=1, sweetselftest=1 and sweetdeadman=600. The selftest waits 240 seconds and takes one frame before normal recovery. A phone result must show ready, handover, QRTR services and verified PAS shutdown. Intentional XO/CX floors are distinct from an uncertain or incomplete cleanup.

**Missing.** The first offline test is in progress. UIM remains a separate staged change. USB v5 SSH can help later NAS/WDS iteration. A root-only QMI request interface would need bounded buffers, request/reply matching, timeouts, a default-off gate and a fresh audit before use. It is deferred until it saves more time than a controlled rebuild.

## ast148, safe refusal before PAS

**Measured.** Local `ios-asteroids/logs/ast148-modem-offline.txt:4331` records error 22 after 4 ms. Line 4332 confirms no PAS activity and cleanup=ok. Firmware preflight passed at line 222. No power vote or shared-memory initialization was reached. The selftest completed after 353 seconds with a clean root. M2 remains incomplete. No modem crash or reset was observed.

**Inferred.** The failure is in the CX/MSS/XO resource lookup or level validation. The old log cannot distinguish those cases. Do not waive the check or attribute the refusal to a missing clock without another measurement.

**Code.** Patch 2175 adds strict `sweetmprobe=1` for read-only hardware prerequisite inspection. It reports command-db lookup errors and bounded level tables, then exits before any shared-memory write if `sweetmpss` is off. Patch 2176 prepares strict `sweetmuim=1` for UIM Get Card Status and Get Slot Status. It sends only those two read requests, matches endpoint and transaction, bounds retries, and emits at most four ICCID digits. The client has no generic QMI injection API. Wire layouts follow [libqmi's UIM schema](https://github.com/linux-mobile-broadband/libqmi/blob/main/data/qmi-service-uim.json).

**Measured, host.** `check-uim.sh` covers present/absent card status, app state, suffix-only BCD parsing, malformed lengths, actual sender gates, request opcodes, reply matching and retry limits. Existing protocol and lifecycle checks still pass. The actual power inspector is tested against valid, missing and malformed resource data. These are synthetic checks, not SIM evidence.

**Missing.** A combined Sol review and matching build are pending for 2175, 2176 and USB v5. The next phone step is the read-only power probe. UIM stays off until a prior modem run proves M2. The owner has the phone after ast148; verify fastboot or OrangeFox before any next run. The last measured modem-run root was clean, with the inert firstboot hook and normal selftest files.

## 2177, ARC level clamping

**Inferred.** Read-only analysis of the local stock AOP image finds a candidate MSS table whose maximum is 320. The prior minimum of 384 rejects that table. Its relationship to the live command database still requires the phone probe. No firmware bytes or table dump are included here.

**Code.** [Linux rpmhpd](https://github.com/torvalds/linux/blob/master/drivers/pmdomain/qcom/rpmhpd.c) clamps requests above the supported maximum. Patch 2177 follows that behavior and retains the highest complete CX/MSS proxy floor. It exposes the original aux length, refuses truncated or malformed tables and reports the chosen index. It does not substitute a resource address or claim the live refusal is resolved.

**Measured, host.** The actual power inspector passes the maximum-320 regression and rejects oversized, odd, short and descending tables. The UIM suite now explicitly rejects a matching response before its first request. The combined USB/modem full build and final remake passed. Every rebased kernel segment in the three prepared stage images matches the built kernel. Their command lines contain 553, 591 and 603 bytes.

**Missing.** Fresh 2177 Sol review and the live read-only power probe precede another PAS attempt. The previous audit is `docs/modem-2175-2176-audit.md`; its staged approvals do not cover an unreviewed power change. The owner has lifted the time reservation, but every run still requires fastboot or OrangeFox and no active dev.py.


## Power probe, ast149

**Measured.** The combined 2170 to 2177 and USB v5 build passed Sol review in `docs/modem-2177-audit.md`. ast149 confirmed live command-db resource lookups and complete active maps. MSS ends at 320, so the old minimum-384 guard caused the earlier refusal. Local log `ios-asteroids/logs/ast149-modem-power-probe.txt:6620` shows the bounded selection. Line 6632 reports hardware preflight error 0. Line 6634 reports no PAS activity and cleanup ok.

**Measured, limit.** The loop returned after 230 seconds with a clean root, but its persistent console lacks the final selftest marker. The verdict is reset-unknown. A screenshot and a clean filesystem do not prove the termination path. SSH over USB worked and logged USB counters had no stale or lost TX. No modem was started.

**Code.** The next read-only probe keeps the exact kernel and image. Its selftest preserves previous frames, checks screenshot success, writes a fresh completion token and waits 15 seconds after DONE before reboot. This fixes evidence collection without widening the modem gates.

**Missing.** Recorded clean probe completion still gates the offline PAS run. No M2 or SIM evidence exists yet.


**Measured, ast150.** The repeated probe met the completion conditions. Local log `ios-asteroids/logs/ast150-modem-power-probe-repeat.txt:4970` reports hardware preflight error 0, line 4972 records no-PAS cleanup ok, and line 10587 records DONE. The fresh completion token and checked frame match this run. It returned after 245 seconds with a clean root, no stale files, no fired deadman or panic, and no USB stale/lost TX. The separately reviewed offline image is now being tested with UIM off.


## Offline modem boot, ast151

**Measured.** MPSS authenticated and booted. Local log `ios-asteroids/logs/ast151-modem-offline-usb.txt:8753` records PAS authentication success, line 8766 ready and handover, and line 8771 the GLINK mpss/IPCRTR channel. Lines 8955 and 8966 record actual pd-mapper and rmtfs responses. Line 10775 announces UIM service 0x0b. NAS and WDS services also announced while the modem stayed offline. No TFTP server ran during this boot.

**Measured, failure.** After the bounded 120-second runtime, SMP2P stop acknowledgment arrived at line 11697. PAS shutdown returned raw status -3145800 at line 11698. The next line reports STOP UNVERIFIED and retains memory and power. The selftest completed in 366 seconds and recovery found a clean root. M2 remains incomplete because shutdown did not verify. No UIM request was sent.

**Code.** Patch 2178 clears the stop request after acknowledgment or timeout, following [Linux qcom_q6v5_request_stop](https://github.com/torvalds/linux/blob/v6.12/drivers/remoteproc/qcom_q6v5.c#L196). It applies [Linux SCM error mapping](https://github.com/torvalds/linux/blob/v6.12/drivers/firmware/qcom/qcom_scm.h#L160) to shutdown transport failures, preserving raw logs. Unknown negative transport errors map to -EINVAL, so the existing bounded [decrypt-shutdown poll](https://github.com/torvalds/linux/blob/v6.12/drivers/remoteproc/qcom_q6v5_pas.c#L188) can retry them. Nonzero results never establish a verified stop. Cleanup failures and missing handover now reach the final worker error.

**Inferred.** The missing stop-bit clear and raw-versus-mapped error handling are source differences that can explain shutdown failure. The vendor error itself has not been decoded.

**Measured, host.** Production PAS and cleanup tests pass for transient and permanent vendor failures, bounded retries, known fatal errors, stop-bit clearing on acknowledgment and timeout, and retaining buffers after failure. Preflight and UIM tests also pass, and the matching kernel remake finished successfully.

**Measured, review.** `docs/modem-2178-audit.md` gives a conditional GO for one matching offline image. Its ten rebased kernel segments match the successful build. The retry waits for the queued phone test to finish. UIM remains off until measured M2 and a fresh Sol review.


**Code, additional shutdown reference.** Nothing's [volcano MPSS descriptor](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/drivers/remoteproc/qcom_q6v5_pas.c#L2502) selects free-after-auth behavior, which enables `retry_shutdown` in its probe. Its [qcom_scm_pas_shutdown_retry](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/drivers/firmware/qcom_scm.c#L860) retries failed shutdown calls at 100 ms intervals up to 50 times. This independently supports bounded retries on this board family. It does not establish the meaning of ast151's vendor status.


## ast153 and sysmon shutdown candidate 2179

**Measured.** ast153 again reached ready and handover and discovered modem services. `ios-asteroids/logs/ast153-modem-offline-2178.txt:10088` records stop-ack and request-bit clearing. Lines 10089 through 10138 show all 50 PAS shutdown calls returning raw status -3145800. Lines 10139 and 10140 retain memory/power and report final_error=5. M2 did not pass. No UIM request was sent.

**Measured, harness.** The selftest stopped before its body because the repeated phone RTC and PID produced an existing frame archive name. The 600-second deadman returned the phone to recovery. result.json records root clean and stale=false. The collision-safe archive loop now tries bounded numeric suffixes and preserves all old frames. Its host test exercises three archives with the same timestamp and PID. No modem-caused reset occurred.

**Code.** Linux's [sysmon shutdown](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/drivers/remoteproc/qcom_sysmon.c#L359) sends QMI service 43 request 0x21 before remoteproc PAS shutdown. Volcano's sysmon instance is 0x12. ast153 announced packed instance 0x1202 at line 7236. Our earlier runtime omitted that request.

**Code.** Patch 2179 adds a fixed SSCTL client behind `sweetmstop=1`, default off. It binds replies to the selected modem endpoint, accepts only the matching shutdown transaction, and waits at most ten seconds for success plus the shutdown indication or DT shutdown-ack bit 7. It continues RAM-backed rmtfs service during that wait. Missing service, malformed replies, timeout or failure fall back to the existing SMP2P stop and PAS attempt. Only actual PAS success allows reclaim. No generic QMI interface or online command is added.

**Measured, host.** The actual extracted sender and response handler pass sanitizer tests for endpoint binding, truncation, wrong transaction/opcode, duplicate result TLVs, stale acknowledgment, timeout and transport failure. Existing lifecycle tests cover the graceful path skipping the stop bit while still requiring PAS success. `plans/modem/logs/start-check-2179.txt` and `uim-check-2179.txt` pass. The full Trash-safe --remake completed with exit 0 in `build-2179.txt`.

**Inferred.** Missing sysmon shutdown may explain the failed stop, but the secure error has not been decoded. This candidate follows the matching kernel lifecycle and does not establish a hardware fix. **Missing.** Fresh Sol GO, packed-image checks and a clean offline result before the separate UIM trial.


## Shutdown result correction 2180

**Code.** Sol's 2179 review approved one diagnostic offline boot and found that a transport or fatal error first observed during shutdown could leave final_error=0 if PAS subsequently stopped successfully. Patch 2180 logs both flags and returns failure even when memory cleanup succeeds. It changes reporting and acceptance, not the hardware sequence. The active 2179 phone run does not contain this later patch.

**Measured, host.** The added combined test uses the real SSCTL request/response functions, QRTR dispatch, RAM rmtfs handler, SCM/PAS wrapper and cleanup. A simulated modem reads and writes RAM during shutdown, then acknowledges. Cases also cover timeout fallback, permanent PAS failure retaining buffers, and shutdown-time transport/fatal errors with successful PAS. All pass under sanitizers. MMIO, SMC and IPC delivery are simulated; this does not prove hardware shutdown. The full --remake build exited 0. No 2180 phone approval exists yet.


## ast155 and read-only BCM survey 2181

**Measured.** `ios-asteroids/logs/ast155-modem-offline-2179.txt:10521` sends SSCTL shutdown. Line 10522 receives result0/error0. Line 10524 records the second RAM-only rmtfs write. Line 10526 records the response, indication and new shutdown-ack bit all accepted. All 50 subsequent PAS shutdown attempts still return -3145800. Lines 10585 and 10586 retain memory/power and report final_error=5. No UIM request was sent.

**Measured, harness.** ast155 finished normally in 361 seconds, with one fresh screenshot, the matching nonce, DONE at line11342, root clean and stale=false. The frame archive correction works on the phone. This is not M2 because PAS did not verify shutdown.

**Code, vendor prerequisite.** The matching [PAS driver](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/drivers/remoteproc/qcom_q6v5_pas.c#L1124) votes crypto-to-DDR bandwidth before PAS shutdown. Its helper requests UINT_MAX average and peak bandwidth. Our runtime does not yet make these interconnect votes. The [volcano interconnect table](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/drivers/interconnect/qcom/volcano.c#L1661) maps the path through CE0, SN3, SN0, SH1, SH0, MC0 and ACV. **Inferred.** Missing votes are a candidate cause, not a decoded explanation of the secure error.

**Code.** 2181 adds `sweetmbcm=1`, default off, to the read-only `sweetmprobe=1` survey. It logs bounded command-db metadata for those seven resources and IPA's IP0. BCM auxiliary data is a little-endian 32-bit unit, 16-bit width, byte VCD and reserved byte. It is not an ARC voltage list. The code copies at most eight bytes after the existing extent checks and issues no BCM vote, PAS start, shared-memory write or new mapping.

**Measured, host.** Start/control, metadata and joined shutdown tests pass in `plans/modem/logs/start-check-2181.txt`. They cover valid metadata, missing resources, truncated auxiliary data, opted-out behavior and unchanged ARC decoding. The full --remake exited0 and the new BCM image's ten rebased segments match kernel SHA256 `93f7555614ce18a9a2077caf92ed0cb282ece47b39eef99e9ad89418338c5f6a`. Image SHA256 is `1dc152b77e3bc705717fbfeb072c96d5f543e40072a3ad123905cdaed32fcfe4`. Sol review is running. **Missing.** Live BCM metadata and an audited interconnect implementation before another modem start.


## ast157 BCM survey and 2182 handover candidate

**Measured.** ast157-modem-bcm-probe-2181 completed in 244 seconds with fresh completion nonce, one frame, selftest DONE and clean root. Local log lines 6659 to 6674 contain all eight resource records. Line 6678 reports hardware preflight error 0, line 6680 confirms no PAS activity and cleanup ok, and line 6687 reports final_error 0. No modem start or clock vote ran.

| Resource | Address | Unit | Width | VCD |
| --- | --- | ---: | ---: | ---: |
| CE0 | 0x50034 | 7935 | 8 | 6 |
| SN3 | 0x5001c | 530000 | 8 | 3 |
| SN0 | 0x50010 | 530000 | 16 | 3 |
| SH1 | 0x50008 | 4200000 | 32 | 1 |
| SH0 | 0x50004 | 4200000 | 32 | 1 |
| MC0 | 0x50000 | 1600000 | 8 | 0 |
| ACV | 0x50068 | 1 | 0 | 12 |
| IP0 | 0x50038 | 40 | 8 | 7 |

**Measured.** Every record used slave 5, version 0x1000, eight auxiliary bytes and reserved value zero. ACV has VCD 12. Do not copy the unpublished draft's assumed VCD limit of 10 into a future vote implementation. No BCM write is implemented or approved by this survey.

**Code.** Patch 2182 adds strict default-off sweetmho=1. Once MPSS has started and both ready and handover bits are present, it releases the modem-only MSS vote through the existing serialized RSC path. Shared CX and XO stay pinned. A failed release marks the run failed and retains uncertain TCS ownership. PAS shutdown remains mandatory, and an unverified PAS stop still retains low memory and power until reset.

**Code.** The DT mx-supply phandle 0x77 resolves to mss.lvl. The matching vendor qcom_pas_handover calls disable_regulators, which removes its proxy votes. This supplies the basis for 2182. It does not prove that the retained MSS vote caused the secure shutdown error.

**Measured, host.** The full Trash-safe --remake build exited 0. Sanitizer checks exercise off, incomplete handover, fatal/not-started, successful one-time release and release timeout. The existing joined QRTR/rmtfs/SSCTL/PAS checks pass. The offline and UIM images contain all ten expected kernel segments and have 593-byte and 605-byte argument lines.

**Missing.** Sol GO and phone evidence for 2182. Only the offline image is submitted for review. UIM remains off until verified M2 and a fresh UIM audit.

**Code, integration.** IPA owns 2200 through 2219 and data owns 2220 through 2239. The modem worker is the sole QRTR owner. Ports 0x4004 and 0x4005 are reserved for IPA, which must add two bounded local slots. No userland raw-QMI interface exists in this series. The current API and patch cutoff are in plans/modem/README.md.


## ast158 result and 2183 bandwidth candidate

**Measured.** ast158-modem-offline-2182 released the MSS proxy at line7173 and accepted SSCTL shutdown at10636. PAS still returned -3145800 on every attempt. Lines10704 and10705 retain memory and report final_error5. The selftest reached DONE at11443 in364 seconds, with a fresh frame/nonce, stale=false and clean root. Proxy release alone did not resolve the shutdown failure. No UIM request ran.

**Code.** 2183 adds default-off sweetmbw=1. It validates the seven crypto-to-DDR BCM descriptors against the read-only ast157 values, including ACV VCD12. It derives the vendor UINT_MAX bandwidth request from node bus widths and channel counts, requires the result to saturate each rate field, and preserves all legal enable/performance bits for the enable resources. The resulting shared floor remains until reset. IP0 is not touched.

**Code.** The RSC path checks hardware command capacity, owns the existing shared touch/modem lock, chooses one idle active TCS and submits seven commands in VCD order. Commit and wait bits appear only on the last command in each VCD group. It requires a fresh completion IRQ plus acknowledgements for all seven commands. Timeout preserves the programmed TCS and marks cleanup incomplete. The vote happens before PAS initialization and stays present through shutdown. Partition access remains read-only.

**Measured, host.** Sanitizer checks cover the observed metadata, malformed/version-mismatched descriptors, rate saturation, enable masks, VCD ordering, absent gate, fresh versus stale completion, partial completion timeout, retained uncertain state and all three TCS choices. The actual handover-to-shutdown joined tests added after the 2182 review also pass. They exercise PAS attempts and buffer retention after a failed proxy release.

**Measured, build.** The full Trash-safe --remake exited 0. Both images contain all ten expected kernel segments. Offline arguments are 586 bytes and UIM arguments are 598 bytes.

**Missing.** Sol audit and phone measurement. The bandwidth hypothesis comes from the vendor driver and does not decode the secure error. No further unchanged 2182 run is planned.


## 2184 shared SN3 correction

**Code, review finding.** Sol rejected2183 because touch's later SN3 keepalive replaces the same APPS request. A shared mutex serializes transactions but does not combine requested levels. ast158 log9474 shows touch writing SN3 after modem startup. See docs/modem-2183-audit.md. No2183image booted.

**Code.** 2184 makes the touch SN3 vote use the maximum x/y fields only with the same strict sweetmbw=1 gate. Other resources and absent, bare, duplicate or invalid gate values retain the original keepalive. This preserves the modem floor regardless of thread order without changing the default touch behavior.

**Measured, host.** One mocked RSC now runs the actual touch at_bcm, at_amc and at_amc_locked functions alongside the actual modem bandwidth function. Both startup orders preserve the effective SN3 request. Replacing only at_bcm with the exact pre2184 function makes the regression fail. The full sanitizer suite and Trash-safe build pass. Production runtime and bandwidth encoding are unchanged from2183.

**Missing.** Fresh Sol GO for2184 and one offline phone trial. UIM remains off.


## ast160 bandwidth timeout and independent SIM diagnostic

**Measured.** ast160-modem-offline-2184 timed out in the bandwidth transaction at9736 before any PAS initialization or modem start. Line9740 reports cleanup INCOMPLETE, and9743 reports final_error5. Touch then selected maximumSN3 at10664 and completed its separate TCS1 transaction at10665. The selftest reached DONE at12557 in366 seconds, with fresh nonce/frame and cleanroot. The timeout does not establish that masked IRQ reporting was its cause.

**Code.** 2185 rejects duplicate singleton QMI result/card-status/slot-status TLVs. Sanitizer tests cover conflicting duplicates and retain support for unknown optional TLVs. The client still sends only UIM GetCardStatus0x2f and GetSlotStatus0x47, with at most three attempts each. It never sends a PIN, changes slots or provisioning, or requests online mode. ICCID output remains suffix4 only.

**Code, proposed diagnostic scope.** The next UIM image omits sweetmbw. Its modem power sequence therefore matches the measured2182 run, including MSS proxy handover and shared CX/XO retention. PAS shutdown remains mandatory on all exits. An unsuccessful secure stop retains backing and reports failure until the guarded reset. Reading SIM status under that known limitation requires an explicit fresh Sol review. It would measure M3 independently and would not claim M2 or approve ONLINE.

**Missing.** Fresh Sol approval and actual UIM phone evidence. Bandwidth completion needs its own diagnosis before that stage can be used again.


## 2186 RSC completion reporting candidate

**Code.** Patch2186 adds strict default-off sweetmbwi=1, effective only with sweetmbw=1. It enables reporting for one owned active TCS while polling its fresh completion, following the vendor RSC driver. Before enabling it, the worker reads DT SPI5/INTID37 at GICD0x17100000 and requires positive nonsecure evidence plus disabled and inactive delivery. It never writes the GIC. A zero group and priority refuses the operation as unknown. The existing shared touch/modem RSC lock remains held through the bounded transaction.

**Code.** The worker clears only the selected idle TCS completion before unmasking it, checks that it is clear after unmasking, then retains the existing fresh IRQ, idle and all-command acknowledgment checks. A timeout logs the IRQ state and all seven command statuses, masks its own report and retains the programmed transaction. No MPSS starts after a failed vote. No UFS write path was added.

**Measured, host.** The real transaction and touch code pass sanitizer checks with both raw and IRQ-enable-masked completion models. Tests reject active/enabled/unknown GIC state, mapping failure, stale hidden completion, absent fresh completion and partial acknowledgments. They also check nonzero NS priority with a read-as-zero group register. This model test does not identify ast160's hardware cause.

**Inferred.** Arm IHI0069 section4.8.1 makes secure interrupt priority fields read zero to nonsecure software. A nonzero priority therefore supplies evidence that group-register zero alone cannot supply. See [Arm GIC specification](https://documentation-service.arm.com/static/67eaa4d098aa3c3b6eea7351). No other driver in this test kernel is intended to enable INTID37.

**Missing.** Full build completion, fresh Sol GO and a phone result. The successful UIM2185 image remains frozen and does not contain this change.


## M3 measured in ast161

**Measured.** ast161-modem-uim-2185 completed the separately approved offline SIM diagnostic in363 seconds. Local log ast161-modem-uim-2185.txt:10764 reports a present card in logical slot1. Line10765 identifies its USIM application, type2, state1, detected, with PIN1 disabled. Line10770 reports ICCID suffix0660. This completes M3, SIM seen. It does not establish USIM state7 readiness or mobile data.

**Measured.** PAS authentication succeeded at7968, ready plus handover at7973, MSS proxy release at7977 and GLINK at7981. pd-mapper and rmtfs reads progressed at7996 through8012. SSCTL response, indication and fresh shutdown acknowledgment were accepted at11784. All50 PAS shutdown attempts still returned raw-3145800. Lines11847 and11848 retain backing and report final_error5. The full shutdown window has no transport/fatal error or subsequent ownership reclaim/power drop.

**Measured.** Selftest DONE is at12577, after worker completion. The completion marker matches the frozen script nonce, one checked frame exists, stale=false and root before/after is clean. The phone returned to OrangeFox recovery. Root changes are the inert selftest hook, completion markers and frame archives. No firmware or modem executable was pushed. Acceptance follows the explicit2185 diagnostic audit; the strict M2 checker still fails the known PAS condition and has not been weakened.

**Missing.** M2 still needs verified PAS shutdown. M4 needs a ready USIM, completed IPA setup and a fresh integrated audit before ONLINE. The next UIM change will poll card status and preserve the primary GW provisioning index. HakiOS found an unprovisioned index0xffff can leave an application detected until a provisioning session is activated. That state-changing request is not part of this read-only result.


## ast162 bandwidth completion measured

**Measured.** ast162-modem-offline-2186 completed in362 seconds with normal DONE, a fresh matching nonce/frame, stale=false and clean root. Local console line7766 reads IRQ37 group1=1, priority0, enabled0, active0. The seven-command bandwidth vote completed at7767. PAS authentication succeeded at8519, ready/handover at8536 and MSS proxy release at8542. Touch preserved maximumSN3 at10608 and its transaction completed. This establishes the guarded completion path on this phone.

**Measured.** SSCTL completed at11711. PAS shutdown still returned raw-3145800 fifty times. Lines11778 and11779 retain backing and report final_error5. DONE is12623. No shutdown-time fatal/transport failure or post-retention reclamation was logged. The bandwidth fix did not resolve secure shutdown, so M2 remains incomplete. No UIM or IPA ran in this test.

**Missing.** The secure error still needs diagnosis. The known proxy-vote and bandwidth discrepancies are now corrected but neither changed it. Repeating these same settings will not answer the remaining question. The data agent has the next reserved phone slot; modem work continues at the desk.


## 2187 selected USIM readiness and eight ports

**Code.** 2187 raises QCOM_QRTR_MAX_LOCAL to8 for rmtfs, pd-mapper, UIM, SSCTL, two IPA ports and two data ports. Service-zero clients already remain unannounced, including HELLO replay. A new regression checks all eight registrations, table-full refusal and the exact server-only announcement count.

**Code.** Exact sweetmready=1 requires sweetmuim=1 and refreshes GetCardStatus every5 seconds within the existing120-second worker. It records the primary GW application index. Readiness requires that index to select a present card and a type2 USIM at appstate7. Index0xffff, absent/error cards, other app types and non-ready states cannot signal readiness. Slot status/ICCID still stops after its first successful reply. The absolute card-query cap is180 for the later bounded data session; this120-second probe cannot reach that cap.

**Code.** The client consumes each matching response once, rejects stale transactions and duplicate replies, revokes readiness after15 seconds without fresh card status, and fails on a discovered UIM endpoint changing or disappearing. No PIN, provisioning, slot-switch or online request was added. Data's sweet_data_usim hook will consume this state in the separate integration patch.

**Measured, host/build.** Parser, actual sender/receiver and eight-port tests pass with ASAN/UBSAN. Tests cover detected-to-ready transitions, stale/duplicate replies, readiness expiry, service loss and the request cap. The full Trash-safe remake exited0. The prepared UIM image contains all ten expected kernel segments and601bytes of arguments. It omits sweetmbw and sweetmbwi and keeps the previously approved2185 power sequence, known failed-PAS retention and guarded reset.

**Missing.** Fresh Sol approval and a readiness phone measurement. The data agent has the current owner-approved15minute reservation. No modem run may start until phone-data-wants and phone.lock are both gone, no devloop is active and recovery is verified clean. 2188 data hooks remain a desk draft and are absent from2187.


2187 first review is NO-GO. Sol found rejected current transactions remained reusable, and service loss was checked only at the send deadline. Revision2 consumes matching responses before parsing, including5/6-byte truncations, and validates current UIM discovery before every poll deadline check and before admitting any matching reply. Loss/change revokes readiness and both pending requests. New regressions cover error/malformed-then-success reuse on both opcodes, short headers, service removal/change before cadence and late replies after removal. The public2187 patch is updated in place because no2187image booted. Rejected image tag2187 remains preserved; corrected artifacts will use tag21871. Currenthardware-testedcutoff remains2186. Data hooks remain a separate deskdraft.


**Code.** Sol approved the corrected2187 source and frozen21871 image for one bounded offline readiness diagnostic after data releases the phone. The runner now explicitly limits devloop to one boot attempt and verifies the frozen artifacts after waiting. This does not approve provisioning or ONLINE and does not resolve M2. See `modem-2187-reaudit.md`.


## ast165 readiness and patch2188 flow control

**Measured.** `ast165-modem-uim-21871.txt:10619` reports primaryGW0 ready1, with slot1/app0 USIM state7 on line10621. Eight fresh READY responses followed the initial detected response. The separate slot query reported ICCID suffix0660 at line10552. After nine card responses and one slot response, subsequent requests got no reply. Readiness expired at line11384. No provisioning is needed to reach READY.

**Measured.** SSCTL returned success and fresh shutdown_ack at line11686. All50 PAS4 shutdown calls failed with raw -3145800. Line11743 retains memory and power, line11744 reports final_error5. Normal selftest DONE is at line12413. The365-second run had a fresh completion nonce, clean root before and after, and returned to recovery. The strict M2 checker must still fail. The only pushed file was the selftest hook.

**Code.** Patch2188 sends RESUME_TX from the receiving local client to the actual remote sender and puts the local client node/port in its payload. This matches [Linux qrtr_send_resume_tx](https://github.com/torvalds/linux/blob/master/net/qrtr/af_qrtr.c). The previous code used control ports and named the remote sender in the payload. Pending acknowledgments survive transmit-queue backpressure in a bounded16-flow table. Exhausting that table fails the worker. A worker poll retries after draining TX and teardown logs the counters.

**Inferred.** The wrong acknowledgment explains the ten-response stall. This remains a hypothesis until a phone run receives more than ten fresh replies with2188.

**Measured, host.** Sanitizer tests cover60 replies through a ten-packet peer window, exact header/payload fields, backpressure, duplicate confirms, queue capacity, bad destinations and truncated packets. An extracted real worker test checks drain-before-retry and failure propagation. The existing storage check permits only sweet_ufs_read_part and sweet_ufs_part_size. No partition write API is added. Full Trash-safe kernel build passed and the image matches all ten kernel segments.

**Missing.** Fresh Sol approval and phone confirmation for2188. Data worker integration moves to2189 and is not in this image. IPA, data sessions, provisioning and ONLINE remain absent.


**Code, revision2.** Sol rejected2188 revision1 because the real TX queue marked acknowledgment backpressure fatal. Revision2 keeps retry-owned RESUME_TX pending without poisoning the worker and preserves failure on ordinary reply loss. The regression now includes the real queue callback and five consecutive GLINK refusals, followed by exactly one delivered response and acknowledgment. Full build and host checks pass. Image21881 needs fresh review. The rejected image must not boot.


**Code, reviewed scope.** Sol approved one frozen21881 offline flow/readiness diagnostic after DATA and IPA release ownership. The reviewed runner is waiting for the reservation and lock to clear. This extends the limited known-PAS-retention exception for this measurement only. It requires more than10 fresh UIM replies, sustained selected readiness, healthy flow counters and normal guarded return with clean root. It does not establish M2 or approve ONLINE. See `modem-2188-reaudit.md`.


## Data worker integration, patch2189

**Code.** Apply2189 after2188 revision2. It requires data patch2220 before building, because it calls that module directly. The current21881 diagnostic image contains only modem2170 through2188. Patch2189 is desk work and has no phone approval yet.

**Code.** Eight QRTR slots and silent service-zero client ports are already provided by2187. The worker calls sweet_data_attach after QRTR initialization and immediately before MPSS loading. IPA must install its immutable TX/RX binding before that call. The worker sends selected-USIM readiness transitions through sweet_data_usim and calls sweet_data_poll each iteration. It revokes readiness before subsequent callbacks on expiry, endpoint loss, GLINK failure or fatal state.

**Code.** Exact sweetmsession=1 selects a600-second post-authentication session. It requires sweetdata=1 or sweetqmiraw=1, UIM readiness polling, GLINK, RAM-rmtfs, pd-mapper, sysmon, handover and exact fbarm/selftest guards with sweetdeadman=900. Missing dependencies fail before hardware setup. Without the session gate, the120-second offline worker is unchanged. This gate does not itself authorize ONLINE.

**Code.** Teardown revokes readiness, calls sweet_data_stop and pumps transport/poll for up to5 seconds. Both idle and cleanup0 are required for clean data teardown. All other outcomes mark failure while preserving the owed PAS shutdown attempt. IPA integration must quiesce its AP channels before sweet_data_detach, then run SSCTL/PAS4. Failed secure stop retains backing as before. PAS15 teardown and IPA backing release remain conditional on verified modem stop.

**Measured, host.** The ordered-series lifecycle/UIM suites and actual-hook sanitizer tests pass in an isolated reconstruction. Checks cover required session gates,900-second guard, stale readiness before another QRTR callback, endpoint removal, no readiness revival during teardown, bounded stop, partial attach and every nonzero cleanup result. The integration source syntax check passes against kernel headers with existing header warnings. Full kernel build and integrated Sol review are pending.

**Missing.** IPA hardware preparation and a separately reviewed combined image. The owner accepts safe PAS4 retention for bounded data trials and asked to stop separate shutdown-only phone runs. The next hardware priority is sustained QRTR/UIM transport, followed by the IPA readiness barrier and ONLINE sequence in `cellular-data.md`.


## ast168 sustained transport

**Measured.** `ast168-modem-uim-21881.txt:11379` first reports selected USIM READY. Eighteen READY reports continue through line12483. The run received19 card-status replies and one slot-status reply, with ICCID suffix0660 at line11314 and no freshness expiry. This confirms the2188 receive-flow fix beyond the previous ten-response stall. Line12504 reports resumes5, pending0, failed0, rx34, tx41 and refused0.

**Measured.** PAS authentication succeeded at9086, MSS proxy handover at9129 and GLINK at9131. Offline work ended with error0 at12493. SSCTL response, indication and fresh shutdown_ack were all accepted at12503. The known50 PAS shutdown failures followed, with retained backing at12561 and final_error5 at12562. No ownership reclaim or power drop followed retention. Normal DONE is at13283 after365seconds. The nonce matches the frozen script, one checked frame exists, stale=false and root before/after is clean. The phone returned to recovery and the modem hold was released for IPA.

**Code, next integration.** Sol accepts the600-second lifetime design but found two2189 wiring gaps. Data service removals must reach its cache before a later response in the same receive batch can advance setup. UIM freshness must also revoke unconditionally when ready/handover bits clear and traffic stops. The owners are fixing both with connected host tests. See `modem-2189-audit.md`. No further PAS4-only phone diagnostic is planned.


## 2189 data worker revision2

**Code.** The 600-second worker expires USIM evidence before every transport/data step, even if SMP2P ready or handover clears. It calls the data discovery hook after every accepted QRTR packet and UIM endpoint check, preserving removal events before later replies. This requires data2220 revision8a0b901c, reviewed in `docs/data-2220-audit6.md`.

**Measured, host.** The extracted worker and real CellSession pass the cleared-state/no-traffic freshness regression. The actual modem receive and data discovery functions pass DEL_DPM/late-reply and DEL/NEW tests with real QRTR, DataServices and CellSession under ASAN/UBSAN. `tools/modem/check-data-hooks.sh` reproduces these checks. The corrected 2189 revision and data2220 audit6 completed a full Trash-safe kernel build.

**Code, reviewed.** Sol closed both findings and approved the source wiring and guarded 600-second lifetime in `docs/modem-2189-reaudit.md`. Fresh isolated host checks passed and 35 reconstructed source files match production.

**Missing.** The final combined build and IPA readiness/quiescence approval. No 2189 phone image is approved. PAS4 retention remains the owner-accepted bounded-trial policy, with M2 incomplete. M3 and sustained USIM READY are measured in ast168.


## Interface-bound packet proof

**Code.** `tools/modem/sweeten2.c` makes one IPv4 HTTP HEAD exchange to 1.1.1.1 through en2. It requires an up en2 address, sets and reads back IP_BOUND_IF, binds that source address and verifies the connected source. There is no DNS lookup, interface fallback or routing change. One monotonic 10-second deadline covers connection, send and receive. Output contains only the interface/index, HTTP status and elapsed time. It does not print response bodies or modem identifiers.

**Measured, host.** `python3 tools/modem/check-en2.py` passes localhost tests of the actual bound socket, fragmented/malformed/truncated status, absent interface and total timeout under ASAN/UBSAN. `sh tools/modem/build-en2.sh` builds and signs the arm64e phone executable, with PAC stripped for this kernel. The binary stays outside the public repository.

**Missing.** No phone run yet. This tool proves an interface-bound exchange when it passes. It does not prove DNS, TLS, Safari or configd primary-route selection. Those need the data agent's en2 DHCP profile and route evidence. USB Internet Sharing cannot carry the socket because XNU IP_BOUND_IF restricts its outgoing interface.


## Combined IPA source

**Code.** The modem tree now includes data2220 audit7, adapter2221 and IPA2200 through2206. The worker binds IPA after preparation and before data attach, refreshes its readiness at receive/discovery/poll boundaries and quiesces AP channels before detach. An uncertain modem stop retains IPA backing and power. These changes are in IPA2205. The exact application order and hashes are recorded in the workspace's modem README and IPA cutoff manifest.

**Measured, host.** Independent ordered reconstruction matches all19 IPA-touched files in xnu-modem. Isolated firmware, lifecycle, datapath and compiled MMIO checks pass, including the actual worker and real2221 adapter. The combined full Trash-safe build exited0. All53 frozen source hashes still match. Its kernel SHA256 is e203a6aab91eb59d716a90f2a2812ad57f6dbda9099a0bf84904f185f08286b6.

**Code.** Additive IPA2207 now guards indirect stage2 references and extended StreamID validity. It was applied after the full build ended. The fresh IPA source review is NO-GO because global leaf descriptors and a shared VMID permit AP/uC TLB identity collisions despite distinct ASIDs. IPA owns the additive correction. No2206 or2207 image may boot under this review.

**Missing.** The corrected source needs a fresh review, rebuild and exact-image GO. PAS15, the IPA handshake and ONLINE remain unmeasured. ast170 proved the read-only context survey and showed why the earlier zero-only context selector refused preparation.


## Bounded data runner

**Code.** `tools/modem/run-data-usb.py` requires a reviewed manifest with pinned artifacts, takes the modem hold and atomic phone lock, and consumes a one-attempt record before invoking dev.py. It refuses replay and extra boot attempts. It verifies the actual first-boot hook composition before launch. Afterward it restores the original network profile from the same phone in clean recovery and verifies the bytes. An uncertain restoration keeps phone ownership.

**Measured, host.** `python3 tools/modem/check-data-runner.py` passes changed-artifact, approval, busy-phone, wrong-phone, dirty-root and restoration-readback refusal cases. No approved manifest exists and this runner has not operated the phone.


## Combined2208 image

**Code.** IPA2208 adds non-global leaves, distinct nonzero VMID leases and live collision checks. It refuses unsupported global modes and verifies the private identity before maintenance. The frozen2200 through2207 patches remain unchanged.

**Measured, host.** The corrected combined build passed. Kernel SHA256 is1dcd7de654ed7b81ed721cf95a2cfa4f5cc07a8893fea87d2d7616216e21d869. The data image SHA256 isb141136242ee89934c78e29e78dc881d1800c499fa5a16153af7cd34a478dd8a, with all10 packed kernel segments verified. Independent ordered reconstruction and isolated lifecycle/datapath suites pass, including shared-TLB isolation and the maintained2221 regression cases. The unapproved manifest is refused before any phone claim or mutation.

**Missing.** Fresh source and exact-image review, followed by successful PAS15-only preparation and offline IPA handshake evidence before ONLINE. The modem hold is released while those prerequisites are pending.


**Code, reviewed.** Sol gave conditional GO for the exact2208 ONLINE image and runner in `modem-2208-data-image-audit.md`. No integration fix is required. The fresh IPA source review closes the TLB isolation finding, but launch still requires successful separately reviewed PAS15-only and offline IPA/GSI/QMI handshake trials. The manifest remains unapproved until those hardware prerequisites pass.


**Code, APN plan.** The replacement trial line includes the carrier APN from the operator's public help page and its authentication setting within607bytes. Carrier-specific values stay in private workspace notes. It drops sweetsb=1 and sweettu=sysmem,nocb,nolrz and shortens the panic-screen delay from10 to9 seconds. Every modem/IPA/data gate and all required dev, thermal, fetch-sync and USB arguments remain. The builder refuses an old kernel without authentication parsing. This requires the data owner's reviewed APN/auth update and a new exact-image review.

**Measured, blocker.** ast171 failed IPA preparation before PAS15 and reset after63seconds, leaving a dirty root. The2208 ONLINE candidate is suspended. IPA is preparing a different SMMU-only probe and revised teardown under a fresh review. No modem phone operation or APN request has occurred.


**Code, reviewed.** Dataaudit8 accepts2220 revisionf331702b with request-only APN authentication and IPv4-first setup. The three changed data files are imported into xnu-modem. The actual modem/data hook tests and isolated protocol, service-cache and adapter suites pass. The replacement combined build waits for the revised IPA teardown cutoff.


## Replacement2209 data image

**Code.** IPA2209 retains backing after partial or unproved setup. Verified stopped contexts are left disabled with zero TTBRs and their private VMIDs, instead of replaying unknown reset fields. The shared APPS allocator excludes those parked banks. The replacement data image combines this change with reviewed dataaudit8 APN/authentication support.

**Measured, host.** All56 combined source files reconstruct exactly. The lifecycle, datapath and modem/data hook tests pass. The safe kernel build passed, and all10 packed segments match the replacement image. Its kernel SHA256 isbfa56dd9f1198ff099ebe7a62df26d9d5ae8897498687c3298e39b74b400738b and image SHA256 isf2360ae9a17f538a3452f48c5301809647bf4299942e2c73533c9a2d66120a50.

**Missing.** A new exact-image review is running. Separate benign root cleaning, SMMU-only validation, PAS15 and offline IPA handshake trials must pass before ONLINE. The77-artifact manifest remains unapproved. No replacement image has touched the phone.


**Review.** The new exact-image review in `modem-2209-data-image-audit.md` found no new combined kernel or runner defect, but remains NO-GO because the prerequisite SMMU-only checker rejects its own required success line. IPA owns that correction and fresh review. A final data-image decision follows accepted clean-root, SMMU-only, PAS15-only and offline handshake evidence.


**Measured, recovery.** The separate benign clean-root run ast172 ended normally after214seconds. It replayed the journal, returned a clean root with stale=false and logged no IPA or modem worker activity. IPA accepted the capture and released ownership. The SMMU-only checker still needs its corrected review before that prerequisite can run.


**Measured, SMMU-only trial.** ast173 ended normally after211seconds with a fresh nonce, clean root and stale=false. Setup failed exact SCTLR readback before PAS or GSI, and retained its backing without a panic. The requested optional ASIDPNE hint did not read back. IPA owns the additive correction and another reviewed SMMU-only trial. ONLINE remains blocked.


**Code.** IPA2210 clears only the optional ASIDPNE request and keeps exact readback and private VMID isolation. It is now in the APN/auth combined tree.

**Measured, host.** The safe build and lifecycle regressions pass, all56 source files reconstruct and all10 packed segments match. The replacement kernel hash isce5eb6768b314c0d0f7c72a7d0e443d8eed5d05b72b61f1b0f2087bc56de348e and data image hash is9e71563d91d32b976f0a1bad8b66b07df168791925863390a26aabcca404f582. Its77-artifact manifest remains unapproved pending the hardware prerequisites and final exact-image review.


**Measured, SMMU prerequisite passed.** ast174 completed the2210 SMMU-only trial normally in214seconds. Setup completed at log7734, private contexts were parked and backing released at7940, final_error0 appeared at7942 and DONE at11129. The fresh nonce matched, root before/after was clean and stale=false. No firmware, PAS15 or GSI ran. The phone returned to recovery and ownership was released. PAS15-only and offline IPA handshake trials remain before ONLINE.


## IPA firmware teardown correction

**Measured.** ast175 returned successful PAS15 initialization, memory setup, authentication and shutdown at raw log lines5512,5520,5523 and5534. Powered IPA/GSI register reads appeared at5527. The run reset after63seconds and collection reported a dirty root with stale=true. It does not qualify the firmware lifecycle.

**Code.** IPA identified an unaligned firmware unmap in the panic. Native ml_io_unmap passes the exact length to pmap_remove. Additive2211 maps and unmaps the16KiB-page-rounded0xc000 span while firmware storage and parser bounds remain0xa000. The combined tree includes this exact published patch.

**Measured, host.** The real runtime regression uses16KiB CPU pages. Lifecycle and datapath sanitizer suites pass, all56 source files reconstruct, the safe build exited0 and all10 packed segments match. Kernel SHA256 is44b85b9bc2f6645a84b30c3e3f48e7b8743bba707cd3438e1851b67e7a38c68f. The replacement data image remains unapproved.

**Code, blocker.** The current hardware setup also rejects measured flavor0xb0f0b1a because its producer-range check uses the wrong direction. The reference defines our RX as an IPA producer and our TX as an IPA consumer. IPA owns a separate correction before the offline handshake trial.

**Missing.** A reviewed benign root cleanup, accepted firmware teardown and offline IPA/GSI/QMI handshake still precede a fresh exact ONLINE-image review. No data profile or packet-proof helper has been installed.


**Code.** Additive2212 fixes the measured flavor partition check. It validates AP TX endpoints against the hardware consumer range and AP RX against the producer range before configuration writes.

**Measured, host.** The actual0xb0f0b1a flavor and0x880 shared-memory value pass the hardware setup model. Invalid partitions are refused before writes. The combined datapath sanitizer suite,56-file reproduction, safe build and10-segment image comparison pass. The frozen data kernel is a37e260bf19409e2f4dadf8615ef786cb1e6eb93c18644d40f0061c77a61303e. Its image remains unapproved pending hardware prerequisites and a fresh exact-image review.


**Measured, root restored.** The separate benign cleanup ast176 completed normally in214seconds. Its raw log shows journal replay at384 and DONE at12627, with no IPA or modem worker output. The accepted capture hashes match, root_state is clean and stale=false. IPA released ownership in recovery before its separately reviewed2212 firmware trial.


**Measured, PAS15 prerequisite passed.** ast177 completed the2212 firmware-only trial normally in214seconds, with clean roots before/after, a fresh nonce and stale=false. PAS15 init/memory/authentication returned0 at6628/6630/6631, powered register reads appeared at6632 and shutdown returned0 at6639. DMA guards were intact, private SMMU contexts parked and backing released at6649, final_error0 at6652 and DONE at12610. The exact checker and accepted result/log/marker hashes independently pass. No MPSS, full GSI or ONLINE ran. The offline IPA/GSI/QMI handshake remains before the final data-image review.
