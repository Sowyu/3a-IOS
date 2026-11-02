# Recovery mode through the shim

2026-11-02. Nothing here ran on the phone. I changed no source tree and committed nothing. All files are in `plans/recovery/`.

**Workable on paper, no blocker found.** The shim can tell recovery mode from normal mode with no real doubt, and the real stock kernel ran from the shim's address under QEMU with the real OrangeFox initrd. What only the phone can answer is listed under "Unknown".

Labels: **Proven** means I read it in the stock binary or a backup, or ran it on the Mac. **Upstream** means Qualcomm source only (`clo/la/abl/tianocore/edk2`, commit `9b3a9f1`). **Inferred** means reasoning with no run behind it. **Unknown** means only the phone can answer. Addresses are PE addresses in the unpacked `backup-asteroids/abl_a.img`, using the method in `analysis/bootslot.md`.

## Two things in the brief need correcting

### Attempt (a) did not fail from `force_normal_boot`

This ABL build replaces the boot image's ramdisk with stock `init_boot` (Proven). `LoadImageAndAuth` sets a flag at `Info+1116` when `init_boot` has a v4 header and a nonzero ramdisk (0x33c6c to 0x33cc4). `BootLinux` then overwrites the ramdisk size at 0x730c and the source pointer at 0x73e4 and 0x8170, in every mode. Upstream at the same commit does not have this. It also explains the comment in `loader.c` that ABL ignores the boot image's ramdisk.

So `orangefox-ram.img` never delivered OrangeFox. It ran the stock kernel on vendor ramdisk + Android's `init_boot`, which is a normal Android first stage. That fits "no USB, then reset" (Inferred, I have no log of that boot). The ban on that route stands, for a stronger reason: **any `fastboot boot` of a real kernel is an Android boot while `init_boot_a` is stock.**

`androidboot.force_normal_boot=1` cannot appear on this phone (Proven at 0x13ce8 to 0x13d80, and the default byte at 0x901d8 is `0`):

| Mode | Recovery flag | `recovery` has no kernel | Bootconfig gets |
| --- | --- | --- | --- |
| Recovery | 1 | 1 | No key |
| Normal, `recovery_a` kernel-less (today) | 0 | 1 | `force_normal_boot=0` |
| Normal, `recovery_a` has a kernel | 0 | 0 | No key. The `1` is written to the byte but never added, because the phone has slots |

Absence of the key therefore does not prove recovery mode, so the shim does not rely on it alone.

### `live.dtb` is a recovery-mode capture

It does not show normal mode. Its initrd is 59528168 bytes, which is exactly vendor ramdisk 8612045 + OrangeFox ramdisk 48350666 + `init_boot` 2564096 (rounded up to a page) + bootconfig 1361 (Proven by arithmetic). ABL's own formula then gives `initrd-start` 0xb473a000 with kernel end 0xb8000000, the value in the file, and `iomem.txt` shows the DTB at 0xb4539000, 2 MiB + 4 KiB below it. `NOTES.md` says these dumps came from Lineage recovery. The sizes match this OrangeFox image to the byte, so I trust the sizes. It makes `live.dtb` the right positive fixture. The test has to build the normal-mode case itself.

## What ABL gives the kernel

The initrd in recovery mode, in order (Proven at 0x8290 to 0x8450, confirmed by the sum above): vendor ramdisk, recovery ramdisk, `init_boot` ramdisk, vendor bootconfig (175 bytes), ABL's bootconfig, trailer. The kernel's cpio unpack lets later files win, so Android's `/init` overlays OrangeFox's, as it always has.

In normal mode the recovery ramdisk is missing, so the initrd is about 11.2 MiB (0xaa8e00 or so) against 59.5 MiB.

`androidboot.*` keys for a v4 header go only to bootconfig, not `bootargs` (Upstream `UpdateCmdLine.c`, Proven by `cmdline.txt` having none). `androidboot.mode` appears only for charger and FFBM (Proven at 0x144a0). No mode string says "recovery". `skip_initramfs` is not added on this phone, because dynamic partitions are on (Proven at 0x13474).

**The DTB is ABL's own and fits Linux.** The DTB appended in `build.sh` is ignored (Proven at 0x7d30: for header v3 and later the DTB offset is computed from `vendor_boot`, and Upstream `BootLinux.c:502` matches). ABL picks the Volcano DTB out of 13 in `vendor_boot`, applies `dtbo`, and that is what arrives in `x0`. `splash_region` is in no `vendor_boot` DTB, so the overlay adds it, and the comment in `build.sh` about the appended DTB is wrong. I left it. So in recovery mode the shim receives exactly what the stock kernel gets from ABL. The kernel, `vendor_boot` and `dtbo` are unchanged, so it fits.

## How the shim decides

`recovery_check` in `recovery.c` returns yes only if all of these hold. Any failure boots iOS.

