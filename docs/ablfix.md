# Load Error after 18:05: cause and recovery

2026-11-02. Nothing here ran on the phone. Files are in `plans/ablfix/`. Labels and address method as in `analysis/bootslot.md`. Disassembly: `plans/ablfix/abl.dis` from `backup-asteroids/abl_a.img`.

## Cause

At 18:05 `fastboot flash boot_a recovery-probe-boot.img` wrote 22171648 bytes. That overwrote the signed boot vbmeta that sits inside `boot_a` at offset 14118912. The AVB footer in the last 64 bytes of `boot_a` was not touched and still points there. Every load since then reads that footer, finds random bytes where `AVB0` should be, and stops with `Load Error`. Reflashing the 8 MB iOS image does not repair it, because fastboot writes only the image's own length.

Evidence:

1. `vbmeta_a` chains `boot` (rollback location 3) and `recovery` (location 1). It has hash descriptors only for `dtbo` and `vendor_boot`. `init_boot` is a hash descriptor in `vbmeta_system_a`. Proven with `avbinfo.py`.
2. Stock `boot_a` footer: vbmeta at 14118912, 2496 bytes. Our images: `next-flash-boot.img` 8032256 bytes, below it. `recovery-probe-boot.img` 22171648 bytes, past it, and its byte 14118912 is `c943d761`, not `AVB0`. Proven.
3. libavb follows every chain descriptor whether or not the partition was requested, and reads the footer and vbmeta from flash, also for `fastboot boot` (Upstream `avb_slot_verify.c` chain loop, `avb_ops.c` `AvbReadFromPartition`). The stock binary has the same strings and the read helper at 0x3d094.
4. A bad vbmeta header gives result 6 (0x3f9e8). LoadImageAndAuthVB2 continues on an unlocked phone only for results 0, 3, 4 and 5 (mask 0x39, 0x352b8 to 0x352cc). Anything else goes to 0x34cf4, which sets boot state RED and calls 0x18990(0), the slot-unbootable path. Proven.
5. `fastboot boot` calls LoadImageAndAuth at 0x63f18 (0x3390c) and prints `Failed to load/authenticate boot image: %r` at 0x63f28. `%r` of `EFI_LOAD_ERROR` is `Load Error`. Proven.
6. Step 1 shows fastboot does not erase the rest of the partition. If it did, the footer would be gone, libavb would read vbmeta at offset 0 (our boot header) and fail the same way at 16:49.

The same mechanism explains step 4. `orangefox-ram.img` is 62459904 bytes and stock recovery's vbmeta is at 48357376, so flashing it into `recovery_a` broke the `recovery` chain until the 17:33 restore. The 18:04 success fits: at that moment `boot_a` held the 8 MB image and `recovery_a` was stock again.

The backup `recovery_a.img` is not the OrangeFox image padded with zeros. The first 62459904 bytes differ. It has `AVB0` at 48357376, so the restore is correct.

## What was ruled out

- `init_boot_a` restore. The backup dates from 2026-11-01 15:39, before any of today's work, and covers the full 8 MiB including the footer. Load Error already happened at 18:09 with the empty `init_boot`, before the restore. With the empty one the flag at `Info+1116` is clear and `init_boot` is not requested (0x3523c). Proven.
- Step 3 Android first stage. Nothing it can write (misc BCB, metadata snapshot state, devinfo) is read on this path. A higher stored rollback index would give result 4, which an unlocked phone ignores. Inferred, since I have no log of that boot.
- `vendor_boot_a` and `dtbo_a`. Never written.

## Other Load Error paths in LoadImageAndAuthVB2

All of them go to the same exit at 0x34cf4:

- avb_slot_verify result 1 (OOM), 2 (IO), 6 (metadata), 7 (version), 8 (argument). With the phone unlocked, each requested hash partition is read whole, so the sizes are 100 MiB each for `boot`, `vendor_boot` and `recovery`, 24 MiB for `dtbo` and 8 MiB for `init_boot`.
- SlotData NULL (0x352ac).
- Fewer partitions loaded than requested (0x35790).
- The TZ rollback syscall failing (0x34ce8).
- The boot header failing to load (0x35410).

Requested partitions: `boot` unless already in RAM, `dtbo`, `vendor_boot`, `recovery` in recovery mode while `recovery` has no kernel (0x3522c), `init_boot` when Info+1116 is set and the header is v4 or later (0x3523c), and `pvmfw` when Info+1117 is set.

## Recovery

`plans/ablfix/boot_a-ios-fixed.img`, sha256 `d222ba81...afbaa99`, is the full 100663296-byte partition. It holds `next-flash-boot.img` at offset 0, zeros, then the stock `boot_a` tail from 14118912: the signed vbmeta and the footer. It contains no stock kernel. Built by `make_fixed_boot.py`, which asserts each part.

1. `fastboot getvar current-slot`, `fastboot getvar slot-unbootable:a`, `fastboot getvar slot-unbootable:b`. Read-only, for the record.
2. `fastboot flash boot_a plans/ablfix/boot_a-ios-fixed.img`. This is the same write as at 16:49 plus the restored tail. It also resets the retry count to 7 and clears unbootable.
3. `fastboot boot next-fix/boot/next-flash-boot.img`. RAM only, so it costs no retry. Success means iOS starts. If it still says Load Error, stop and run `fastboot oem dmesg`. That command exists in this build, but I did not check that it prints the ABL log.
4. Return to fastboot with Power + Volume Down, then choose START. Expect iOS.

## Rule for later builds

Any image flashed to `boot_a` or `boot_b` must end below 14118912 bytes, or must carry the vbmeta and footer itself. For `recovery_a` the limit is 48357376. `make_fixed_boot.py` refuses an image that is too long.

The 22 MB recovery shim cannot use the stock offset. It could copy the stock vbmeta blob to just after its own data and write a new footer pointing there. The footer is not signed, and the vbmeta's own signature and key stay valid. The hash of `boot` will not match, and an unlocked phone allows that. Inferred, not tested.
