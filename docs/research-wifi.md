# Wi-Fi on the Nothing Phone (3a) Pro under iOS 26.6.2: research

Research only, 2026-11-02. I ran nothing on the phone, edited no code and pushed nothing. Scratch clones live in `/tmp/wifi-research`.

Tags. "Stated" means a file, log or commit says it, with a path or URL. "Inferred" means my reasoning with no run behind it. "Not reached" means I could not read the source.

## Short answer

The chip is a WCN6750-class part, marketed on Milos as WCN6755, and it is not on PCIe. It is an integrated radio whose firmware runs on the WPSS remote processor. The host reaches it through shared memory, GLINK/QRTR, QMI and copy-engine rings, with MSIs delivered as GIC SPIs 768 to 799. The same firmware is already on the `modem_a` backup in `backup-asteroids`.

HakiOS did this exact chip on a POCO X5 Pro (SC7280) with unmodified Linux ath11k on a LinuxKPI layer under XNU. It went from "start the Wi-Fi DSP" to "WPA2 joined, HTTPS page fetched" in about six hours of wall clock. We have most of the iOS side already, validated on iOS 26.6.2 with a different chip. The unknowns that are ours alone are the apps SMMU under Gunyah and the PMIC/SPMI writes, and both can fault the kernel.

## 1. Chip and attach path

### Chip identity

Stated. The live device tree dump `ios-asteroids/research/asteroids-hw/asteroids.dts` has a node `qcom,wcn6750` at lines 26940 to 27000 with `compatible = "qcom,wcn6750"`, and a sibling `bt_wcn6750` with `compatible = "qcom,wcn6750-bt"` at line 27104. The Android board config builds `qcom/opensource/wlan/qcacld-3.0/.qca6750` (`android_device_nothing_asteroids/BoardConfig.mk:172`). The WPSS firmware name is `qca6750/wpss.mdt` (`asteroids.dts:5996`).

Stated. Mainline Linux has `milos.dtsi` with a node `wifi@17110040`, `compatible = "qcom,wcn6755-wifi", "qcom,wcn6750-wifi"` (torvalds/linux `6bf981752daa`, 2026-09-16, Luca Weiss). The binding commit `e506d34e5723` says the WCN6755 is "completely pin and software compatible" with the WCN6750 and that "the original downstream kernel just pretends the WCN6755 is a WCN6750". Mainline `milos-nothing-asteroids.dts` (added in `edbf03c9fb5f`, 2026-04-27, "Add the Nothing Phone (3a)"; latest change `7d3670ccf61b`) enables `remoteproc_wpss` with `firmware-name = "qcom/milos/nothing/asteroids/wpss.mbn"` and reserves GPIO 63 and 64 as "WLAN UART". It does not enable the `wifi` node.

Stated. The firmware version string in `backup-asteroids/modem_a.img`, file `verinfo/ver_info.txt`, reads `wlan_msl: WLAN.MSL.3.0.1-00156.1-QCAMSLSWPLZ-1`. The Bluetooth partition holds `WCN6750ROM ... BTFW.MOSELLE.1.2.0-00350`. Moselle is the WCN6750 family name.

Inferred. The part is a WCN6755 that behaves as a WCN6750. Treat it as WCN6750 in every driver table.

### Bus

Stated. The `wifi` node has no PCIe link. Its resources are `msi_addr` 0x17110040 and `smmu_iova_ipa` 0xc0000000, 32 edge interrupts on SPI 768 to 799 (`0x300` to `0x31f`), `iommus = <0x49 0x1400 0x01>`, `qcom,rproc-handle` pointing at the WPSS node and `dma-coherent`. The vendor driver is `icnss2`, not `cnss2`. The ath11k hw entry for this chip sets `hybrid_bus_type = true`, `static_window_map = true`, `fixed_fw_mem = true` (`drivers/net/wireless/ath/ath11k/core.c:578-660` in Linux). ath11k drives it through `ahb.c` with `pcic.c` ops, which is the "hybrid" path.

Stated. The DT also has `pcie0_msi@17110040` at `asteroids.dts:12680`, the same address as the Wi-Fi MSI register. HakiOS's source describes this register as a GIC register that raises the SPI whose number is written (`drivers/wifi/hakios.wifi.c`, `RADIO_MSI_REGISTER`, SC7280 value 0x17a10040). Inferred: on Milos it is the same kind of SPI doorbell at a different address.

Stated. The two PCIe controllers at 0x1c00000 and 0x1c08000 exist in the tree. Mainline marks PCIe1 clocks as protected on this phone. Inferred: ignore them for Wi-Fi.

### WPSS remote processor

Stated, `asteroids.dts:5991-6025`:

