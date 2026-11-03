# iOS 26 cellular on a Qualcomm modem: research

Read-only research. Nothing was built, run on the phone, or committed. Evidence is `strings`, `otool -L` and `ipsw dt` output on the mounted root and the DSC. I did no disassembly, so anything about control flow is inference.

## What this IPSW and root serve

Facts:
- `ipsw/BuildManifest.plist` lists only `iPhone12,1` and board `n104ap`. This is a single-device IPSW. The root is not shared with Qualcomm iPhones.
- The manifest has `BbChipID 0x00000068` and one baseband firmware: `Firmware/ICE19-7.02.01.Release.bbfw`. No Mav firmware is shipped.
- `/usr/lib/bbmasks` holds only `N104_*.kdm` files.
- `ios26-root-full.img` is not mountable (`no mountable file systems`, it starts with zeros). The three DMGs in `ipsw/23G90__iPhone12,1/` mounted read-only without a password: the 8.5 GB system volume `094-95954-090.dmg` and two cryptex volumes. All results below come from the system volume plus the DSC.
- `ipsw dyld` could not open this DSC because the `.symbols` subcache is missing. I ran `strings` over a clonefile copy of the split cache instead. I can name dylibs by path, but I cannot say which dylib holds a given string.

## 1. Binaries and plugins on this root

Facts, on disk:
- `/System/Library/Frameworks/CoreTelephony.framework/Support/CommCenter`, arm64e, 35 MB. Also `CommCenterMobileHelper` and `CommCenterRootHelper`.
- Launch plists `com.apple.CommCenter.plist` and `com.apple.CommCenter-noncellular.plist`. Both run the same binary as user `_wireless`. The cellular one has `_LimitLoadToDeviceTree = baseband`. The noncellular one has `_LimitLoadFromDeviceTree = baseband`. The noncellular one drops the `com.apple.CellularPlanDaemon.xpc` and `com.apple.commcenter.carrierspace.xpc` Mach services.
- Stub frameworks (Info.plist only, code is in the DSC): `AppleBasebandManager`, `AppleBasebandServices`, `AppleBasebandLink`, `BasebandTraceHelper`.
- `/System/Library/LaunchDaemons/com.apple.DumpBasebandCrash.plist`, `/usr/bin/DumpBasebandCrash`, `/private/var/wireless/baseband_data`.

Facts, `otool -L CommCenter`:
- Links `libARI.dylib`, `libQMIParserDynamic.dylib`, `libBBUpdaterDynamic.dylib`, `libETLDynamic.dylib`, `libATCommandStudioDynamic.dylib`, `libTelephonyBasebandDynamic.dylib`, `libTelephonyIOKitDynamic.dylib`.
- Also links `libCommCenterCommandDrivers.dylib` and `libCommCenterMCommandDrivers.dylib`, and the `AppleBasebandManager` and `AppleBasebandServices` frameworks.

Facts, DSC paths (all in this one DSC):
- `/usr/lib/libBasebandCommandDriversARI.dylib`, `...QMI.dylib`, `...MIPC.dylib`, plus `libBasebandCommandDrivers.dylib`.
- `/usr/lib/libBasebandManager.dylib`, `libBasebandManagerICE.dylib`, `libBasebandManagerDAL.dylib`.
- `libARIServer.dylib`, `libMIPCSdk.dylib`, `libBasebandDiagnostics.dylib`, `libBasebandSharedServices.dylib`, `libCommCenterKCommandDrivers.dylib`.
- Path strings `/usr/local/standalone/firmware/Baseband/Mav20` through `Mav25` and `Mav30`.
- Strings `Initializing Mav13 command driver`, `Initializing legacy command driver`, `Mav20QMIAttachApnCommandDriver created for %s`, `DATA:: ... DATA.QMIDriver`.
- Strings `Initializing MIPC coredump command driver` and `[MIPC] Msg ...`, a third driver family.
- `PLBBMav10/13/16/16BHwMsgParser` classes in PerfPowerServices, and `mav1`..`mav17` strings.
- Source paths `AppleBasebandServices/ATCommandStudio/QMI/QMux.cpp`, `QMIControl.cpp`, `TLV.cpp`, `QMIServiceMsg.cpp`.
- Strings `Creating new QMux for service %s, channel %s` and `[%s] QMux in sync with baseband, starting clients`.
- CommCenter itself has QMI TLV types for `dms`, such as `qmi::dms::tlv::MDNData` and `IMDIData`, and a `QMICDMASettingsHandler`.

Answer: both paths ship in the same CommCenter and DSC. The ARI path serves ICE (Intel). A QMI path with QMux exists. A Mav (Qualcomm) command driver path exists alongside it. MIPC also exists.

Inference:
- `Mav` is Apple's Qualcomm family. Evidence: the PowerLog parsers log `MPSS`, `LPASS` and `RPM`, which are Qualcomm subsystem names, and `Mav20`/`Mav25` firmware directories next to `AMAuthInstall...Mav20ElfMBN`.
- `Ice` is the Intel family.
- Apple's Qualcomm path is therefore real and present, but only the Mav-side code, not Mav firmware.

