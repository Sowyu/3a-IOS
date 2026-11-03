# iOS 26 baseband stand-in: reverse engineering notes

Read-only research on build 23G90, iPhone12,1 (n104ap). Nothing was built, run on the phone, or committed. This corrects parts of `ios-cellular-research.md`, so read section 0 first.

Sources:
- The three mounted DMGs, the DSC with a patched `.symbols` copy in /tmp, and `n104ap.adt`.
- Our ADT `ux-fix/ux-420pt-2x-840x1860.adt`.
- The iPhone12,1 kernelcache, which was not on disk. I fetched only the kernelcache from Apple's CDN with `ipsw download ipsw --device iPhone12,1 --build 23G90 --kernel` into /tmp.

Method: `strings`, `ipsw kernel cpp/kexts/extract`, `ipsw dyld disass/dump/a2s`, `otool -tV`, and the kernelcache prelink plist parsed with python.

Tags: FACT means read directly from a binary or plist. INFER means a conclusion drawn from facts. UNKNOWN means not established.

## 0. Corrections to the previous report

- FACT: this kernelcache has no `AppleBasebandPCI*` kext. `ipsw kernel kexts` lists `com.apple.driver.AppleBasebandI19`, `AppleConvergedIPCBBControl`, `AppleConvergedIPCOLYBTControl`, `IOACIPCFamily` and `AppleConvergedPCI`. There is no Qualcomm or MAV kext.
- FACT: the names `AppleBasebandPCIControl`, `AppleBasebandPCIUserClient` and `AppleBasebandPCIMAVPDPManager` appear only in userland strings. The DSC is shared across devices. They are the kernel interface of Qualcomm iPhones, and this device never loads them.
- FACT: the iPhone 11 kernel interface is called ACIPC (Apple Converged IPC), with "RTI" rings over PCIe. It is not the "PCI user client" described before.
- FACT: userland does not read `baseband-chipset` anywhere I could find. The vendor comes from the hardware model string (section 1). Only the kernel reads that property.

## 1. Chipset selection

### Kernel side

- FACT: the `baseband-chipset` string and the symbol name `getBasebandChipset` are in `AppleConvergedIPCBBControl`. The kernelcache strings also show `AppleCellularDataPacketPool: failed to retrieve baseband chipset information`.
- FACT: the personalities in that kext carry a `constants` dict keyed by chipset, for example `AppleConvergedIPCPDPManager.constants = {fallback: "ice19", ice19: {...}}`. The same shape exists for `AppleConvergedIPCPDPADAM`.
- FACT: the only key present is `ice19`, and `fallback` is `ice19`.
- INFER: an unknown chipset value falls back to `ice19`. The kext cannot be steered to a Qualcomm table through this property, because no Qualcomm table exists in it.
- FACT: ADT `defaults` has `baseband-chipset: "ice19"` (n104ap.adt line 6032). Our ADT also has it (`ux-420pt` line 315).
- FACT: our ADT has no `baseband` node. `ipsw dt` greps for `baseband` match only `baseband-chipset`. Because of that, launchd loads the `-noncellular` CommCenter plist on our phone today.

### Userland side

- FACT: `AppleBasebandManager.framework` (code in the DSC) holds three library paths in a table at `0x279708470`. The entries in order are `/usr/lib/libBasebandManager.dylib`, `libBasebandManagerICE.dylib`, `libBasebandManagerICE.dylib`, `libBasebandManagerDAL.dylib`.
- FACT: the selector is at `0x246d9c248`. It calls `capabilities::radio::vendor()`, subtracts 1, requires the result to be 0 to 3, and calls `dlopen(table[vendor-1])`. It then looks up `AppleBasebandManagerInitServer/StartServer/StopServer/ShutdownServer/GetProfile` with `dlsym`.
- INFER: vendor 1 is Qualcomm Maverick (`libBasebandManager`, QMI transport). Vendors 2 and 3 are ICE (ARI transport). Vendor 4 is DAL (MIPC transport). The order is from the table; the names come from the library contents (QMI/ARI/MIPC transport sources).
- FACT: `capabilities::radio::vendor()` in `libTelephonyCapabilities` calls `TelephonyRadiosGetRadioVendor` at `0x1aaee6df0` in `libTelephonyUtilDynamic`.
- FACT: the init block behind it (`0x1aaf0d3a0`) builds a table of short strings and numbers. `libTelephonyUtilDynamic` asks MobileGestalt for `HWModelStr`, `HasBaseband` and `DeviceName`, and logs `Detected a baseband device with hardware model string '%s'`.
- INFER: the vendor derives from the board model (N104AP), not from `baseband-chipset`. I did not finish decoding the table at `0x1aaf5d0a8`, which holds pairs such as `(0x63, 'U')` and `(0x14, 'D')`. I did not confirm that N104 maps to ICE.
- INFER: to force the Mav path without a new IPSW, someone has to make MobileGestalt return a model string that maps to Maverick, or patch the table. Changing `target-type` in the ADT would also change kernel BSP matching, which is risky.
- FACT: `libCommCenterMCommandDrivers` and the QMI command drivers (`Mav13`, `Mav20`..`Mav25`, `Mav30`) are chosen later, from the firmware chip id (`libBasebandCommandDrivers` has `BasebandHardwareChipID`, `kKeyBasebandChipID`). A stand-in must report a chip id for which a Mav driver exists.
- UNKNOWN: which chip id values map to which Mav command driver.

### ADT properties for a Qualcomm node

- UNKNOWN: this IPSW cannot tell us the exact Qualcomm node. No kext here matches a Qualcomm `compatible`.
- FACT: the only baseband personality is `AppleBasebandI19` with `IONameMatch: "baseband,i19"`, `IOMatchCategory: AppleBaseband`, `IOProviderClass: IOPlatformDevice`, `IOUserClientClass: AppleBasebandUserClient`. The `valid_providers` lists in ACIPC name `AppleBasebandI19`, `AppleBasebandMacOSIce`, `AppleBasebandSimI19`, `AppleBasebandSimINT1`, `AppleBasebandINT1` and `AppleBasebandMacOSInt`.
- FACT: the Intel node in n104ap.adt (lines 5845 and following) has `device_type "baseband"`, `compatible "baseband,i19"`, `class 3`, `default-options 4`, `interrupts [45 4 16]`, `backpowering-pin-count 0`, `backpowering-alias-count 1`, `spmi-device`, and `function-*` entries for `bb_on`, `reset_det`, `coredump`, `pmu_exton`, `pcie_port_control` and `sac`.
- INFER: a stand-in node needs `device_type = "baseband"` for launchd, and some `compatible` that our own kext matches. The existing `AppleBasebandI19` kext would also attach to `baseband,i19`, so using a new name avoids that.

