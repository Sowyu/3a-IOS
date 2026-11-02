# Boot slot retry countdown: safety review

2026-11-02. Nothing here ran on a phone. I wrote no partition, changed no source tree, committed nothing and did not update `NOTES.md`, because this task limits writes to `plans/bootslot/` and this file.

Labels as in `plan-boot.md`. **Proven** means I read it in the stock binary or a backup, or ran it on the Mac. **Upstream** means Qualcomm source only. **Inferred** means reasoning with no run behind it. **Unknown** means only the phone can answer.

Addresses are PE virtual addresses in the unpacked `backup-asteroids/abl_a.img`, or runtime addresses in the XBL inner image. To reproduce: `extract_abl.py`, then `aarch64-elf-objdump -D -b binary -m aarch64 --adjust-vma=0x1000` on the PE `.text` (file offset 0x10b8, size 0x8f000), then `xref.py` or `dis_range.py`. Upstream is `git.codelinaro.org/clo/la/abl/tianocore/edk2`, branch `LA.VENDOR.14.3.4.c1`, commit `9b3a9f1`. The stock ABL matches it closely, with the differences noted below.

## Decision

Do not write the GPT. Reflash `boot_a` from the Mac when the count runs low. That costs about a minute per 7 boots and uses the ABL path this phone has already run once.

Add the on-screen count (`plans/bootslot/01-shim-show-retries.patch`) so the owner knows when. It is read-only.

Make the "Boot to Alternate Slot" menu item go away by flashing iOS to `boot_b` (`plan-boot.md` phase 2 step 6). Until then, never select that item, because it starts stock Android, which formats `userdata`.

If the phone later has to run away from the Mac for more than 7 boots, a one-time `dd` from OrangeFox is the only write I would accept. Its brick risk is low, and it is not zero: the table it changes lists `tz`, `hyp`, `aop`, `uefi` and `abl`, and this project has no EDL programmer to recover with.

## What ABL does at each boot

Attribute byte, entry offset 54, bits 48 to 55 of the attributes field. Decoded and matched against your `getvar` output (Proven by host test):

| Bit | Meaning | `boot_a` stock 0x77 | `boot_a` after flash 0x3f | `boot_b` 0xf3 |
| --- | --- | --- | --- | --- |
| 0-1 | priority | 3 | 3 | 3 |
| 2 | active | 1 | 1 | 0 |
| 3-5 | retry | 6 | 7 | 6 |
| 6 | successful | 1 | 0 | 1 |
| 7 | unbootable | 0 | 0 | 1 |

`FindBootableSlot` (0x18df8, one call site at 0x3397c in `LoadImageAndAuth`) does this. All of it is Proven from the disassembly.

1. Not unbootable and successful: boot with no decrement and no write (0x18ed4 to 0x18fe8). So `successful` does skip the decrement.
2. Not unbootable, not successful, retry above 0: decrement if all guards pass, write, boot (0x18f84, 0x19434 to 0x19460).
3. Anything else: `HandleActiveSlotUnbootable(0)` at 0x191b0.

The decrement guards are `IsABRetryCountDisabled` (0x9ed0, returns 0 in this build, so counting is on), `IsBootDevImage` (0x9ea0) and `IsABRetryCountUpdateRequired` (0xfaa4). The last returns false for the charger screen, `BootIntoFastboot` and `BootIntoRecovery`.

| Action | Costs a retry? | Status |
| --- | --- | --- |
| Power on, hard reset, panic reset into normal boot | Yes | Proven |
| Fastboot menu START | Yes. Action 1 at 0x39adc calls `RebootDevice(NORMAL)` (0x3a3a8), so ABL restarts as a normal boot | Proven. `plan-boot.md` missed this |
| Power + Volume Down, or restart into the fastboot menu | No | Proven guard. Upstream `LinuxLoader.c:105` |
| `fastboot boot IMG` (iOS or OrangeFox) | No | Proven guard |
| Recovery boot, charger screen | No | Proven guard |

ABL writes the same way every time. `UpdatePartitionAttributes` (0x15fc0) loops over every LUN, reads the 5 blocks of each copy, changes the attributes that differ from its in-memory table, recomputes both CRCs and rewrites. It does primary, then backup. Each copy goes out as one 20 KiB write. The stock binary also flushes, which upstream does not (0x168a4). For `boot_a` that is LUN 4. Both copies are written and both CRCs updated.