1. The DTB pointer is aligned and in the RAM bank, with a valid FDT header and a complete structure walk.
2. `/chosen` has 64-bit `linux,initrd-start` and `linux,initrd-end`, and `bootargs`.
3. `bootargs` has no `androidboot.force_normal_boot`, `androidboot.mode` or `skip_initramfs`.
4. The bootconfig starts exactly at start + vendor + recovery + `init_boot`, and is at most 16 KiB. A normal boot stops here.
5. The bootconfig trailer has the right magic, size and checksum.
6. Bootconfig has neither `force_normal_boot` at any value nor `androidboot.mode`, and has `androidboot.hardware=qcom`.
7. A 64-bit FNV hash of the vendor and recovery ramdisks in memory matches the build-time value.

Why Android cannot start this way: step 7 proves the OrangeFox ramdisk is in the initrd, so `/system/bin/recovery` exists, and first-stage init stays in recovery unless it sees `force_normal_boot=1`, which step 6 refuses. A stale ramdisk left in RAM does not count, because step 4 checks the size from this boot's `/chosen`. The hash identifies the ramdisk and is no defence against a crafted collision, which is outside this threat.

The cost of a normal boot is one FDT walk. The hash runs only when steps 1 to 6 pass, and reads 57 MiB with caches off, about 1 second by my estimate (Inferred).

## The jump

The check runs first in `shim_main`, before any console or UFS code. `start.S` has only masked interrupts, set the stack and vectors, and cleared the shim's own `.bss`. UFS, the panel and the SMMU are exactly as ABL left them, so the question of `ufs.c` leaving a bad state does not arise.

