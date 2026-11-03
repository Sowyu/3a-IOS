# Modem and SIM: desk findings (2026-11-03)

Desk work only. Nothing ran on the phone, and no patch exists. The agent stopped before writing the modem start: the assistant's safety system interrupted the TrustZone start code, and the work was not continued.

Labels as in `docs/input.md`: **Measured**, **Code**, **Inferred**, **Missing**.

## Facts

- **Code, firmware.** The `modem_a` partition (FAT16, 4096-byte sectors) holds `image/modem.mdt` and `modem.b01` to `b30`. That is 30 relocatable load segments, about 109 MB, spanning 0xd700000. It fits in `mpss_mem` (0x8ac00000 + 0xe600000). There is no `modem_dtb.mdt`. Mainline Linux runs this SoC family's modem as PAS id 4 without a DTB image. `plans/modem/tools/fatls.py` lists and extracts it read-only. The firmware stays out of this repo.
- **Code, DT.** `remoteproc-mss@04080000`, compatible `qcom,volcano-modem-pas`. Its regions are `mpss_mem`, `q6_mpss_dtb_mem` (0x99200000 + 0x80000) and a third (phandle 0x7a). Its interrupts are wdog, fatal, handover, ready, stop-ack and shutdown-ack. It has a "stop" SMP2P state, GLINK edge "mpss" (remote pid 1, IPCC mailbox) and AOP QMP signalling.
- **Code, memory.** XNU's RAM is 0x8c0000000 + 0x134000000, so the kernel cannot allocate below 4 GB. rmtfs needs a 2.5 MiB buffer below 4 GB. The bank at 0xe0a00000 is the only free low RAM, and 0xe1000000 to 0xe2000000 is unused.
- **Code, storage.** `sweet_ufs_read_part()` reads `modem_a` and the EFS partitions read-only. The kernel's UFS write path refuses anything outside `userdata`. The modem's EFS (`modemst1`, `modemst2`, `fsg`) holds the IMEI and RF calibration. It must never be written: serve it read-only, and keep any writes in RAM.
- **Code, missing pieces.** This kernel has no RPMh client for the apps RSC and no AOSS QMP sender for SM7635. Both are needed for the rail votes and `load_state`.
- **Code, reuse.** The Poco F1 port booted SDM845's modem on this same xnu-12377 (`ios-asteroids/xnu/patches-12377/1104` to `1116`: SMP2P, rmtfs over QRTR and GLINK, tftp, pd-mapper). SDM845 used MBA, while this phone uses PAS. rootstock's `ref/UniXNU-asteroids/package/` has SMEM, SMP2P, GLINK, QRTR, QMP and an ADSP start for this phone.
- **Inferred, from HakiOS (`ref/hakios-xnu-kernel`, SC7280).** The modem announced UIM about 30 s after rmtfs was served, while still offline. Going online without IPA crashed the modem. So reading SIM status needs rmtfs, and likely tftp and pd-mapper too, but not IPA.

## State

- `plans/modem/src/lift/` holds unfinished drafts that do not compile. `ios-asteroids/research/xnu-modem` is a fresh kernel tree without the daily series applied.
- Calls, SMS and data need an iOS-side stand-in for Apple's baseband interface. `analysis/daily-driver-gaps.md` section 12 covers that.