The only slot attribute writers I found are these, and none of them sets `successful`:

| Site | Effect on boot slot attributes |
| --- | --- |
| 0x664ac `FastbootUpdateAttr`, called after `flash` or `erase` of a partition named `boot*` (0x63adc, 0x66108) | Clears 54 and 55, ORs priority 3 and retry 7 |
| 0x18008 `SetActiveSlot`, used by `set_active` and forced alternate | Sets priority, active, retry 7. Clears 55. Clears 54 only with `ResetSuccessBit`. Lowers the other slot to priority 2 |
| 0x18a7c `HandleActiveSlotUnbootable` | Sets 55 |
| 0x18d20 `ClearUnbootable`, called by `fastboot boot` (0x63f00) | Clears 55 on the active slot |
| 0x19460 | Decrements retry |
| 0x15a38 first boot, all attributes zero | Not reachable here |
| 0x62e58 after `flash partition` | Writes whatever the new table holds |
| 0x600bc end of `set_active`, tail calls 0x18988 and 0x1a6dc | Commit changes already listed above |

## How many boots, and what happens at zero

A flash gives 7 normal boots. The 8th stops. The count now is 7 minus any normal boots since the flash, including the first boot if you left fastboot with START or a power cycle. `slot-retry-count:a` in `02-boot-slots.sh status` is exactly the number left.

| Step | Byte | Result |
| --- | --- | --- |
| After flash | 0x3f | retry 7 |
| Boots 1 to 7 | 0x37, 0x2f, 0x27, 0x1f, 0x17, 0x0f, 0x07 | iOS runs, one GPT write each |
| Boot 8 | 0x87 | `boot_a` marked unbootable, written, then fastboot |

At boot 8 the check at 0x18acc requires the other slot to be successful and not unbootable. `boot_b` is unbootable, so it returns `EFI_LOAD_ERROR` and `LinuxLoader` falls through to fastboot. So yes, with stock `boot_b` the fallback is fastboot and not Android (Proven).

What the owner sees (Proven from the menu table at 0x99058 and the code at 0x3afa4, 0x17d78): "FastBoot Mode" with the text "Your device is unbootable. It may not work properly. If needed, please press power button to force boot from alternate slot." and five entries: START, Restart bootloader, Recovery mode, Power off and **Boot to Alternate Slot**. The fifth appears only when both slots are unbootable. Upstream has neither the item nor this text.

**That fifth item is the dangerous one.** Action 8 at 0x39bd0 calls `HandleActiveSlotUnbootable(1)`, which skips the check at 0x18ac0, then `SetActiveSlot(b, FALSE)` clears `boot_b`'s unbootable flag, swaps the slot GUIDs, switches the UFS boot LUN and reboots (0x18be8, 0x186e8). Stock Android then starts from `_b`, on `_b` firmware. `plan-boot.md` records that Android formats `userdata`. Whether `_b` firmware runs correctly at all is Unknown.

The other entries are harmless at zero. START reboots and comes back to the same screen, because the state is unchanged (Inferred from the code). Recovery mode does the same, because it also goes through `FindBootableSlot` (Upstream).

### What clears the state

| Command | Effect on `boot_a` | Works at zero? | Status |
| --- | --- | --- | --- |
| `fastboot flash boot_a IMG` | 0x3f: retry 7, bootable, not successful | Yes | Proven, you ran it. Needs the battery check to pass (0x10e70) |
| `fastboot erase boot_a` | Same flags, but no kernel | Yes, and useless | Proven |
| `fastboot boot IMG` | Clears unbootable only | No. `FindBootableSlot` then sees retry 0, re-marks it and fails with `Failed to load/authenticate boot image` | Code path Proven, result Inferred |
| `fastboot boot` of OrangeFox | Same | No, so `dd` from recovery is unreachable at zero | Inferred |
| `fastboot set_active a` | 0x3f, and `boot_b` drops to priority 2 | Probably | Upstream `SetActiveSlot(…, TRUE)`, off limits, see below |

`boot_b`'s unbootable flag is cleared by flashing or erasing `boot_b`, `set_active b`, or the forced alternate action.

### Is reflashing enough?

Yes for a phone near the Mac. It is the same command, the same result, and the write is confined to `boot_a` plus ABL's own attribute update. Two limits apply. It needs the Mac, and ABL can refuse to flash on low battery, which matters because iOS 26 does not charge here (`NOTES.md`). So reflash at 2 left with a charged phone, not at 0 with a flat one. Whether ABL charges the phone at the fastboot menu is Unknown.

