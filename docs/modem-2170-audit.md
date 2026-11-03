# Patch 2170 audit

2026-11-03. Review only. No phone operations, kernel build, boot, firmware extraction, or implementation changes. I read `3a-IOS/HANDOFF.md` first, then `plans/modem/README.md` and `3a-IOS/docs/modem.md`. I applied the ponytail-review and unslop review guidance.

## Decision

**GO for the exact read-only `sweetmodem=1` preflight, conditional on a successful full SWEET kernel build of this exact source using Trash-safe cleanup.** Absent and numeric zero are runtime inert. The existing board and GPT are assumed intact, as required by the current UFS driver. The build is still pending, so this is source-level conditional approval, not a claim of kernel or phone validation and not an instruction to boot.

This approval covers FAT/MDT inspection and bounded reads only. It grants no approval for PAS, memory ownership changes, firmware placement, low scratch writes, modem start, rmtfs, tftp, UIM, or online mode. The parser limitations below do not create writes or unbounded traversal in this patch, but PASS must not become authorization for a loader.

## Exact material and checks

The patch SHA256 is `2eeb835d8710b71cc33ed6af8dc0d45bc6daf5db4188b42b5185ac484ec9baf2`.
The production `bsd/dev/sweet_modem.c` SHA256 is `0748428a04744b38776c5d26314c00d933925033602d96446acf18d346c4ec83`.
The added source extracted from the patch matches production byte for byte. Production `bsd/conf/files:710` registers it as standard, and `bsd/dev/sweet_ufs.c:83,1325` contains the declaration and call in the expected board branch. All production paths in this report are beneath `ios-asteroids/research/xnu-modem/dxb/xnu/`.

