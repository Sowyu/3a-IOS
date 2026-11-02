NO-GO

Source re-audit of `plans/freeform/2130-asteroids-app-metal-exec.patch`, 2026-11-02. Two blockers remain under the requested exact-target and no-change-when-off requirements. The ordinary in-place rename and append accounting look sound.

References below use actual patch-file lines. `K/` means `ios-asteroids/research/xnu-local/dxb/xnu/`. Kernel line numbers refer to the current tree with 2058, before applying 2130.

## Blockers

### 1. The vnode path is relative to the calling process's root, so a different executable can match

Patch lines 80–88. `K/bsd/vfs/vfs_subr.c:3695`, `:3732`, `:3774`; `K/bsd/vfs/vfs_cache.c:539`, `:865`; `K/bsd/kern/kern_exec.c:533`, `:3026`.

`vn_getpath()` passes flags zero and uses `vfs_context_current()`. The path builder takes that process's `fd_rdir` as its root and stops at it. This is not necessarily the machine's root path.

Concrete failure path: a launcher already rooted at `/private/var/trial` executes `/Applications/Freeform.app/Freeform`. The actual vnode is `/private/var/trial/Applications/Freeform.app/Freeform`, a different binary from the intended installed app. `exec_save_path()` preserves the supplied absolute-looking string, and `vn_getpath()` returns `/Applications/Freeform.app/Freeform` relative to the launcher's root. Both comparisons pass. An ordinary arm64e Mach-O with no rejected flags or credential transition receives the automatic injection. This also applies to a child spawned by an already chrooted launcher. Establishing a chroot requires privilege, but executing later within an inherited chroot does not make this the intended system Freeform vnode.

Smallest fix: fail closed for an altered process root, or obtain the vnode path with `VN_GETPATH_NO_PROCROOT` and compare the machine-root path. Account for the difference between the calling process and the target process during spawn. Add a real path-builder or context-aware negative check. The current `vn_getpath` host stub cannot reproduce this failure.

This leaves earlier finding 4 only partly closed. Other bundle names and ordinary symlinks to a differently named vnode are now rejected, but the exact installed-app boundary is not established in all supported exec contexts.

### 2. The sweetexecenv composition fix changes behavior with appmetal absent or refused

Patch lines 143–149. Baseline `K/bsd/kern/kern_exec.c:7027`, `:7039`.

The new assignment to `sweet_existing` is unconditional. It applies even when `sweet_appmetal_enabled()` is false.

Concrete failure path: use `sweetsb=1`, no `sweetappmetal`, and `sweetexecenv=Freeform:DYLD_INSERT_LIBRARIES=/x.dylib`, then execute an otherwise eligible arm64e Freeform. With 2058 alone, sweetexecenv appends `/x.dylib`, does not set `sweet_existing`, and automatic 2058 insertion appends the sweetsb variable. With this patch, the added assignment suppresses the sweetsb append. The environment changes from two insertion variables to one. Setting `sweetappmetal=Notes` or `sweetappmetal=0` produces the same change.

Removing those duplicates is a useful composition fix, but it violates the explicit requirement that appmetal off or another value cause no behavior change relative to 2058. Gate the added bookkeeping on `sweet_appmetal_enabled()`, or obtain an explicit change to that requirement for this independent fix. Both args absent and no active sweetsb append do preserve the original environment.

Earlier finding 3's unconditional stripping itself is closed. This is a separate off-mode regression introduced by the composition fix.

## Should-fixes

### 3. Reuse the actual MAC transition decision instead of asking twice

Patch lines 16–26. `K/bsd/kern/kern_exec.c:7726`, `:7820`, `:7939`; `K/security/mac_vfs.c:867`; `K/security/mac_policy.h:274`.

The first query feeds `sweet_am_mac`; the second query inside `exec_handle_sugid()` controls the real label update. These are separate policy invocations, not a retained result. A policy whose answer changes from zero to nonzero between them can cause a non-disjoint transition while leaving P_SUGID clear. The late check would then use the earlier zero and miss that transition unless another rejection condition applies. The reverse sequence unnecessarily refuses injection.

I found no registered `mpo_cred_check_label_update_execve` implementation in the supplied source tree, so I have not promoted this conditional policy-extension failure to a current-tree blocker. Return the transition decision from `exec_handle_sugid()` through an output parameter or retain it in image_params. This also avoids invoking policy hooks twice and makes the stated guarantee follow the decision that actually controls exec. The host stub returns a constant and replaces `exec_handle_sugid()` with a no-op, so it cannot check this issue.

### 4. Add the missing boundary and integration checks to the host tests

`plans/freeform/tests/exec_env_check.py`, particularly STUBS, MAIN, CASES and NEW_ONLY.

The existing scenarios check the renamed value and count, but not adjacent string bytes, minimum remaining capacities, 32-bit pointer accounting, or the actual copied-out pointer vector. They also omit appmetal-off composition and process-root behavior. Add checks with environment strings before and after a refused exact sweetexecenv insertion, multiple string lengths/alignment paddings, both pointer widths, and capacities immediately below/at the append thresholds. Verify every output pointer and unchanged neighboring byte.

For timing, test a credential/signature change performed inside a functioning sugid stub rather than preloading the final flags and making sugid return zero. Keep the tests small, but exercise what this patch claims to protect.

## Earlier findings and verified mechanics

### Finding 3: stripping is gated correctly