- Node `remoteproc-wpss@9bb00000`, compatible `qcom,volcano-wpss-pas`, `reg = <0x9bb00000 0x10000>`. Mainline uses `remoteproc@8a00000` for the same block. I did not resolve which is the real register block. The ADSP start in rootstock does not need it.
- Image memory `wpss_mem` at 0x9bb00000, size 0x1900000, no-map (`asteroids.dts:276`).
- Wi-Fi MSA memory `wlan_msa_mem` at 0xa6400000, size 0xc00000, no-map (`asteroids.dts:324`). ath11k's WCN6750 path uses a fixed 12 MiB region and does not request DDR over QMI (`qmi.c:1823`).
- Supplies `cx` and `mx` through power domains, clock `xo`, `qcom,qmp` to the AOP mailbox.
- SMP2P edge to remote pid 13 with entries `master-kernel`, `slave-kernel`, `wlan`, `wlan_soc_wake`, `wlan_ep_power_save`.
- GLINK edge `wpss`, transport smem, IPCC client 0x18, one channel `IPCRTR`.
- PAS id 6, no DTB image, no `qcom,vmid` region assignment (Linux `qcom_q6v5_pas.c`, `sc7280_wpss_resource`, reused for `qcom,milos-wpss-pas` at line 2002).

### Regulators, clocks, GPIOs

Stated, `asteroids.dts:26940-27000` and `27104-27140`. Phandles resolved by me from the same file.

| Rail in `qcom,wcn6750` | Regulator | Vote range, uV |
|---|---|---|
| vdd-ipa-2p2 | `pm3001a_s1j` | 2200000 |
| vdd-1.3-rfa | `pm7550_s2` | 1256000 to 1408000 |
| vdd-1.8-xo | `pm7550_s1` | 1856000 to 2040000 |
| vdd-cx-mx | `pm7550_s3` | 968000 to 1040000 |
| bt io | `pm7550_l7` | 1800000 to 2080000 |

Stated. Fairphone 6, the same SoC, uses the same split in its `wcn6750-pmu` node: `vreg_s3b`, `vreg_s2b`, `vreg_s1b`, `vreg_s1j`, `vreg_l7b` (`milos-fairphone-fp6.dts:230-240`). Inferred: Nothing's PM7550 rails line up with those, but the mainline asteroids dts does not carry a PMU node, so the mapping is by name only.

Stated. Pins and clocks:

- Bluetooth/chip enable GPIO 0x35 = 53, pulled down in `bt_en_sleep` (`asteroids.dts:5032`). FP6 uses `bt-enable-gpios = <&tlmm 53>` on the PMU node for the whole chip.
- `qcom,wl-reset-gpio` 0x2c and `bt-sw-ctrl-gpio` 0x2d are listed on the BT node.
- TLMM 63 and 64 are "WLAN UART". Leave them alone.
- PMK8550 GPIO 5, function `func1`, output enabled, is the `alt_sleep_clk_default` state (`asteroids.dts:9613-9622`). The FP6 enable commit `30931aed15b4` says the missing sleep-clock pinctrl on exactly this pin was why "the WPSS keeps crashing".
- `RPMH_RF_CLK1` is the PMU's clock in the FP6 tree.

PMIC-GLINK is not on the Wi-Fi path. The ADSP is not needed for Wi-Fi in the sources I read. Inferred.

## 2. How Linux drives it

### The pieces

Stated, line counts from torvalds/linux at the time of the 7.4 merge window (my fetch, 2026-11-02).

| Piece | File | Lines |
|---|---|---|
| WPSS PAS remoteproc | `drivers/remoteproc/qcom_q6v5_pas.c` | 2094 |
| Q6v5 common | `drivers/remoteproc/qcom_q6v5.c` | 394 |
| MDT loader | `drivers/soc/qcom/mdt_loader.c` | 508 |
| SCM, PAS calls | `drivers/firmware/qcom/qcom_scm.c` | 2912 |
| SMEM | `drivers/soc/qcom/smem.c` | 1307 |
| SMP2P | `drivers/soc/qcom/smp2p.c` | 794 |
| GLINK SMEM and native | `drivers/rpmsg/qcom_glink_smem.c`, `qcom_glink_native.c` | 385, 1986 |
| QRTR | `net/qrtr/af_qrtr.c`, `smd.c` | 1408, 111 |
| QMI helpers | `drivers/soc/qcom/qmi_interface.c`, `qmi_encdec.c` | 854, 945 |
| ath11k, the 17 files a station needs | `core hal qmi ce hw htc wmi mac reg peer dp dp_rx dp_tx hal_rx hal_tx dbring ahb` | about 47000 |

Of the ath11k lines, `wmi.c` (10180), `mac.c` (10891), `dp_rx.c` (5809) and `qmi.c` (3370) are the large ones. HakiOS replaced `mac.c`'s mac80211 dependency with its own station layer, so the mac80211 stack itself is not in play.

