# Developer connectivity and non-Wi-Fi connectivity: plan

2026-11-02. Nothing here ran on a phone. I ran no adb or fastboot, built no kernel, committed nothing and changed no existing file. Every prepared fix is an unapplied patch in `plans/usb/` and is **untested on a phone**.

Labels: **Proven** means I read it in code, a log, a disassembly or the device tree. **Inferred** means reasoning with no run behind it. **Unknown** means no evidence either way.

## What changes the picture

### 1. The SMMU aborts came from how the probe was compiled (proven)

The on-hold conclusion rested on aborts at FAR 0x15000024 and 0x15000020. Both faulting addresses hold a post-index load, which Gunyah can't emulate.

- `ast-usbprobe2-boot.img`, offset 0x2408, holds `ldr w2, [x19], #4` (0xb8404662). The photo `research/cam/usbprobe4.jpg` shows ELR ending 0x2408 and FAR 0x15000020.
- `ast-usbprobe-boot.img`, offset 0x23e8, holds `ldr w4, [x19], #-4` (0xb85fc664). The photo `usbprobe.jpg` shows FAR 0x15000024. Its ELR digits are blurred and fit 0x23e8, so the match for this one is strong but not certain.
- ESR is 0x96000010: synchronous external abort with ISV 0. A load with writeback gives the hypervisor no syndrome to emulate.
- Patch 2001 already recorded this: "a writeback load took an external abort, run 4". `usbprobe.c`'s `rd()` is a plain volatile dereference, and `-O2` folded two reads into a post-index load.
- `plans/usb/host/check_shim.py` reproduces it. The old source compiled with the shim's compiler and flags gives two writeback loads, the first `ldr w2, [x19], #4`, the same instruction as the photo. The patched source gives none.

The code comment "Without ufs_prepare() first, a read of 0x15000024 raised an external abort" therefore blames the wrong cause (inferred).

SMR 7 at 0x1500081c is also not a general limit (proven). `at_smmu_route` in patch 2001 scans every SMR with a plain `ldr` and has no early exit on a free slot. Twenty-five saved logs show `SID 0x576 -> SMR 113` and `SID 0x563 -> SMR 112`, so those scans read past SMR 7 without aborting. I have not found the instruction behind the original SMR 7 abort, because that shim build is gone. Its cause is **unknown**.

### 2. This phone already routes SMMU streams from our kernel, every boot (proven)

- `SWEET TOUCH: SMMU SID 0x576 -> SMR 113 smr=0x80000576 s2cr=0` appears in 25 logs, with readback checked. Touch DMA works over it.
- Those entries copy the S2CR of the UFS stream, context bank 0. The shim's `smmu_check_ufs` refuses to boot unless that stream passes straight through, so bank 0 is pass-through on every boot we have run.
- The logged sCR0 is 0x2a0406, which has USFCFG (bit 10) set. Unmatched streams fault, so ABL must have had some mapping for SID 0x40 while fastboot ran.
- Rootstock uses the same pass-through recipe for QUP (`package/SmmuBypass.h`). HakiOS builds a fresh stage-1 context on the same SMMU family for Wi-Fi, per `analysis/research-wifi.md`.

This contradicts `ios-asteroids/analysis/usb-plan.md`, which treats SMMU access as unprobeable. Its caution about DMA stays valid, and section "USB gadget" below keeps it as gates.

### 3. A software path into fastboot already works (proven)

Patch 2004 writes reason 2 to PMK8550 SDAM 0x7148 bits [7:1] and calls PSCI SYSTEM_RESET. The device tree agrees: `reboot_reason` points at `restart@48` of `sdam@7100` (`asteroids.dts:9457`, `13002`). `NOTES.md:106` records it working: "Opening Settings -> fastboot".

The live tree has no `qcom,msm-imem-restart_reason` node and no `reboot-mode` node. So the register is SDAM, not PON spare or an IMEM cookie.