## 2. Kernel side

### Kexts and classes (FACT, from `ipsw kernel cpp`)

- `AppleBasebandI19` kext holds `AppleBaseband`, `AppleBasebandI19`, `AppleBasebandPlatform`, `AppleBasebandPCIEDevice`, `AppleBasebandBackpowering`, `AppleBasebandLogging` and `AppleBasebandUserClient`.
- `AppleBaseband` is at `0xfffffff0083a3a04` (init) with size 0xd8. `AppleBasebandUserClient` init is at `0xfffffff0083a6990`, size 0x128, vtable at `0xfffffff007c31ab0`.
- `AppleConvergedIPCBBControl` kext holds `AppleConvergedIPCICEBBRTIDevice`, `AppleConvergedIPCLedaBBRTIDevice`, `AppleConvergedIPCPDPManager`, `AppleConvergedIPCPDPInterface`, `AppleConvergedIPCPDPADAM`, `AppleConvergedIPCDirectPDPInterface`, `AppleCellularRTIRing`, `AppleCellularDataPlane*`, `AppleCellularDataPacketPool*` and `IOAppleConvergedIPCBBControlUserClient`.
- `IOACIPCFamily` holds `IOAppleConvergedIPCRTIDevice`, `IOAppleConvergedIPCRTIInterface`, `IOAppleConvergedIPCRTIContext` and `IOAppleConvergedIPCRTISharedInterface`.
- `AppleConvergedPCI` matches PCI ids `0x5fa014e4 0x5f6914e4 0x5f3114e4 0x5f7114e4 0x5f7214e4 0x5f8314e4`. The ICE personality matches `baseband-pcie` or `pci8086,7660`. `AppleConvergedPCILedaBB` matches `baseband-pcie-leda`, `pci106b,1c04` and `baseband-pcie-pandia`.

### Match chain (FACT, from the prelink plist)

1. `AppleBasebandI19` matches `baseband,i19` on the platform node and publishes service class `AppleBaseband`.
2. `AppleConvergedIPCICEBBControl` has `IOProviderClass: AppleBaseband`, `valid_providers` including `AppleBasebandI19`, and `IOUserClientClass: IOAppleConvergedIPCBBControlUserClient`.
3. Below it sit `IOAppleConvergedIPCRTIInterface` children, one per protocol. `AppleCellularDataPlane` and `AppleCellularDataPacketPool` match on those.

### RTI interfaces on the ICE control (FACT, pipe pairs and boot stage)

| protocol | pipes | bootstage |
|---|---|---|
| boot | 18, 19 | 1 |
| data | 0, 1 | 2 |
| adam | 2, 3 | 2 |
| ari | 4, 5 | 2 |
| gnss | 6, 7 | 2 |
| ogrs | 8, 9 | 2 |
| trace | 10, 11 | 2 |
| at | 12, 13 | 2 |
| loopback | 14, 15 | 2 |
| vcom | 16, 17 | 2 |
| mediactl | none | 2 |

- FACT: ARI traffic runs on pipes 4/5. Qualcomm QMI has no entry in this list.
- INFER: pipes are ring pairs in PCIe shared memory. That is the only transport this kernel offers. QMI would need its own pipes.

### Userland entry points (FACT)

- `libTelephonyBasebandDynamic` calls `IOServiceMatching("AppleBaseband")`, then `IOServiceGetMatchingService` and `IOServiceOpen`. It uses `IOConnectCallMethod` and `IOConnectCallScalarMethod`, `IORegistryEntrySetCFProperty` for reset properties, and `IOServiceAddInterestNotification` for events.
- FACT: it logs `AppleBaseband IOService is not found. Finding it via matching service`, `Failed to self-identify with AppleBaseband` and `Unable to communicate with AppleBaseband driver`.
- FACT: `AppleBasebandUserClient` strings include `userland`, `Helper`, `CommCenter`, which suggests a client-name or entitlement check at `initWithTask`.
- FACT: selector immediates seen in wrappers near the IOConnect calls include 0x21 (`AddFreqListExt`), 0x0f and 0x40 (`GetCPMSPPMBudget`), 0x2e, 0x0b and 0x12.
- UNKNOWN: the full selector table with argument shapes. The wrappers dispatch through a controller object, and my scan of call sites was lossy. It needs a proper pass over `AppleBasebandUserClient` `externalMethod`, which I did not finish.

### Provider operations behind the user client (FACT, function names in `AppleBasebandI19`)

`resetModem`, `powerCycleModem`, `radioOn`, `coredumpModem`, `timeSync`, `clientCheckIn`/`clientCheckOut`, `enableTraceMux`, `radioUsbOn`, `pmuExtOnConfig`, `getPmuExtOn`, `setPowerOnBBPMUPin`, `setSoftwareShutdownPin`, `setResetPin`, `quiesceBackpoweringPins`, `restoreBackpoweringPins`, `setVoltage`, `setClock`, `powerDebugUnblock`, `add/remove/clearFreqListInst`.

Notifications: `ocpDataReady`, `spmiReady`, `ocpError`, `spmiError`, plus `registerKernelClientToAppleBasebandFunction`.

### Power-off policy (FACT)

- `clientCheckOut` logs `All CommCenter clients are gone, powering radio off`, and a power-off timer then calls `radio off`. This means CommCenter's open user client keeps the radio state alive.
- States: `kBasebandStateInvalid/Reset/Booting/Booted/BootError/ShuttingDown/HasShutdown/ShutdownError/CoredumpStarted/CoredumpFinished/CoredumpError/Error/Crashed`.

### Transport for QMI/ARI (FACT and INFER)

- FACT: ARI goes over ACIPC pipes 4/5. There is no `/dev` node for it in the kernel strings.
- FACT: for the Mav QMI path, `libBasebandManager` creates "QMux channels per service" with `RouterIPCController`, strings `ipc.router.ctrl` and `ipc.router.delegate`, and an object named `MavRouter`. Those are Apple-side classes, not a device node.
- INFER: on a real Qualcomm iPhone the kernel presents an IPC-router-like channel over PCIe through the `AppleBasebandPCI*` kext family. That kext is not in this root, so its user-client interface is UNKNOWN.

## 3. Boot and health handshake

Facts from `libBasebandManager` (the Mav manager), `libBasebandManagerICE` and the daemon strings:

