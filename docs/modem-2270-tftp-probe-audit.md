# GO for one supervised offline TFTP request probe

I grant fresh source and exact-image GO for exactly one offline service4096 probe through the pinned `run-tftp-probe.py` runner. This includes its first advertisement. It permits fixed denial replies only, with the existing PAS4 shutdown and uncertain-stop retention policy.

The manifest remains `approved=false`, with no consumed attempt. I verified all 138 pins, including the brief added after its text was written. Final admission may add this report pin and set approval true after live admission checks, as specified in the brief. Any other changed reviewed byte requires review again.

## Exact artifacts

- Image `plans/modem/out/tftp-probe-2270-boot.img` SHA256 `d44e7eca648bb753b839392e1fdd1782ce7bdfb7ed86b7cc26ba0453cfb13f83`.
- Kernel SHA256 `3fcc55de8ab81a74b1a520ef8fddc290fb7cb8b9aa9eafdb3ef94d501e20c5ad`.
- Reviewed manifest SHA256 `435fd504dac90b8a1b98df8987d4628136eba5ba9da90bd4cc5ce3e31cb0094c` before final admission.
- Patch2270 SHA256 `6efe561a53dfb91a0a697f4da0dbbae753a3139b4df9e8fae84c6dcb1df7c43f`.

I read the full brief first, then `3a-IOS/HANDOFF.md`, the modem README, current modem documentation and actual artifacts. The old clean-recovery statement is historical evidence only. Service tier remained default throughout this review.

## Source and transport

The 57 frozen source files match the production tree and their hashes. Ordered reconstruction from pristine tracked source with modem2270, data audit10/2221 and IPA through2218 reproduces all 57 exactly. Compared with the previous audit10 snapshot, only `ModemRuntime.inc` changes and `ModemTftpProbe.h` is added. The saved full-build log ends successfully, and independent image verification matches all ten packed kernel segments against the production kernel, which matches the frozen kernel hash. I did not rebuild the kernel during review.

The actual image line is 556 bytes. Exact image arguments match the pinned text, have the 600-second deadman and framebuffer/selftest guards, and exclude IPA, data, ONLINE, raw QMI and long-session admission. Exact single-token dependencies are checked before runtime hardware mapping. Bare, duplicate and numeric-alias required gates fail closed. Forbidden gates use the same strict parser as their actual runtime consumers. Probe-off code neither registers the service nor runs its callback.

`m_glink_prepare` registers service4096 with packed instance value1, meaning version1 and instance0, at port0x4006. The offline image uses five of eight local slots. QRTR HELLO announces actual nonzero services, with no invented file-transfer port or state. The actual QRTR receive path checks packet extents and destination node, then dispatches the borrowed payload to the registered port callback. The callback keeps no borrowed pointer.

RRQ/WRQ parsing bounds payload acceptance to480 bytes, path to127 characters, mode to octet and options to eight pairs with24-character components. Paths are zero-initialized and sanitized for bounded logging. Valid requests receive exactly15 bytes of ERROR/access-denied. Malformed RRQ/WRQ receive ERROR/illegal-operation. Other opcodes, including ERROR, receive nothing, so the probe does not create a TFTP error loop. There is no file open, storage interface, allocation or transfer-data state in the new path.

The budget counts every delivered datagram, including ignored opcodes. At most32 requests are logged and at most64 receive denial replies. Datagram65 latches failure. Failed reply enqueue also latches failure, later callbacks become inert and the owner loop enters cleanup. Queue acceptance is not proof of delivery. Existing QRTR resume acknowledgments survive queue pressure, while ordinary reply loss is terminal. Failed service announcement enqueue sets runtime failure through the same bounded queue even though the generic announcement helper discards its return value.

## Failure and storage paths

All post-mapping exits reach existing cleanup. Preparation, shadow loading, ownership assignment, votes, firmware loading, authentication, readiness, fatal transport and request-budget failures retain the existing stop obligations. A callback failure suppresses graceful SSCTL and uses the bounded stop-bit/PAS fallback. A healthy stop can continue servicing bounded denial replies while SSCTL waits.