Stated. Vendor side, in bytes because I did not count lines: `icnss2/main.c` 141 KB, `icnss2/qmi.c` 106 KB, `icnss2/power.c` 27 KB, plus `cnss_utils/wlan_firmware_service_v01.c` 206 KB; the Android host driver `qcacld-3.0` is about 27 MB of C and headers (`nothing-sm7635-devs/android_kernel_nothing_sm7635-modules`, branch `lineage-24.0`, `qcom/opensource/wlan`). Nobody should port that. ath11k is the sane reference.

Stated. `nothing-sm7635-devs/android_kernel_nothing_sm7635` is empty. The real vendor kernel is `NothingOSS/android_kernel_msm-6.1_nothing_sm7635`, branches `sm7635/v/mr` (head `d5f5f965fd0b`, 2025-11-11), `sm7635/b/mr` and `sm7635/b/mr_Frogger` (head `3d5142984841`, 2026-10-19). I listed its DT directory (`arch/arm64/boot/dts/vendor/qcom`, `volcano*.dtsi`) but did not read the Wi-Fi nodes there, because the live dump already carries the final values.

### ath11k sequence on this chip

Stated, from `ahb.c` and `qmi.c`:

1. `ath11k_ahb_probe` sets up resources, maps copy-engine registers, takes the `qcom,rproc` handle, registers SMP2P state.
2. The remoteproc boots, WLFW (QMI service 0x45) appears over QRTR.
3. QMI: indication register, host capability, no DDR request on this chip, MSA ready, board data download, regdb download, WLAN config, mode on.
4. Copy-engine rings (CE) and HAL SRNG are programmed through a static register window, HTC connects, WMI reaches "unified ready".
5. Station layer scans, joins, installs keys; data moves through the DP rings.

Stated. The WCN6750 hw entry uses a 256 KiB board file with `cal_offset` 128 KiB and `supports_regdb = true`. Linux normally loads `ath11k/WCN6750/hw1.0/board-2.bin` and `regdb.bin`. The WPSS image itself comes separately as `wpss.mbn`.

### Firmware we hold

Stated. `backup-asteroids/modem_a.img` is a FAT16 image with 4096-byte sectors. I listed it read-only with a short parser. It holds:

- `image/qca6750/wpss.mdt` (4844 bytes) plus `wpss.b00` to `wpss.b10`, `wpss.b12` (about 8.2 MB total), `wpss.qdb`, `Data.msc`.
- `image/qca6750/bdwlan.bin` (58180 bytes) plus `bdwlan.b01` to `b20` (58180 bytes each) and `bdwlan.e01` to `e08` (59932 bytes each), the board data variants.
- `image/qca6750/regdb.bin` (24278 bytes) and `qdss_trace_config.cfg`.

Stated. The `wpss.mdt` ELF header is a standard MDT. The signing certificates inside carry the strings "NOTHING", "NT Root CA", "NT Attestation CA", so the image is Nothing-signed and TZ will accept it, as the ADSP and GPU zap images already are.

Stated. `bluetooth_a.img` holds the Bluetooth patch files for the same chip. `dsp_a.img` is an ext4 image I did not open. `hyp_a.img`, `tz_a.img`, `qupfw_a.img` exist but are not needed here.

Stated. HakiOS fetched the same files from `/vendor/firmware_mnt/image/wpss.{mdt,b02..b07}` and bundled them into its small boot image (`notes.md`, "adb pull ... wpss.mdt", and commit `c0ba05a`, "the phone's board data and regulatory database go into the small image"). Inferred: on ours the file read can reuse rootstock's `AdspFirmware.c` pattern, which reads this same FAT volume from `modem_a` (LUN 4) through the existing raw UFS reader.

Unknown. ath11k wants `board-2.bin` format for a variant lookup, or a plain `board.bin`. HakiOS fed the phone's own board data to the firmware ("the firmware takes ath11k's exchange and the phone's board file, and says ready", commit `c4fea0f`). Which `bdwlan.*` file matches this board ID I did not determine. The WLFW capability reply carries the board id, so the host can pick the match at run time.

## 3. Prior art

### HakiOS, the closest match

Stated. Repo `oosexgang/hakios-xnu-kernel` (head `1ad456f`, 2026-11-01, 767 commits, license field NOASSERTION, `NOTICE` says the layer is MIT) and `oosexgang/hakios-sm7325`. The device table `docs/devices/redwood.md` lists "wifi: works. WCN6750 with Linux's ath11k: radio, driver, a network joined (wifi=1)". Target phone is the POCO X5 Pro, SC7280, XNU 8796 with iOS 16.5 userland. WCN6750 and the WPSS are the same silicon family as ours.

Stated, how it works.