- Boot state values CommCenter reads: `BasebandBootStateIsReady/Booted/WillReset/DidReset/WillShutdown/HasShutdown/Dead/Unknown`, command `CommandGetBasebandBootState`.
- Events: `EventBasebandBootStateChange`, `OperatingModeChange`, `WakeReason`, `NVDataErased`, `PowerOff`, `ResetDetected`, `ConfigUpdated`.
- Power assertions `com.apple.AppleBasebandManager.RadioNotReady.Bootup` and `.Shutdown`, with a watchdog. The assertion releases when the baseband is ready.
- Mav manager log lines: `Baseband image push time = %u millisec`, `Baseband Firmware Path Not Found`, `Failed to find the firmware in "%s"`, `Successfully received ping response from baseband (firmware version is %s)`, `QMux channels created`, `Notified CXM that Baseband is Ready`, `Success setting QMAPSettings`.
- Health: a ping timer runs. When total health failures reach a threshold the manager requests an EFS erase and resets (`Total health failure count reached EFS erase threshold`).
- Extra states: `Baseband Billboard is Ready`, `Baseband Coredump Ready`, `OCP Read/Write Ready`, `PSI is not running enhanced mode, unsupported firmware`.
- BBUpdater (`libBBUpdaterDynamic`, `libBasebandManager` imports it): `GetPersonalizationParams`, TSS ticket, `unable to retrieve baseband chip revision`. Firmware assets: `com.apple.MobileAsset.MAVBasebandAssets` and paths under `/usr/local/standalone/firmware/Baseband/Mav20..25,30`.
- Power data: `CPMS` budgets pushed only when the baseband is ready and online (`Not sending budget to BB due to this condition`).

INFER, what a stand-in can answer trivially:
- Boot state `Booted/IsReady` set from the real modem's QRTR announcement, instead of an image push.
- `kAppleBasebandManagerDaemonReady`, power assertion release, ping response with a version string.
- Reset and power calls (`resetModem`, `radioOn`, `checkIn`/`checkOut`) as no-ops that acknowledge.
- Coredump, trace mux, SPMI, PMU and frequency-list calls as no-ops that return success.

INFER, what is not trivial:
- Image push and BBUpdater personalization. Skipping them needs the manager to see a baseband already booted (`Already booted` style short cut). I did not locate that branch.
- QMAP settings and flow control, which the manager sets on the data path.
- PRI and carrier-config push (`Baseband PRI matches our cached version`). A modem that reports no PRI version may trigger a PRI/EFS push that cannot be satisfied.

## 4. Data path and `pdp_ipN`

- FACT: the string `pdp_ip%u` (also `pdp_ip%.2u`, `pdp_ip%1u`) is in `AppleConvergedIPCBBControl`. The classes that use it are `AppleCellularNetworkInterface(Int1)` (an IOSkywalk network interface) and `AppleConvergedIPCPDPInterface`.
- FACT: `AppleCellularDataPlaneUserClient` creates and removes them. Its logs: `Requesting new Network Interface (interfaceID, prioUL, rss, context)`, `Removing Network Interface`, `starting/stopping Network RX queues`, `starting/stopping TX Network Queues`, `Requesting data of FilterRule`, `Testing Offload Request`.
- INFER: CommCenter (or its data helper) opens `AppleCellularDataPlane`, asks for interface N, and the kernel registers `pdp_ipN` with ifnet. It does not create the BSD interface from userland.
- FACT: the data plane uses a packet pool configured in the personality: rx pool 8700 buffers of 32768, tx pool 4000 buffers of 2048. The PDP manager `constants.ice19` give `max_packet_size 2048`, `rx_slot_count 1024`, `tx_slot_count 512`, `ifnet_eflags IFEF_3CA`.
- FACT: the kernel code for the data plane lives in the same kext as the RTI rings, so it assumes the PCIe ring format. Rings: `kernel-dataplane` contexts 1 and 2 with completion rings, plus `user-pdp` on pipes 25/26.
- INFER: for a Qualcomm stand-in, the minimal route is a small kext (or our existing modem driver) that registers `pdp_ip0` as an `ifnet` with ioctls CommCenter and SystemConfiguration need, and forwards packets to the real rmnet/QMAP path of the modem.
- UNKNOWN: which `ioctl`s or IOKit calls CommCenter makes on the Mav side, since the Mav PDP manager (`AppleBasebandPCIMAVPDPManager`) is absent from this kernel.

## 5. Carrier bundles

- FACT: they are on the system volume at `/System/Library/Carrier Bundles/iPhone`: 681 `*.bundle` directories and 1865 other entries, mostly symlinks. There is a `Default.bundle` and an extra `System` directory.
- FACT: entries are named by MCC/MNC with optional match suffixes, for example `20801`, `20801_GID1-41`, `20801_ID-893301`, `23410_GID1-0AFFFF`, `310410_GID1-2A`. Each one is a symlink to a bundle such as `TMobile_Germany.bundle`.
- FACT: CommCenter has match modes spelled `device+mvno+multimode+carrier`, `mvno+multimode+carrier`, `device+mvno+carrier`, `mvno+carrier`, `mvno+gsma+carrier` and `device+mvno+gsma+carrier`. It logs `Failed to get matching carrier bundle by keys`.
- INFER: it picks by MCC/MNC from the SIM's IMSI first, then narrows by GID1, ICCID prefix (the `ID-` suffix) and MVNO keys, and falls back to `Default.bundle`. ICCID is a refinement, not the primary key.
- FACT: bundles hold `StatusBarImages`, `CarrierName`, `APN` settings, `MatchingICCIDPrefixes`, `SupportedSIMs`, IMS settings and voicemail. APNs for Mobile Data come from here.
- INFER: no extra bundle work is needed. The stand-in only has to report correct MCC, MNC, IMSI-derived GID1 and ICCID.

## 6. Assessment: what a stand-in must provide

Layers: (A) kernel: an `AppleBaseband` service and user client. (B) a vendor flip so userland takes the Mav path. (C) a QMI source for the Mav command drivers. (D) a data plane.

### (i) SIM showing in Settings, rough effort 1 to 2 weeks if (B) works

Needs:
1. A `baseband` DT node so the cellular CommCenter plist loads.
2. A kernel service named `AppleBaseband` with an `IOUserClient` that accepts CommCenter and returns success to the selectors it calls at startup and for `registerForEvents`.
3. Vendor flip to Maverick (B), so `libBasebandManager` loads.
4. A boot-state of Booted and a ping response (section 3).
5. A channel that carries QMux frames for `DMS`, `UIM`, `NAS` and `WDS`. Our modem already announces UIM 0x0B, DMS 0x02, NAS 0x03 and WDS 0x01.
6. A ready SIM from `UIM` (ICCID, IMSI, EF_AD or GID1) mapped into what the QMI command drivers expect.

### (ii) Bars and carrier name, rough effort another 1 week

