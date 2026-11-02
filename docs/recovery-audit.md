# NO-GO for steps 3 to 6

The blocker is an unchecked recovery command in live `misc`. A successful recovery-mode probe proves the ramdisk checks pass, but says nothing about whether OrangeFox will automatically wipe `userdata`. Installing the real shim makes that possible on START too if ABL still sees `boot-recovery`.

I found no ordinary normal-mode path through this shim to Linux with the current ABL, partition layouts and fresh DTB. I verified both relocated artifacts locally. The relocation itself has no identified blocker. This is a conditional boot-path assessment, not a guarantee that arbitrary `fastboot boot` images are safe.

Read-only review on 2026-11-02. Only this report was written. No phone access, adb, fastboot, builds, test compilation, commits or source edits. I used the review and unslop skills. Previously recorded QEMU and host results were read, not rerun.

## Blocker

1. Live BCB contents are unknown and recovery can process a wipe before a shell is available.

   References: `plans/recovery/01-shim-recovery.patch:343`, `:388`, `:461`; `analysis/recovery.md:124`, `:143`.

   Concrete path: `misc` contains a recovery request whose recovery argument area includes `recovery\n--wipe_data\n`. ABL selects recovery, supplies the authentic OrangeFox initrd, and every shim check passes. Linux starts OrangeFox, which consumes the argument and schedules a data wipe or format without the owner choosing Wipe in the UI. This also applies to a menu START or power-on redirected into recovery by the BCB. Other stale update/script commands can make recovery write partitions too.

   This is supported by the actual backed-up ramdisk, not just a generic Android warning. Its `/system/bin/recovery` contains `--wipe_data`, `Startup Commands`, `Received OpenRecovery` and `/cache/recovery/command` strings. Its init scripts start `/system/bin/recovery` normally. OrangeFox's upstream `startupArgs.cpp:24-32,76-84` converts wipe arguments into OpenRecoveryScript commands, including a possible format-data build option. `twrp.cpp:529-530` parses arguments before `Clear_Bootloader_Message` at line 622, and `twrp.cpp:275-281` runs pending scripts. Clearing the BCB during recovery startup therefore does not retract a wipe already converted into a script. Exact build identity and conditional options of this unofficial binary remain unverified. Sources: [startup argument handling](https://gitlab.com/OrangeFox/bootable/Recovery/-/blob/fox_12.1/startupArgs.cpp), [recovery startup](https://gitlab.com/OrangeFox/bootable/Recovery/-/blob/fox_12.1/twrp.cpp).

   The backup `backup-asteroids/misc.img` has a zero first 4096 bytes. That establishes the old backup state only. Today's live BCB is unknown. A matching ramdisk hash does not hash `misc`, and bootconfig checksum validation does not inspect recovery commands.

   Required before a real Linux jump: inspect and neutralise live recovery commands through an independently safe path, or arrange a reviewed shell-only recovery startup that prevents the recovery executable and automatic scripts from running until inspection finishes. Merely booting ordinary OrangeFox and racing to `adb shell` is unsafe because recovery can act first. If neither safe path exists, remain NO-GO. Any later BCB repair should preserve unrelated `misc` state and use a verified current backup and targeted update. Do not blindly restore the whole old partition or assume zeroing its first 4 KiB preserves all boot metadata.

## Should-fixes

2. Test the negative probe path before replacing the probe with the real shim.

   References: `analysis/recovery.md:115-120`; `plans/recovery/test/test_recovery.c:131`; `plans/recovery/01-shim-recovery.patch:431-447`.

   Only the positive recovery probe has run on the phone. The intended step 3 RAM-boots the old iOS shim, which never exercises the new recovery gate, then step 4 exercises that gate for the first time with a live Linux jump enabled. Run the existing probe in RAM and through START first, while it still cannot launch Linux. A recovery answer on START must be explained, including BCB redirection, before enabling the real image. A power cycle and warm return from the positive probe should also produce the expected decision for the mode ABL actually selected. This closes a validation gap, not a demonstrated code bypass.

3. Repair instructions must restore the full partition, including its footer.

   References: `analysis/recovery.md:128`; `plans/ablfix/make_relocated.py:17-24`; `analysis/ablfix.md:42-47`.

   Reflashing only the 8 MB `next-flash-boot.img` over the relocated partition leaves the relocated footer and vbmeta in place. That may remain bootable if the tail survived, but is not a known full repair and does not cure a damaged relocated vbmeta/footer. Use `plans/ablfix/boot_a-ios-fixed.img` as the repair image. The old recovery document also still recommends flashing the bare 22 MB images at lines 116 and 119, the operation that already destroyed the stock vbmeta. Those instructions must be superseded before anyone follows them.

