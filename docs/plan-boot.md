# Boot and data safety plan

2026-11-02. Nothing here has run on a phone. I ran no adb or fastboot, changed no existing file, and committed nothing. Prepared fixes are in `plans/boot/`, unapplied, and all of them are untested on a phone.

Labels: **Proven** means I read it in code, a log, a partition backup or the ABL binary, or ran it on the Mac. **Inferred** means reasoning with no run behind it. **Unknown** means only the phone can answer.

## Decisions

1. Turn on the HFS+ journal first. It fixes the ast20 failure class and needs no kernel change.
2. Flash the shim to both boot slots, without `set_active`. A failed boot then lands in iOS again or at the ABL fastboot menu, never in Android.
3. Keep restart going to the fastboot menu until a boot guard exists. Pressing START there boots iOS with no Mac.
4. Do not add a periodic sync job. The kernel already does it.
5. Do not build fsck into the kernel. Add a userland fsck only if corruption survives the journal.
6. Keep the root on `userdata`. No other partition is big enough, and Android cannot wipe it once it cannot start.

## What the evidence says

### Boot slots

| Fact | Status | Source |
| --- | --- | --- |
| `boot_a`: active, priority 3, retry 6, successful, bootable | Proven, as of the 2026-11-01 backup | `backup-asteroids/gpt-sde.bin`, attribute byte 0x77 |
| `boot_b`: not active, priority 3, retry 6, successful, **unbootable** | Proven, same backup | attribute byte 0xf3 |
| ABL only decrements retries on a slot not marked successful | Strings proven, logic inferred from upstream Qualcomm LinuxLoader | `Active Slot %s is bootable, retry count %ld` in `abl_a.img` |
| With no bootable slot, ABL enters fastboot | Proven string | `No bootable slots found enter fastboot mode` |
| ABL skips verification when unlocked | Proven string | `Device is unlocked, Skipping boot verification` |
| Fastboot menu has START, Restart bootloader, Recovery mode and Power off | Proven strings | `abl_a.img`, LZMA section at 0x1078 |
| ABL can refuse to flash on low battery | Proven string | `Error battery voltage: %d Requireed voltage: %d, can flash: %d` |
| No Virtual A/B merge is pending | Proven, same backup | `misc.img` at 0x8000: version 2, merge status 0 |
| The boot image fits | Proven | 8019968 bytes against a 100663296 byte partition |
| `vbmeta` can stay stock | Inferred | From the unlocked string above |
| The same image works from flash as from `fastboot boot` | Inferred | Upstream uses one `BootLinux` path. Normal boot takes DTBs from stock `vendor_boot`, which should carry the same `splash_region` |

`fastboot set_active` is off limits. Upstream resets retries and clears the successful flag there (inferred), which would start a countdown that no iOS code can clear. A lock state change is also off limits, because ABL holds a `--wipe_data` string for recovery and that would format `userdata` (inferred from upstream).

### The SMMU check

**Proven by code:** `smmu_check_ufs()` in `ios-sweet/asteroids/shim/ufs.c:140` reads SMRs in order until it finds stream 0x60. Its own comment says the hypervisor aborts reads of SMR 7. If a boot leaves the stream past slot 6, or nowhere, the read aborts and the shim stops on the red exception screen with no UFS reason.

**Proven by code:** removing it loses no protection. A translating mapping makes the next DMA fail, and the shim prints why (`START STOP UNIT timed out`, `READ failed`). The shim issues no writes.

**Unknown:** I found no log of this check failing. Your brief calls it known, and the failure above is the only mechanism the code shows.

### Disk

| Fact | Status | Source |
| --- | --- | --- |
| The current root is unjournaled | Proven | `tear-fix/ios26-gpu14-root.simg` header: attributes 0x80000100, journal block 0 |
| The kernel syncs every 5 s and flushes the UFS cache | Proven | `sweet_ufs.c:1690`, and `syncer every 5 s` in ast20 to ast22 |
| Corruption happened with that syncer running | Proven | `ast20-nofp16.txt:139`, then 287 `hfs_mark_inconsistent` lines from line 221 |
| Journal code is in this kernel | Proven | `bsd/conf/files:464` |
| `sweet_ufs` handles the flush ioctls the journal uses | Proven | `sweet_ufs.c:789` |
| A journaled root booted iOS 16 on an older kernel | Proven in notes | `NOTES.md:1101-1112` |
| Journaling was dropped for tooling reasons only | Proven | `tools/renegade/README.md:40` |
| The Mac can journal a finished image | Proven on the Mac | `check-05-journal.sh` |
| `/sbin/fsck_hfs` is in the root | Proven | Read from the simg |
| It runs on iOS 26 here | Unknown | |
| launchd's fsck boot task skips the root | Proven on iOS 16 only | `NOTES.md:1124` |
| A journaled root on iOS 26 and xnu-12377 works | Inferred | Needs one boot |