Patch lines 66–76 and 139. `sweet_appmetal_own()` returns false before comparing strings unless the parsed value is exactly `Freeform`. App-only, combined and renamed-denied strings are retained with the arg absent or refused. Existing 2058 sweetsb stripping remains active according to sweetsb. With appmetal enabled, exact inherited app/combined values are removed globally, including on child execs. Arbitrary insertion values still suppress automatic injection.

The local 16-byte name buffer starts zeroed. The real boot-arg parser truncates strings with termination, and numeric values write into the zeroed buffer rather than acting as `Freeform`. Other names and longer strings cannot become the accepted eight-character name through this truncation. Refused nonempty values can emit a diagnostic. The unsynchronized static cache can duplicate first-use parsing/logging, but the boot args and computed result are immutable. I found no concrete wrong enable decision from it here.

### Finding 4: ordinary paths improve, with blocker 1 remaining

Both the saved exec string and the current executable vnode's reconstructed path must match. `/Applications/Other.app/Freeform`, helper paths, traversal spellings and a normal symlink whose target reconstructs elsewhere fail. A symlinked parent that reconstructs with a `/private` prefix also fails closed. There is no prefix normalization that accidentally broadens the match.

`vn_getpath()` is a cached namespace reconstruction, not an immutable canonical identity. It follows firmlink presentation by default. For hard-linked vnodes its own implementation warns that `v_name` can be stale, and the flags used here disable filesystem re-entry to repair it (`vfs_cache.c:635`). A hard link at the accepted path is the same file object, so hard links alone are not proof of injection into a different vnode. Nevertheless, cache aliases, renames or missing cache parents can cause missed matching. If the experiment requires pinning a particular installed artifact across namespace changes, compare a trusted vnode/file identity rather than claiming this string is a canonical identity. The path pair does not authenticate executable contents.

### Finding 5: the late placement closes the current flag/credential timing problem

Extraction is at baseline `kern_exec.c:2069`. Load-result flags and inherited entitlement flags are assembled at `:2199–2226`. The new check runs after `exec_handle_sugid()` at `:2403`, which can further update `ip_csflags` through MAC credential labeling and finishes saved-ID/security-token changes before returning. The check reads image flags, which is correct here because publication to the proc happens later in `process_signature()` at `:8760`.

The check precedes apple-string construction and `exec_copyout_strings()` at `:2572`. The environment has not yet been copied into the target address space. An error from sugid takes the failed-exec path, so skipping refusal on that error does not release an executable process with the unmodified insertion.

P_SUGID, effective set-id mode bits, CS_RESTRICT, CS_RUNTIME, either LV bit, and the three listed privileged entitlement flags all reach a refusal. A combined refusal removes sweetsb from that same variable as documented. See should-fix 3 for the duplicated MAC decision. The notes' separate `__RESTRICT`-segment limitation remains: this check does not inspect Mach-O segments. The notes say the installed trial Freeform has none; I did not independently inspect that artifact. This is source clearance for the listed kernel policy flags, not a proof covering arbitrary replacement binaries or every dyld restriction mechanism.

### The rename preserves the string area and later pointers

Patch lines 46, 98–100 and 127. The matched source string is an exact complete app-only or combined string, with a trailing NUL supplied by extraction. Both variable-name prefixes occupy 22 bytes. The memcpy writes exactly those 22 bytes, leaves the value and terminator untouched, and cannot cross either matching string's boundary. Total lengths including NUL remain 62 and 91 bytes.

The scan begins at `ip_endargv` and ends before `ip_endenvv`. Extraction NUL-terminates each environment string and fills trailing alignment bytes with zeros, so scanning the padding advances one byte at a time without leaving the area. No env count, string cursor, remaining-space count or string address changes during refusal. At copy-out, the kernel copies the full string area and then derives pointers by walking the unchanged string lengths (`kern_exec.c:6574–6745`). There is no pointer movement or neighboring-string corruption in this mechanism.

The finder identifies a value rather than retaining the actual append pointer. Thus an exact app/combined value deliberately appended by sweetexecenv can also be refused, even for a different executable path. This is conservative for privilege containment, but should be documented if explicit sweetexecenv overrides must survive.

### Composition and capacity remain bounded

Patch lines 160–179. App-only and combined strings use constant bounded values. A successful automatic append consumes one environment entry and one pointer slot. It requires `length + 3 * pointer_size` argument bytes and `length + pointer_size` string bytes, retaining 2058's space for the terminator and tail alignment. The combined length is 91 bytes including NUL. Arithmetic is small and bounded for both pointer widths. Failure of either capacity check skips the append and continues ordinary exec.

With both features enabled, matching Freeform receives a single automatic insertion with sweetsb first. Other UI apps remain on the existing sweetsb path. A retained inherited insertion or one appended by sweetexecenv sets `sweet_existing` and suppresses this automatic append. The patch does not deduplicate multiple variables explicitly supplied by sweetexecenv itself. Near-capacity inherited strings can still fail during copy-in/preflight before stripping can refund them, as in the earlier audit.

## Checks and limits

I read both patch versions, both host-check scripts, the design section, and the connected exec, credential, MAC, path-building and boot-arg implementations. `git apply --check` passed against the specified kernel tree. An independent in-memory application matched all six hunks with full context. Read-only Python assertions confirmed both prefix lengths and both full-string lengths.

I did not run the host scripts because they compile programs, create temporary trees and remove them. No phone, adb, fastboot, build, compiler, commit, source mutation or removal was performed. Only this report was written. No runtime claims are inferred from the host test authors' stated results.