4. Tighten the claims about the checked ramdisk bytes.

   References: `plans/recovery/01-shim-recovery.patch:157-164`, `:332-338`, `:388`; `analysis/recovery.md:144`.

   The word-wise FNV hash covers 56,962,704 of the 56,962,711 vendor-plus-recovery bytes. The last seven bytes, `4f500000000000`, are ignored. It hashes none of the 2,564,096-byte rounded `init_boot` section. Thus changing `init_boot` at the same size does not necessarily cause fallback, contrary to the document. Today's backed-up `init_boot` contains stock first-stage `/init` and no `/system/bin/recovery` replacement. A future same-size init_boot can change the code that runs after the hash passes. Hash all intended identity bytes and include init_boot identity, or explicitly constrain the guarantee to the current known init_boot. The missing tail is not a demonstrated normal-mode bypass for today's image.

5. Reject ambiguous DTBs and bootconfig forms before broadening the input guarantee.

   References: `plans/recovery/01-shim-recovery.patch:250-326`, `:352-385`, `:395-410`.

   The FDT walker accepts duplicate chosen properties with last-value-wins semantics, does not require a single empty-named root, and does not require a NUL-terminated bootargs value. A Linux parser can select a different duplicate property from the shim. Bootconfig is checked by substring, not parsed: a nested `androidboot { force_normal_boot = 1; }` form can evade the contiguous-name rejection while the kernel normalises the key for init. A separate `androidboot.hardware=qcom` string can still satisfy the positive substring test. Unexpected `rdinit=` or `init=` overrides are also not refused. These are concrete examples of insufficient validation for arbitrary inputs, but I found no evidence today's ABL generates them, so they are should-fixes rather than current blockers.

   Runtime Linux-source offsets are trusted from the built header without checking `linux_off <= image_size` and `linux_size <= image_size - linux_off` before dereferencing. The shipped artifacts satisfy those relations. Harden this if the check is intended to cover damaged or independently supplied shim images. Add targeted cases for these boundaries when changing the gate. Do not treat the existing random-DTB test as proof that Linux and the shim interpret every accepted DTB identically.

## Normal boot and stale RAM

`recovery_check` is called before UFS preparation or console work. The entry code saves ABL's x0 DTB in x2 after clearing BSS, then supplies it to `shim_main`. No intervening instruction overwrites x0. A failed check returns to the iOS path. The probe always stops, even when its screen says “boots iOS”.

The DTB address must be 8-byte aligned and its header and complete declared extent must lie in `[0xa7000000,0xe0600000)`. Total size is capped at 2 MiB. Structure and string offsets are checked with subtraction before use. Tags, property lengths and names are read inside their declared blocks. Padded structure positions can move past the block end but are not dereferenced there. No out-of-bank read caused by FDT offset arithmetic was found. The semantic limitations are finding 5.

For today's constants, `bc_start = initrd_start + 56,962,711 + 2,564,096`. The start/end range check precedes every initrd dereference, so computing bc_start earlier cannot turn an out-of-bank or wrapping input into a valid one. Passing requires 21 to 16,384 bytes after that fixed boundary, the 12-byte trailer magic, exact trailer size and matching additive checksum. The bounded sum is safe. The hash then reads aligned words wholly within the admitted initrd range. It is a custom word-wise FNV variant shared by the generator and checker, not standard byte-wise FNV-1a or cryptographic authentication. I recomputed `0xf927f52e7b05c515` from the backups.

Stock ABL's ramdisk copy has an explicit recovery gate at `plans/ablfix/abl.dis:7338`, address `0x82a4`, and further gates for kernel-less recovery and nonzero recovery ramdisk. Normal mode skips that recovery copy. It assembles vendor plus init_boot, roughly 11.2 MB, against roughly 59.5 MB in recovery. The size equation rejects normal mode before hashing. ABL supplies fresh chosen start/end values for the assembled initrd. Old OrangeFox bytes elsewhere in RAM cannot enlarge the current initrd. A warm reset that actually follows ABL's normal path therefore does not pass merely because recovery bytes survived.

Power-on, START, hard reset and panic reset follow that reasoning if ABL selects normal mode. A `boot-recovery` BCB changes the selected mode: the Linux jump is then expected even if the owner pressed START. This is the blocker’s path. Hardware reset persistence of the entire ABL state was not tested. The 18:27 probe confirms the positive recovery path only.

