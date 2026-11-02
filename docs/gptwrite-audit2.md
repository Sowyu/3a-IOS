GO for the reviewed one-time operation. No blocking implementation defect found in `phone_mark.py` or its four-write plan.

Reviewed 2026-11-02. Read-only review of source and saved GPT dumps. No phone access, adb/fastboot execution, commits, test images, or temporary files. This report is the only file written. Assumes the intended phone remains in idle recovery with no concurrent partition, flash, or slot operation. GO does not establish power-cut recovery through the still-unverified UEFI partition driver.

## Blockers

None found. The three earlier blockers are closed. Some earlier should-fixes are only partly closed, as detailed below.

## Earlier blockers

1. `phone_mark.py:20-24,66-74,94-100` closes stdin consumption. Every subprocess receives `stdin=DEVNULL`. The four writes live in a Python list, so an adb stdin reader cannot consume later iterations. Success requires finishing the loop and passing final validation.
2. `phone_mark.py:21-24,82-85` closes masked write/flush failure. `dd ... conv=notrunc,fsync && sync && echo WROTE` cannot emit its success marker after either command fails. Nonzero adb status stops execution, and the independent marker check also stops execution when an older shell transport loses remote status. `2>/dev/null` still hides useful dd diagnostics, but does not turn failure into success.
3. `phone_mark.py:27-31,55,86-88` removes unsupported `iflag=direct`. Preflight reads and verification use the same ordinary dd path. Each write explicitly requests fsync, then a checked shell command drops clean caches before reading back. The initial read-only run does not test drop_caches, so preflighting that operation remains a should-fix.

## Should-fixes

1. Finish remote-read status handling and keep diagnostics. `phone_mark.py:28-30` checks the exec-out client's return code, but exec-out itself does not carry the remote dd exit code, even with modern adb. A remote dd that emits the full requested bytes and then exits nonzero can be accepted. Length checks reject ordinary truncation, and the caller validates CRCs or exact expected blocks, so this does not reopen the write-durability blocker. Use a supported raw shell-v2 binary read with checked status, or an explicitly checked remote-file read/pull path. Keep dd stderr on the write command at line 82 so a failed fsync has a useful explanation.
2. Preflight cache eviction before the first GPT write. `phone_mark.py:70-72,86` means the reported no-write run never exercises `/proc/sys/vm/drop_caches`. If that write is denied or unavailable, the current program stops after changing the backup array, leaving that copy CRC-invalid and the primary unchanged. Test `sync && echo 3 > /proc/sys/vm/drop_caches` before entering the destructive loop and verify the read path afterward. The semicolon in the current cache command hides only that extra sync's status, while the cache write's status is the shell status. The durability-critical sync at line 82 is already guarded by `&&`.
3. Preserve an explicit no-concurrency operating rule. `phone_mark.py:42,73-81` creates a fresh host directory and PID-based staging paths, but does not lock the device against a second process or recovery slot/partition actions. The current-block check covers only the block about to be written. For example, another writer can change an untouched backup array block after line 92 validates it, leaving an invalid backup during the primary update. Keep recovery idle and prohibit concurrent operations for this one-time run. The code does not enforce that part of the earlier should-fix.
4. Finish the tests and stale comments. `check_mark_successful.py:194-201` now correctly mutates the byte after boot_b's allowed byte and expects refusal, but the checked-in positive tests still use stock boot_b `0xf3` at lines 120 and 142. Add a positive case for live `a=0x37,b=0x3b`, ideally exercising the phone wrapper with mocked subprocesses. `mark_successful.py:5` still says nobody has written a wrapper, and `check_mark_successful.py:4-5,236` still says the real backup was never read. These earlier documentation/test requests are not fully closed.

## Earlier should-fix closure

- Fresh output directory and staged block validation are implemented at `phone_mark.py:42,73-79`. Existing output directories fail before device access. Each push is followed by exact 4096-byte size and SHA-256 comparison against the in-memory planned block. PID-based remote names separate simultaneous processes on this host, but are not globally unique across hosts or PID reuse.
- Device binding is implemented at lines 21 and 43-54. SERIAL is supplied by the operator and passed with `-s` on every command. Root, block-device type, 4096 logical block size, 8388608 sysfs sectors, and exact `/dev/block/sde11` boot_a resolution are required. Plan validation additionally checks the stock disk GUID and entry layout. The serial is fixed for a run, not hardcoded to one handset in source.
- Remote read failures are only partly covered, as should-fix 1 explains. All commands pass through the subprocess return-code guard. The cache-write status is checked when shell-v2 is available. Binary data is never text-decoded or stripped.
- Backup gating and final validation are implemented at lines 90-99. The complete backup header and CRC-covered array validate after write 2 and before any primary write. Final validation checks both copies, equal arrays, boot_a `0x77`, and boot_b `0x3b`. Concurrency prevention remains operational.
- Exact live attributes are pinned at lines 13 and 63-65. This closes the too-broad boot_b acceptance for this phone wrapper even though the generic planner still permits any value at that byte. The corrected neighboring-byte refusal test is valid. Positive live-state coverage and stale comments remain open.