So the button press is needed in only two cases:

- A panic stays on its screen, because `sweet_reboot_restart` returns -1 on asteroids. `sweet_reboot_init` returns early on anything but beryllium, so the old `sweetreboot=bootloader` panic route never arms here.
- A hang, where nothing ever calls restart.

Patches 2060 and 2061 close those two cases.

### 4. The Android trap can be closed at the same time (inferred)

If the reason is stored at boot, any uncontrolled reset also lands in fastboot. All 25 saved logs that print it read `0x7148 = 0x00` at boot, and `NOTES.md:106` records software reboots to fastboot in that period, so ABL or earlier firmware probably clears the field. I did not match each log to the reset before it or read ABL, and Phase 1 step 2 tests this directly. A flat battery may still lose it, so this does not replace the standalone-boot work in `daily-driver-gaps.md`.

## USB gadget: the questions asked

**DWC3 peripheral mode under Gunyah, with what SMMU state?** SID 0x40 (DT `iommus = <0x49 0x40 0x00>`, also in mainline `milos.dtsi:2231`). Route it to bank 0 pass-through as touch does, after the controller is proven halted.

**How does ABL DMA under the hypervisor?** Unknown in detail. USFCFG=1 means ABL needed a match for SID 0x40 (inferred). For UFS, ABL leaves its SMR in place and we use it (proven). The same may hold for USB. Patch 2062 prints the answer.

**Can ABL's leftover mapping be used?** Possibly, and we don't depend on it. There are three outcomes:

1. An SMR exists and points at a pass-through bank: use it as left.
2. An SMR exists and points at a translating bank: don't reuse it, because its tables may sit in memory the kernel owns. Repoint it to bank 0 after halting the core.
3. No SMR exists: add one with the touch recipe.

**Safe path that avoids the aborts:** plain single `ldr`/`str` only, GDSC and clock reads before any DWC3 read, the log synced before each step, and the reason armed so that any failure ends in fastboot.

**Which gadget?** CDC-ECM, already written in `sweet_usb.c`, proven on the Poco F1. `usb-plan.md` verified that macOS's `AppleUserECM` dext matches its classes with no VID/PID restriction. CDC-NCM gains nothing at this speed. CDC-ACM gives no SSH. usbmux needs Apple's stack on the phone side, so it isn't worth it.

**Remaining DMA risk (stated plainly):** pass-through lets the controller write any RAM the hypervisor allows the VM, including kernel memory and UFS buffers, if a TRB or event address is wrong or if a stale ABL address is live. Stage 2 should bound it to our VM's memory (inferred). The gates in Phase 3 are aimed at this. A worse outcome than a crash would be corrupting the HFS root, which means a reflash, not a brick.

## Phases

Estimates are agent time for the code. Each test also needs a kernel CI run, whose time I did not measure, and one owner boot until Phase 2 removes the owner.

### Phase 0: first evidence from the shim. 15 minutes, no kernel build

Depends on nothing. Risk: low, the worst case is a screen or hang needing Power + Vol Down, as today.

1. Apply `shim-usbprobe-plain-ldr.patch` to `ios-sweet`.
2. Build with `SHIM_USBPROBE=1` through `asteroids/build-boot.sh`.
3. Owner boots it and photographs the screen.

Verify on the phone: no red exception screen, SMMU IDs and SMR 0-6 printed, then either "not clocked" or the DWC3 registers. A red screen with a plain `ldr` at the ELR disproves finding 1 for that address.

### Phase 0 result, 2026-11-02 23:31

The owner booted `plans/devloop/out/usbprobe-boot.img` (plain-ldr patch, no writeback loads in `usbprobe.o`) with `fastboot boot` from the fastboot menu. Blue screen, no exception, `USBPROBEdone`. Photo: `ios-asteroids/research/cam/usbprobe-asteroids-20261102.jpg`, not in this repo.

Read from the photo:

