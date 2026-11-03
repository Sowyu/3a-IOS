# Modem and SIM: desk findings (2026-11-03)

MPSS boots offline and announces UIM, NAS and WDS services. ast151 and ast153 proved ready, handover, GLINK/QRTR and RAM-backed rmtfs, but PAS shutdown still fails. M2 remains incomplete and no SIM status has been queried. Patch 2179 adds the standard Linux sysmon shutdown handshake before PAS. Its host checks and build passed; a fresh Sol review is running. The data-path plan is below.

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

- `plans/modem/src/lift/` holds unfinished drafts that do not compile. The dedicated `ios-asteroids/research/xnu-modem` tree now has the requested daily series, USB v5 and modem patches 2170 through 2178. Other kernel trees were not edited.
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