The Image (35633664 bytes, `image_size` 0x22a0000, `text_offset` 0, flags 0xa, so place-anywhere) sits raw behind the payload. It is copied to the next 2 MiB boundary behind the shim. For the current payload the shim is at 0xa8080000 with `image_size` 0x36f55f0, so Linux runs at 0xab800000 and ends at 0xadaa0000, about 106 MiB below the DTB (Inferred from ABL's stock kernel load: `iomem.txt` has kernel code at 0xa8090000). A runtime check refuses a target that reaches the DTB or initrd.

ABL loads the stock kernel at 0xa8080000, which is not 2 MiB aligned, and that works. I used 2 MiB as `booting.rst` asks. `x0` = DTB, `x1`-`x3` = 0, MMU and D-cache off, `ic iallu` before the jump.

## ABL's checks on the combined image

All Proven against the stock code, and `check-abl.py` checks each one on the built image.

| Check | Value |
| --- | --- |
| Partition, 100 MiB | 22171648 bytes |
| Header | v4, no ramdisk, DTB behind the gzip stream, as today |
| arm64 magic at 0x7890 | Present |
| `image_size` below `DeviceTreeLoadAddr` - load address at 0x79e8 | 0x36f55f0 against 0xc4b9000 in recovery, more in normal mode |
| Decompress output limit at 0x78e8 | Same limit |

Boot image size grows from 8 MB to 22 MB. Normal boots gunzip another 35 MiB in ABL, which I have not measured. `flash` of `boot_a` resets the retry count to 7 as before. A recovery boot uses no retry (`analysis/bootslot.md`).

## What ran on the Mac

`check-recovery.sh` passes, log in `check.log`:

- 40 host cases on the real `live.dtb` and real ramdisks: the positive case, a normal boot with OrangeFox still in RAM, `force_normal_boot` at 0 and 1, charger, bit flips in each ramdisk, Android's ramdisk in place of OrangeFox, broken trailers, wild and wrapping addresses, and a damaged FDT. 4000 random DTB corruptions made no read outside RAM and never changed the initrd used.
- The plain build still works and has no recovery code in it.
- QEMU with a mock Linux: entered at a 2 MiB boundary, `x0` = DTB, `x1`-`x3` = 0, MMU and D-cache off, interrupts masked, EL1.
- QEMU normal boot: never reached Linux and took the iOS path (blue failure screen, because QEMU has no UFS).
- QEMU full chain with the real shim, real stock kernel and real initrd: `Load bootconfig: 1341 bytes`, `Freeing initrd memory`, `Run /init`, `init first stage started`, Nothing's own modules loading. It stops there because QEMU has no phone hardware.

The bootconfig text in the fixture is my fill, since the real one holds the serial number. Its format follows 0x54eb0 and the kernel accepted it.

## Unknown

- Whether the bootconfig really has no `androidboot.mode` or `force_normal_boot` in recovery mode, and the real checksum. A surprise here falls to iOS, and the probe shows it before anything starts.
- Whether the stock kernel is happy at 0xab800000 on real hardware. Gunyah gives no sign of caring: XNU runs from 0x8c0000000 through this shim. QEMU ran it fine.
- Why attempt (b) returned to fastboot. I did not trace it. A kernel in `recovery_a` takes a different ABL path with an AVB check on `recovery` (Upstream `VerifiedBoot.c:1228`), and this design does not use that path.

## Next best option, if the shim route fails

Flash `init_boot_a` with a valid v4 header and zero ramdisk. The flag at `Info+1116` then stays clear (Proven at 0x33c9c to 0x33cc4), so ABL uses the boot image's ramdisk, and `fastboot boot recovery/ramboot/orangefox-ram.img` delivers OrangeFox from RAM. It takes no shim code and no extra boot size. Three costs:

- It writes one more partition, restored from `backup-asteroids/init_boot_a.img`.
- Stock Android on slot a stops booting, which is the goal anyway.
- It conflicts with the shim route, because the shim's sizes include `init_boot`.

OrangeFox's own `/init` would run instead of Android's, and that has never been tried on this phone.

Reading logs from iOS instead, with a helper in the root, stays third. It cannot help when iOS fails to boot, which is when logs matter most.

## Test

About 10 minutes. Stop at the first step that differs, and photograph the screen. If you built with another payload, use it: `plans/recovery/build-recovery.sh PAYLOAD`. The images in `out/` use `next-fix/boot/next-flash.payload`, the payload in the newest shim build (the one in `ios-sweet/asteroids/shim/out/Image`). I did not confirm it is the one on your phone.

Start at the fastboot menu, phone charged.

1. Run `fastboot boot plans/recovery/out/recovery-probe-boot.img`. Expect a blue screen: `PROBE boots iOS`, `WHY initrd size is not recovery`, `SIZE` near 0xAA8E00. This is RAM only and writes nothing.
2. Hold Power + Volume Down to return to fastboot. Run `fastboot flash boot_a plans/recovery/out/recovery-probe-boot.img`.
3. Run `fastboot reboot recovery`. Expect `PROBE recovery mode: would start Linux`, `WHY all checks passed`, `SIZE` 0x38C53E8, `HASH` equal to `EXPECT`, `TARGET` 0xAB800000, `FIT ok`. Nothing has started yet.
4. Hold Power + Volume Down. Run `fastboot boot plans/recovery/out/recovery-boot.img`. Expect iOS to boot normally. This is the safety test of the real image.
5. Enter fastboot again. Run `fastboot flash boot_a plans/recovery/out/recovery-boot.img`, then `fastboot reboot recovery`. Success is the OrangeFox screen and `adb devices` listing the phone in recovery. Allow about 1 second more than before.
6. In OrangeFox choose Reboot, then System. Expect iOS. Then power cycle and expect iOS again.

Leave `boot_b` alone until this has worked. On slot b the shim's checks would fail and it would boot iOS.

A leftover `boot-recovery` command in `misc` is possible. ABL writes it on `fastboot reboot recovery` (0x65c60) and only the recovery clears it (Upstream `Recovery.c`, Inferred for OrangeFox). The old shim hid it by booting iOS regardless. If a plain boot lands in OrangeFox after step 5, that is why, and it is not a failure. Rebooting from OrangeFox should clear it. If it keeps returning, the first 4 KiB of `misc` in `backup-asteroids/misc.img` are all zero, so `adb shell dd if=/dev/zero of=/dev/block/by-name/misc bs=4096 count=1` restores that state.

## If it fails

Every failure below has one recovery: hold Power + Volume Down until the fastboot menu appears, then flash your previous iOS image, for example `fastboot flash boot_a next-fix/boot/next-flash-boot.img` if that is the one you use. This writes `boot_a` only. Restore the same way if a step above differs.

| What you see | Meaning | Do |
| --- | --- | --- |
| Step 1 says `recovery mode` | Detection is wrong. Nothing started | Stop, do not flash |
| Step 3 says `boots iOS` | `WHY` names the check | Send the photo, do not continue |
| Step 3 shows `HASH` different | A partition differs from the backup | Rebuild against a fresh dump of it |
| Step 5 shows iOS | The shim fell back. Run the probe again | Compare `WHY` |
| Step 5 black or frozen | Linux hung or crashed on real hardware | Restore, then try the next best option |
| Step 5 resets repeatedly | The kernel panics | Restore. Nothing was written to `userdata` by the shim |

Never choose "Boot to Alternate Slot" at the menu, and never `fastboot boot` a stock kernel image.

## Risks

- OrangeFox can write to `userdata` if told to. It runs as it did before iOS was flashed, so keep the same care there.
- Any reflash of `recovery_a`, `vendor_boot_a` or `init_boot_a` changes the hash, so the shim boots iOS until rebuilt. That is the safe direction, but updating OrangeFox now means rebuilding the boot image.
- The hash and sizes come from the backups, so a phone whose `vendor_boot_a` was changed since 2026-11-01 shows a mismatch at step 3.
- Stock kernel modules write to Nothing's log partitions in OrangeFox as they always did. `NT_reserve_kernel_log` appeared in the QEMU log.

## Files

- `plans/recovery/01-shim-recovery.patch` applies in `ios-sweet` with `git apply`. It adds `recovery.c` and `recovery-defs.py`, and changes `build.sh`, `loader.c` and `start.S`. With no `SHIM_RECOVERY_*` variables the build is unchanged in behaviour.
- `plans/recovery/build-recovery.sh PAYLOAD` makes both images in a scratch copy.
- `plans/recovery/out/recovery-probe-boot.img` and `recovery-boot.img` are built from the current payload.
- `plans/recovery/check-recovery.sh`, `check-abl.py` and `test/` are the Mac checks, and `check.log` is the last run.