### `fastboot set_active`

I do not recommend it, and it gains nothing over a reflash. It also changes `boot_b`'s priority and runs the UFS boot LUN check at `SetActiveSlot`, which writes a device attribute if it finds a mismatch (Upstream `PartitionTableUpdate.c:1486`). On `a` with the boot LUN on 1 there should be no change, but I have not proven that for this phone, and a wrong boot LUN is a path to a dead phone. `02-boot-slots.sh` already refuses it, and that should stay.

## Marking the slot successful, ranked by brick risk

Every write needs the attribute byte in both copies, the array CRC at header offset 88 and the header CRC at offset 16, in both headers. `mark_successful.py check` prints the exact result on a test state: LBA 0xffffb and LBA 2 change 1 byte at 0x536, LBA 0xfffff and LBA 1 change 8 bytes at 0x10 to 0x13 and 0x58 to 0x5b. Nothing else moves. Android does the same through its boot_control HAL, so the operation itself is normal for this table.

The rule from `ios-asteroids/AGENTS.md` lines 13 to 17: "the iOS root goes into the existing `userdata` partition (no GPT changes). Keep bootloaders, recovery, modem, persist and the backups intact, and confirm each destructive step with the user before running it." That was written about the root's location, and `plan-boot.md:185` reads it as a general ban. Any attribute write needs the owner's explicit decision either way.

### What depends on this table

LUN 4 holds `uefi`, `aop`, `tz`, `hyp`, `abl`, `keymaster`, `boot`, `vbmeta`, `dtbo` and more, for both slots (`gpt-sde.bin`, 77 entries). `xbl` itself is on LUN 1 and 2, so it still starts, but it has to find the rest here.

**XBL does fall back (Proven by disassembly of the XBL loader in `xbl_a.img`).** At 0x14881b70 it validates the header at LBA 1. On failure it validates the header at the last LBA of the device (0x14881b78 to 0x14881b88). The validator at 0x14881cc0 checks signature, header CRC over 92 bytes, its own LBA, and the array CRC. If both fail it returns -1.

**Unknown:** the partition driver inside the UEFI stage, which gives ABL its partition list. It lives in a compressed section I did not unpack. Upstream EDK2 falls back and repairs. If this one does not, a bad primary could boot through XBL and then leave ABL unable to see partitions.

**Recovery of last resort does not exist here.** `oem reboot-edl` is in the string table, but no EDL programmer for this phone is in this project, so a dead LUN 4 table means a repair service.

### a. ABL marks it itself: does not exist

Proven: no code path sets bit 54 on a slot (table above). The only `orr` of bit 54 in the binary, at 0x1f4fc, is DTB match flags. The `oem` commands in the table are `debug`, `swsku`, `bmanufacture`, `fsn`, `psn`, `hwversion`, `uart`, `slubdebug`, `installment`, `show`, `dmesg`, `dmesg-clean`, `device-info`, `reboot-edl`, `off-mode-charge`, `select-display-panel`, `enable-charger-screen`, `disable-charger-screen`, `audio-framework`, `set-hw-fence-value`, `set-gpu-preemption`. None touches the slot. `misc` is used for recovery commands, FFBM and the Virtual A/B merge state, and upstream keeps slot data there only on NAND.

`fastboot flash partition:4` is an ABL-run GPT write, and I rejected it. It writes MBR, primary header, backup header, primary entries, backup entries (upstream `WriteGpt`, and the error strings at 0x178d4 to 0x17a00 in the stock binary run in that order). After the two headers, neither copy matches its entries, so a cut there leaves no valid table at all. The host test models this and catches the window. It also needs a backup GPT image this project does not have.

### d. OrangeFox `dd`, one time: lowest risk of the writes

Would it survive? Yes (Proven). Normal boots then do no attribute write at all, `MarkPtnActive` only sets the active bit, and nothing clears bit 54 except `flash` or `erase` of `boot_a`, and `set_active`. So one write lasts until the next kernel flash, which clears it again. During active kernel work that means the write has to be repeated, or the kernel has to be written from OrangeFox instead.

It has the safest conditions of the write options: USB power, a charged phone, a person watching, no iOS running, and every byte computed and checked on the Mac. It is unreachable at zero, so it must happen with at least 1 retry left.