Needs `NAS` signal strength, serving system, operator name and registration indications, forwarded as QMI to the Mav command driver. Plus a carrier bundle match from (i). Risk is indication ordering and timing, not the transport.

### (iii) Mobile Data working, rough effort 2 to 4 weeks

Needs:
- `WDS` start network with the carrier bundle APN, then bind the data-call to `pdp_ip0`.
- A kernel `ifnet` called `pdp_ip0` with the properties SystemConfiguration expects (`Cellular WAN: pdp_ip%d`), forwarding IP packets to the modem's rmnet/QMAP path.
- Answers to `AppleCellularDataPlane`-style requests, if the Mav path asks for them (UNKNOWN).

### Riskiest unknowns

1. The user-client interface CommCenter uses on the Mav path. The kernel half is not on this root, so selectors and structs must come from disassembling `libTelephonyBasebandDynamic`, `libTelephonyIOKitDynamic` and `libBasebandManager`. Section 2 only has fragments.
2. Whether the vendor flip (B) can be done cheaply. Finding the exact input of `TelephonyRadiosGetRadioVendor` is the first job. If it depends on a signed board table, a DSC patch may be needed.
3. Mav firmware expectations. The manager may demand firmware and PRI/EFS pushes, a firmware version in a known format, and a Mav chip id. A Qualcomm SM7635 is a different generation from any Mav that Apple ships, so the command driver may issue QMI messages that this modem answers differently.
4. Data plane timing and QMAP flow control, which only show up under load.
5. Entitlements. The user client checks the caller (`CommCenter`, `Helper`). Our kernel service must accept the real CommCenter, and any code signing or AMFI checks in the stand-in path are UNKNOWN.

## Recommendation

Do the vendor flip investigation first, because it decides whether the Qualcomm QMI path is reachable at all and it costs about two days: finish decoding the table at `0x1aaf5d0a8` in `libTelephonyUtilDynamic` and see what `HWModelStr` value gives Maverick. In parallel, disassemble `libTelephonyBasebandDynamic` and `libBasebandManager` to get the real `AppleBaseband` selector table and the Mav transport contract, since the kernel half does not exist in this root. If the flip is possible, build the stand-in in the order SIM (QMux over our existing QRTR to the Mav command driver), then NAS, then `pdp_ip0`. If the flip needs a patched DSC or a different model string that breaks other subsystems, the ICE/ARI route is worse, because this kernel already speaks ACIPC and the stand-in would have to fake ring semantics. In that case the better fallback is to accept a SIM-only milestone (i) and revisit data later.

## 7. Qualcomm follow-up, 2026-11-03

This section supersedes the vendor uncertainty in section 1 and the proposed Qualcomm data-plane contract in sections 4 and 6. FACT means a binary instruction, symbol, string or plist value. INFER means a proposed interpretation or implementation. UNKNOWN means the contract has not been established. This is a static research result, not a tested port or a complete driver specification.

### 7.1 Scope and reproducibility

FACT: I fetched only the kernelcache for iPhone15,4, iOS 26.0 build 23A341, with:

```sh
ipsw download ipsw --device iPhone15,4 --build 23A341 --kernel --confirm --output /tmp/everios-modem-followup
```

The source was Apple's CDN:
https://updates.cdn-apple.com/2025FallFCS/fullrestores/093-41248/3861DA81-73EF-48C6-BDC1-A7D6D307569F/iPhone15,4_26.0_23A341_Restore.ipsw

FACT: the analyzed, decompressed Mach-O kernelcache has SHA256 `c6ec140934e801bfbf394b8777ef264df6a2846a9a706b0b519dd34dde68cb0d`. Kernel addresses below belong to this kernel, not the 23G90 Intel kernel. Userland addresses belong to the existing 23G90 DSC. This build mismatch is deliberate and must be considered when using the selector tables.

I used `ipsw kernel cpp --json --methods`, extracted the four baseband kexts, disassembled their `__TEXT_EXEC.__text`, and parsed `_PrelinkInfoDictionary`. I disassembled the relevant DSC libraries, including `libPCITransport`, `libIOABP`, `libmav_ipc_router_dynamic` and `libBasebandCommandDriversQMI`, which resolve several indirections in the two requested libraries.

FACT: the original DSC lacks local symbols. I used the existing synthetic `.symbols` workaround only on scratch copies. Every instruction emitted for the eight userland libraries used here was compared against a fresh clone of the original project DSC: 1,013,659 instructions, zero byte mismatches. Symbol names are available exports and tool annotations, not recovered private debug symbols.

All new scratch files were under `/tmp/everios-modem-followup` and were moved to recoverable Trash after recording these results. No phone access, sudo, mounts, kernel-tree changes or commits were used. The only project edit was this appended section.

### 7.2 The model table is decoded

FACT: `0x1aaf5d0a8` is a pool of eight-byte constants used to construct model records. It is not a ready-made array of vendor pairs. The function at `0x1aaf0d3a0` constructs twelve-byte comparison records on its stack, compares them, then stores product, radio and vendor results.

The record layout is:

```text
+0  uint32 model number, decimal
+4  char   model prefix, for example 'N' or 'D'
+5  char[3] model suffix, NUL terminated, for example "", "s", "b", "p"
+8  uint32 hardware configuration
```

FACT: the constructors use configuration 6. Comparisons such as `0x1aaf0ed68..0x1aaf0ed84` accept equality or a wildcard configuration of 0 or 6. This explains why raw constants such as `(104, 'N')` alone did not reveal a vendor.

FACT: I evaluated the record construction and comparison instructions in a scratch ARM64 emulator, intercepting only the bounded string-copy stub. I stopped at the comparison entry and then evaluated each constructed record through the result stores. This executes local analysis code, not any code on the phone. The relevant outputs are:

| model | product | radio | vendor |
|---|---:|---:|---:|
| N104 | 91 / 0x5b | 8 | 2 |
| D421 | 92 / 0x5c | 8 | 2 |
| D20 | 62 / 0x3e | 5 | 1 |
| D27 | 132 / 0x84 | 12 | 1 |
| D73 | 134 / 0x86 | 12 | 1 |

FACT: N104 maps to vendor 2, the ICE manager entry. `iPhone12,1` is a product type, not the string this routine parses. The N104AP hardware model supplies the N104 record through `TelephonyUtilParseHardwareModel`, called at `0x1aaf13158`.

FACT: MobileGestalt is queried for `HasBaseband` at `0x1aaf130bc` and `HWModelStr` at `0x1aaf130e8`. The parsed record is cached at `0x1eccb61f0`. The comparator stores the product/radio pair at `0x1eacbb0b8`, the hardware configuration at `0x1eacbb4e8`, and vendor at `0x1eccb6120`. `TelephonyRadiosGetRadioVendor`, `0x1aaee6df0`, returns the last value behind `dispatch_once` token `0x1eccb6200`.