| Register | Value |
|---|---|
| SMMU idr0, idr1 | 4C017E72, 60000048 |
| SMMU idr2, scr0 | 00005111, 002A0406 |
| SMR0 to SMR6 / S2CR | 80000060/0, 80000540/1, 80021C00/2, 80000040/3 (USB SID matched), 80000480/4, 0/00020000, 0/00020000 |
| Context bank 3 SCTLR, CBAR, CBA2R | 000000E0, 0001F000, 1 |
| USB GDSC | gdscr=F822F800, master=1 |
| CFGNOC | 1 |
| GSNPSID | 5533330A |
| GCTL | 00102001 |
| GSTS | 7E800000 |
| GHWPARAMS0, 1, 3 | 4020400A, 0162493B, 10420085 |
| GUSB2PHYCFG | 00002400 |
| GUSB3PIPECTL | 0B080402 |
| DCFG | 0048080C |
| DCTL | 10F00000 |
| DEVTEN | 00001277 |
| DSTS | 00D212E4 |
| DALEPENA | 0000000F |
| GEVNTADRLO, HI | D017B000, 0 |
| GEVNTSIZ | 80001000 |
| GEVNTCOUNT | 0 |

What it means. Proven by the reads succeeding: the USB GDSC is on and the DWC3 core is clocked after ABL, so no power-up through the PMIC is needed to talk to it. The rest is my reading of the register fields against the DWC3 and SMMUv2 layouts, not yet checked against the source:

- The core is DWC_usb3 3.30a (`GSNPSID`), in device mode (`GCTL` PRTCAPDIR = 2).
- ABL stopped it cleanly: `DCTL` RUN_STOP is 0 and `DSTS` DEVCTRLHLT is set. `DCFG` still has ABL's SuperSpeed setting and USB address 1 from fastboot.
- ABL's event buffer is still programmed at 0xD017B000, 4 KiB, interrupt masked. That is ABL memory. Reprogram `GEVNTADR` and `GEVNTSIZ` to our own buffer before setting RUN_STOP, or the controller writes events into memory we don't own.
- The USB stream 0x40 maps through SMR3 to context bank 3. Its SCTLR M bit is 0, so stage 1 translation is off and DMA addresses pass through. Whatever bounds them is Gunyah's stage 2, as this plan assumed. Keep the Phase 3 DMA gates.

Next is Phase 3, the gadget stages on top of these values. The Poco F1 CDC-ECM code is the starting point.

### Phase 1: fail-safe kernel. 1 hour agent, 1 CI run, 3 owner boots

Depends on nothing. Patches 2060, 2062. Hold 2061 for Phase 1b.

1. Copy 2060 and 2062 into `xnu/patches-12377`, and build.
2. Boot once with `sweetfbarm=1 sweetusb=1 sweetlog=1 ufslogsecs=1`, then `Settings > Shut Down`. Log shows `sweetfbarm: armed (0x7148 0x00 -> 0x04)`, phone reaches fastboot, next boot reads `0x7148 = 0x00`.
3. Boot with `sweetdeadman=180`. Log shows the deadman line, phone reaches fastboot without a press, root mounts clean next boot.
4. Boot with `sweetpanicfb=10` and force a panic. Phone reaches fastboot after 10 s. The panic text must be in the pulled log.

Risk: **no brick path found.** The only register written is the byte stock Android and patch 2004 already write. A failed write never resets. If step 2's final read is not 0x00, ABL does not clear it. Then stop using `sweetfbarm`, because every later reset would stay in fastboot, which is recoverable but annoying. A forced deadman reset can leave HFS dirty: `NOTES.md:14` ties a past corruption to a forced reset, which is why the clean reboot goes first.

Known doubt: with `debug=0xc`, I could not confirm from `debug.c` alone which exit a panic takes, so the patch hooks both `kPEPanicRestartCPU` and `panic_spin_forever`. Step 4 settles it.