- `drivers/wifi/hakios.wifi.c`, 4444 lines, does the whole subsystem start: IPCC, RPMh, AOP QMP, SMEM, SMP2P, TrustZone `PAS_INIT_IMAGE` / `MEM_SETUP` / `AUTH_AND_RESET` for pas id 6, GLINK `IPCRTR`, QRTR, then the WLFW exchange. It is gated by `wifi_step=N` boot args so every step can be run alone, and the first five steps only read.
- `tools/mk-ath11k.sh` pulls Linux 7.0.14 (sha256 pinned) and FreeBSD 15.1 LinuxKPI headers, patches six headers, and compiles 18 ath11k files unmodified into one object linked into the kernel.
- `wifi/lx/src/` is the layer: `lx_80211.c` 81 KB (a station in place of mac80211: scan, auth, assoc, WPA2-PSK four-way), `lx_base.c` 31 KB, `lx_bus.c` 30 KB, `lx_wpa.c` 17 KB. The kernel side is `hakios.lx.c` 582 lines, `hakios.sta.c` 371, `hakios.wlfw.c` 545 and `hakios.apple80211.c` 739.
- The radio's DMA goes through a fresh SMMU context the kernel builds itself (`wifi_smmu=2`): first free stream match register, lowest free context bank, own stage-1 tables, IOVAs from the vendor's 256 MB pool. It writes the SMMU and "everything read back as written".
- Rings are polled once a millisecond at first, and later interrupts were added.
- Presented to iOS as `en0` (`drivers/net/hakios.net.c`) with Apple80211 answered in the kernel (`hakios.apple80211.c`), and wifid listed under a `wlan` node as an `IONetworkController` (`hakios.wlan.cpp`, commit `80f5042`).

Stated, the clock. All times UTC from `git log`:

| Commit | Time | Event |
|---|---|---|
| `bf99866` | 2026-10-13 11:15 | first step, Wi-Fi DSP in the small image |
| `17fe05e` | 11:24 | "the phone's Wi-Fi DSP started by our kernel" |
| `293cd2c` / `c4fea0f` | 12:36 / 12:40 | WLFW exchange, board file, firmware says ready |
| `a9489a9` | 12:59 | mission mode, radio registers answer |
| `3910d2e` | 12:43 | a register read before mission mode ended the kernel, step fenced off |
| `2938d90` | 16:05 | WMI "unified ready" |
| `38e5764` | 16:42 | scan hears networks |
| `785d3be` / `3a57ccb` | 17:26 / 17:35 | WPA2 joined, DHCP 3.5 s later, HTTPS 200 from example.com |
| `497866c` | 10-14 17:51 | network joined from Settings with a finger |
| `4c6b184` | 10-14 18:30 | 55 to 70 Mbit/s with block ack, interrupts, wide channel |

Stated. Their driver review `docs/driver-review/wifi-bringup.md` lists 13 findings in `hakios.wifi.c`, two rated HIGH (GLINK length overflow that spins forever under a lock, unchecked SMEM item extents), fixed in `79b00af`. Anything lifted from this file needs those fixes.

Inferred. Six hours is the best case: the author had a fast fastboot-boot loop, a phone with no firmware protection of the radio's stream, and a working ADSP path already. It still says the work is days, not months.

### UniXNU sheng and Ath12kWifi

Stated. `UniXNU/Ath12kWifi` (last push 2026-10-24) compiles Linux ath12k, MHI and QMI sources over its own shim (`shim/src/lx_rt.c` 14.6 KB) and adds a station layer (`src/nw_sta.c` 50 KB, `nw_mac80211.c` 15 KB, `nw_crypto.c` 8 KB). Board side lives in `UniXNU/UniXNU-sheng` (`package/WifiProbe.cpp` 88 KB, `WifiNet.cpp` 40 KB). On the Galaxy Tab S9 (WCN7850 over PCIe) it "joins WPA2-Personal, gets DHCP, Safari loads pages", about 12 Mbit/s down (`docs/.vitepress/device-records/sheng.toml` area `network`, revision `a57341b`). The first probe commit is 2026-10-22 08:19 and the DHCP/Safari state is 2026-10-24. That is about 2.5 days including PCIe training.

Inferred. This is ath12k and PCIe, so it does not carry over directly. It does show a second team doing the same pattern.

### rootstockos/UniXNU-asteroids

Stated. Fresh head `dbddb09` (2026-11-01 11:53Z); my earlier local clone was at `1d8c8d5`. `porting.toml` area `network` is still `planned`, inputs "not yet identified for a driver", next action "Identify the Wi-Fi module", findings "No network driver runs; bluetoothd is disabled". `paths = ["package/Wifi*"]` matches nothing in the `package/` tree. So rootstock has no Wi-Fi code for this phone.

Stated. It does have the shared plumbing, all in `package/`: `QcomSmem.c` 112 lines, `QcomSmp2p.c` 125, `QcomGlink.c` 453, `QcomQrtr.c` 226, `QcomQmp.c` 102, `QcomMdt.c` 157, `QcomScm.h` 44, `QcomRpmh.c`, and `AdspProbe.cpp` 698 which starts the ADSP on this exact phone with PAS ids 1 and 36 and the SMC encoded in `AdspProbe.cpp:271-293`. Total about 1900 lines for the pieces WPSS also needs. The commits `e1f22e6`, `dd26f2d`, `f8cb496` (battery) and the audio T40k notes in `porting.toml` show the start working on this phone. License text says BSD-2-Clause-Patent per the earlier gaps survey; the GitHub license field is NOASSERTION.