| Cut during | State after | XBL | Recovery |
| --- | --- | --- | --- |
| Write 1, backup entries | Backup invalid, primary old and valid | Uses primary | Normal. Next ABL attribute write rewrites the backup (Upstream) |
| Write 2, backup header | Same | Uses primary | Same |
| Between writes 2 and 3 | Backup new, primary old, both valid | Uses primary | Normal. The flags disagree until ABL's next attribute write |
| Write 3, primary entries | Primary invalid, backup new and valid | Uses backup | Depends on the Unknown UEFI driver. Worst case fastboot cannot see partitions |
| Write 4, primary header | Same | Uses backup | Same |
| After write 4 | Done | Uses primary | None needed |

The primary window is two 4 KiB writes, milliseconds long. The realistic brick causes are a tool bug, the wrong device node, or flash failure, and the guards address the first two: exact GUID, every entry byte compared to stock except the one, CRCs, primary and backup identical, a block-for-block check before each write, read-back after each write, and primary untouched unless the backup verified. OrangeFox's `sde` is only assumed to be LUN 4, so the GUID check is what confirms it.

**A gap to close first:** `gpt-sde.bin` is 24576 bytes, LBA 0 to 5. **There is no backup of the backup GPT.** The test uses one synthesised from the primary. Its LBA, 0xffffb to 0xfffff, is Inferred from `AlternateLBA` 0xfffff, `LastUsableLBA` 0xffffa and the 5 block span ABL reads. Reading it needs OrangeFox and writes nothing: `adb exec-out dd if=/dev/block/sde bs=4096 skip=1048571 count=5`. Do the same for all six LUNs if you want the backups complete.

### b. Shim writes the byte: rejected

`ufs.c` has no write path, and its header comment makes that a property of the file. This option adds one that runs with the MMU off, with no Linux to test it against, at every boot, on battery, with no one watching. A check of the bit could make it one-shot in effect. The first test of the new write code would be against the live LUN 4 table.

### c. Kernel `sweet_ufs_lun_io` one-shot: rejected, worst

The function cannot do this today, which is good. It takes no LUN argument (`sweet_ufs.c:1115`, LUN 0 only), and `ufs_write_blocks_ex` refuses anything outside the root partition: "the only write path to the device". That refusal is the project's main protection against any kernel bug reaching a bootloader partition. This option has to add LUN selection and remove or widen that refusal, for the sake of one byte.

## What was checked on the Mac

`python3 -B plans/bootslot/check_mark_successful.py` ran: 300 checks passed, backups unchanged by hash.

1. Flag decoding matches the `getvar` values you reported.
2. The countdown model from the disassembly gives 7 boots and ends at 0x87, and a successful slot stays put for 100 boots.
3. For every retry 7 to 1, the plan writes backup first, changes exactly the expected bytes, leaves `boot_b` and the MBR alone, and both copies validate.
4. A simulated cut at each of the 4 writes, with 0, 512, 2048 and 4095 bytes landed, always leaves one valid copy under XBL's rules, with the slot byte old or new.
5. Refused: wrong size, CRC errors in either copy, copies that differ, the real `sda` table, a wrong GUID, any other entry changed, retry 0, already successful, inactive, unbootable, a stale plan, `/dev/null`, a short file, and the backup directory.

What this does not cover: real UFS behaviour on power loss (a torn block may read as an error, which XBL also treats as a failed copy), the real backup GPT, OrangeFox's `dd`, and the UEFI driver. `mark_successful.py` has no phone code at all. `apply_to_image` only accepts a 4 GiB regular file, so nothing it does can reach a phone.

The shim patch compiled on a copy with the project's flags. It is not applied and has not booted. Its one new command is a READ on LUN 4. A failed read logs and the boot goes on, except a timeout, which stops with a reason on screen as the other commands do. At 2 or fewer boots left it shows the count on the blue screen for 5 seconds.

## Corrections to `plan-boot.md`

I did not edit it.

- The failed boot table says the menu's START boots iOS. It does, and it costs a retry each time. If restart keeps going to the menu, each restart uses one.
- "ABL tries b" is only true if `boot_b` is successful and not unbootable. Today it refuses `boot_b`, and the only path to it is the menu item.
- Phase 2 step 5 asks for 8 boots from a cold start to expose a countdown. With the flag cleared, that test ends at boot 8 in fastboot, which is now the expected result.
- It says retries restart with `set_active` as a workaround. A reflash does the same, and it is what I recommend.
- `gpt-*.bin` are primary copies only.