### Phase 1b: hypervisor watchdog. 30 minutes agent, 1 CI run, 2 owner boots

Depends on Phase 1 step 2 passing. Patch 2061. This is the only patch that catches a wedge of all CPUs.

1. Boot with `sweetfbarm=1 sweetgwdt=30`. Log shows `hypervisor watchdog 30 s`. The phone must run more than 5 minutes with no reset, proving the pet works.
2. Trigger a hard hang on purpose, for example a known GPU hang case. The phone should reset within about 30 s and reach fastboot.

Risk: **highest of the prepared fixes, read this before booting it. Not a flash risk, but a possible data-loss risk.** A bite resets with no chance to write a reason. If firmware clears or ignores SDAM on a watchdog reset, the phone boots Android, and Android formats userdata (`NOTES.md:75`). That destroys the iOS root, which a reflash restores. It could also land in a ramdump state needing a button. The patch refuses to start unless the reason read back armed. The SMC numbers come from mainline `gunyah_wdt.c`, and rootstock's PING (0x86000007) runs on this phone (their source). CONTROL and SET have not been exercised on this firmware by anyone I could find. Test it first with a recent root backup at hand.

Does a bite honour the reason? **Unknown.** Linux's driver and `reboot_reason` suggest Android relies on the same field after crashes (inferred).

### Phase 2: no owner in the loop. 2 hours agent, 1 root reflash

Depends on Phase 1. Files: `host/handsfree.py`, `host/framecmp.py`, `root/sweetselftest.*`, `patches/root-selftest.patch`.

1. Build `sweetselftest` and confirm `sweetshot`, `sweettouch` and `sweetopen` exist for iOS 26. Locally `research/sweetnet/ios18` has only `sweetuicache`, so they need building (`tools/sweetnet/build.sh`) or the berga-tools cache.
2. Apply the root patch and rebuild the root. Owner flashes once.
3. Add to test images: `sweetfbarm=1 sweetpanicfb=5 sweetdeadman=420 sweetselftest=1`.
4. Run `handsfree.py` on one image with the owner watching. Then capture reference frames on a good build.

What the loop does: `fastboot boot` test image, wait, self-test launches apps, takes frames with `kern.sweet_fb_shot`, sends touches through the HID system, reboots into fastboot. The Mac then boots OrangeFox, pulls the 4 MiB log and frames, runs `adb reboot bootloader`, compares frames and writes `results.json`.

Design choices:

- Simulated touch uses the existing `sweettouch` (IOHID dispatch), not the kernel driver. Add a kernel path only if HID dispatch fails on iOS 26.
- Frames land on the root and are read with `sweet-hfs-pull.py`, which already exists.
- The allowlist in `handsfree.py` refuses `fastboot reboot`, because with reason 0 it boots Android. It also refuses flash, erase, format, oem and any `.simg`.