### Poco F1 work in this repo

Stated. `docs/WIFI_IOS26.md` and `WIFI_IOS26_PHASE2.md` on the `ios-asteroids` repo describe a WCN3990 path on iOS 26.6.2 and xnu-12377, the same kernel we run. Patches `1100` to `1130` in `xnu/patches-12377` (11783 diff lines for 31 files). Boot 30 (2026-10-25) was a read-only probe. Boot 54 (2026-10-26 about 22:46Z) was a WPA2 join in 251 ms, DHCP 192.168.1.189 and established TLS sessions. That is about two days, with modem bring-up (MBA, MPSS, rmtfs, TFTP, pd-mapper) which the WCN6750 does not need.

Stated, what carries over to any chip:

- `bsd/dev/sweet_80211.c` gives wifid an `en0`, answers `SIOCGA80211`/`SIOCSA80211` and posts kernel events. Settings listed the network and joined from the UI on boot 54, with a known defect fixed in `1130` (a 460-byte ASSOC_DONE blob).
- `sweet_wpa.c` and `sweet_wlan_frames.c` (patch `1126`): WPA2-PSK four-way, group key, auth/assoc frames, Ethernet to native wifi. They are host-tested and the 4-way completed on air in patch 1127-1129.
- A backend ops struct (`power`, `scan`, `join`, `leave`, `tx`, plus callbacks) between the iOS side and the chip side (`WIFI_IOS26_PHASE2.md` section 2).

Stated. `NOTES.md:30` says `sweet_ufs_init` starts none of the Wi-Fi drivers on asteroids. So the code is in the series but does not run on this phone.

### Others

- `dipa-studio/ios-sweet` README line 31: "No Wi-Fi, Bluetooth, cellular...". No help.
- `edi194/ios-fajita` (OnePlus 6T) README: "Wi-Fi / Bluetooth / modem / camera: no drivers".
- `oosexgang/hakios-xnu-kernel` also has a cellular path with `en1` and IPA (`drivers/modem/hakios.cell.c`, `hakios.ipa.c`). Per its README the modem is up, SIM bound, no registration yet.
- "ios-froggerpro": no repo by that name. I found `twrpdtgen/android_device_nothing_FroggerPro` and `elizamainline/FroggerPro-Status`, which is a mainline Linux status page for a Snapdragon 7 Gen 4 (SM7750) phone. It is not an iOS port and a different SoC.
- "Bergamota" as a separate external repo: not found. The Bergamota notes I could read are the in-repo `docs/BERGAMOTA_*.md`, which are about fastboot, charge and metrics. WCN3990 is in `docs/WIFI_IOS26.md`.
- `rootstockos/iosonandng`: not reached (does not exist publicly, per the earlier survey).

## 4. Gunyah and secure-world constraints

### What the evidence says works from our kernel

Stated.

- SCM calls from EL1 under Gunyah work on this phone. Our own `2005-asteroids-a810-cp-zap.patch` does PAS 13 for the GPU zap, and all 16 GPU stages pass (`NOTES.md:90`, `analysis/daily-driver-gaps.md:83`).
- rootstock starts the ADSP (PAS 1) and its DTB image (PAS 36) from XNU on this phone and plays audio through it (`AdspProbe.cpp:61`, `porting.toml` T38 to T41).
- The Poco F1 notes record working `hyp-assign` SCM calls (`WIFI_IOS26.md` section 5.7, `completed=1 status=0`), but that was SDM845 with a different hypervisor.
- PAC works at EL1 under Gunyah (`NOTES.md:536`).

Inferred. A WPSS PAS start (id 6, `init_image`, `mem_setup`, `auth_and_reset`) is the same call shape as the ADSP. Linux does it with no hyp-assign for WPSS. Nothing in the sources says Gunyah blocks pas id 6.

### What may block us

Not established. These are the unknowns that are specific to this phone.