## 2. Kernel transport and data interface

Facts, DT (`ipsw dt n104ap.adt`):
- `baseband` node: `compatible "baseband,i19"`, `device_type "baseband"`, with `function-reset_det`, `function-coredump`, `function-bb_on`, `function-pcie_port_control`, `spmi-device`.
- A `baseband-pcie` child under PCIe `apcie-port: 3`, with `function-perst` and `function-clkreq`.
- Root `defaults`: `baseband-chipset: "ice19"`.
- `iop-aop-nub` has `has-baseband`. A `baseband-vol` entry mounts `Baseband-Data` at `/private/var/wireless/baseband_data`.

Facts, strings (DSC and CommCenter):
- IOKit classes `AppleBasebandPCIControl`, `AppleBasebandPCIControlUserClient`, `AppleBasebandPCIUserClient`, `AppleBasebandPCIInterface`, `AppleBasebandPCIPDPManager`, `AppleBasebandPCIICEPDPManager`, `AppleBasebandPCIMAVPDPManager`, `AppleBasebandPCITraceBackendRaw`, `AppleBasebandUserClient`.
- Entitlements `com.apple.driver.AppleBasebandPCI.user-access` and `com.apple.driver.AppleBasebandPCIControl.user-access`.
- `AppleBaseband IOService is not found. Finding it via matching service`, `Failed to send reset properties to AppleBaseband driver`, `Failed to self-identify with AppleBaseband`.
- `kAppleBasebandConnectMethodRadioOn`, `...NotifyBasebandPoweringDown`, `...QuiesceBackpoweringPins`.
- `TelephonyBasebandPCITransportCreate`, `TelephonyBasebandPCITransportMapRegion`, `...UnmapRegion`.
- Device nodes `/dev/tty.baseband-boot-logger`, `/dev/tty.p2m`, `/dev/tty.stockholm`. The log scratch dir is `/var/wireless/Library/Logs/AppleBasebandManager/pci.scratch`, and the power assertion is `com.apple.AppleBasebandManager.RadioNotReady.Bootup`.
- Data interface: `pdp_ip0` to `pdp_ip15`. CommCenter logs `DATA:: ========== start pdp_ip%d`, names SystemConfiguration services `Cellular WAN: pdp_ip%d`, and writes `Caches/com.apple.aps.tcpdump.pdp_ip0.plist`.

Inference:
- The iPhone 11 already uses PCIe, not USB or UART. Intel and Qualcomm share the `AppleBasebandPCI*` kext family and differ in the PDP manager subclass (`ICE` vs `MAV`).
- `pdp_ipN` is created by the kernel PDP manager, not by CommCenter. CommCenter only starts and stops it and tags the SC service. I have not confirmed this in code.
- QMux frames are carried through a PCI user-client channel, not a `/dev/` node. Evidence is the `Creating new QMux for service, channel` string and the absence of any QMI device-node string.
- The kernelcache is not in the IPSW directory. The kext side is not verified here. Use the kernel tree you already have for `AppleBasebandPCI`.

## 3. What CommCenter checks before cellular works

Facts:
- Launchd gate: the cellular CommCenter loads only if the DT has a `baseband` node (`_LimitLoadToDeviceTree`). Without one, the noncellular plist loads instead.
- Boot state: CommCenter takes `/cc/assertions/baseband_booted`. Strings: `Acquiring baseband booted assertion for auth`, `Fetched baseband boot state from BasebandManager`, `baseband boot-state matches; committing new mode`.
- BasebandManager and BBUpdater do firmware and identity work: `unable to retrieve baseband chip revision ..., so skipping chip-revision/firmware-recipe check`, `Failed SysGetInfoV2 ARI command`, `GetCurrentBootState`, `GetPersonalizationParams`, `CsiSecReadSikPKey`, `Already waiting for OTA connection to update baseband ticket`. Signed firmware and TSS tickets are involved.
- PRI and carrier data: `Baseband PRI (%d.%d.%d) matches our cached version`, `Detected major/minor/release version mismatch: cached ... baseband ...`, `Carrier bundle has changed, Hence send PRL, PRI and EFS files`. Carrier bundles are at `/System/Library/Carrier Bundles/{iPhone,System}`. `Failed to get matching carrier bundle by keys` is logged.
- SIM and activation: `ICCID mismatch, received (%s), expected (%s) - ignoring`, `No activation information available`, `fFatalActivationBlockers`, `Carrier Bundle is disabling OTA activation`. Other strings show `liblockdown`, `MobileActivation` and `ABMHelper` links.
- Radio mode: `kCTRadioBasebandOperatingModeOnline/Off/LowPower/CampOnly`, plus the `wrm-enable-baseband-state` pref.