**How close to hands-free:** after panic, clean exit, deadman and userland hang, fully hands-free. With 2061 working, hard hangs too. Still needing a person: a dead battery, a hang while 2061 is off, cable or fastboot stalls (`NOTES.md:978` records a "Sending" stall on the Poco F1's ABL, unknown here), and judging anything a frame compare can't see. Charging while looping is **unknown** with the ADSP down (`daily-driver-gaps.md` section 1). Long unattended runs must watch for that. A cycle should take about 5 to 8 minutes unattended (inferred), against 2 minutes plus a person today.

Risk: low for the harness. Reflashing is the owner's existing step. Test the first image attended.

### Phase 3: gadget, staged. 2 to 5 days agent, about 6 to 10 unattended boots

Depends on Phase 1 and on the 2062 output. Each stage is a new `sweetusb` level. Level 2 and up need new code, none of it prepared, because it depends on the probe's values.

1. **Level 2, halt only.** If the core is running: clear RUN_STOP, wait for DEVCTRLHLT, then zero GEVNTADR. Verify: halt seen, no fault, boot continues. This must come before any route, so a stale ABL transfer can't write through a new route.
2. **Level 3, route.** Add or check the SID 0x40 SMR. Keep sCR0 fault reporting visible for this stream's test and log sGFSR.
3. **Level 4, event buffer.** Dedicated pool, addresses checked against that pool before every register write. Add a canary page each side, checked after each stage. Plain pool first with explicit `dc civac` (`sweet_dma_sync_va`), since the DT's `dma-coherent` may not hold through bank 0 (unknown).
4. **Level 5, enumerate.** High speed only. `usb-plan.md` is right that existing descriptors aren't valid at SuperSpeed. Do not reset the eUSB2 PHY or repeater. Adopt ABL's state, and fall back to the DT override sequences only if the dump shows them powered down. On the Poco F1 ABL left the PHY powered down (`docs/BERGAMOTA_FASTBOOT.md`), so expect some PHY work. Repeater writes need the owner EE from 2062.
5. **Level 6, ECM.** Existing `sweet_usb.c` paths, then `tools/sweet-usbnet.sh`, `ping 192.168.7.2`.

Verify: Mac sees `1209:0001`, ping answers, canaries intact, `fsck_hfs` clean over 3 boots. A forced replug must be tested separately from a USB reset, as `usb-plan.md` notes. The role is ADSP-owned in Android, and ABL worked with the ADSP down, so VBUS override should do (inferred).

Risk: **medium for data, no brick path found.** Wrong DMA can corrupt RAM and then the root, needing a reflash. Nothing here writes firmware partitions.

Biggest technical risks: the eUSB2 PHY or repeater needing writes we can't make, and an ADSP dependency for VBUS.

### Phase 4: shell, SSH, files, live logs. 1 day agent

Depends on Phase 3.

1. `rt.ssh`, `rt.usbnet`, `rt.netcfg` and dropbear are already in the root builder. No new code expected.
2. `ssh -i ~/.ssh/id_ed25519 root@192.168.7.2`, `scp` for files, `sweet-usbnet.sh` for live logs (0077 on the Poco F1).
3. `sweetreboot fastboot` from SSH gives the fast loop. The kernel's asteroids restart path always goes to fastboot, so the mode argument is ignored there (proven).
4. Bergamota's known_hosts fix applies if that app is used (`NOTES.md:791`).
5. Internet through the Mac: `tools/sweet-mac-nat.sh`, patch 0110 (proven on the Poco F1).

Verify: SSH works, a 100 MB scp checksum matches, logs stream during a GPU hang.

Reboot to recovery: **skipped.** Reason 1 is the value on the Poco F1 (`docs/REBOOT_MODE.md`) and is unconfirmed here, and stock `recovery_a` is not OrangeFox. Fastboot boot of OrangeFox already reaches it with no button. Add it when OrangeFox is flashed to a recovery slot. A key chord is skipped too, as Power + Vol Down already does the job, though it can't sync. Add one if dirty roots from long presses become common.

### Phase 5: user connectivity that isn't Wi-Fi

Ranked by effort to something usable.

| Rank | Route | Effort (agent) | Needs | Honest value |
| --- | --- | --- | --- | --- |
| 1 | USB gadget + Mac NAT | Hours after Phase 4 | A computer | Developer internet now. Not a user feature. |
| 2 | Bluetooth, no tethering | 2 to 4 weeks | GENI UART on SE11, RPMh rails, reset GPIO 0x35 (53), firmware from `bluetooth_a` | Accessories. `rt.bluetooth`, BlueTool stub and `hcitest` exist. Worked on the Poco F1 iOS 18 (`NOTES.md:802`). |
| 3 | BT tethering to a phone (PAN) | Rank 2 plus 1 to 3 weeks, may fail | iOS's PAN client working under a fake identity | **Unknown** whether iOS 26 will use it. About 1 to 3 Mbit/s at best (inferred). |
| 4 | USB host + USB-C Ethernet | 4 to 8 weeks | xHCI driver, RTL8153 or AX88179 driver, IONetworkInterface glue, VBUS out | No Apple USB host stack in our kernel (inferred, no kexts load). Host role and VBUS likely need the ADSP path. |
| 5 | USB host + tethering from a phone | Rank 4 plus 1 week | NCM or RNDIS host | Same base as rank 4. |

Decision: don't start ranks 4 and 5. Wi-Fi is cheaper per `research-wifi.md`.

Bluetooth depends on the shared WCN power sequencing. GPIO 53 enables the whole chip per `research-wifi.md`, so the two share rails. Do Bluetooth after Wi-Fi's Phase 1 to avoid doing the power work twice.

### Phase 6: harden USB DMA, only if needed

A dedicated stage-1 bank and a mapping limited to the USB pool, as `usb-plan.md` preferred and HakiOS did for Wi-Fi. 2 to 4 days. Do it if a daily-driver build keeps USB on by default. A dev-only gadget behind a boot-arg can stay pass-through.

## Ordering with Wi-Fi

`analysis/research-wifi.md` puts first internet at 4 to 7 weeks and needs many boots, with its hardest risk the same Gunyah SMMU.

1. Phase 0, 1, 1b, 2: now. Every other track (GPU, stability, Wi-Fi) gets faster.
2. Phase 3, 4: next, and only as far as they stay cheap. The time box is one week. If the eUSB2 PHY blocks it, stop, because the hands-free loop still works and Wi-Fi can carry SSH later.
3. Wi-Fi: start its read-only Phase 1 in parallel with Phase 3. They share only the SMMU reads in 2062, and Wi-Fi's SID 0x1400 can be added to that dump.
4. Bluetooth: after Wi-Fi's power work.
5. USB host: not planned.

SSH over Wi-Fi is the fallback developer route if USB stalls. It is how the Poco F1 ran many later tests (`NOTES.md:668`).

## Unknowns, biggest first

1. Whether a Gunyah watchdog bite keeps the SDAM reason. Wrong answer means Android formats the root.
2. Whether the eUSB2 PHY and PMIV0104 repeater are usable as ABL leaves them, and whether EE 0 owns the repeater. This is the likely gadget blocker.
3. What SID 0x40's SMR looks like after ABL.
4. Whether the panic path with `debug=0xc` reaches our hook.
5. Whether the phone charges during long hands-free runs with the ADSP down.
6. Whether `sweetshot`, `sweettouch` and `sweetopen` work on iOS 26 on this kernel. `kern.sweet_fb_shot` was built for the older display paths, and patches 2045-2048 changed the framebuffer.
7. What caused the SMR 7 abort.

## Prepared patches

See `plans/usb/README.md` for what each does and how to apply it. Hashes are in `plans/usb/SHA256SUMS`. All are unapplied and untested on a phone.

## Sources

Local: `ios-asteroids/NOTES.md`, `analysis/usb-plan.md`, patches 2001, 2004, 2043, `research/asteroids-hw/`, `research/cam/usbprobe*.jpg`, `asteroids/out/ast-usbprobe*-boot.img`, `usb-work/clean` (reconstructed tree through 2043, and patches 2044-2049 don't touch these files). `analysis/daily-driver-gaps.md`, `analysis/research-wifi.md`.

Fresh: `rootstockos/UniXNU-asteroids` at `dbddb09` (2026-11-01): no USB code, `package/BootWatchdogPolicy.c` pets the Gunyah watchdog, `package/SmmuBypass.h`. Mainline Linux master: `drivers/clk/qcom/gcc-milos.c`, `drivers/watchdog/gunyah_wdt.c`, `arch/arm64/boot/dts/qcom/milos.dtsi`. I found HakiOS repositories (`oosexgang/hakios-*`) by search and used only what `research-wifi.md` reports from them, so HakiOS's USB state is unread.