`fastboot boot` needs a narrower answer. RAM-booting this shim still applies these checks. RAM-booting the old iOS shim never calls this Linux gate. RAM-booting an arbitrary stock Linux image bypasses the gate entirely and can start Android with stock init_boot. No shim installed in boot_a can make “fastboot boot of anything” safe. The relocated footer does not change this.

I unpacked the three backed-up LZ4 cpio ramdisks in memory. OrangeFox supplies `/system/bin/recovery` and recovery init scripts. Stock init_boot overlays `/init` but does not replace that recovery executable. In the expected bootconfig, absence of force_normal_boot and presence of the recovery executable retain recovery mode. This follows [Android's documented generic boot flow](https://source.android.com/docs/core/architecture/partitions/generic-boot). It does not neutralise recovery's own pending wipe commands.

## Linux jump and persistent writes

Both artifacts decompress to 57,628,144 bytes, header `image_size=0x36f55f0`, at assumed ABL base `0xa8080000`. The embedded Linux starts at offset `0x14f9bf0`, is 35,633,664 bytes, and exactly matches the decompressed `recovery/ramboot/stock.kernel`. Its header has `text_offset=0`, reserved runtime size `0x22a0000`, flags `0xa`.

The shim ends at `0xab7755f0`. Rounding up places Linux at `0xab800000`, with its reserved Image extent ending at `0xadaa0000`. This lies beyond the shim and its source payload, below DTB `0xb4539000` and initrd `0xb473a000`, and in the checked RAM bank. Copying forward does not overlap source, executing shim, BSS or stack. Linux file size is divisible by eight, so `copy64` does not overrun this artifact. The runtime check reserves the larger Linux Image extent, not only the copied bytes.

ABL decompresses the outer gzip. Linux is already a raw Image when copied, so there is no second Linux gzip expansion into an unaccounted destination. The outer decompressed size fits ABL's DTB-derived output limit. I also decoded the stock kernel’s embedded IKCONFIG in memory. It has CONFIG_RELOCATABLE=y, CONFIG_BOOT_CONFIG=y, no embedded bootconfig, an empty CONFIG_INITRAMFS_SOURCE, and CONFIG_CMDLINE_EXTEND rather than FORCE. Its built-in command line includes bootconfig and no force_normal_boot, rdinit or init override. The two Android boot image header command lines are empty. The appended gzip-tail DTB is not the argument passed to Linux. The code retains ABL's live x0 DTB, including its selected vendor DTB and overlays.

At the Linux call, x0 is that DTB and x1-x3 are zero. DAIF is masked by start.S. `dsb sy; ic iallu; dsb sy; isb` orders the copy and invalidates instructions. The code assumes ABL entered with MMU and D-cache off at EL1. It does not disable or clean an enabled D-cache itself. The saved QEMU mock checks those initial conditions under a synthetic entry stub. They do not measure real SCTLR or Gunyah restrictions on this phone. No new barrier, argument or alignment defect was found under the documented ABL entry contract.

The shim's new writes are to RAM, including BSS, stack, hash diagnostic and the Linux copy. None is a UFS command or RPMB write. An incorrect target or cache state can corrupt RAM, fault, hang or panic. That alone is not a mechanism for permanently overwriting UFS. Once Linux runs, DMA, storage drivers, firmware services and OrangeFox have persistent-write capabilities. Recovery logging already writes vendor log partitions, and a queued wipe can destroy userdata. A later hang cannot prove that nothing persistent was written first. I cannot guarantee that every conceivable kernel corruption is only a hang or rule out all driver-mediated damage.

## Relocated vbmeta and ABL side effects

`make_relocated.py:12-24` uses the correct 64-byte footer layout. Magic is at 0, big-endian version words at 4 and 8, and big-endian 64-bit fields at 12, 20 and 28. It preserves version 1.0 and reserved bytes. The artifacts have original_image_size 22,171,648, vbmeta_offset 22,171,648 and vbmeta_size 2,496. The vbmeta starts at a 4096-byte boundary with ample separation from the final footer. This matches [libavb's footer definition](https://android.googlesource.com/platform/external/avb/+/refs/heads/main/libavb/avb_footer.h).

The script sets original_image_size to the aligned vbmeta position. Generally that field should describe the pre-AVB image length rather than new padding. Here both inputs are already 4096-aligned, so `new == len(d)` and the value is exact. libavb uses the footer’s offset/size to locate vbmeta, not original_image_size as the boot hash length. See [the verifier](https://android.googlesource.com/platform/external/avb/+/refs/heads/main/libavb/avb_slot_verify.c).

I verified the files are each exactly 100,663,296 bytes. Despite the “100 MiB” description in the notes, that is 96 MiB. Each begins with its corresponding out/ image byte-for-byte and contains the stock signed vbmeta byte-for-byte at the new position. Remaining padding is zero. Probe SHA256 is `3fa423c8658e3b24d3262200917c6567f96b518d86df4c2eacc9e22d2ce2a2e9`. Real SHA256 is `9cb0edf5f3a88c8875bd524b6d494181066224dc56c0473b916dbe2eefad1ff8`.

The boot hash descriptor inside the chained boot vbmeta still says image_size 14,118,912. That covers the first 14,118,912 bytes of the replacement image, not the relocated vbmeta. The hash mismatch is expected. The footer is unsigned, while the moved vbmeta’s signature, key and rollback index remain unchanged. The unchanged signature does not authenticate the replacement Linux or all 22 MB of image bytes. This relies on the device remaining unlocked.

Stock ABL permits AVB results 0, 3, 4 and 5 on the unlocked path, with non-null SlotData, at `plans/ablfix/abl.dis:53345`, addresses `0x352b8-0x352cc`. Those mean OK, verification error, rollback-index error and public-key rejection. Metadata error 6 and other failures can mark the slot unbootable through `0x34d28-0x34d2c`. The 18:27 positive probe is direct evidence that the relocated probe was accepted. Both relocation structures match, so the real image has no distinct footer defect identified.

“ABL cannot write anything” would be false. Its ordinary flash and boot flows update slot attributes. Its AVB rollback-write callback at `0x3e074` can call the persistent index writer `0x1c880` at `0x3e150`, conditional on the slot-status predicate at `0x3c708`. That predicate tests successful and unbootable GPT bits through `0x17a64`. The TZ rollback syscall path also exists around `0x34c98-0x34ce8`. The shim relocation does not remove those paths, and I have not proven the complete callback reachability or secure-world storage behaviour for this boot.

The moved boot vbmeta still declares rollback index 1,775,347,200, with boot’s chain assigning location 3. Relocation does not increase it or request a new location. Nothing in the footer directly asks ABL to write RPMB or change slot selection. Flashing boot_a normally clears success/unbootable and resets retries. A normal boot can consume a retry and update GPT. Thus “unlocked accepts rollback errors” is not evidence that rollback storage is never written. I found no relocation-specific rollback advancement or permanent-brick path, but cannot certify zero secure-storage writes.

## Safer sequence after resolving the blocker

1. Keep the probe installed and establish its negative decisions for RAM boot, START and a normal warm/cold return, with BCB-directed recovery explained rather than dismissed.
2. Resolve live BCB and automatic command/script safety before allowing any real recovery execution, including an unexpected recovery decision during START.
3. Keep the probe partition intact while RAM-testing the real recovery shim itself in normal mode. RAM-booting only the old iOS image does not test the new gate. Restore full known-good boot_a immediately on any unexplained decision or load failure.
4. Flash the verified full relocated real partition, verify the old known-good iOS RAM boot, then test START, explicit Recovery mode, and OrangeFox Reboot to System. Stop at the first unexpected result. Verify the recovery exit cleared its request before expecting normal mode.
5. Recover via the hardware fastboot menu and the full `plans/ablfix/boot_a-ios-fixed.img`, then the known-good iOS RAM image. Do not use a stock Linux image as a diagnostic boot or experiment with init_boot/recovery/GPT changes during this test.

The verified repair SHA256 is `d222ba811df2499358b5ac1442c1984cb23440aba9d3d2d9b5b037806afbaa99`. It is 100,663,296 bytes. Leave boot_b and other partitions unchanged. Flashing boot_a replenishes its retry budget. Repeated START/reset attempts without repair can exhaust it. Returning to fastboot repairs a boot failure only if ABL and the partition table remain usable. It cannot undo a userdata format.

## Not verified

Live misc/BCB and command files cannot be read now. I did not verify current slot attributes, live init_boot identity, actual normal-mode ABL DTB/bootconfig, real entry cache/MMU state, or Linux operation under Gunyah at the relocated address. The positive probe does not establish these. Existing host/QEMU logs reach first-stage init, not a complete OrangeFox session with the phone’s storage.

I compared artifact structure, embedded stock kernel and backup ramdisk contents. I did not rebuild to prove every compiled instruction matches the patch, independently audit every binary recovery routine or vendor driver, or prove secure-world rollback storage semantics. There is no tested EDL recovery path here. These limits do not supply extra hypothetical blockers. The concrete NO-GO remains unchecked automatic recovery commands with a possible irreversible userdata wipe.