1. The apps SMMU. WPSS DMA goes through `apps_smmu` stream 0x1400 mask 1 (`asteroids.dts:27001`; SC7280 on HakiOS used 0x1c00). The radio needs a translation or bypass for its rings. The live DT shows Android using `qcom,iommu-dma = "fastmap"` with an IOVA pool at 0xb0000000. `analysis/usb-plan.md` says the Android VM overlay describes a virtual SMMUv2 with 6 context banks and 14 SMRs, that the owner saw an external abort at SMR7 `0x1500081c`, and that the project has not found a safe USB mapping on this firmware. The Wi-Fi stream is the same class of problem. HakiOS reached the same SMMU on SC7280 under that phone's hypervisor and found "the hypervisor grants it", but that is a different hypervisor build. Inferred: the Wi-Fi SMMU context is the largest technical risk.
2. SPMI writes. The sleep clock needs a PMK8550 GPIO 5 write. `analysis/daily-driver-gaps.md:73` records an SPMI external abort at `0x0C74D008` for some peripherals under Gunyah during UEFI. Our kernel already writes PMK8550 SDAM 0x7148 through EE 0 (`NOTES.md:84`), so some channels work. Whether GPIO 5's APID is owned by our EE is unknown. A read-only probe settles it.
3. RPMh votes for the six rails and the RF clock. `sweet_rpmh.c` exists. HakiOS says each rail needs mode, voltage and enable requests (`sheng.toml` for WCN7850) and rootstock has `QcomRpmh.c`. Unknown whether Gunyah lets our VM post to the RSC DRV we have. Our DT shows `rsc drv-0` at 0x0af20000 in `iomem.txt`.
4. The AOP QMP message `{class: image, res: load_state, name: wpss, val: on}` that HakiOS sends. Rootstock's `QcomQmp.c` does the same for the ADSP.
5. The MSA region. Vendor DT has a fixed 12 MiB `wlan_msa_mem`. Mainline's FP6 path sets `use_tz` because the `wifi` node has no `wifi-firmware` child, which means TZ or the hypervisor sets up the radio's access (`ahb.c:1025-1031`). I could not confirm what exactly TZ does for MSA on Milos. I did not find a `memory-region` on the mainline `wifi` node, so where ath11k gets the MSA address on Milos is unresolved. Not reached: the lore thread (HTTP 403).

### Could anything brick the phone?

Stated, from the sources.

- A WPSS crash that the kernel does not stop leaves the DSP running, and HakiOS saw the phone "restarted by itself" half a minute later (`hakios.wifi.c` step 16 comment). Another case: an SMMU change removed by an earlier shutdown made the next start take the phone down "every second start, and never a first" (`hakios.wifi.c:1805-1812`).
- On our phone a spontaneous reset reboots into whatever the boot slot holds, and `NOTES.md:22-23` and `53-54` say a reset can start Android, which formats userdata. That is the real loss risk here: the iOS install, not the hardware.

Inferred.

- The SCM `auth_and_reset` rejects an image it cannot verify and returns an error. It writes no fuse and changes no partition. The firmware is Nothing's own signed image, so the signature path is the one Android uses.
- No step needs a flash or partition write. The firmware read is read-only (`modem_a`, same path rootstock uses).
- The hardware-damage path is wrong rail voltages. Stay inside the DT ranges in section 1. Do not touch TLMM 63 and 64.
- A fault inside SMMU or SPMI is a panic and reset, not permanent damage.

Avoid it with: boot-arg gates off by default, read-only steps first, WPSS shutdown on every exit and panic path (HakiOS added this after its first reset), and never leave the phone in a state where a reset boots Android with `userdata` writable. The last point is a pre-existing project issue, not something Wi-Fi introduces.

## 5. How iOS would use it

### The smallest path

Stated. The iOS 26 side is mostly done. `docs/WIFI_IOS26.md` section 1 records what wifid needs (an ADT node named `wlan`, IOKit properties `IO80211InterfaceRole` and `WiFiHardwareHealthy`, a BSD interface, the two Apple80211 ioctls, and one event class). It describes the sync and async join paths and the 908-byte ASSOCIATE layout. `sweet_80211.c` plus `sweet_wpa.c` already implement them and were run on iOS 26.6.2 up to Settings-driven join.

Inferred. The least work is a new chip backend behind the existing backend ops: ath11k-on-LinuxKPI as in HakiOS, or a hand-written ath11k-class path as the Poco F1 team did for ath10k. Nothing in `sweet_80211.c` is chip-specific.

### Credentials and DNS

Stated. Two proven routes:

- HakiOS reads `fw-wifi/network` from the boot image and joins at boot (`3a57ccb`: "The network's name and passphrase come from fw-wifi/network through the small image; they are in no log and not in the repository."). Settings then takes over (`497866c`).
- The Poco F1 flow joins from Settings with the passphrase delivered as a PMK by wifid, so the kernel needs no PBKDF2 (`WIFI_IOS26.md` 6.3).

Stated. DHCP, DNS and routes come from configd and bootpd: boot 54 got 192.168.1.189/24, IPv6 SLAAC, a default route, and TLS sessions to Apple with no extra code. HakiOS saw the same ("iOS has an address by DHCP 3.5 s later, resolves a name, and NSURLSession fetches https://example.com/"). No user-space wpa_supplicant is needed. The 4-way handshake already lives in the kernel.

Stated. HakiOS found the system cannot store the Wi-Fi password in the keychain on sheng ("keychain error -25291") and wrote `wifipw_test` as a stand-in (`e4eb322`). Expect the same: joins work, networks may not be remembered across reboots without a stub.

