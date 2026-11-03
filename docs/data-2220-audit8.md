# Patch 2220 eighth audit

Reviewed on 2026-11-03. GO carries over for the bounded configurations approved by audits 4 through 7 and the 2221 re-audit. I found no new blocking defect in this delta. This revision is acceptable as a reviewed source replacement for frozen 2220, subject to recording the replacement in the cutoff and copied owner snapshot before claiming successful cutoff reproduction.

What this change does: the session accepts request-only APN authentication and delays IPv6 Start Network until IPv4 has usable settings. The kernel attachment validates the APN length and authentication before registering either data QRTR port.

## Scope and identities

I reviewed `git -C 3a-IOS diff 2319253 HEAD -- patches/data tools/data docs/cellular-data.md`, the complete connected session paths, the boot-argument parser, audits 4 through 7, `docs/data-2221-reaudit.md` and the actual workspace-root `plans/ipa/cutoff-2206.txt`. HEAD was `a83e3ff1d434e82d15510103287dda20817a91d0`. The requested delta paths had no working-tree implementation changes.

The new 2220 SHA-256 is `f331702b1bf1f5583a43bef11f76786f0da0000a543fbc987c2bf174af04359c`. The patch in baseline `2319253` hashes to `410ee41d9269ab7e6f97b1beb4c4550af28464b5503c5719790094bedc2ebdf8`. All fourteen added source and header files match `plans/data/src` byte for byte.

2221 remains byte-identical with SHA-256 `4bdc74ce4ad45000ba2efe18bf45454169dcb6464483d5722c918892875e1e49`. Source references below use line numbers under `plans/data/src`.

## APN and authentication

The author's account is accurate for the production attachment and session path. `CellConfig` carries `auth` and `v6_after_v4`. `sweet_data.c:434-442` maps absent authentication to 0xff, none to 0, PAP to 1 and CHAP to 2. Other strings return an error. The omitted-TLV default comes from the production initializer and updated host fixtures, so a separate zero-initialized CellConfig would explicitly request no authentication.

`sweet_data.c:458-469` validates session arguments before either QRTR registration at lines 472-473. The APN buffer holds 102 bytes. The local XNU `pexpert/gen/bootargs.c:153` copies at most buffer size minus one and terminates it through `argstrcpy2` at line 358. A 100-byte APN fits, while any token longer than 100 retains at least 101 bytes and fails the length check. This removes the former silent truncation. The eight-byte authentication buffer cannot truncate a longer invalid token into any accepted three- or four-byte value.

Invalid arguments return EINVAL with session and raw operation disabled, no data port registered, no session initialized and no attachment established. Raw-only mode continues to ignore session arguments. The successful-attach once latch at `sweet_data.c:449-451` and line 496 is unchanged. Partial QRTR registration failure remains terminal for that QRTR lifecycle under the inherited owner contract.

`CellSession.c:245` supplies the APN and selected authentication to Start Network with null username and password and the existing profile field. `QmiData.c:381-394` emits APN TLV 0x14, authentication TLV 0x16 when not 0xff, and family TLV 0x19. Production leaves the profile field zero, so it sends no explicit profile selector.

No persistent write is added. Neither the session nor the raw allow list can reach Create Profile or Modify Profile. Existing profile-list and profile-settings reads before readiness are unchanged, as are the other approved state-changing setup requests after readiness. The exact Start Network regression confirms the requested PAP TLV and the omitted-auth case.

There is no new production username or password input, decode or log path. Generic QMI builders already support those fields and profile writes, so a literal claim that such code does not exist would be incorrect. The session leaves the credential pointers null, and raw admission denies the credential-bearing profile-settings request and profile writes.

The attachment log says only whether an APN was supplied. The new refusal log does not print the rejected values. Existing profile name and APN logging remains read-only and unchanged. Earlier N5 restrictions on opaque raw output and untrusted free-form text remain necessary, so this audit does not promise credential secrecy for arbitrary vendor responses.

No actual carrier APN or operator help-page URL is supplied in this delta. The test uses `example.apn`. I verified request construction and PAP selection, not the provenance of the eventual live APN or the final image's boot arguments.

## IPv4 sequencing, budget and ownership

`CellSession.c:239` gates only IPv6 Start Network. IPv6 Bind Mux and Set IP Family may occur earlier, behind the existing readiness barrier. IPv4 becomes up only after a successful start with a nonzero handle and settings with a usable IPv4 address. This is the state-machine meaning of IPv4 working here. It does not prove external packet connectivity.