FACT: `TelephonyRadiosGetProduct` at `0x1aaf10fd8` and `TelephonyRadiosGetHardwareConfig` at `0x1aaf11020` share this initialization. Changing only the returned vendor leaves product/radio identity unchanged.

Decoded comparison records, grouped by the resulting vendor, follow. These are model records, not a claim that every model is supported by this particular downloaded kernel.

- Vendor 0: `U99n, W99h, P99d, M64d`.

- Vendor 1: `J99a, J128, J121, J208, J72s, V57, V54, V53, J638, J608, J482, D94, D93, D48, D47, J721, J718, J411, D84, D83, D38, D37, J538, J508, J272, D74, D73, D28, D27, J621, J618, D49, J408, J311, D64, D63, D17, D16, J523x, J523, J518x, J518, D54p, D53p, D53g, D52g, J72b, J72t, D22, D21, D20`.

- Vendor 2: `T1153, T742x, N218b, N218s, N210, N208b, N208s, N199, N198b, N198s, N149b, N149s, J182, N188b, N188s, J172a, J308, N158b, N158s, N142b, N142s, D79, J418, J421, J172, D431, D421, N104, J218, J211, N146b, N146s, N841, J321x, J321, J318x, J318, D331p, D331, D321, D221, D211, D201, N141s, N141b`.

- Vendor 3: `C743, T742, V159, J738, J708, J582, D23, J821, J818, V59`.

- Vendor 4: `T1232, N228b, N228s, N244b, N244s, N230`.

### 7.3 Hook choice and its limits

FACT: `capabilities::radio::vendor()` calls the C getter at `0x1cde34b70`. However, other capability functions call `TelephonyRadiosGetRadioVendor` directly and cache their own decisions. Examples include `ct::supports5G` at `0x1cde13d7c`, `radio::getPacketFilteringVersion` at `0x1cde14528`, `ipc::typeInternal` at `0x1cde339bc`, `ipc::supportsUnifiedQMuxV2` at `0x1cde33a54`, `pci::controlChannelCount` at `0x1cde342bc`, `radio::maverick` at `0x1cde34d4c`, `radio::supportsDataService` at `0x1cde3504c`, and `radio::ice` at `0x1cde35fd8`.

FACT: `libTelephonyBasebandDynamic` itself calls the C getter at `0x2a2b12f70` during legacy safe reset and at `0x2a2b13edc`. The Mav manager's `BasebandTransport::getSupportedProtocols`, `0x2a1949850`, branches on vendor at `0x2a1949878`. The AppleBasebandManager loader selector remains `0x246d9c248`, with library table `0x279708470` from section 1.

INFER: the smallest coherent experiment is an early process-local entry-point hook of exported `TelephonyRadiosGetRadioVendor` returning 1, installed before telephony static initialization in each participating process. This catches both the manager loader and capability helpers that bypass `capabilities::radio::vendor()`. A hook of only the latter can select the Mav manager while leaving transport, reset, packet filtering and data-service decisions on ICE.

INFER: use the existing injection mechanism, but do not assume symbol rebinding alone catches direct DSC calls. An entry-point trampoline must handle arm64e calling conventions and pointer authentication. I did not change or test `sweetsb.m`.

INFER: a narrowly scoped MobileGestalt hook returning `HasBaseband = true` and a decoded Qualcomm board such as `D27AP` for `HWModelStr`, before both once caches initialize, changes product/radio as well as vendor. That is a broader compatibility experiment. Global hardware-model spoofing also changes unrelated feature decisions. The direct C getter hook is narrower, while retaining N104 product identity may require additional targeted capability fixes.

UNKNOWN: neither hook has been tested in CommCenter. A successful `dlopen` does not prove startup, and changing vendor does not add missing kernel services, firmware assets, activation state or a data plane. Do not describe the hook alone as enough for cellular.

### 7.4 Qualcomm kernel services

FACT: this kernel contains `com.apple.driver.AppleBasebandM20`, `AppleBasebandPCI`, `AppleBasebandPCIMAVControl` and `AppleBasebandPCIMAVPDP`. The actual personalities establish:

| service | provider / match | user client |
|---|---|---|
| AppleBasebandM20 | IOPlatformDevice, `baseband,n41`, category AppleBaseband | AppleBasebandUserClient |
| AppleBasebandPCI | IOPCIDevice, `baseband-pcie` | interface services created below it |
| AppleBasebandPCIMAVControl | AppleBaseband | AppleBasebandPCIControlUserClient |
| AppleBasebandPCIMAVPDPManager | AppleBaseband, `valid_providers = AppleBasebandN71` | no AppleCellularDataPlane user client |

FACT: discovered transport classes include `AppleBasebandPCIInterface`, `AppleBasebandPCIUserClient`, `AppleBasebandPCITraceUserClient`, `AppleBasebandPCIMHIDevice`, `AppleBasebandPCIMHIInterface`, `AppleBasebandPCIBHIDevice` and `AppleBasebandPCIBHIInterface`.

FACT: key vtables are `AppleBasebandPCIUserClient = 0xfffffff007bdf578`, `AppleBasebandPCIControlUserClient = 0xfffffff007bddbf0`, `AppleBasebandPCIMAVControl = 0xfffffff007bef530`, and `AppleBasebandPCIMAVPDPManager = 0xfffffff007bf2c20`. These are unslid kernel addresses.

INFER: the upstream Mav kernel binary cannot simply attach to SM7635. Its platform, PCI, SPMI, IOMMU and interrupt dependencies target Apple hardware. Implement the public-facing service and user-client contract above the existing working modem driver instead of importing Apple's hardware driver unchanged.

### 7.5 External method tables

FACT: `AppleBasebandPCIUserClient::externalMethod` is at `0xfffffff008cc3860`. It selects 24-byte dispatch records from `0xfffffff007bdfc88` for selectors 0 through 12. The four uint32 fields after the function pointer are scalar-input count, structure-input size, scalar-output count and structure-output size. `V` below is `0xffffffff`, the variable-size marker. Structure sizes are bytes, scalar counts are uint64 elements. Async scalar calls can supply a user buffer address and length even when the dispatch record also permits variable structure data.

Names were cross-checked against the 23G90 `libIOABP` call wrappers, not guessed from selector order.