`hfs_mounthfsplus` accepts a dirty root only because patch 0031 lets it (`hfs_vfsutils.c:205-212`). The cut in ast20 left the bitmap and catalog disagreeing, and the mount then ran orphan cleanup on top of that.

### Corrections to other documents

I did not edit these.

- `daily-driver-gaps.md` section 1 says the root "is journaled". It is not.
- `NOTES.md:18` says the gpu14 root was not booted. `logs/ast22-*` shows it booted at 01:41, with a fresh RTC offset and a first-boot job ending `done (rc=0)`.
- `NOTES.md:14` leaves the clean mount after Shut Down unconfirmed. It is still unconfirmed, because ast21 and ast22 both mounted freshly flashed roots.

## Phase 0: safety net

Depends on nothing. Agent time 15 minutes, plus 10 minutes for the owner. Risk: none.

1. Write a SHA-256 manifest of `backup-asteroids/`, which has only sizes today in `backup.log`.
2. Read `slot-*`, `current-slot` and `unlocked` with `02-boot-slots.sh status` and compare to the table above. The backup is a day old, and `fastboot boot` can clear unbootable (`CmdBoot: ClearUnbootable` string).
3. Copy the owner's iOS data off first if any exists. No backup covers `userdata`.

Verify: `current-slot` is a, `slot-successful:a` is yes, `unlocked` is yes. If not, stop and rethink phase 2.

## Phase 1: journal

Depends on phase 0. Agent time 1 hour, plus 1 reflash and 2 boots. Risk: medium, because a journal bug can stop the root mounting. Recovery is a reflash of a simg without a journal.

1. Build the next root as usual, then run `05-enable-journal.py` on its raw image as the last step before `img2simg`.
2. Flash `userdata`. That takes about 10 minutes and erases the current iOS state.
3. Boot and shut down from Settings. Boot again, then force a reset while the phone is writing. Boot once more.

Verify on the phone:

1. Boot 1 logs `hfs: Initializing the journal`.
2. No boot logs `mounting dirty non-journaled` or `hfs_mark_inconsistent`.
3. `SWEET UFS STATS` shows `flush_err 0`.
4. Pull the raw image once and run `fsck_hfs -fn` on the Mac, to catch damage the kernel did not report.

Side effect: `tools/sweet-hfs-pull.py` ignores the journal, so after a crash it can read a stale catalog (inferred). The log ring at the end of `userdata` is not affected. `hfsroot_owners.py` refuses a journaled image, so keep the unjournaled simg as the build input.

## Phase 2: boot without a Mac

Depends on phase 0 and patch 01. Phase 1 is strongly advised. Agent time 2 hours, plus 3 to 4 boots. Risk: medium for iOS, low for bricking, since ABL, XBL and the hypervisor stay untouched.

1. Apply patch 01 and build the boot image with the current kernel. The kernel needs no change for this phase.
2. Run `02-boot-slots.sh try IMG`. This proves the shim change with nothing written.
3. Run `CONFIRM=flash 02-boot-slots.sh flash a IMG`. Read the before and after flags it prints. Stock `boot_b` stays as it is.
4. If the flags did not change, reboot from the ABL menu with START and let iOS boot.
5. Power off fully, power on, and check iOS comes up. Do this 8 times, more than the retry count of 6, to expose any countdown.
6. Run `CONFIRM=flash 02-boot-slots.sh flash b IMG`. Android can now start from neither slot, which removes the wipe trigger.

Verify: iOS reaches the lock screen from a cold power-on with no cable, and `slot-*` values do not change across the 8 boots.

Failed boot paths:

| What happens | Result | Status |
| --- | --- | --- |
| Shim fails | Reason on screen, CPU parked, no reset. Hold Power + Volume Down | Proven by code |
| Kernel panic | Stays on screen. Holding Power reboots into iOS, Power + Volume Down gives fastboot | Code proven, key behaviour inferred |
| launchd critical-process reboot, restart or power off | ABL fastboot menu. START boots iOS | Code proven, START inferred from strings |
| `boot_a` loses successful and retries run out | ABL tries b, and with neither bootable enters fastboot | Strings proven |
| Android somehow starts | Formats `userdata` | Proven in notes |

Recovery: Power + Volume Down, then `02-boot-slots.sh try` with a known image, or re-flash. Stock images are in `backup-asteroids`. Restoring stock boot means Android formats `userdata` on its next normal boot, so restore only when finished with iOS.

If step 3 clears the successful flag, the phone drops to fastboot after about 6 boots. A short-term workaround is `set_active`, which restarts the count (inferred). A proper fix would have to write the GPT flag from iOS or the shim, which I would avoid. That decision belongs after seeing the real flags.

## Phase 3: restart stays in iOS

Depends on phase 2 and patches 03 and 04. Agent time 3 hours including a CI build, plus 3 boots. Risk: low, because the default stays fastboot.

1. Copy patch 03 into `xnu/patches-12377/` and let CI build it.
2. Add `sweetflashed=1` to the boot-args of flash images only. RAM images must never carry it, because stock `boot_a` plus a plain reset is the Android wipe.
3. Apply patch 04, which adds the guard job to the next root.

Behaviour: restart returns to iOS while the guard allows it. After 3 boots in a row that die within 5 minutes, restart stops at the ABL menu. Power off tries PSCI SYSTEM_OFF and falls back to fastboot.

Verify: `asteroids: 0x7148 ... (reason 0), PSCI SYSTEM_RESET` in the log, Settings restart returns to iOS, and killing backboardd 4 boots in a row ends at the menu. Also check that the 0x7148 register reads 0 on the following boot.

Unknown: whether Gunyah honours PSCI SYSTEM_OFF, and whether the phone stays off with a charger plugged in. On this path the guard needs `/var/jb/usr/sbin/sysctl`, which is in the current root.

Later step, once logs from real use exist: make a panic reset to the fastboot menu after 30 s. A frozen panic screen drains the battery in a pocket.

## Phase 4: updates without a full reflash

Depends on nothing for raw images. On the phone it depends on a clean shutdown. Agent time 30 minutes for the first phone run. Risk: medium, because it writes `userdata` from outside the filesystem. It keeps an undo file of every old block.

What survives each action:

| Action | Survives | Lost |
| --- | --- | --- |
| New boot image, flashed or RAM booted | Everything on `userdata` | Nothing |
| `06-hfs-put.py` | Everything except replaced files | Old file contents, kept in the undo file |
| `sparsediff.py` delta flash | Only valid if the partition still matches the old image exactly. Any boot breaks that | Use it only straight after a flash and before booting |
| Full simg flash | Nothing on the root. FILL chunks zero it | `/private/var`, first-boot state, RTC offset |

Which to use:

1. Kernel, ADT or boot-args change: boot image only, no root touch.
2. Chlorophyll dylibs, `bgl-launch`, or other changes to existing files: `06-hfs-put.py` from recovery. That turns a 10-minute flash and a rerun first-boot job into seconds, and keeps state.
3. New files or launchd jobs: `renegade update` and a full flash, until phase 6.

First phone run: `--dry-run` with `--adb`, then one small file, then boot and check the file's hash from a log or by reading it back. The adb path has never run. `exec-in` on OrangeFox's adbd is unknown. The tool refuses dirty volumes, so it depends on Settings > Shut Down working, which is itself unconfirmed.

## Phase 5: fsck, only if needed

Do this only if phase 1's forced-reset tests still show corruption. Agent time 1 day, plus 3 to 4 boots. Risk: medium.