Inference:
- CommCenter does not ask for an Intel part by name. It asks the baseband manager for boot state, chip revision, PRI version and SIM status, then drives whatever command driver is active.
- Real "Mobile Data" needs, in order: a `baseband` DT node, a registered `AppleBaseband` IOService with a user client, a booted state, a SIM reporting ready, a matching carrier bundle, then a PDP context that comes up on `pdp_ipN`.
- Firmware signing probably matters only to the real BBUpdater and TSS path. A stand-in transport may never reach it. This is unverified.
- I did not find where `baseband-chipset` (`ice19`, `mav17`) is read or how it picks `ICE`/`QMI`/`ARI` drivers. This needs disassembly.

## 4. Public prior art

Facts from search, found:
- ARIstoteles (arXiv 2604.23457, Ulm University) reverse-engineers ARI, the Intel interface, built a Wireshark dissector, and fuzzed CommCenter with 42 crashes. It says the interface is undocumented. It covers iOS 12 to 14.
- 37C3 talk "What your phone won't tell you" and the `seemoo-lab/BaseTrace` project. BaseTrace is a Wireshark dissector and QMI injector for Qualcomm iPhones, jailbroken device only.
- Stefan Sterz's TU Wien thesis on Intel baseband fuzzing on iOS. Troopers 2025 covers Hexagon baseband firmware emulation in QEMU. That emulates the modem firmware, not CommCenter's side.
- Reported iOS-on-Android ports: UniXNU plus a UEFI loader, iOS 26 booted on a Surface Duo 2 and a Pixel 10 Pro Fold headless (windowslatest, 2026-10-27). Coverage mentions display and touch only.

Facts from search, not found:
- No public work faking `AppleBaseband` or `AppleBasebandPCI` for CommCenter on a non-Apple host.
- Nothing on cellular for UniXNU, HakiOS or rootstock. I found no technical writeup, and for Corellium only a secondary page saying baseband is not fully replicated.

Inference: this has probably not been done in public. The ARIstoteles and BaseTrace work gives message formats to start from, not a working transport.

## 5. Rough estimate of the two paths

(a) Emulate Apple's transport and keep QMI. Estimate: weeks of kernel-side work, more risk than (b) on the userland side, a smaller protocol translation.
- Needed pieces: an `AppleBaseband` IOService with a user client that CommCenter will open, plus the `TelephonyBasebandPCITransport` channel semantics. Also a `pdp_ip` PDP manager that moves packets to the Qualcomm data path, and the boot-state handshake that BasebandManager does.
- CommCenter and the DT currently say `ice19`, not `mav*`. A device tree edit (`baseband-chipset`, `compatible`) would be needed to take the Mav/QMI code path. The IPSW has no Mav firmware or Mav-specific config, so those files would be missing.
- QMux framing from CommCenter would have to be forwarded to the real QRTR services. Real QMI services on the 3a Pro (UIM, NAS, WDS, DMS) already speak QMI, but QRTR vs QMux wire framing differs and client IDs must be remapped.
- Hard part: the exact PCI user-client method table and message layout, which is not public. It needs disassembly of `AppleBasebandPCI` in the kernelcache and of `libTelephonyBasebandDynamic`.

(b) Translate Intel ARI to QMI. Estimate: larger and open ended.
- The device is already configured for Intel, so DT, drivers and carrier handling fit without edits. ARI would still need an `AppleBaseband` stand-in below it.
- ARI is undocumented. ARIstoteles gives a dissector, not a spec, and the message set is large (CSI, SysGetInfoV2, personalization, PRI push, IMS).
- Every ARI call CommCenter makes would need a QMI answer, and many ARI calls have no QMI equivalent (`CsiSec*`, chip revision, firmware recipe). These would have to be faked.
- Hard part: breadth, and bugs surface as hangs in a stack that expects Intel timing.

Rough ranking: (a) is cheaper if the Mav path can be forced from the DT. (b) only makes sense if (a) is blocked. Both need the same `AppleBaseband` stand-in first, so that is the shared first step.

## Open items

1. Where `baseband-chipset` is read and what it selects. Needs disassembly of `libBasebandManager.dylib` or CommCenter.
2. The PCI user-client method table and the channel IDs. Needs the kernelcache.
3. Whether a CommCenter-only stand-in could skip the kernel work. Not checked.

## Answer

Probably yes, in principle. This root already contains Apple's Qualcomm path: a QMI/QMux stack, Mav command drivers, a `MAV` PDP manager, and a `baseband` DT node that makes CommCenter load. CommCenter would show "Mobile Data", the carrier name and bars if it sees an `AppleBaseband` IOService, a booted baseband, a ready SIM, a carrier bundle and a working `pdp_ipN`. It does not require an Intel part by name. But this IPSW is iPhone 11 only, so the DT says `ice19`, no Mav firmware or config is present, and the AppleBasebandPCI user-client interface is private and undocumented. I found no public project that has done it. Emulating Apple's transport and keeping QMI looks like the more tractable route. Translating ARI to QMI has far more messages to fake.