| selector | operation | scalar in | struct in | scalar out | struct out | kernel handler |
|---:|---|---:|---:|---:|---:|---|
| 0 | open | 1 | 0 | 0 | 0 | 0xfffffff008cc2a9c |
| 1 | close | 0 | 0 | 0 | 0 | 0xfffffff008cc2b04 |
| 2 | write / writeAsync | 2 | V | 0 | 0 | 0xfffffff008cc2b50 |
| 3 | read / readAsync | 2 | 0 | 0 | V | 0xfffffff008cc2d44 |
| 4 | sendImage / sendImageAsync | 2 | V | 2 | 0 | 0xfffffff008cc2f30 |
| 5 | readRegister | 1 | 0 | 0 | V | 0xfffffff008cc3100 |
| 6 | abortChannelAsync | 1 | 0 | 0 | 0 | 0xfffffff008cc3164 |
| 7 | startChannelAsync | 1 | 0 | 0 | 0 | 0xfffffff008cc32a4 |
| 8 | timeSyncAsync | 3 | 0 | 0 | 0 | 0xfffffff008cc33e4 |
| 9 | mapMemoryAsync | 3 | 0 | 0 | 0 | 0xfffffff008cc3504 |
| 10 | unmapMemoryAsync | 1 | 0 | 0 | 0 | 0xfffffff008cc3660 |
| 11 | registerTimeEvent | 1 | 0 | 0 | 0 | 0xfffffff008cc37b8 |
| 12 | deregisterTimeEvent | 1 | 0 | 0 | 0 | 0xfffffff008cc380c |

FACT: `IOABPClass::readAsync`, `0x2a2284728`, passes `[buffer address, uint32 capacity]` as two uint64 scalar inputs to selector 3 at `0x2a22847bc`. Its async reference has three entries, with callback and context at entries 1 and 2. `writeAsync` uses selector 2 at `0x2a22848f8` with the same pointer/length arrangement. `mapMemoryAsync` calls selector 9 at `0x2a22875d0`; its arguments are region id, address and length. `unmapMemoryAsync` calls selector 10 at `0x2a22876e8` with the region id.

FACT: both shared-memory handlers explicitly reject synchronous mapping. The debug strings at `0xfffffff008cc3604` and `0xfffffff008cc375c` name `_mapSharedMemory` and `_unmapSharedMemory`. An implementation that just returns synchronous success will leave its caller waiting for async completion.

FACT: the control client's `externalMethod` is `0xfffffff008cbd0dc`, with table `0xfffffff007bde278` and four entries:

| selector | operation | shape, scalar in / struct in / scalar out / struct out | handler |
|---:|---|---|---|
| 0 | open | 1 / 0 / 0 / 0 | 0xfffffff008cbd008 |
| 1 | close | 0 / 0 / 0 / 0 | 0xfffffff008cbd070 |
| 2 | readLogsAsync | 3 / 0 / 0 / V | 0xfffffff008cbd0bc |
| 3 | loggerTune | 4 / 0 / 0 / 0 | 0xfffffff008cbd0cc |

FACT: the platform `AppleBasebandUserClient` has a separate 52-entry table at `0xfffffff007bd0598`, selected by `externalMethod` at `0xfffffff008c7c578`. Selectors are valid through 0x33. This contract is separate from the 13-entry transport table. Its raw shape map is recorded below so a stand-in does not confuse equal selector numbers across clients.

```text
selector  handler             scalarIn structIn scalarOut structOut
0x00      0xfffffff008c7c6ec  1, 0, 0, 0
0x01      0xfffffff008c7c72c  1, 0, 0, 0
0x02      0xfffffff008c7c768  1, 0, 0, 0
0x03      0xfffffff008c7c7a4  0, 0, 1, 0
0x04      0xfffffff008c7c7dc  0, 0, 0, 0
0x05      0xfffffff008c7c834  0, 0, 0, 0
0x06      0xfffffff008c7c8d0  0, 0, 0, 0
0x07      0xfffffff008c7c908  0, 0, 0, 0
0x08      0xfffffff008c7c93c  1, 0, 0, 0
0x09      0xfffffff008c7c978  0, 0, 0, 0
0x0a      0xfffffff008c7c988  1, 0, 0, 0
0x0b      0xfffffff008c7c9ec  0, 0, 1, 0
0x0c      0xfffffff008c7ca24  0, 0, 0, 0
0x0d      0xfffffff008c7ca58  1, V, 0, 0
0x0e      0xfffffff008c7caa0  1, V, 0, 0
0x0f      0xfffffff008c7cae8  0, 64, 0, 0
0x10      0xfffffff008c7cc04  0, 0, 1, 0
0x11      0xfffffff008c7cc3c  0, 0, 1, 0
0x12      0xfffffff008c7cc74  0, 0, 1, 0
0x13      0xfffffff008c7ccac  1, 0, 0, 0
0x14      0xfffffff008c7ccf0  0, 0, 2, 0
0x15      0xfffffff008c7cda4  1, 0, 0, 0
0x16      0xfffffff008c7cde0  1, 0, 0, 0
0x17      0xfffffff008c7ce1c  0, 0, 1, 0
0x18      0xfffffff007004000  0, 0, 0, 0
0x19      0xfffffff007004000  0, 0, 0, 0
0x1a      0xfffffff008c7ce54  0, 0, 1, 0
0x1b      0xfffffff008c7ce8c  1, 0, 0, 0
0x1c      0xfffffff008c7cec8  1, 0, 1, 0
0x1d      0xfffffff008c7cf08  2, 0, 0, 0
0x1e      0xfffffff008c7cfc4  0, 120, 1, 0
0x1f      0xfffffff008c7d008  0, 48, 0, 0
0x20      0xfffffff008c7d058  1, 0, 0, 0
0x21      0xfffffff008c7d094  2, V, 0, 0
0x22      0xfffffff008c7d15c  2, V, 0, 0
0x23      0xfffffff008c7d224  1, 0, 0, 0
0x24      0xfffffff008c7d260  3, 64, 0, 0
0x25      0xfffffff008c7d2a4  3, 0, 0, 64
0x26      0xfffffff008c7d2e8  1, 32, 1, 0
0x27      0xfffffff008c7d334  0, 0, 1, 32
0x28      0xfffffff008c7d378  1, 32, 1, 0
0x29      0xfffffff008c7d3c4  0, 0, 0, 160
0x2a      0xfffffff008c7d400  0, 0, 1, 0
0x2b      0xfffffff008c7d438  0, 0, 0, 0
0x2c      0xfffffff008c7d470  0, 64, 0, 0
0x2d      0xfffffff008c7d4b4  2, 0, 0, 0
0x2e      0xfffffff008c7d570  0, 0, 0, 0
0x2f      0xfffffff008c7d5a4  0, V, 0, 0
0x30      0xfffffff008c7d5e0  1, 0, 0, 15
0x31      0xfffffff008c7d624  1, 0, 0, 0
0x32      0xfffffff008c7d660  0, 0, 1, 0
0x33      0xfffffff008c7d698  0, 0, 0, 30
```