## adb status and binary transport

The installed adb path resolves to `/opt/homebrew/Caskroom/android-platform-tools/37.0.1/platform-tools/adb`. I inspected the path and binary as files without running it. Modern [AOSP adb command handling](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/client/commandline.cpp) negotiates shell-v2 using the device's shell2 feature, uses raw mode for noninteractive commands, returns the remote exit packet, and defaults to failure on disconnect before that packet. The exact recovery's advertised features were not inspected under this task's no-device rule. The WROTE guard independently protects writes if remote status is unavailable.

exec-out has different behavior. The client copies socket bytes to stdout and then returns zero. The [daemon exec service](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/daemon/services.cpp) starts a raw subprocess with no shell protocol, so remote status is absent. Raw mode avoids PTY newline conversion. Here dd stderr is redirected away from the binary stream, Python captures bytes, and exact length is required. Full-length reads are then checked against the plan or GPT CRCs. Disconnects during transfer become a short-read refusal unless all requested bytes have already arrived.

## LBA and slice checks

`8388608 * 512 / 4096 = 1048576` logical blocks, so `LAST_LBA=0xfffff`. The backup dump correctly starts at `LAST_LBA-4=0xffffb` and contains five blocks through the final header.

The GPT entry array is `96*128=12288` bytes, exactly three blocks. In the backup dump the array is bytes `[0:12288]`, an extra non-array block follows, and the header is bytes `[16384:20480]`. The primary dump begins at LBA 1, so its header is `[0:4096]` and its array is `[4096:16384]`. Every `copy_valid` invocation at lines 92 and 96 receives the correct header, three-block array, and current-header LBA. These slices agree with `mark_successful.py:17-27,67-83,140-145`.

The real saved dumps produce exactly these writes, in order:

| Order | LBA | Changed bytes |
| --- | --- | --- |
| 1 | `0xffffb` | 1 attribute byte |
| 2 | `0xfffff` | 8 CRC bytes |
| 3 | `0x2` | 1 attribute byte |
| 4 | `0x1` | 8 CRC bytes |

Both array writes change offset `0x536`, boot_a `0x37` to `0x77`. Both headers change only offsets `0x10..0x13` and `0x58..0x5b`. boot_b is outside the written array block and remains `0x3b`.

## Interruption behavior

Any uncaught timeout, subprocess error, short read, failed marker, stale-block check, validation failure, or KeyboardInterrupt exits before subsequent writes. An already launched remote dd may continue after the host loses adb. There is no automatic rollback or retry.

| Interruption point | Remaining valid copy under the modeled write order |
| --- | --- |
| Before the first write | Both original copies |
| During/after backup array write, before backup header completion | Original primary |
| During/after backup header write, including lost acknowledgement or failed read-back | Original primary, possibly completed new backup |
| Backup validation fails or cannot complete | Original primary, with no primary write attempted |
| During/after primary array write | Validated new backup |
| During/after primary header write or final validation | Validated new backup, possibly completed new primary |

In particular, adb dying between a write and its read-back does not cause the next write to run. It can leave the operation incomplete, but does not by itself invalidate the other copy. After the last write, a lost acknowledgement can leave both copies successfully updated without printing DONE. Do not blindly rerun after an interruption. The planner refuses CRC-invalid or disagreeing copies and also refuses an already-successful boot_a. Preserve power and inspect both copies before any repair or reboot decision.

These conclusions assume completed fsync operations provide the requested device durability and no unrelated writer intervenes. Prefix-tear modeling does not prove UFS hardware behavior, damage containment, or usable fastboot after falling back through the unverified UEFI partition driver described in `analysis/bootslot.md:110-114,128-135`.

## Host verification

I ran the actual planner against `backup-asteroids/gpt-live/gpt-primary.bin`, `gpt-backup.bin`, and stock `gpt-sde.bin`, entirely in memory. I also ran 105 existing pure decoding/countdown checks.

I exercised `phone_mark.main()` with filesystem and adb operations replaced by in-memory stand-ins. The success path executed 39 mocked adb calls and four ordered writes. The no-write path performed zero device writes. All 78 injected interruptions immediately before/after each adb call stopped without DONE and retained at least one CRC-valid copy. All 16388 cases across four writes and every 0..4096-byte prefix tear also retained at least one valid copy. Additional checks covered a missing WROTE marker, truncated read-back, exact binary preservation including CR/LF/NUL/0xff, DEVNULL stdin, serial binding, and nonzero subprocess status.

These mocks verify Python control flow and byte layout, not the real remote shell or flash device. I did not rerun the reported 300-check suite because it writes and deletes test images outside the sole permitted report path. Its changed refusal test was inspected directly.

Reviewed source SHA-256 values:

```text
6b934e60b56dc52a42615706d87edd8566eb4c4d8df624bdda01700a1112bd89  phone_mark.py
7fdf31c8497cdcd0b379894fd8f83abab7321d084871a3a47cc9afee1be33f79  mark_successful.py
56597e77f5e80e6ef4eb2b697b8b8a59e7b06a7e99cb0ae482727dc11d2fa809  check_mark_successful.py
```
