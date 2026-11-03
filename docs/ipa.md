# IPA on SM7635

## Checkpoint, 2026-11-03

I1 is complete. I2 is in progress. Patch 2200 passed its audited read-only phone run, ast159. Firmware bounds and the APPS SMMU survey passed, with no IPA/GSI MMIO or PAS15 call. The isolated xnu-ipa tree now carries copied modem patches 2170 through 2188 revision 2. Mobile data does not work yet.

Host inspection of the existing extracted ipa_fws.mdt found three relocatable loads, 17824 file bytes and a page-rounded span of 0x7000. This fits the 0xa000 GSI reservation. `tools/ipa/inspect_mdt.py` checks ELF bounds, overlap and relocation span without loading firmware. It does not check split-file contents or authenticate the image. Three synthetic host tests pass, including nine malformed cases. Local evidence is `plans/ipa/logs/host-check.txt` and `plans/ipa/logs/firmware-inventory.json`.

The existing modem log `ios-asteroids/logs/ast151-modem-offline-usb.txt:9586` announces service 0x31, packed instance 0x201, node 0, port 28. This proves discovery only. Never pin that port across boots.

## Sources and driver choice

The matching Nothing vendor source is commit `d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7` in [NothingOSS/android_kernel_msm-6.1_nothing_sm7635](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/tree/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/vendor/qcom/opensource/dataipa/drivers/platform/msm). `include/uapi/linux/msm_ipa.h` maps DT value 0x17 to IPA_HW_v5_2. `ipa/ipa_v3/ipa.c:7719` selects GSI_VER_5_2. These are software descriptions, not live version measurements.