UNKNOWN: most platform selector names still need their individual provider-call targets named. The shapes and handler addresses above are decoded facts. This is not permission to return success indiscriminately for every selector.

### 7.6 Message route, shared memory and rings

FACT: the actual userland route has an additional library below the Mav manager:

```text
libBasebandManager / QMI command driver
  -> RouterIPCController / mav_router
  -> libmav_ipc_router_dynamic pci_shim
  -> libPCITransport TelephonyBasebandPCITransport*
  -> libIOABP IOABPClass
  -> AppleBasebandPCIUserClient
  -> AppleBasebandPCIMHIInterface / MHI channels
```

Evidence includes `mav_router::device::pci_shim::handle_transmit` at the beginning of `libmav_ipc_router_dynamic`, `handleRead` in the same library, `TelephonyBasebandPCITransportCreate = 0x2a2857a8c`, `TelephonyBasebandPCITransportMapRegion = 0x2a2858d78` and the `IOABPClass::readAsync/writeAsync` call sites above. The PCI transport dispatches to `IOABPClass::writeAsync` at `0x2a28540f0` and `readAsync` at `0x2a2855098`.

FACT: this is a buffer-and-completion user-client API. The decoded read/write interface does not require userland to manipulate the modem's MHI producer and consumer rings directly. Explicit mapped regions exist as an additional async API. The hardware DMA ring contract is below the IOUserClient boundary.

INFER: a stand-in can preserve the read/write buffer API while implementing queues above QRTR, avoiding emulation of Apple PCIe hardware rings. It still needs correct async completion counts, cancellation, buffer lifetime, event notifications and reconnect behavior. A hook at the transport factory could make the first control-plane experiment smaller than a complete kernel replica, but that alternative was not built or tested.

FACT: the `AppleBasebandPCIMAVControl` personality has `chipsetDeviceMap` entries `mav13/mav16 -> MHIDeviceLegacy`, `mav17 -> MHIDeviceLegacyADPL`, `mav20 -> MHIDeviceQDSS`, `mav21/mav22/mav30 -> MHIDeviceLowLatency`, and `default -> MHIDeviceRSC`. The chosen descriptor therefore depends on chipset as well as vendor.

FACT: all descriptor families include these MHI channels. Values in the size column are literal `transfer_ring.size` plist values. I have not established whether that key is consumed as bytes or element count, so do not multiply it by an assumed descriptor size.

| protocol | outgoing index, direction 1 | incoming index, direction 2 | size values |
|---|---:|---:|---|
| sahara | 2 | 3 | 2048 / 2048 |
| qmux1 | 14 | 15 | 1024 / 512 |
| qmux2 | 16 | 17 | 1024 / 512 |
| qmap | 100 | 101 | 8192 / 16384 |
| diag | 4 | 5 | 2048 / 2048 |
| refs | 10 | 11 | 256 / 256 |
| at | 32 | 33 | 2048 / 2048 |

FACT: modern descriptor families additionally contain `qmap_control` 109/110, `mediactl` 80/81, `qdss` 8/9, and boot logger channel 25. LowLatency and RSC contain low-latency 105/106. RSC adds channel 104 with direction 3 and event-ring sharing involving channels 101/104.

FACT: the Legacy descriptor has 128 channel contexts, 128 event contexts, five event rings, two hardware channels, two hardware events and four MSI vectors. Its command-ring size is 1024. Event-ring sizes are `[1024, 4096, 8192, 16384, 32768]`. QMAP transfer-ring alignments are 8192/16384, and its two event-ring alignments are 16384/32768. Modern descriptors expand this configuration. They must not be treated as one universal ring layout.

UNKNOWN: I have not recovered the byte offsets of every MHI channel/event context, transfer descriptor flag, mapped-region header or completion payload. The plist describes channel allocation and ring configuration, not the complete packed ABI. A driver written only from this report would still need those details if it emulates below the user-client boundary. A QRTR bridge above that boundary can avoid the hardware ring ABI, but cannot avoid the completion ABI.

INFER: QMux service/client allocation must be terminated locally and mapped to QRTR endpoints. Preserve transaction IDs, indications and disconnect semantics. Do not send QMux headers or MHI descriptors to QRTR as QMI payloads. The four announced services do not prove that all Apple startup services or private messages are supported.

### 7.7 Boot, health and firmware requirements

FACT: `libTelephonyBasebandDynamic` supplies AppleBaseband discovery and control, not the high-level QMI firmware-version transaction. Its `AppleBaseband` matching uses `IOServiceMatching`, `IOServiceOpen`, registry properties and notifications. Its vendor-dependent reset paths are identified in 7.3. The kernel platform and transport clients are separate opens.

FACT: the Mav manager contains `BootModule::bootstrap`, `BootModule::boot_sync`, `BootModule::hardReset_sync`, `BootModule::softReset_sync` and `HealthModule::bootstrap`. The boot request at `0x2a198332c` calls `capabilities::abs::shouldBoot()` before dispatching boot work. The nearby log `Bootup request rejected as we are already in %s state`, `0x2a1983264`, is a manager-state guard. It is not proof that an already-running foreign modem skips firmware download.

FACT: image push is real. `Baseband Firmware Path Not Found` is referenced at `0x2a195f650`, and `Baseband image push time = %u millisec` at `0x2a1980538`. The PCI transport exposes a separate image-send method, selector 4. The kernel includes BHI device/interface classes and Sahara channels 2/3.

FACT: the MHI state string converter around `0xfffffff008cfbf04..0xfffffff008cfbf7c` names `M0`, `M1`, `M2`, `M3`, `reset`, `ready`, `error` and `unknown`. These hardware transport states are separate from the ABM Booted/Ready states CommCenter sees. Merely publishing Booted does not implement MHI startup.

FACT: the health timer's ping callback at `0x2a1a2f250` calls `HealthModule::getBasebandFWVersion_sync`. That method obtains a command driver from `CommandDriverFactory` and dispatches through its virtual interface. The QMI implementation is `radio::QMICommandDriverBase::getBasebandFWVersion`, `0x2a182ecf4`.

FACT: this implementation constructs QMI message `0x23` at `0x2a182eeac..0x2a182eeb0`, sets a send timeout field to `0x61a8` at `0x2a182eec8`, and calls `qmi::Client::send` at `0x2a182efb8`. Its reply parser calls `findTlvValue(1)` at `0x2a182f15c..0x2a182f160`. The library exports `dms::tlv::abm::BasebandFWVersion` parser helpers and stores the resulting dictionary under `radio::kKeyBasebandFWVersion` at `0x2a182f5a0`.