Stated. A stub or real en0 also removes the 300 s Settings freeze. `NOTES.md:410` shows Preferences blocking in `_WifiStateChanged` until configd's `WaitStackTimeout`. A temporary root tweak sets 5 s. The real fix is a stub `IONetworkStack` with a user client.

### What of ath11k would have to run

Stated. HakiOS built 18 ath11k files unmodified: `core hal qmi ce hw fw htc wmi mac reg peer dp dp_rx dp_tx hal_rx hal_tx dbring p2p`. Left out on purpose: debugfs, spectral, CFR, testmode, WoW, thermal, PCI, MHI.

Inferred. The pieces nobody can skip are QMI WLFW (`qmi.c`), CE and HAL rings, HTC, WMI and the DP receive and transmit paths. A hand port of those is the larger job (the Poco F1 team wrote roughly 11000 diff lines for the lighter ath10k path). Compiling Linux's own files avoids that, at the cost of a LinuxKPI layer of about 8 files and 160 KB.

### Stopgaps, ranked by evidence

1. A fake `en0` with the existing synthetic backend (`sweet_80211.c`, patches `1100` to `1103`). It ends the Settings freeze and shows a network list. It gives no internet. Days, mostly already written. Not a way to browse.
2. USB Ethernet to a Mac with NAT (`tools/sweet-usbnet.sh`, `sweet-mac-nat.sh`). `analysis/usb-plan.md` says gadget mode is refused: no safe DMA mapping under Gunyah is proven. Estimate in the gaps survey is 1 to 3 weeks. Needs a computer attached, so it is a development tool, not a daily phone.
3. Bluetooth PAN or tethering. No Bluetooth stack exists for WCN6750 (`daily-driver-gaps.md` section 7, 2 to 4 weeks) and it needs another device.
4. Cellular data through MSS and IPA. HakiOS has the modem up with no registration; `daily-driver-gaps.md:212` calls it effectively impossible here.

None of these gives internet for YouTube without something else attached or something harder than Wi-Fi. Wi-Fi through the WPSS is itself the shortest route to the owner's goal.

## 6. Ranked plan

Effort is agent time with the phone available only for owner boots. The CI build per kernel is a separate delay. All estimates are inferred from the timelines above; HakiOS and Poco F1 numbers are observed, ours are not.

| Phase | Work | Agent time | Risk | Phone? |
|---|---|---|---|---|
| 0 | Read-only desk work. Lift rootstock's `Qcom*.c` plumbing and HakiOS's WPSS start into a patch with its W1 to W9 fixes. Generate the WLFW QMI tables. Build ath11k with `mk-ath11k.sh` for xnu-12377. Add host tests. Check licenses. | 4 to 7 days | none | no |
| 1 | Read-only boot probe. Log SMEM, IPCC, AOP mailbox, RPMh DRV state, PMK8550 GPIO 5 and TLMM 53 registers, and a bounded SMMU look that skips SMR7. | 1 to 2 days | low; a bad read can still abort | one owner boot |
| 2 | Power sequence and WPSS start with a clean stop on every exit. Rails, sleep clock, RF clock, enable GPIO, AOP `load_state`, `init_image` + `mem_setup` + `auth_and_reset`. | 3 to 6 days | medium: SPMI, RPMh and reset behavior | owner boots |
| 3 | GLINK `IPCRTR`, QRTR, WLFW exchange: host cap, MSA, board data, regdb, calibration, mission mode. | 3 to 5 days | low to medium | owner boots |
| 4 | Radio memory view (SMMU context or proven bypass), CE rings, HTC, WMI unified ready, scan. | 1 to 3 weeks | high: the Gunyah SMMU | owner boots |
| 5 | Station join with the existing `sweet_wpa.c`, `en0` data path through `sweet_80211.c`, DHCP, a page load. | 4 to 8 days | low | owner boots |
| 6 | Interrupts on SPIs 768 to 799, block ack, Settings password storage, remembered networks, idle power, WPSS crash recovery. | 1 to 3 weeks | medium | owner boots |

Totals, inferred. First proven internet connection: 4 to 7 weeks. Stable daily use: 6 to 10 weeks. That is below the 2 to 3 months in `analysis/daily-driver-gaps.md:50`, because HakiOS shows the same chip working through a route that skips a hand-written driver. It is not much below the lower end, because Phase 4 may not resolve at all.

First milestone that cannot hurt the phone. Phase 1 is read-only. Its output is a persistent-log dump of the Wi-Fi block states on the existing log path, plus a host-side QMI encode and decode test of the WLFW messages against the captured Linux tables.

First milestone that starts the radio. Phase 2 reaches "WPSS handover and ready SMP2P bits logged, then stopped cleanly". The next, in Phase 3, is "QRTR announces service 0x45, `fw_ready` and `cal_done` indications arrive, then clean stop". Both are readable from the log without a network.