Mainline now has IPA 5.2 support. Inspected commit `6628e7d4c6b25c8d18f1345e3a20d21829aae7e4` has [data/ipa_data-v5.2.c](https://github.com/torvalds/linux/blob/6628e7d4c6b25c8d18f1345e3a20d21829aae7e4/drivers/net/ipa/data/ipa_data-v5.2.c), selected by `qcom,milos-ipa` in ipa_main.c. It has no direct SM7635 match in that table. Use its smaller driver architecture as a reference and the Nothing vendor data for this board. Do not equate a supported hardware revision with a tested board port.

Local copies are under `plans/modem/reference/nothing-dataipa-*` and `plans/ipa/reference/`. The HakiOS reference `ref/hakios-xnu-kernel/drivers/modem/hakios.ipa.c` uses the SC7280 IPA 4.11/GSI 2.11 layout. Its header also describes a POCO X5 Pro default board. Its broad identity mapping, register constants and firmware lifecycle are not SM7635 implementation evidence.

## DT and firmware ownership

`ios-asteroids/research/asteroids-hw/asteroids.dts:23195` has a disabled generic ipa_fws loader. The active IPA node independently names ipa_fws, PAS 15 and memory-regions phandle 0x26d. Resolve that phandle: it is ipa_gsi_region at 0x9ea90000, length 0xa000. The adjacent ipa_fw_region at 0x9ea80000, length 0x10000, is a different reservation. GPU microcode begins at 0x9ea9a000. Never spill into it.

The MDT loads start at relative addresses 0, 0x5ee0 and 0x5f60. Memory lengths are 0x5ee0, 0x80 and 0x3e8. Relocate to the GSI reservation, retain zero-fill semantics, and exclude the Qualcomm hash segment from executable loads. A future kernel loader must reuse the bounded FAT/VFAT reader, read only modem_a, validate every ipa_fws.bNN length and chain, and authenticate with PAS 0x0f. It must not reinterpret the MPSS span checks as IPA checks.

PAS init makes shutdown owed even if a later operation fails. A shutdown timeout must retain DMA backing, mappings and required power until the bounded reset. It must never report clean shutdown or free memory still accessible to hardware. PAS15 success alone says nothing about AP endpoints or the modem uC handshake.

## Register and ring layout

DT describes IPA base 0x03e00000 length 0x84000, GSI base 0x03e04000 length 0xfc000, whole block length 0x200000 and IPA configuration offset 0x140000. The configuration register base is therefore 0x03f40000. Do not add configuration offsets to a mapping bounded by the first DT resource length. Map each justified region separately.

Vendor ipahal_reg.c inherits unoverridden entries forward through hardware versions. IPA 5.2 inherits these IPA 5.0 offsets relative to the configuration base:

| Register | Offset |
| --- | --- |
| FLAVOR_0 | 0x0000 |
| SHARED_MEM_SIZE | 0x0040 |
| FILT_ROUT_CACHE_FLUSH | 0x0404 |
| ENDP_INIT_CFG(n) | 0x1008 + 0x80*n |
| ENDP_INIT_HDR(n) | 0x1010 + 0x80*n |
| ENDP_INIT_MODE(n) | 0x1020 + 0x80*n |
| ENDP_INIT_AGGR(n) | 0x1024 + 0x80*n |
| ENDP_INIT_SEQ(n) | 0x103c + 0x80*n |
| ENDP_STATUS(n) | 0x1040 + 0x80*n |

Vendor gsihal_reg.c also inherits entries. GSI 5.2 uses the GSI 3.0 register family below, relative to GSI base. EE stride is 0x12000, channel/event stride is 0x80, doorbell stride is 8. AP owns EE0 only.

| Register | EE0 offset |
| --- | --- |
| Channel context word j | 0x14000 + 0x80*channel + 4*j |
| Event context word j | 0x1c000 + 0x80*event + 4*j |
| Channel doorbell low/high | 0x24000 + 8*channel, plus 4 |
| Event doorbell low/high | 0x24800 + 8*event, plus 4 |
| GSI status | 0x25000 |
| Channel command | 0x25008 |
| Event command | 0x25010 |
| Generic command | 0x25018 |
| HW_PARAM_2 / SW_VERSION / HW_PARAM_4 | 0x25040 / 0x25044 / 0x25050 |
| Error log / clear | 0x25240 / 0x25244 |

These addresses differ materially from HakiOS's channel base 0xf000, EE stride 0x4000, status 0x12000 and IPA endpoint stride 0x70. Copying its register writes would target the wrong registers. Field encoders must also use the selected version, not just substituted bases.

Use 16-byte GPI transfer elements with a 64-bit IOVA, bounded length and version-correct flags. Allocate separate aligned transfer and event rings, with ownership and wrap tracking. Mainline 5.2 uses 256 entries for command/LAN RX/WAN RX and 512 for WAN TX. The vendor DT requests a WAN RX ring size of 128. Choose and test one bounded configuration explicitly. Event ring IDs are allocated independently of channel IDs. Respect DT's reserved MHI event range 9 through 11 rather than equating channel and event IDs.

Before recycling any buffer, validate event type, channel, TRE address, range, alignment, outstanding ownership and completion length. Retain one unused ring slot. A timeout is not completion. Stop and verify channel state before event reset/deallocation or page unmapping.

## Endpoints and QMAP

The matching ipa_utils.c IPA_5_2 table gives these assignments:

| Client | IPA endpoint | GSI channel | EE |
| --- | ---: | ---: | ---: |
| AP command producer | 9 | 6 | 0 |
| AP LAN producer | 6 | 4 | 0 |
| AP LAN consumer | 11 | 7 | 0 |
| AP WAN producer | 2 | 5 | 0 |
| AP WAN consumer | 18 | 9 | 0 |
| Q6 WAN producer | 7 | 0 | 1 |
| Q6 LAN consumer | 12 | 3 | 1 |
| Q6 WAN consumer | 16 | 7 | 1 |

Q6 WAN consumer 16 is the ordinary WAN peer. Q6 LAN consumer 12 is a separate client. The earlier modem summary listed the latter but did not identify the WAN peer. Do not write the modem-owned channel contexts from AP code.

Bring up the command endpoint and LAN exception sink first, then initialize memory and tables using command completions. Set resource groups and sequencers from the 5.2 table. WAN framing must match WDA negotiation. A minimal no-checksum QMAP path needs a four-byte header, big-endian packet length, bounded padding and an approved mux ID. Do not reuse mainline's checksum-enabled endpoint settings while advertising no checksum. Aggregate parsing must reject truncation, overrun, unknown mux and zero-progress records.

## SMMU and power prerequisites

AP SID is 0x4a0, WLAN is 0x4a1 and uC is 0x4a2. The 11ad context is 0x4a4. AP and uC have IOVA pools [0x20000000, 0x40000000). AP additionally identity-maps 0x14683000 through 0x14684fff and declares q6 SMEM size 0xb000. These are separate contexts even where IOVA ranges overlap. Leave WLAN and 11ad untouched.

The DT's dma-coherent and 64-bit DMA properties do not authorize bypassing the SMMU. XNU RAM is above 4 GiB. Establish isolated AP mappings for owned rings, data and table pages, plus the required shared resources. Determine current SMR/S2CR/context-bank ownership from an audited read-only survey before changing it. Do not reuse the SDM845 helper's broad identity domain. Prove TLB invalidation and readback, then revoke only mappings owned by this driver after verified DMA stop.

Clock index 12 is RPMH_IPA_CLK using command-db BCM IP0. IPA has three interconnect paths named ipa_to_llcc, llcc_to_ebi1 and appss_to_ipa. Read command-db unit/width/VCD metadata first. Coordinate TCS arbitration with touch and modem. ARC voltage indices are not BCM clock votes. The vendor enables power before register reads, so even a nominally read-only IPA MMIO probe needs reviewed power setup. Do not read an unclocked block blindly.

DT IPA SPI is 654 and GSI SPI is 432, level-high. GIC interrupt IDs add 32. Polling may simplify the first bounded test but does not remove the need to acknowledge and mask only owned interrupt sources. IPA has its own SMP2P input/output maps, separate from the modem stop state.

## QMI handshake and shared tables

Publish an AP IPA server with service 0x31, version 1, instance 1, packed 0x101. Discover the modem instance 2, packed 0x201, on the existing QRTR transport. Preserve the modem agent's ownership of MPSS, GLINK and transport shutdown. IPA needs both an AP server and a modem client.

The modem sends INDICATION_REGISTER 0x20 to the AP. Respond using its transaction and endpoint, and remember the request. The AP sends INIT_MODEM_DRIVER 0x21 to the modem after its real tables exist. Accept success only for the matching endpoint, transaction and message. Send MASTER_DRIVER_INIT_COMPLETE indication 0x22 only after table setup and successful initialization, and only after registration. Handle modem INIT_MODEM_DRIVER_COMPLETE request 0x35 and send its response. Initial readiness requires the successful 0x21 exchange, the registration/indication exchange and the 0x35 completion. An announcement or one success response is insufficient. Bound each wait and reject malformed TLVs, stale transactions and endpoint changes.

Vendor ipa_utils.c:6708 defines the 5.2 SRAM partition. Add the measured restricted-byte prefix when encoding the fields that require it. The following are local SRAM offsets, not DDR physical addresses:

| Region | Offset | Bytes |
| --- | --- | --- |
| uC shared / info | 0 / 0x80 | 0x80 / 0x200 |
| IPv4 hashed / nonhashed filter | 0x288 / 0x308 | 0x78 each |
| IPv6 hashed / nonhashed filter | 0x388 / 0x408 | 0x78 each |
| IPv4 hashed / nonhashed route | 0x488 / 0x528 | 0x98 each |
| IPv6 hashed / nonhashed route | 0x5c8 / 0x668 | 0x98 each |
| Modem header / AP header | 0x708 / 0x948 | 0x240 / 0x1e0 |
| Modem processing context / AP context | 0xb40 / 0x1660 | 0xb20 / 0x200 |
| Q6 quota stats / AP quota stats | 0x1868 / 0x18c8 | 0x60 / 0x48 |
| Filter/route stats / drop stats | 0x1cd0 / 0x2870 | 0xba0 / 0x20 |
| Modem memory | 0x2898 | 0xd48 |
| NAT / PDN configuration | 0x35e0 / 0x3ee8 | 0x900 / 0x100 |

End offset is 0x3fe8. IPv4 and IPv6 each have 19 route indices: modem 0 through 10 and AP 11 through 18. The vendor QMI field named num_indices is populated with the inclusive high index 0xa. Preserve the protocol's meaning instead of sending a guessed count of 11.

Initialize empty filter/routing entries to valid mapped zero-rule storage with the correct 5.2 encoding and canaries. Send real header bounds, route/filter starts, modem memory, processing context, statistics and shared-memory size. The vendor advertises AP WAN endpoint 18 as ctrl_comm_dest_end_pt. Its optional zero-size compression table is absent. Hash-table validity follows hardware capability. Validate all additions against actual SRAM size and reserved regions before sending QMI. Table offsets alone do not implement the command encodings or permissions.

## Remaining implementation and release gates

1. Obtain fresh Sol approval for the 36-bit SMMU correction and run one bounded PAS15-only probe.
2. Review the built 2202 GSI, tables and bidirectional QMI implementation.
3. After verified probe cleanup and explicit 2202 approval, prove I3 on the phone.
4. Coordinate the first ONLINE call with the modem agent after another Sol GO, then prove I4.

The isolated tree and Trash-safe build path are complete. Every build uses --remake -j6. PAS4 teardown remains the modem agent's responsibility. The combined driver must retain IPA backing and power if modem shutdown cannot be verified.

## Read-only phone result

Sol approved the exact 2200 source for one controlled preflight. The full remake with -j6 passed. The boot image was checked against all ten kernel segments and the expected boot arguments before use.

ast159 completed normally in 214 seconds with root clean and no GPU recovery. The per-run completion nonce matched. The kernel reported three firmware loads, 17824 bytes and span 0x7000. APPS SMMU ID0 was 0x4c017e72, ID1 0x60000048 and ID2 0x5111. Streams 0x4a0, 0x4a1 and 0x4a2 had zero matching SMRs. The worker returned final_error=0. PAS15, IPA/GSI registers and SMMU writes were untouched.

Local evidence is ios-asteroids/logs/ast159-ipa-preflight-2200.txt and its result.json, completion marker and summary. The screenshot was blank with hardware scaling, so it is not visual evidence. The phone returned to recovery. The IPA hold and lock were released. The root retains the normal selftest hook, logs, screenshot and /private/var/tmp/ipa-preflight-complete.txt. No data-path program or firmware was installed.

## 2200 implementation

`sh tools/ipa/check.sh` reconstructs the new parser from the public patch and tests it with ASan/UBSan. The parser validates MDT metadata, rejects overlapping/oversized loads and bounds every relocation to the GSI reservation. Its real extracted MDT result is three loads, 17824 bytes and span 0x7000, recorded locally in plans/ipa/logs/host-2200.txt. The kernel additionally checks split-file FAT chains and exact lengths. sweetipaprobe=1 uses the existing single modem worker, returns before MPSS work, and reads only SMMU identification and matching stream/context registers. Absent, bare, duplicate and nonexact probe arguments cannot enable it.

The tree was freshly cloned without hardlinks from the existing pristine XNU Git repository, then patched in order, using the battery agent’s safe preparation approach. Source trees were read only. The original script’s destructive first-clone branch was not executed. All actual kernel builds use --remake -j6 with per-tree Trash wrappers, preserved make intermediates and corrected config/kextsymboltool cleanup. The 2173 difflib patch needed new-file mode metadata in the apply stream for this Git version; the copied patch bytes were not changed.

## Draft contract for data patch 2220

Decided on 2026-11-03. These are implementation choices, not live I3 evidence. The current phone proof remains the read-only ast159 probe.

1. QMAP v1 with no checksum offload in either direction. IPA endpoint checksum bits stay zero. The data agent owns QMAP framing and IP checksums. Do not negotiate QMAP v5 or checksum headers/trailers in WDA.
2. Each AP WAN RX buffer is 8192 bytes. Planned IPA generic aggregation uses a 6 KiB byte threshold, 500 microsecond timeout, 16-packet limit and software EOF close enabled. The timeout is five 100 microsecond pulses. Keep the IP MTU at 1500, so the next complete QMAP datagram still fits after the byte threshold. WDA downlink aggregation must not advertise more than 8192 bytes or 16 datagrams. A completion can contain several QMAP frames and padding.
3. The public API will be in bsd/dev/modem/SweetIpa.h. `int sweet_ipa_transmit(const void *qmap, size_t bytes)` accepts one complete QMAP v1 frame, including its four-byte header. It copies the bytes into an owned bounded queue before returning 0. The caller may immediately reuse its buffer. ENETDOWN means the IPA path is unavailable. EAGAIN means queue full. EMSGSIZE means over 2048 bytes. No caller pointer survives the call. It is callable from the network output path, outside interrupt context, and never sleeps waiting for hardware.
4. `typedef void (*sweet_ipa_rx_fn)(void *context, const uint8_t *qmap, size_t bytes);` and `int sweet_ipa_register_rx(sweet_ipa_rx_fn receive, void *context)` register one kernel-lifetime receiver. Registration returns EBUSY if a different receiver is already registered. The callback runs in the modem/IPA worker, outside interrupt context and without the TX queue lock held. It receives one completed aggregate, at most 8192 bytes. Its pointer is valid only during the callback. The data agent copies any data it keeps. GSI repost happens after the callback returns. Registration is intended for a built-in driver, not an unloadable module.
5. The SM7635 DT/vendor and Linux 5.2 endpoint plan is AP WAN producer endpoint 2, GSI channel 5, and AP WAN consumer endpoint 18, GSI channel 9. AP command endpoint 9 uses channel 6. AP LAN exception consumer endpoint 11 uses channel 7. All are EE0. Planned AP event rings are 0, 1, 2, 3, allocated only after hardware reports them free. Modem WAN producer is endpoint 7, EE1 channel 0. Modem WAN consumer is endpoint 16, EE1 channel 7. No EE1 register is programmed by this driver.

`bool sweet_ipa_ready(void)` will report true only after hardware setup and the IPA QMI handshake complete. It is not proof of NAS registration or a WDS data session. Patch 2220 must own those states. Endpoint IDs above are source-derived until an audited live GSI setup verifies them. The 2202 kernel exports this API. Firmware-only mode keeps it unavailable.

## 2201 lifecycle candidate

The default-off PAS15-only probe now requires strict sweetmpss=1, sweetmbw=1, sweetmbwi=1 and sweetipafw=1. It returns before MPSS authentication. It claims separate AP and uC SMMU contexts, maps only owned DMA and required shared resources, validates firmware from read-only modem_a, loads through PAS15, reads five powered identification registers and attempts verified shutdown. No GSI command or packet channel is enabled.

Cleanup verifies every saved SMMU context field before freeing backing. Any uncertain PAS shutdown, TLB completion, restoration or DMA guard retains memory and power until reset. Copied modem2184 preserves the shared SN3 floor across late touch votes. Copied2186 permits polled RSC completion only while GIC IRQ37 delivery is disabled. IPA CN0/IP0 transactions use the same guarded completion path. IP0 requests the vendor's initial 120 MHz rate.

Host checks reconstruct the public patch and exercise the actual lifecycle with ASan/UBSan. They cover every allocation failure, PAS failures, shutdown retention, dropped restore writes, guarded GIC state, masked RSC status, dropped enable/clear writes and stale completion. Run `sh tools/ipa/check-lifecycle.sh` from 3a-IOS. The full remake passed and all ten kernel segments match the replacement image. No firmware phone run has occurred yet. The RSC review approved an earlier image. That approval is now superseded by the 36-bit correction described below.

Obsolete image evidence is plans/ipa/logs/host-2201-irq.txt, build-2201-irq.txt and verify-2201-irq.txt. The kernel SHA256 is e80b6e1c224b5830d38f42e1ca2ee444a6da9ee1afbd11c76b59e5815211ea67. The image SHA256 is 1299ced60e52df0aeaea3536435cf5297c2ec9ed896bfe3cc6f2681fd6d39f72.

## 2202 candidate and SMMU width correction

The GSI/table/QMI implementation and the public TX/RX API now compile in the isolated kernel. `sh tools/ipa/check-datapath.sh` reconstructs the public source and tests ring wrap, completion validation, command timeouts, SRAM/table readback, retained teardown, copied TX buffers, reentrant RX callbacks, SMP2P power responses and QMI readiness after transport flush. These are host results, not live endpoint measurements.

Before the first firmware boot, I found that ast159 ID2=0x5111 advertises 36-bit input, output and upstream addresses. The earlier 39-bit IOVA and 48-bit PA configuration exceeded that geometry. The queued probe was cancelled before phone access, and its hold was released. The corrected tables and allocator enforce 36 bits, with TCR2 PASIZE=1 and TCR T0SZ=28. Boundary tests reject a table or mapping crossing 64 GiB. A fresh audit is required. No IPA firmware has been loaded yet.

Modem2187 revision2 is copied unchanged with SHA256 f1dd3d1fd6406fca105bf001123ecf0f2850ba3c51013e4684c0bd3a6db8790f. It supplies eight QRTR slots. IPA registers server/client ports 0x4004/0x4005 during GLINK preparation, before QRTR starts. Future data binding must call sweet_data_set_ipa and register the RX callback after IPA preparation, before sweet_data_attach adds ports 0x4010/0x4011. The data setter refuses changes after attach. The 2202 candidate contains no data attach or ONLINE call.

Full mode requires sweetipa=1 plus strict modem, bandwidth, guarded completion, GLINK, RAM-rmtfs, pd-mapper and sysmon gates. Firmware-only sweetipafw and full mode are mutually exclusive. Full mode builds the AP-owned channels and tables before MPSS authentication. On exit it revokes software readiness first. Failed PAS4 shutdown retains every IPA mapping, ring, firmware allocation and power vote. A verified modem stop permits GSI teardown, then PAS15 shutdown, then verified SMMU restoration and backing release.

The previous firmware image is obsolete and must not boot. The current common kernel SHA256 is 9967fa32fb1242ca523dae10306a0585120badf1ad7a8049a844e93295ab543e. The firmware-only image is 39e08eecd19009f5fccc027ec2cba78d06b551f0d4127fa4df467531ddfb57a3. The handshake image is dc92356124af17ed81fb8bffbefd4f3d921bafd5a5ced76de7681ef1470c4ca8. Both passed all ten segment checks. Neither is approved for a phone run yet. The runner permits one boot attempt and forbids replay boots.

The ordered public series now reproduces all 18 IPA-touched files exactly from pristine XNU and the specified prerequisites. Local evidence is plans/ipa/logs/series-check.txt. Current parser, lifecycle and datapath sanitizer checks all pass. A fresh staged Sol review covers the firmware-only probe and the subsequent offline handshake, with separate approval decisions. Neither runner has started.

## ast166 firmware preparation refusal

The one approved Stage A attempt finished normally in 217 seconds, with a fresh matching nonce, stale=false and clean root before and after. The phone returned to recovery and the IPA hold/lock were released for the owner's next data USB slot. Local evidence is ios-asteroids/logs/ast166-ipa-firmware-2201.txt and its result directory.

CN0 and IP0 votes succeeded at 53.54 and 53.55 seconds. Preparation then returned error 5 before any PAS15 call or powered IPA/GSI read. The worker logged final_error=5. Its generic cleanup message reported released backing and preserved power floors, but does not establish that PAS15 started or that any SMMU context was installed. Stage A did not pass and I3 remains unproven.

Patch 2203 adds a read-only survey of all measured SMMU context banks and the modem SMEM partition/item497. This will distinguish lack of SMEM capacity from unused-context selection or register readback failure. It also adds precise preparation diagnostics to a later reviewed firmware attempt. The next survey uses only sweetipaprobe, with MPSS/IPA firmware and GSI gates absent. It requires a new exact-image review before use.

The next desk source includes copied modem 2188 revision2, hash 01d1979ea44147622f2aba7468ba22b475d306ba6128aa2835c853b9ec54e878. The copied revision1 is obsolete. No IPA image has run 2188 yet.

The corrected 2202 request now sends vendor TLV 0x23 with filter-statistics offset 0x1cd0, size 832 bytes and inclusive indices 128 through 179. Unlike the other SRAM table fields, the vendor uses this offset without the restricted-memory prefix. TLV 0x24 carries block_start_addr zero and the verified 0xb000-byte SMEM item 497 size. Optional peripheral telemetry remains absent because the driver does not implement it. Golden wire tests verify both extensions with a nonzero SRAM prefix against the pinned [Nothing QMI wire schema](https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635/blob/d5f5f965fd0b478eaf4eb63fd984c5a4a92135a7/vendor/qcom/opensource/dataipa/drivers/platform/msm/ipa/ipa_v3/ipa_qmi_service_v01.c).

The read-only 2203 image has SHA256 456f6108e87b52d9096203d70406b35ec432dde311542d26bade338d02258792, with kernel 3c320313139626124ba4882ed68d1c9bd79d74dd3d61de795d3af1cce767b091. Full remake and all ten segment checks passed. A fresh Sol review is running. The survey runner acquires the hold exclusively and consumes a per-image attempt record, so two launchers cannot share a hold or repeat an approval.

## ast169 and plain MMIO correction

The 2203 survey reset after 36 seconds. The captured kernel log reports an unhandled implementation-specific error after the IPA SID scan and before the first context-bank record. It contains no completed SMEM survey, PAS15 call or IPA/GSI access. This is one unexpected reset on this survey line. The run returned to recovery with clean root before and after, stale=false, and no fresh completion marker. The pulled frame does not prove this run completed. The hold and lock were released.

The frozen sweet_modem.o contains `ldr w16, [x25], #0x4` at object offset0x1900 for the first CBA2R survey read. The existing measured touch driver explicitly prohibits this writeback instruction form on the Gunyah-emulated APPS SMMU. This is a concrete compiler defect in the access path and a strong explanation of the panic. The log does not identify the faulting register or prove that every context bank is accessible.

Patch2204 adds explicit plain ARM64 ldr/str accessors for IPA's SMMU and IPA/GSI accesses. It preserves the modem owner's helpers and all lifecycle gates. It does not relax context selection, add a power vote or enable firmware. Host parser, probe, lifecycle and datapath sanitizer suites pass. `sh tools/ipa/check-mmio.sh` cross-compiles the actual production accessors and verifies plain instructions and barriers. Ordered patch reconstruction passes. A fresh Sol review of the rebuilt frozen survey is required before another phone run.

Local evidence is ios-asteroids/logs/ast169-ipa-memory-2203.txt, its result.json, and plans/ipa/frozen-2203-survey/sweet_modem-disassembly.txt. The frozen old image and its single-attempt record remain preserved. The iOS root contains the normal inert selftest hook and prior logs/frames/IPA markers. No IPA firmware or data executable was installed. I3 is unproven.

## Readiness contract for adapter2221

`sweet_ipa_ready()` means the IPA hardware and modem IPA handshake have completed. It is not a TX-queue-capacity test. The first true transition requires configured and running AP endpoints, successful INIT_MODEM_DRIVER0x21, INDICATION_REGISTER0x20, modem INIT_MODEM_DRIVER_COMPLETE0x35, the AP completion indication0x22 and a drained outgoing transport queue. A full TX queue can still return EAGAIN while readiness is true. NAS registration, ONLINE mode, WDS setup and IP connectivity remain separate data-agent states.

The published2202 through2204 code establishes that initial true transition, but its error-path revocation has the gap identified in data2221's audit. The private integration draft makes revocation terminal for this boot, clears copied TX on GSI/QMI/transport failure or stop, and publishes false before later QRTR/data callbacks. It also quiesces AP channels before data detach while retaining PAS15 and backing until verified PAS4 stop. Those changes have host coverage but are not yet in a phone candidate. Adapter2221 must wait for the audited integrated revision before a live data trial.

## Historical integration2205 desk status before publication

Public2204 is only the plain-MMIO correction. The next integration patch is2205. The working draft in plans/ipa/draft2205 implements all three IPA obligations from data2221's audit: terminal readiness revocation, the real2221 bind/refresh calls, and AP channel quiescence before data detach. It uses modem2189revision2 SHA9b4b570d and data2220audit6 SHA8a0b901c, with adapter2221 SHA4bdc74ce. Modem source approval does not approve this integration or an image.

The worker binds exactly once for sweetdata, after IPA preparation and before attach/MPSS. A partial bind failure preserves its errno and never reaches attach or refresh. Raw-only operation skips binding. IPA readiness is synchronized before and after each QRTR receive, before data discovery, before each normal/cleanup data poll and on transport loss. GSI/QMI errors clear the mutex-protected queue latch and drop copied TX before returning failure. The latch cannot revive after stop or failure.

After bounded WDS cleanup, the worker closes RX aggregation and stops owned AP GSI channels before data detach. It keeps channel/event ownership, mappings and power until verified PAS4 stop permits final GSI/PAS15 teardown. An uncertain stop remains an error and retains backing. Host sanitizer fixtures exercise the actual merged receive/stop code, real2221 partial-bind failure, successful handshake followed by service/GSI failure, DEL_SERVER followed by a data reply, and all four channel-stop failures.

Historical status before publication: this was private desk work while the reviewed2204 survey ran. It is not yet a public frozen2205 cutoff, full integrated build or phone GO. The data agent is checking one further readiness-before-service-sync edge before the combined review.

## ast170 and frozen integration cutoff

ast170 completed normally in214seconds, with fresh nonce48c6e4d1660b4ae78061c3f2943d8757, stale=false and clean root before and after. All72 context records and the SMEM record survived. No PAS15, power vote or GSI access occurred. The phone returned to recovery, with the IPA hold and lock released. The root retains the normal inert selftest hook, logs, frame and IPA completion markers. The blank hardware-scaled frame is not visual evidence.

The first five banks are referenced. Every unreferenced bank5 through71 has SCTLR.M clear, but nonzero SCTLR and TTBR reset values, with CBAR/CBA2R/FSR zero. The zero-only2201 selector necessarily rejects all of them. SMEM item497 is absent, while the partition has ample free space for its0xb000-byte allocation. The earlier ast166 log did not distinguish allocation from selection failure. The survey proves a selector defect, not which return executed in ast166.

Patch2206 accepts an unreferenced bank only when SCTLR.M, CFRE, CFIE and CFCFG are clear, CBAR/CBA2R are zero and FSR is zero. It saves the nonzero fields, rechecks ownership/state, clears control before configuration, and restores the original disabled control after other fields. Every saved register must match before backing can be freed. Dropped write, active/configured/faulted bank and measured nonzero-state tests pass. The arbitrary reset fields are consistent with ARM IHI0062D.c section16.2 and16.5.29, https://documentation-service.arm.com/static/5f900d34f86e16515cdc08fb. This is a source fix requiring a fresh Sol image review before any write probe. Powered identification reads now use the same explicit plain-MMIO accessor. Cleanup logs distinguish PAS15 not started and SMMU untouched.

2205 now implements all three obligations from data2221's audit: immediate terminal readiness revocation, bind before attach with per-pass and per-receive refresh, and AP channel quiescence before data detach. This includes offline/partial-start shutdown when data never attached. It also initializes all four FNR statistics base registers before handing tables to the modem. Data2220audit7 supplies revoke-sync-grant ordering before callbacks can advance setup.

### Exact application order

Start with the brief's2130v4,2140..2144,2150,2151,2153,2152v4,2154 and USB2160/2161v5 series. Apply copied modem2170..2189 in its README order, including2187revision2,2188revision2 and2189revision2. Apply data2220audit7 and2221 unchanged. Apply the current IPA2200..2206 in numeric order. The current2200..2202 runtime hunks are rebased onto2189v2. Do not use their older2188-based copies. Their non-runtime hunks and2203/2204 are unchanged.

From the project root, run `python3 plans/ipa/tools/reproduce-cutoff.py`. It checks copied owner bytes and reproduces every IPA-touched file from pristine source and the full ordered series. Run `sh tools/ipa/check.sh`, `sh tools/ipa/check-lifecycle.sh`, `sh tools/ipa/check-datapath.sh` and `sh tools/ipa/check-mmio.sh` from3a-IOS. The datapath suite reconstructs actual worker functions and the real2221 adapter, including partial bind failure, receive/discovery ordering, stopped-channel retention and teardown outcomes. Build only in your own tree with --remake -j6. The integrated IPA build is running. These source tests are not image approval or I3 evidence.

Frozen source hashes follow. No phone image is approved by this cutoff.

- `2189-asteroids-modem-data-worker-hooks.patch` `9b4b570d07d9a5b02288f006e500a733bfdea0876acaa938ef5b7395497ed58f`
- `2220-asteroids-data-qmi-session-and-link.patch` `410ee41d9269ab7e6f97b1beb4c4550af28464b5503c5719790094bedc2ebdf8`
- `2221-asteroids-data-ipa-binding.patch` `4bdc74ce4ad45000ba2efe18bf45454169dcb6464483d5722c918892875e1e49`
- `2200-asteroids-ipa-read-only-preflight.patch` `eee590a673ad46eabbb272825d576b660122531437ad45898f58b5cf9fb49b9a`
- `2201-asteroids-ipa-pas15-isolated-dma.patch` `334ea045a2c066fa59fc48836312e2865d637127c59062f1857b7e24570ca9b9`
- `2202-asteroids-ipa-gsi-qmi-data.patch` `c54dc55bdeee05a74a0ac7f50c5e8bef30aada631e11e6ce6b49b67efb0ceef3`
- `2203-asteroids-ipa-memory-read-only-survey.patch` `0aec26665a2f71c227d6dd9bbf480d04b0934ae5b865bfc1297d9309c87b9c52`
- `2204-asteroids-ipa-plain-mmio.patch` `15b52570e717c5537435e490a1477fd97af632bac31298cdc8e2b43eb76e28b3`
- `2205-asteroids-ipa-data-worker-integration.patch` `479f4c1df57b857745b75262b8f4e9895d44e8d22e643c9fa6ef81beaf067590`
- `2206-asteroids-ipa-unused-context-reset.patch` `0e2795df6d0bd53aa9bd790eba558d261b8a098bd91cd893d08f6629811d9425`

## 2207 source audit and 2208 correction

The full 2206 integrated build and incremental 2207 remake passed. Both 2207 images matched all ten kernel segments. They must not boot. Sol's fresh source review, plans/ipa/sol-2207-source-audit.md, found that the old global page descriptors and shared VMID0 allow an architecture-permitted TLB hit across AP and uC despite separate ASIDs. No phone operation followed that NO-GO.

Patch 2208 allocates two distinct nonzero 8-bit VMIDs absent from every inherited context bank, including disabled banks. It rechecks each lease under the shared allocator claim, rejects CLIENTPD, extended StreamIDs and 16-bit VMID mode, and leaves global controls unchanged. The existing generic APPS allocator uses VMID0; touch and USB reuse inherited contexts. The dedicated GPU SMMU has a separate TLB namespace. IPA leaves now set nG as well.

The driver verifies the complete private translation configuration while disabled, before invalidating that VMID or enabling the bank. This also prevents a dropped CBAR write from causing maintenance under inherited VMID0. Cleanup verifies the installed private identity and disables translation before maintenance. Partial setup that never enabled a bank skips maintenance. All old fields still require exact restoration before freeing backing. Accepted plain-MMIO maintenance writes are a hardware assumption. A zero TLBSTATUS does not independently prove acceptance of a silently dropped write-only command, and the architecture permits over-invalidation.

The maintained shared-TLB regression uses actual descriptors and VMID allocation. It seeds an inherited global entry and a stale entry in the new lease, fills the AP translation, and proves that the empty uC domain faults for the same IOVA. Its negative control reproduces the old global/shared-VMID cross-domain hit. Fault-injection tests assert that no maintenance command targets VMID0. Mode-change retention and inherited-VMID collision checks pass. The maintained hardware suite now includes the additional good-handshake/GSI-failure and queued-TX/pending-active-RX stop cases from data's 2221 re-audit.

Apply 2207 after the frozen 2200 through 2206 series, then 2208. The earlier cutoff remains byte-for-byte unchanged. Ordered reproduction now checks all 56 integrated modem, data and IPA files. Use `python3 plans/ipa/tools/reproduce-cutoff.py` from the project root. No IPA or ONLINE image has phone approval yet. A fresh Sol review must inspect the corrected source and exact rebuilt image.

- 2207 SHA256 `cac60147ce18dacf3b0d6804e93425f35dbf6fe0ffa0cf8fde8ebae35fc21106`.
- 2208 SHA256 `afc38c0297678892e8e7e1a8410e629903c34867b589fcae11df9d6cda52aacb`.