INFER: the recurring health ping can likely be satisfied by the real DMS Get Revision response, including a successful QMI result and TLV 1 firmware string. It is not a special kernel ping selector. The kernel bridge must keep QMI alive and deliver responses, while the real DMS service supplies version data.

UNKNOWN: I did not find an accepted-firmware-version whitelist or prove there is none. The observed parser accepts a firmware string. Firmware image selection, chip identity, command-driver selection and personalization are separate gates. A valid version response alone does not establish readiness.

FACT: the Qualcomm command-driver library also constructs these private requests:

| operation | message id at construction | evidence |
|---|---|---|
| getBasebandStatus | 0xe014 | 0x2a182e608..0x2a182e60c |
| getFactoryCalibrationStatus | 0xe018 | 0x2a182f870..0x2a182f874 |
| getRFCalibrationStatus | 0xe009 | 0x2a182ff7c..0x2a182ff80 |

FACT: the same library implements `getFirmwareNonce`, `getPkHash`, `getPersParams`, `getFirmwareInfo`, `getManifestStatus`, `getManifest`, `getSIMSummary` and `getSIMPresence`. Their existence demonstrates more required surface area than the four advertised standard QMI services. It does not prove every operation is mandatory on every boot. I have not established the service ids and full TLV schemas of all these private transactions.

FACT: `DataModule::configureQMAPSettings_sync` has success/failure callback logs at `0x2a19ff068` and `0x2a19ff0a0`. Its command dispatch is a virtual call at `0x2a19fee7c..0x2a19fee90`. A successful callback here must correspond to a data format the bridge actually implements.

INFER: a stand-in must provide three different sets of answers, rather than one fake firmware string:

1. Kernel-facing service opens, client check-in, asynchronous buffer completions, power/reset notifications and coherent transport state.
2. Real standard QMI responses for service discovery/client allocation, DMS revision and operating mode, UIM card/application/file state, NAS registration/signal indications and WDS sessions.
3. A deliberate policy for Apple-only status, calibration, firmware identity, personalization and data-format requests, implemented by translating supported operations or bypassing their callers at a higher layer.

INFER: prefer pass-through for supported standard QMI messages. Treat unsupported private requests as explicit unsupported operations until their callers are understood. Returning success with an empty payload can fail later TLV parsing or start an unintended recovery loop. A reset acknowledgement must not silently claim that calls and sessions survived if the real modem restarted.

UNKNOWN: the complete ordered cold-boot QMI transcript, mandatory-versus-optional capability replies, accepted chip-id mapping and an already-booted shortcut remain unproved. Therefore the earlier statement that firmware/power/health calls can all be no-op successes is withdrawn. This pass narrows the work but does not deliver a complete boot emulator specification.

### 7.8 `pdp_ip0` on the Qualcomm kernel

FACT: neither `ipsw kernel cpp` nor the kernel's baseband kext list contains `AppleCellularDataPlane` or `AppleCellularDataPlaneUserClient`. Those belong to the Intel kernel investigated previously. There is no selector table for that class to map on this Qualcomm kernel.

FACT: Qualcomm data uses `AppleBasebandPCIPDPSkywalkInterface`, `AppleBasebandPCISkywalkInterfaceBase`, `AppleBasebandPCISkywalkProvider`, `AppleBasebandPCISkywalkLogicalLink`, `AppleBasebandPCIQMAPProvider`, `AppleBasebandPCIQMAPControl`, `AppleBasebandPCIPDPQMAPSkywalk`, `AppleBasebandPCIPDPQMAPLowLatency` and `AppleBasebandPCIPDPQMAPRSC`.

FACT: `libPCITransport` matches `AppleBasebandPCIPDPManagerBase` at `0x2a2864dcc..0x2a2864dd0`. The kernel contains `pdp_ip%1u` references at `0xfffffff008ccb120`, `0xfffffff008cd0e50` and `0xfffffff008cd9d48`. The Mav PDP personalities attach QMAP, low_latency, rsc and qmap_control providers to `AppleBasebandPCIMHIInterface` using `ABPInterfaceProtocol`.

FACT: the QMAPSkywalk personality's fallback is `mav17`. Its `mav20` and newer configurations enable `qmap_control_service`, set `request_checksum_offload = false` and `send_checksum_offload_header = true`. Mav21/22 add low latency, and Mav23/24/25 add RSC. For `pdp_ip0`, Mav20 and newer use soft-flow-control thresholds of 1,610,612 and 2,415,920 write bytes.

INFER: the minimum data milestone needs an Apple-facing PDP manager/network interface control contract plus a working native packet path. Creating an interface with the correct name is insufficient. Session/mux association, IPv4/IPv6 configuration, MTU, DNS, routing, flow control and reset teardown must agree between CommCenter, SystemConfiguration and the real modem.

UNKNOWN: I have not completely decoded the dynamic PDP interface creation/control messages or the private Skywalk attachment ABI. The concrete follow-up targets are `AppleBasebandPCIPDPManagerBase`, its Mav subclass, the `libPCITransport` PDP listener and the three `pdp_ip%1u` call sites above. Do not implement the Intel AppleCellularDataPlaneUserClient selectors for the Mav path based on section 4.

### 7.9 Updated effort and highest risks

These are engineering estimates for one developer familiar with this kernel port. They assume stable QRTR transport, working injection and a way to build/load the stand-in. They include reverse engineering still missing here. They are not dates promised by the binary evidence.

| stage | additional effort | highest risks and measurable exit |
|---|---|---|
| (i) SIM in Settings | 3 to 6 weeks | Early coherent vendor selection, service opens, async transport and Apple boot/private-message gates are the main risks. Exit when real ICCID and SIM readiness reach CoreTelephony and Settings across a CommCenter restart. |
| (ii) Bars and carrier name | 1 to 3 more weeks after (i) | NAS indication order, subscription/slot identity and carrier selection remain untested. Exit when live registration and signal changes update carrier name and bars, including loss of service and recovery. |
| (iii) Mobile Data through pdp_ip0 | 6 to 12 more weeks after (ii), possibly longer | Qualcomm PDP/Skywalk control, QMAP mux/format negotiation, the real packet endpoint and reset/flow-control behavior are the largest unknowns. Exit when IPv4/IPv6 traffic uses pdp_ip0 with correct routing and survives reconnects. |

The largest shared risk is the unmeasured difference between Apple's private QMI extensions and SM7635 firmware. The largest data-specific risk is that QRTR control services are working while the modem packet endpoint and iOS PDP control contract are still unavailable. The vendor hook is now a small, concrete experiment. It is not the dominant uncertainty in the whole port.