Unverified PAS4 shutdown returns before reclaim, firmware/shadow freeing or power removal. Verified PAS4 with incomplete ownership or power cleanup also retains backing. Shared clock/power floors remain through the guarded reboot. IPA is gated off and its cleanup takes the disabled path. M2 remains incomplete.

PAS4 loads stock signed firmware through read-only `modem_a` reads and firmware authentication. Rmtfs loads bounded partition shadows using `sweet_ufs_read_part`, then copies writes into RAM shadows only. Neither the patch, runtime nor rmtfs calls a persistent partition writer. No partition/EFS write, PAS15/GSI, ONLINE, modem reset request or raw-QMI operation is authorized. Existing selftest completion and deadman reboot behavior remain in scope, along with devloop root hook/log/frame/marker writes.

## Runner and admission

The runner validates approval, all pins, exact image arguments, duplicate arguments, offline gates and actual generated selftest bytes before taking the hold. It refuses a consumed attempt. Under the atomic lock it requires an idle same-device clean recovery, exact original profile bytes and a freshly pulled root hook whose composed bytes match the pinned root script. It revalidates pins and atomically consumes the attempt before its single child launch.

The child receives `--no-replay`, `--boot-tries 1` and `--button-wait 0`, with no profile or file payload. The only injected root file is the reviewed devloop selftest hook. Its body waits240 relative seconds, checks a screenshot, writes the pinned fresh completion marker and finishes15 seconds later. The normal offline runtime remains120 seconds after authentication, under the independent600-second guard.

Every exit after launch, including child-launch failure, passes through same-device recovery, clean-root and exact unchanged-profile checks before releasing claims. Failed checks or pulls retain ownership. There is no profile PUT, restoration call, overwrite exception, automatic replay or image retry. Prelaunch failures release claims without launching or changing the profile. Claims use absolute recoverable Trash commands.

The host runner has no single wall-clock timeout around `process.wait()`, and sequential devloop recovery/collection timeouts can exceed20 minutes. This GO therefore requires the explicit supervision in the brief, with total ownership kept below20 minutes and ownership retained on uncertain recovery/profile state. It does not approve an unattended runner. Live clean-root/profile admission is still required immediately before launch.

## Independent checks

1. All138 manifest pins, all57 frozen/live source hashes, ordered57-file reproduction and ten packed image segments passed.
2. Actual production parser/gates/callback ASAN/UBSAN checks and fake-phone runner approval/pin/attempt/conflict checks passed.
3. Added independent host assertions passed for127/128-character paths, eight/nine option pairs, mixed-case octet, path sanitization and all65536 opcodes.
4. Copied modem lifecycle/control/QRTR/rmtfs, joined shutdown/handover, bandwidth and worker checks passed with sanitizers.
5. Copied IPA hardware/worker/data-stop and real2221 partial-binding checks passed with sanitizers.

Fresh lifecycle/datapath output is preserved under `/tmp/sol-2270-host-5u90p1mb/`. Parser/callback fixtures are under `/tmp/modem-tftp-h9_lrytz/`, and extra boundary assertions under `/tmp/sol-2270-boundaries-_be8vok8/`. Copied host fixtures changed only temporary-directory settings to `/tmp`; frozen test outputs were not overwritten. An initial extra-check compile failed because the fixture omitted a use of the header's gate function under `-Werror`; the retained corrected fixture exercises that gate and passes.

These are host models and source/image checks. They do not prove firmware requests, successful discovery on hardware, successful file service or the cause of the previous QMI refusal. The local HakiOS reference supports the TFTP hypothesis but concerns another platform and another error. Sanitized paths can still contain private values, so raw request logs must remain local and reports must omit such values.

Before the one launch, pin this report and recheck unchanged artifacts, unused attempt, actual idle recovery, clean root and exact original profile. Stop this approach after two equivalent unexpected resets. No phone operation, implementation edit, delegation, commit, upload or permanent deletion occurred during this review.