The gate precedes the attempt increment and transaction allocation. Waiting for IPv4 therefore consumes no IPv6 Start Network budget. The existing 20-attempt limit, six-second start cadence, transport backoff and reset only on usable call settings remain intact. Calls configured without IPv4 can still start IPv6 because the gate checks whether IPv4 was requested.

`CellSession.c:185-190` exhausts waiting IPv6 when IPv4 exhausts only if IPv6 is neither up nor holding a handle or unresolved start. Pending bind or family replies cannot restart that exhausted call because call_step rejects exhausted calls before doing work. If both calls are exhausted, the existing failure path enters cleanup.

An IPv6 call that started after IPv4 came up retains its ownership if IPv4 later drops and exhausts. An IPv6 handle, an unresolved start and an up presentation each prevent the new shortcut from exhausting it. Existing stopping code at lines 273-289 scans both calls regardless of exhaustion, counts unknown starts, sends Stop Network for handles and permanently latches unresolved cleanup at the five-second deadline.

Late-start matching, malformed-start retention, successful handle adoption during STOPPING, Stop Network retries and cleanup-result reporting are unchanged. My additional host checks exercise delayed IPv6 handle adoption after readiness revocation, IPv4 exhaustion while IPv6 is up, and IPv4 exhaustion while IPv6 has an unanswered start. Each owned IPv6 call receives cleanup and reaches CLEAN only after confirmed stop.

## Inherited safety and cutoff compatibility

The readiness barrier, read-only pre-barrier queries, teardown send guard and transaction cleanup reserve are unchanged. DataServices still revokes before service synchronization and grants afterward. Receive, discovery, poll and USIM paths still use that helper. No new route bypasses readiness or revives terminal setup.

The changes do not alter the exported data hooks, IPA adapter API, TX callback lifetime, QMAP framing, link presentation, worker ownership or cleanup interface relied on by the 2221 re-audit. The frozen 2205 and 2206 hashes still match that report, and neither patch touches `bsd/dev/data`. Their worker ordering and quiescence contracts therefore remain applicable to this source replacement. I did not rerun the broader IPA reproduction or approve later IPA patches in this narrow audit.

The frozen cutoff and `plans/ipa/snapshots/data/2220-asteroids-data-qmi-session-and-link.patch` still identify the old `410ee41d...` bytes. `plans/ipa/tools/reproduce-cutoff.py` checks copied owner byte equality before reproduction, so the current new public patch and old snapshot fail that prerequisite. This is a required integration bookkeeping update, not a kernel defect. Replace the copied 2220 and record the new full hash and this report, then rerun cutoff reproduction and the integrated checks before relying on a new integrated image.

Keep the 2221 re-audit's guarded 600-second runtime, exact independent `sweetdeadman=900`, required gates and harness deadline below 20 minutes. Keep eight QRTR slots, one successful attach per boot, serialized fresh USIM and IPA evidence, per-control-packet discovery, bounded cleanup with error propagation, IPA quiescence before detach, retention on uncertainty and RAM-only EFS behavior. Earlier parser and raw-output limitations remain unchanged. Separate image and hardware approval is still required before a phone boot.

## Validation and limits

`sh tools/data/check.sh` passed freshly from `3a-IOS` with warnings as errors, AddressSanitizer and UndefinedBehaviorSanitizer. The data suite, service-cache suite and 2221 adapter stub suite passed against patch-extracted sources. The new session_carrier test checks exact IPv4 APN/PAP/family bytes, delayed IPv6 start, permanent IPv4 refusal, absence of profile writes and omitted authentication.

I also wrote and ran `tools/data/tests/out/audit8-probe.c` with the same sanitizers. It reruns the maintained data suite and checks the IPv6 ownership cases above plus all twenty retryable IPv4 refusals with no IPv6 start. The latter terminates with both calls exhausted and exactly twenty Start Network requests.

The maintained host suite does not compile sweet_data.c. Argument validation, attachment ordering and kernel log paths were checked statically against the local XNU boot parser. Kernel concurrency, actual modem authentication, external connectivity, DMA and the final integrated image remain unverified.

I edited no implementation files, built no kernel and did not touch the phone. Writes were limited to this report and host artifacts under `tools/data/tests/out`. No files were removed.

Verdict: conditional bounded-trial GO carries over to the exact new 2220 hash. Accept the reviewed replacement in the cutoff after updating its recorded bytes and passing integration reproduction. This report grants no independent image or phone approval.
