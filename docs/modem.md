# Modem and SIM: desk findings (2026-11-03)

Desk work only. Patch 2170 now provides a read-only firmware preflight. It has synthetic host checks but no full kernel rebuild or phone run. M1 is incomplete. MPSS start and SIM detection are not implemented.

Labels as in `docs/input.md`: **Measured**, **Code**, **Inferred**, **Missing**.

## Facts

- **Code, firmware.** The `modem_a` partition (FAT16, 4096-byte sectors) holds `image/modem.mdt` and `modem.b01` to `b30`. That is 29 relocatable load segments plus a hash segment, about 109 MB, spanning 0xd700000. It fits in `mpss_mem` (0x8ac00000 + 0xe600000). There is no `modem_dtb.mdt`. Mainline Linux runs this SoC family's modem as PAS id 4 without a DTB image. `plans/modem/tools/fatls.py` lists and extracts it read-only. The firmware stays out of this repo.
- **Code, DT.** `remoteproc-mss@04080000`, compatible `qcom,volcano-modem-pas`. Its regions are `mpss_mem`, `q6_mpss_dtb_mem` (0x99200000 + 0x80000) and a third (phandle 0x7a). Its interrupts are wdog, fatal, handover, ready, stop-ack and shutdown-ack. It has a "stop" SMP2P state, GLINK edge "mpss" (remote pid 1, IPCC mailbox) and AOP QMP signalling.
- **Code, memory.** XNU's RAM is 0x8c0000000 + 0x134000000, so the kernel cannot allocate below 4 GB. rmtfs needs a 2.5 MiB buffer below 4 GB. The bank at 0xe0a00000 is the only free low RAM, and 0xe1000000 to 0xe2000000 is unused.
- **Code, storage.** `sweet_ufs_read_part()` reads `modem_a` and the EFS partitions read-only. The kernel's UFS write path refuses anything outside `userdata`. The modem's EFS (`modemst1`, `modemst2`, `fsg`) holds the IMEI and RF calibration. It must never be written: serve it read-only, and keep any writes in RAM.
- **Code, missing pieces.** This kernel has no RPMh client for the apps RSC and no AOSS QMP sender for SM7635. Both are needed for the rail votes and `load_state`.
- **Code, reuse.** The Poco F1 port booted SDM845's modem on this same xnu-12377 (`ios-asteroids/xnu/patches-12377/1104` to `1116`: SMP2P, rmtfs over QRTR and GLINK, tftp, pd-mapper). SDM845 used MBA, while this phone uses PAS. rootstock's `ref/UniXNU-asteroids/package/` has SMEM, SMP2P, GLINK, QRTR, QMP and an ADSP start for this phone.
- **Inferred, from HakiOS (`ref/hakios-xnu-kernel`, SC7280).** The modem announced UIM about 30 s after rmtfs was served, while still offline. Going online without IPA crashed the modem. So reading SIM status needs rmtfs, and likely tftp and pd-mapper too, but not IPA.

## State

- `plans/modem/src/lift/` holds unfinished drafts that do not compile. The dedicated `ios-asteroids/research/xnu-modem` tree now has the requested daily series plus 2170. Other kernel trees were not edited.
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

**Code, DT.** IPA base is 0x03e00000 with length 0x84000; GSI base is 0x03e04000 with length 0xfc000. The configuration offset is 0x140000. The hardware version property is 0x17, an enum whose mapping must be verified. EE is zero. Core clock, three interconnect paths, IPA/GSI interrupts and IPA SMP2P mappings are specified. AP and microcontroller SMMU streams are 0x4a0 and 0x4a2, with IOVA pools at 0x20000000 of size 0x20000000. The 64-bit DMA property does not remove SMMU mapping requirements.

**Code, reference.** HakiOS `drivers/modem/hakios.ipa.c` targets IPA 4.11/GSI 2.11 and describes command, LAN RX, modem TX and modem RX endpoints, GSI rings and shared routing/filter tables. These are requirements to investigate, not verified SM7635 register offsets or endpoint IDs. Its PAS loader explicitly warns that loading the adjacent wrong reservation caused unreliable modem behavior.

**Missing.** Inventory the local signed ipa_fws segments read-only, verify their placement and the SM7635 IPA/GSI version, obtain matching endpoint/register tables, arrange clock/interconnect votes, isolated SMMU mappings, bounded DMA buffers and a stop/reset sequence. Implement the modem's IPA QMI setup handshake and tables before online mode. Firmware authentication alone is insufficient. Fresh Sol GO is required before booting these changes. Only then request DMS online and record NAS registration and serving system. Two matching resets stop that attempt.

