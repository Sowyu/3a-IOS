# IPA on SM7635

## Checkpoint, 2026-11-03

I1 is complete. I2 is in progress. Patch 2200 passed its audited read-only phone run, ast159. Firmware bounds and the APPS SMMU survey passed, with no IPA/GSI MMIO or PAS15 call. The isolated xnu-ipa tree now carries copied modem patches 2170 through 2186. Mobile data does not work yet.

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

1. Obtain fresh Sol approval for the 2201 RSC completion correction and run one bounded PAS15-only probe.
2. Finish GSI rings, immediate commands, IPA tables and the bidirectional modem QMI handshake in 2202, with fault tests.
3. Build the combined modem/IPA source, obtain a separate Sol GO and prove I3 on the phone.
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

`bool sweet_ipa_ready(void)` will report true only after hardware setup and the IPA QMI handshake complete. It is not proof of NAS registration or a WDS data session. Patch 2220 must own those states. Endpoint IDs above are source-derived until an audited live GSI setup verifies them. No API is linked into the current 2201 probe image yet.

## 2201 lifecycle candidate

The default-off PAS15-only probe now requires strict sweetmpss=1, sweetmbw=1, sweetmbwi=1 and sweetipafw=1. It returns before MPSS authentication. It claims separate AP and uC SMMU contexts, maps only owned DMA and required shared resources, validates firmware from read-only modem_a, loads through PAS15, reads five powered identification registers and attempts verified shutdown. No GSI command or packet channel is enabled.

Cleanup verifies every saved SMMU context field before freeing backing. Any uncertain PAS shutdown, TLB completion, restoration or DMA guard retains memory and power until reset. Copied modem2184 preserves the shared SN3 floor across late touch votes. Copied2186 permits polled RSC completion only while GIC IRQ37 delivery is disabled. IPA CN0/IP0 transactions use the same guarded completion path. IP0 requests the vendor's initial 120 MHz rate.

Host checks reconstruct the public patch and exercise the actual lifecycle with ASan/UBSan. They cover every allocation failure, PAS failures, shutdown retention, dropped restore writes, guarded GIC state, masked RSC status, dropped enable/clear writes and stale completion. Run `sh tools/ipa/check-lifecycle.sh` from 3a-IOS. The full remake passed and all ten kernel segments match the replacement image. No firmware phone run has occurred yet. The earlier Sol GO is superseded by the RSC correction and a fresh audit is pending.

Local evidence is plans/ipa/logs/host-2201-irq.txt, build-2201-irq.txt and verify-2201-irq.txt. The kernel SHA256 is e80b6e1c224b5830d38f42e1ca2ee444a6da9ee1afbd11c76b59e5815211ea67. The image SHA256 is 1299ced60e52df0aeaea3536435cf5297c2ec9ed896bfe3cc6f2681fd6d39f72.