1. Try `rootrw=0 ufsrw=1` by boot-arg, RAM boot only. Check whether launchd's `mount -P 1` upgrades the root to read-write. On iOS 16 it failed with EROFS only because `ufsrw` was off (`NOTES.md:1124`), so success is inferred.
2. Check that a dirty journal does not stop a read-only root mount. Code exists at `hfs_vfsutils.c`, around `hfs_early_journal_init`, that refuses this for read-only volumes, and I did not trace the root exception. If it refuses, this path dies.
3. Replace `/sbin/fsck` with a wrapper that runs `fsck_hfs -q`, then `-fy` on the raw device only when dirty. Keep the original beside it.
4. Boot with the volume dirty and watch for a clean rebuild. `NOTES.md:351` records `fsck_hfs -fy` segfaulting on one damaged extents tree, on a different build.

A kernel-side check is rejected. It would mean writing a repair tool in the kernel, for a job userland already has.

## Phase 6: read-only system and separate data

Depends on phases 1 and 5. Agent time 2 to 3 days, plus 5 to 8 boots. Risk: high, since it is the largest change here. Worth doing when reflashes start costing real data.

1. Add a second `sweet_ufs` device for a range inside `userdata`, past the 24 GB root and before the log ring. The 230 GiB partition has about 200 GiB unused.
2. Make a journaled HFSX data volume there and seed it from the image's `/private/var`, which holds 6434 files, 6373 of them under `/private/var/jb`.
3. Add an `/etc/fstab` entry so mount-phase 2 mounts it, and run the root read-only.

Gain: a root reflash keeps user data, fsck can run on an unmounted `/private/var`, and a cut can only hurt `/private/var`. It also gives more space, since usable space today is the image size (`daily-driver-gaps.md` section 16).

Unknown: what writes outside `/private/var` on iOS 26. One boot with a read-only root will list it.

Sharing `userdata` with Android's wipe is avoidable only by keeping Android from starting. `super` is 9216 MiB, far too small for the root, and GPT changes are off limits per `AGENTS.md`.

## Battery death

Battery readout and charging belong to another area, but they set the limits here.

1. A cut with the journal costs at most recent writes (inferred). That is the main protection, because nothing warns before the battery dies (`daily-driver-gaps.md` section 1).
2. After a flat battery, plugging in may enter ABL's charger path (`androidboot.mode=charger`, `oem off-mode-charge` strings, proven). The shim ignores the command line, so iOS would boot on plug-in at low voltage and may die again in a loop (inferred).
3. Test this once before relying on standalone boot: drain the phone, plug it in, and watch. If it loops, the fix is small. The shim reads the DTB in `x0`, and on `androidboot.mode=charger` it parks instead of booting.
4. Whether the phone charges at all with the ADSP down is still unknown. A flat phone that does not charge in ABL is the worst case. Fastboot also refuses to flash at low battery.

## Boot time and the first-boot job

Measured from ast22 logs, first boot of a fresh root. The kernel log has no timestamps before launchd, so I cannot give that part.

| Step | Time |
| --- | --- |
| launchd start to first-boot job start | 5 s |
| launchd start to `exec SpringBoard` | 12 s |
| `bgl-launch` waiting for the A810 to publish | 19.5 s |
| First render pipeline compile | 43.6 s (41.2 s in ast21, 21.6 s in the appended ast19 and ast20 log) |
| First SpringBoard log line after launchd start | about 80 s |
| 50 `sweetuicache` registrations | about 52 s (`root_tweaks18.py` comment on ast19) |

1. Shader compile is the largest cost. Whether a second boot reuses a pipeline cache is unmeasured, because the appended `-var` logs do not separate boots. Time `render pipeline` lines on a second boot before optimising anything.
2. The first-boot job is safe to interrupt: it retries until the `.renegade-firstboot-done` marker exists. In ast22 it ended `done (rc=0)`. One hazard follows from the freeze work: a GPU hang during the job leaves it retrying (`freeze.md`, `launch com.sweet.run -> 11` repeated 18 times).
3. `06-hfs-put.py` removes most first-boot reruns, which is the cheap win. I would not start other boot-time work until phases 1 and 2 are done.

## Biggest unknowns

1. Whether flashing keeps `slot-successful:a`. This decides whether phase 2 is stable or counts down.
2. Whether a journaled root works on xnu-12377 with iOS 26.
3. Whether the phone boots from flash exactly as from RAM, with the panel kept on.
4. What a flat battery does on plug-in, and whether it charges.
5. Whether the ABL menu and hardware keys behave as their strings suggest on this phone.