### M5, WDS data call

**Missing.** Read provisioned profiles without changing persistent profiles, obtain the SIM/carrier APN, negotiate WDA framing and bind the correct endpoint and mux before WDS start. Request the IP family and parse assigned IP/prefix, gateway and DNS from runtime settings. Use transient call parameters only. Record the call handle for teardown. Do not guess an APN or treat a WDS success response as packet delivery.

### M6, IPA/rmnet packets

**Missing.** Implement the SM7635 GSI command/TX/RX/event rings, IPA routing/filter/header tables and QMAP mux framing. Bound DMA lengths and aggregated packet parsing, maintain cache coherency, and reject unknown mux IDs. Prove a returned packet with counters and a bounded network test, then verify stop drains DMA before freeing memory. WDS stop must run on success and failure exits before modem teardown.

### M7, iOS interface

**Inferred.** A wired Ethernet presentation is a practical first option. The existing `iokit/Kernel/arm/SweetNetwork.cpp` publishes an IOKit Ethernet facade so configd adopts the USB BSD ifnet. Reuse that pattern for a distinct modem ifnet after M6, keeping USB separate.

**Missing.** IPA carries IP/QMAP, not Ethernet. The adapter must translate Ethernet/ARP or provide a private routed Ethernet link with DHCP and a matching gateway; IPv6 needs its own routing/RA design. A virtual Ethernet label alone does not configure the WDS address or DNS. Test configd adoption, route selection, DNS and app traffic on this iOS build. CommCenter emulation is not planned. No status-bar sysctl is planned.

## Follow-up 2171

**Measured, host.** The original preflight fails against the existing modem_a backup with MDT lookup error 2. VFAT long names map to unrelated short aliases. Patch 2171 adds one-slot VFAT lookup with sequence, ASCII and checksum validation. The real-image candidate now reports 29 load segments, 109371307 bytes, span 0xd700000. Local evidence is `plans/modem/logs/real-image-check-candidate.txt:1`. This is not a live phone result or firmware authentication.

**Code.** 2171 also rejects repeated file clusters, free terminal clusters, inconsistent zero-memory loads and numeric boot-argument aliases. The boot gate accepts the exact text `1`. Synthetic checks reconstruct and compile both patches, including bad VFAT checksums and the file-chain cases identified by Sol.

**Measured, source review.** Sol approved exact 2170 read-only inspection conditionally on a successful full kernel build, with no approval for modem start. The audit is `docs/modem-2170-audit.md`. 2171 needs its own review. The full 2170 build is running in the dedicated tree with recoverable cleanup. Its known file-lookup failure makes it unsuitable for the phone test; apply 2171 and rebuild first.

**Code, build safety.** The dedicated tree's local MakeInc.cmd redirects RM/RMDIR to a wrapper that invokes only `/usr/bin/trash` on absolute build-tree paths. PATH routes unqualified shell cleanup to it. Make preserves intermediate and failed targets. Host checks confirmed that trashing a symlink retains its target and that outside paths are rejected. Do not use MAKEFILES to insert these rules: the build derives its top-level file from MAKEFILE_LIST.

**Measured, build.** Both 2170 and the corrected 2170+2171 kernel installs completed with exit 0. The latter log is `plans/modem/logs/build-2171.txt`, ending in `done building xnu`. `tools/modem/build-probe.py` built `plans/modem/out/preflight-boot.img`, 8044544 bytes, with 598 bytes of boot arguments. It includes sweetmodem=1, sweetfbarm=1, sweetdeadman=600 and sweetselftest=1. The corrected-patch audit and phone run are pending. The USB test still owns the phone; do not start a competing devloop.

**Measured, review and build.** Sol approved 2170+2171 for read-only inspection, conditional on a successful full build; `docs/modem-2171-audit.md` records the exact source hashes. The full build passed. A separate build-tool finding was fixed: kextsymboltool now retains failed output instead of calling unlink, and the rebuilt helper's error-path test retains the existing output. No unlink import remains in that helper. The subsequent full install passed and the RAM image hash stayed unchanged. A retained failed output must be Trashed before retrying a failed build.

**Code, boot parser correction.** The platform parser turns bare `sweetmodem` into `1` and uses the last duplicate value. The image builder emits one explicit `sweetmodem=1`; the source-level claim of requiring an equals sign was too strong. Approval remains confined to read-only inspection. No hardware stage is enabled.