I reran `sh 3a-IOS/tools/modem/check.sh`. Its patch-extracted source passed C11 compilation with `-Wall -Wextra -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer. Additional synthetic checks in `/tmp/modem-2170-audit-ktq1hcff/extra.c` reproduced the file-chain false positives below and confirmed early end-of-chain and overlapping ELF load rejection. No private inputs were used.

The host shim runs the worker synchronously, replaces boot argument parsing and storage, and does not free its allocation. It cannot prove XNU compilation, asynchronous lifecycle, real GPT/FAT acceptance, kernel allocator behavior, or firmware authenticity. No full kernel build was run.

## Findings within the read-only scope

1. File cycles can report PASS. `sweet_modem.c:103-130,264-273` limits traversal by the volume cluster count but does not detect revisited file clusters. With a 512-byte cluster, a 513-byte split segment and FAT entry `4 -> 4`, the last-byte read succeeds and the whole synthetic preflight reports PASS. A one-byte file whose occupied cluster is marked free also passes because the final FAT link is never examined. Directory cycles do fail within the directory buffer/iteration bounds. Reads remain bounded to the volume and partition, so this does not block read-only inspection. Before loading firmware, reject repeated clusters and validate the required chain and terminal link. Add these two cases to the committed host checks.

2. The boot gate narrows numeric values to 32 bits. `sweet_modem.c:311-315` uses an `int`, and `pexpert/gen/bootargs.c:374-400` explicitly casts parsed numbers to `int32_t`. Consequently `sweetmodem=4294967297` selects level 1, despite the claim that every other value is refused. Absent, zero and ordinary unsupported levels are inert as tested. This alias only enables the same read-only worker today, so it does not block the requested exact `sweetmodem=1` case. Before adding any hardware level, use a strict string gate with exact accepted values and test the real parser semantics. Merely extending the integer width does not establish strict textual validation.

3. ELF PASS is a structural check with deliberate gaps. `sweet_modem.c:244-279` checks header/table extents, ELF32 little endian/version, load sizes, 32-bit address overflow, load overlap, relocation flags, one hash marker and the rounded reservation span. It does not validate CPU/type, authenticate hash contents, validate hash payload bounds, or reject `PT_LOAD` with zero memory size and nonzero file size, which is skipped at line 256. A fabricated non-load header with hash flags satisfies the hash count, as the current positive fixture itself demonstrates. These are bounded false acceptance cases, not memory writes in 2170. A future loader must validate every consumed header, reject inconsistent zero-memory loads, validate metadata supplied to PAS, and authenticate before execution. `MPSS_SIZE` checks span only. Relocation into the actual reservation remains unimplemented.

4. GPT validation is an inherited trust boundary. `sweet_ufs.c:898-941` tests `nent * esz` in 32-bit arithmetic, then iterates the original `nent`. For `esz=128, nent=0x02000001`, the size test wraps to 128 and the search can scan beyond the bounce buffer. Partition extents also lack overflow/capacity validation before `nblocks * UFS_BLOCK`. This helper predates 2170, and default root lookup has the same multiplication pattern at lines 582 onward. It is not a new FAT/ELF bug, but the new partition lookup also depends on it. This GO assumes the existing intact GPT, not arbitrary malformed GPT safety. Before treating disk metadata as hostile, compare counts using division, bound iteration to bytes actually read, and validate inclusive extents and byte multiplication against LUN capacity.

## Runtime, bounds and lifecycle review

`fat_mount` uses widened arithmetic for FAT/root/data offsets and checks declared volume bytes against the partition. Sector size and cluster sectors must be powers of two, FAT allocation fits 64 KiB, root directory fits 128 KiB, and cluster count must fit the FAT. Valid cluster extents fit the declared data area. `modem_read` uses subtraction bounds checks and a literal `modem_a` allowlist. Short/invalid/reserved chains fail when encountered. File loops terminate by decreasing offset/length and cluster guards. Directory loops terminate by guard or the 128 KiB limit. A corrupt directory cluster zero can alias the root sentinel in `fat_open`, but remains bounded and cannot select a different partition.

MDT allocation is limited to 64 KiB. Table offset/count checks precede header access, with at most 64 headers. Segment additions use 64 bits, overlaps are checked before acceptance, and the rounded span must fit 0xe600000. Endpoint probes do not read or verify every firmware byte. No parser result is used as a host pointer or physical write address.

The UFS caller publishes `su.ready`, releases its mutex and finishes the preceding board initialization calls before starting this worker. The Nothing board predicate guards the only production call. `sweet_ufs_rootdev` does not reinitialize once ready. Off returns before size lookup, allocation or thread creation. Off still adds about 192 KiB of static buffers plus code to the kernel image, so runtime inert does not mean byte-identical image.

On successful creation the caller drops its thread reference. The worker funnels every handled error through MDT cleanup, prints a numeric result and terminates itself. Kernel thread continuation signatures and the `thread_terminate` declaration match `osfmk/kern/thread.h`; allocation/free follow the existing kernel APIs. No obvious kernel-only declaration mismatch was found, but the host shim excludes the real headers and cannot settle compilation or linkage.

The once-only behavior comes from the current UFS initialization call graph, not a guard inside `sweet_modem_start`. Concurrent or repeated future callers would race on the shared FAT/directory buffers and partition size. Keep the single caller or add a one-shot guard before exposing another entry. The worker has no total wall-clock deadline, although each storage command has a five-second deadline with bounded retries. It may delay competing storage under failures. No shutdown is owed in 2170 because MPSS is never started or initialized through PAS.

## Storage and privacy

The call graph is `sweet_modem_start -> modem_probe -> sweet_ufs_part_size / modem_read -> sweet_ufs_read_part -> ufs_part_locate / ufs_read_blocks -> ufs_read_at -> ufs_scsi_at`. A cache miss reads GPT headers and entries on LUNs 0 through 5. Firmware content reads then use only the located `modem_a` extent. Thus not every physical read is inside modem_a: GPT discovery is an explicit metadata exception.

`ufs_read_at` issues READ(10), with count limited to the bounce window. The partition reader serializes the shared bounce buffer and LUN selection under `sweet_ufs_lock`, restores the saved LUN, and copies into the bounded destination. No call from this modem worker reaches WRITE(10), flush, partition modification, SMC, PAS, `ml_io_map`, or ownership assignment. Normal pre-existing root filesystem/logging activity is outside this read-only worker claim.

The new logs contain level, error, segment count, total bytes and relative span only. They contain no firmware bytes or subscriber/device identifiers. No EFS partition is requested. The existing general write gate bounds writes to the selected root partition, which defaults to userdata; it does not independently enforce that partition's name. Do not change `ufsroot` to a protected partition with writes enabled or infer a universal modem partition write guarantee from that gate.

## Build blocker and smallest Trash-safe solution

The ordinary build command is unsafe under the owner's permanent deletion policy even with `--remake`. `makedefs/MakeInc.cmd:332-333` assigns `RM=/bin/rm -f` and `RMDIR=/bin/rmdir`. Active install rules remove framework symlinks at `MakeInc.kernel:543-551`; other rules remove CTF scratch files and temporary header files. A PATH shim alone cannot intercept `/bin/rm`. Setting only environment `RM` also loses to the makefile assignment.

Keep the existing tree and the mandated `--remake` path. `ios-asteroids/tools/xnu-local-build.sh:22-41` directly executes make without the destructive fresh-tree preparation. Without `--remake`, lines 59-60 force a checkout and permanently remove the tree, violating the policy and losing local patches.

The smallest proposed solution is one local cleanup wrapper, with command-line-origin make overrides passed through `MAKEFLAGS`, plus `rm`/`rmdir` PATH entry points to that same wrapper for unqualified shell calls. The wrapper must translate relative operands to absolute paths, preserve a final symlink as the object to trash, accept only the needed removal flags, tolerate missing paths for force semantics, and invoke only `/usr/bin/trash <absolute-path>` for each existing operand. It must refuse unsupported options, paths outside workspace or /tmp, and failures without falling back to permanent removal. Do not resolve the final symlink to its target. Do not replace cleanup with `true`, since install relies on moving existing symlinks out of the way before recreating them.

Pass `RM=/absolute/wrapper` and `RMDIR=/absolute/wrapper` as make assignments through `MAKEFLAGS`, not plain environment variables, while retaining `XNU_LOCAL_DIR` for xnu-modem, `USE_KDK=1`, and `tools/xnu-local-build.sh --remake -j12 26.5`. The wrapper does not exist yet. Audit the reachable recipes and scripts for absolute removal commands, runtime deletion APIs and make's own automatic deletion paths before executing that command. Route any reachable bypass through the same wrapper or stop. The broader tree contains literal shell removals, including `config/list_supported.sh:34` and `libkern/kxld/Makefile:117`; command overrides alone are not a whole-build guarantee. This is a proposed minimal setup, not a verified safe build invocation. No build, dry-run make, cleanup wrapper or build-file edit was performed here.

## Blockers for the next hardware stage

1. Finish and separately audit the split firmware loader and PAS 4 lifecycle, with shutdown owed on every exit after initialization may have taken effect, including uncertain SCM outcomes.
2. Coordinate apps-RSC DRV2 ownership and shared IRQ register changes with touch, which scans every idle active TCS at `sweet_touch_asteroids.c:390-405`, and implement bounded QMP and SMP2P stop acknowledgements.
3. Validate SMEM/GLINK extents and frames for mpss remote pid 1, SMP2P in 435/out 428, QRTR discovery and strictly RAM-only rmtfs writes with no partition-write callback.
4. Review low-memory ownership, overlap, DMA accessibility and cache handling before any write to tentative scratch at 0xe1000000. XNU RAM above 4 GB does not prove that low range is available. Place MPSS only in the reviewed 0x8ac00000 + 0xe600000 reservation.
5. Prove bounded offline UIM status first, logging at most identifier suffix4, then obtain a fresh exact-patch audit before any modem start or data work.

The legacy `plans/modem/src/lift` files are references, not build inputs. Their exclusive DRV2 assumption is false. No approval here depends on those drafts compiling or being safe.

M4 to M7 remain separate mobile-data work. IPA uses PAS 15 and the ipa_fws memory-region phandle 0x26d at 0x9ea90000 + 0xa000, not the adjacent similarly named reservation. Verify signed image layout, SM7635 register/endpoint tables, clock/interconnect votes, SMMU mappings, DMA bounds and teardown, and the IPA QMI handshake before online NAS. WDS needs provisioned APN/profile reads and transient parameters, then proven IPA/rmnet packets and a distinct iOS interface. Each needs a new review.

No partition writes, firmware publication, flashing, stock boot, reboot, slot change, sudo or phone probe was performed or authorized by this review. No agents were spawned. The only requested repository deliverable is this report.