What needs no phone at all. Phase 0 in full, the host tests, the compile of the ath11k object, the check that the MDT segments fit `wpss_mem` (the MDT is in the backup), the rail table cross-check against the live DT, and review of lifted code.

Steps that risk the data, not the hardware. Phases 2 to 6 run with a kernel that can fault; a fault can reset the phone into the Android boot path. Keep every WPSS step behind a boot-arg that defaults off, put the WPSS shutdown on every exit, and make sure the boot slot cannot reach a state that formats `userdata` before running them. That last condition belongs to the existing boot design and I did not change or check it.

Steps that risk hardware. Only regulator votes outside the DT ranges. Use the table in section 1 and nothing else.

Ranking of routes, by evidence, no decision made here:

1. ath11k unmodified on a LinuxKPI layer, as in HakiOS (same chip, observed result, 6 hours to join).
2. A hand-written ath11k-class port under the existing `sweet_80211.c` backend ops (observed on a different chip in about 2 days, more code).
3. Reuse of UniXNU's Ath12kWifi shim (observed on ath12k only; ath11k would need a new `prepare.sh` and a station layer check).

## What I could not find or reach

- `lore.kernel.org` returned HTTP 403 for the Luca Weiss / Alexander Koskovich bring-up thread, so the exact crash symptoms and any extra patches (for example ath11k or remoteproc fixes for Milos) are unread. The commit messages above are the only sources.
- I did not read the Wi-Fi nodes of the vendor DT in `NothingOSS/android_kernel_msm-6.1_nothing_sm7635`. The live dump is the final tree.
- `nothing-sm7635-devs/android_kernel_nothing_sm7635` is empty, and the `qcom/opensource/wlan/platform` sources were listed but not read.
- Whether Gunyah lets our VM program the apps SMMU stream 0x1400, post to RPMh DRV 0, write PMK8550 GPIO 5, and run `qcom_scm_assign_mem`. No log on this phone covers them.
- Which `bdwlan.*` variant fits this board, and whether ath11k's `board.bin` loader accepts it unmodified.
- The reg block of the WPSS (`0x9bb00000` in the vendor DT, `0x08a00000` in mainline).
- How HakiOS's SC7280 hypervisor differs from Gunyah. Their notes call the hypervisor "the phone's hypervisor" without naming it.
- Licensing: `hakios-xnu-kernel`, `UniXNU-asteroids` and `Ath12kWifi` have no machine-readable license. `NOTICE` says the HakiOS layer is MIT, and rootstock's text says BSD-2-Clause-Patent. Ath11k is BSD-3-Clause-Clear. The owner should read those texts before any code is copied.
- `rootstockos/iosonandng` and a separate external "Bergamota" repo: not found. "ios-froggerpro": no such repo.
- I did not open `dsp_a.img` (ext4) or the `hyp_a`, `tz_a` partitions.

## Sources

- Local: `analysis/daily-driver-gaps.md`, `ios-asteroids/NOTES.md`, `docs/WIFI_IOS26.md`, `docs/WIFI_IOS26_PHASE2.md`, `analysis/usb-plan.md`, `research/asteroids-hw/asteroids.dts`, `iomem.txt`, `cmdline.txt`, `backup-asteroids/modem_a.img`, `bluetooth_a.img`.
- https://github.com/torvalds/linux commits `6bf981752daa`, `30931aed15b4`, `e506d34e5723`, `edbf03c9fb5f`, `7d3670ccf61b`; files `arch/arm64/boot/dts/qcom/milos.dtsi`, `milos-nothing-asteroids.dts`, `milos-fairphone-fp6.dts`, `drivers/remoteproc/qcom_q6v5_pas.c`, `drivers/net/wireless/ath/ath11k/*`.
- https://github.com/oosexgang/hakios-xnu-kernel (commits `bf99866`, `17fe05e`, `c4fea0f`, `3910d2e`, `a9489a9`, `2938d90`, `38e5764`, `785d3be`, `3a57ccb`, `497866c`, `4c6b184`, `e4eb322`, `79b00af`, `80f5042`; files `drivers/wifi/`, `wifi/lx/src/`, `tools/mk-ath11k.sh`, `docs/driver-review/wifi-bringup.md`, `docs/devices/redwood.md`).
- https://github.com/UniXNU/Ath12kWifi (`f6bf561`) and https://github.com/UniXNU/UniXNU-sheng (`a57341b`, `9871f4e`, `bf99866` timeline from `WifiNet.cpp` and `WifiProbe.cpp` history); `sheng.toml` in https://github.com/UniXNU/UniXNU.
- https://github.com/rootstockos/UniXNU-asteroids at `dbddb09` (`porting.toml`, `package/Qcom*.c`, `AdspProbe.cpp`).
- https://github.com/NothingOSS/android_kernel_msm-6.1_nothing_sm7635, https://github.com/nothing-sm7635-devs/android_kernel_nothing_sm7635-modules, https://github.com/nothing-sm7635-devs/android_device_nothing_asteroids.
