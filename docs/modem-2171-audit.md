# Patches 2170 + 2171 audit

2026-11-03. Review only. I read `3a-IOS/HANDOFF.md` and `plans/modem/sol-2170-audit.md` first and used the ponytail-review and unslop guidance. No agents, phone operations, partition writes, sudo, firmware access, log publication, or production edits. I did not start, stop, or otherwise manage the parent's build.

## Decision and exact approval

**Conditional GO for the exact read-only `sweetmodem=1` preflight in 2170 + 2171, after a successful full SWEET kernel build with 2171 applied.** This is source approval for FAT/MDT inspection and bounded firmware reads on the existing intact GPT. It does not certify hostile GPT handling or firmware authenticity. The source findings below do not block that narrow test.

Build safety is a separate NO-GO for claiming the current cleanup changes eliminate permanent deletion. A reachable native build tool still calls `unlink` directly. A successful build does not resolve that policy finding. Fix the bypass before authorizing another build as Trash-safe. This review does not direct any action on the already parent-managed build.

No approval for firmware loading or placement, modem start, PAS, ownership changes, low scratch writes, SMEM/GLINK hardware activity, rmtfs, tftp, UIM, NAS online, WDS, IPA/rmnet, or mobile data. No boot, flash, reboot, slot change, or phone operation was performed by this review. M1 remains incomplete. M4 and later planning remains unchanged.

## Exact source and evidence

The authoritative series is `3a-IOS/patches/modem/`, ordered 2170 then 2171.

- 2170 SHA256 `2eeb835d8710b71cc33ed6af8dc0d45bc6daf5db4188b42b5185ac484ec9baf2`.
- 2171 SHA256 `880052a579262fa352968cfa3f1823531d4590e3dd9e16129ab4853782d0a750`.
- Reconstructed candidate SHA256 `480693fe20e4478697e87aa6f7118afcffe5600aa6c1beb8c6a24011ac3527a3`.

`sh 3a-IOS/tools/modem/check.sh` passed with C11, `-Wall -Wextra -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer. I checked the reconstruction script: it consumes the ordered public patches, asserts removed/context lines against the preceding source, and emits `3a-IOS/tools/modem/tests/out/sweet_modem.c`. It does not substitute the production source.

Additional sanitizer-backed synthetic checks passed in `/tmp/modem-2171-review/extra.c`. They cover the actual boot parser's string-copy path, long value truncation, bare and duplicate arguments, malformed LFN order/type/cluster/UTF-16/terminator/padding/checksum, exact and excessive chain lengths, early EOC, reserved/bad/free links, two-cluster cycles, empty files, and zero-memory/nonzero-file ordinary ELF loads. This supplements the committed tests, which only cover a subset of those cases. The supplemental parser harness copies the actual internal parser and `argstrcpy2` from production, with aborting numeric-path stubs that the force-string tests never execute.

The parent's stated local backup result is 29 loads, 109371307 bytes and span `0xd700000`, recorded privately at `plans/modem/logs/real-image-check-candidate.txt`. I did not reopen the backup or that log. This evidence establishes reported host acceptance, not authentication or a phone result.

The task initially described production as 2170. At the time I hashed it, `ios-asteroids/research/xnu-modem/dxb/xnu/bsd/dev/sweet_modem.c` already matched the reconstructed 2171 candidate exactly. The parent-updated README also reports build completion. I did not make that transition or independently verify build status, build logs, or the resulting kernel's provenance. The full-build condition remains explicit for the parent to establish.

## Findings

1. Reachable permanent deletion bypass in the build, must fix before another Trash-safe build claim. Under the dedicated XNU tree, `SETUP/kextsymboltool/kextsymboltool.c:964-966` calls `unlink(output_name)` on any error when the output is outside `/dev/`. `makedefs/MakeInc.cmd:324` selects this tool, `config/Makefile:78-86` invokes it for symbol-set files, and `config/Makefile:150,194` connects these to configuration/install work. A write/seek failure after opening a symbol-set output, or the duplicate-export failure at lines 846-849, reaches permanent deletion of that output. PATH wrappers, RM/RMDIR overrides, `.PRECIOUS` and `.SECONDARY` cannot intercept a C runtime `unlink`. The smallest safe change is to leave failed output in place, or explicitly send it through the validated absolute-path Trash wrapper, then rebuild the helper. Do not execute a failing repro against this tool under the current policy. I inspected it statically only.

2. The string gate is exact about the returned value, but a bare argument also enables the worker. Candidate `sweet_modem.c:358-364` compares a four-byte string against `"1"`. Production `pexpert/gen/bootargs.c:151-169` synthesizes `"1"` when `sweetmodem` has no equals sign. Thus a command line containing only `sweetmodem` enables the read-only worker too. The actual parser harness reproduces this. Multiple matching non-boolean arguments are scanned and later values overwrite earlier ones, so `sweetmodem=0 sweetmodem=1` enables it as well. This is a documentation/strict-syntax gap, not a new hardware capability or a blocker for the requested explicit `sweetmodem=1`. Before using this gate for hardware stages, validate the complete token if exact `name=value` syntax is required. The committed host shim cannot detect bare-token behavior because it takes an already-extracted value.

3. Existing inherited limits remain. `sweet_ufs.c:916-923` still multiplies GPT entry count and size in 32 bits before iterating the original count. Partition extent arithmetic is also inherited. Approval assumes intact existing GPT, as in the 2170 audit. MDT validation still accepts an unauthenticated hash marker without checking its payload or ELF CPU/type, and probes only split-file endpoint data rather than reading all payload bytes. Hash-flagged headers are deliberately skipped before the ordinary zero-memory load check. These are not loader approvals. A future loader must separately review all metadata it consumes and authenticate before execution.

## FAT, boot argument and regression review

One-slot VFAT matching requires order byte exactly `0x41`, type zero, first-cluster field zero, case-insensitive ASCII equality, and the correct zero terminator followed by `0xffff` padding when the name is shorter than 13 characters. Non-ASCII UTF-16 cannot equal these fixed ASCII names. The short entry must immediately follow the matching LFN and its 11-byte checksum must agree. Deleted entries reset pending state, normal entries consume it, volume labels are skipped, and multi-slot order bytes do not match the one-slot reader. Valid short 8.3 lookup remains available independently of LFN matching. Corrupt LFN therefore does not invalidate an independently exact short name, which is appropriate for this read-only lookup. The old comment claiming long names never match is stale.

`fat_validate_file` walks exactly `ceil(size / cluster_bytes)` allocated clusters and requires the next link to be EOC. Every visited cluster is checked against the data range, reserved range and seen bitmap before FAT access. It rejects early EOC, excess links, free/bad/reserved final links and repeated clusters. A self-cycle, a cycle ending exactly at declared length, and a longer cycle all fail. Empty files require cluster zero. Mount bounds cap the FAT at 64 KiB, so cluster indexes fit the 8192-byte bitmap. The bitmap is static and reused serially under the existing single-worker assumption.

Directories retain bounded traversal through the 128 KiB buffer limit and cluster guard. Cycles fail before lookup can use the repeated directory contents. 2171 also rejects directory cluster zero at lookup, closing the old root-sentinel alias. All path components used by the worker are fixed names that fit the supported one-slot form.

The real `PE_parse_boot_arg_str` delegates to force-string parsing. An explicit value copies at most `max_len - 1` characters and adds NUL, while returning found even if truncated. With this four-byte buffer, a longer value beginning with `1` retains at least its second character, so it cannot truncate to the sole accepted `"1"`. `10`, `1xxxxxxxx`, `4294967297`, `01`, `1.0`, `0x1`, and empty values fail closed. Absent and `0` return before thread creation or storage reads. Bare-token and duplicate-token semantics are finding 2.

Ordinary `PT_LOAD` with zero memory and nonzero file size now fails. Zero-memory/zero-file ordinary loads remain ignored. Header/table bounds, overlap checks, relocation flags, 32-bit endpoint limits and the MPSS reservation span check remain intact. No result is used as a physical destination or executable pointer.

The only production caller remains after UFS/board initialization in the Nothing board branch. The worker retains its bounded MDT allocation and handled-error cleanup, drops its thread reference after creation, and terminates after the result log. Static buffers still require the existing single caller. No global wall-clock deadline was added. Storage failures can delay other storage users despite per-command deadlines.

## Build cleanup review

`plans/modem/build-safe/bin/cleanup:11-35` accepts only the required `f/r/R` flags, converts relative operands by resolving the parent, preserves the final symlink as the object to Trash, rejects paths outside the dedicated `xnu-modem/dxb` tree, and rejects the root and `xnu` directory themselves. It tolerates missing paths only with force, requires empty real directories for its `rmdir` entry point, and invokes only `/usr/bin/trash` with an absolute operand. There is no permanent-deletion fallback. This is static review, not a new live cleanup test.

Dedicated `makedefs/MakeInc.cmd:332-333` now routes RM/RMDIR through those local scripts. Lines 583-584 add `.PRECIOUS: %` and blanket `.SECONDARY`, preserving failed/interrupted targets and intermediates within make processes that include this file. PATH entry points cover unqualified shell cleanup only when the build inherits that PATH. I did not inspect or change the running process environment. `config/list_supported.sh:34` uses an unqualified `rm`, so that inheritance remains necessary.

Avoiding MAKEFILES is correct: `makedefs/MakeInc.dir:61` uses `$(firstword $(MAKEFILE_LIST))` for recursive make. Prepending a preservation file would change that selected entry point. `libkern/kxld/Makefile` explicitly says it is a separate libkxld alias, outside the regular kernel build. Its independent removal rules do not establish a bypass in this SWEET install. Likewise, legacy `SETUP/config` contains direct unlinks, but I did not establish invocation of that legacy generator by this modern build. Finding 1 has an established active call path.

`tools/xnu-local-build.sh` still has destructive preparation outside `--remake`. That branch remains forbidden. No clean, fresh-tree, forced checkout, remote-build or debugger helper invocation is covered by this review. External compiler/linker/CTF tool internals were not exhaustively audited, so this review makes no whole-toolchain guarantee.

## Read-only and privacy boundary

The reviewed modem call path still requests only `modem_a` partition size and bounded reads. UFS partition discovery also reads GPT metadata on LUNs 0 through 5. The read helper serializes bounce-buffer/LUN use and restores the saved LUN. No path added by these patches invokes partition writes, PAS/SMC, MMIO mapping, firmware placement, or modem services. Existing unrelated kernel/root-filesystem activity is outside that claim.

Logs added by the worker contain only refusal/error state, load count, total bytes and relative span. No firmware contents or subscriber/device identifiers are emitted by these patches. Review outputs remain local. Successful host tests and a successful kernel build do not complete M1 or grant any subsequent hardware stage.
