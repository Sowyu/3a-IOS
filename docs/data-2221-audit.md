# Patch 2221 audit

Reviewed on 2026-11-03. NO-GO for a bounded integrated session trial with the supplied dependencies. The adapter's types, bounds, copy lifetime and lock use are sound. The published worker has no adapter calls, and the IPA readiness latch does not provide immediate failure revocation.

What this change does: 2221 connects 2220's data hooks to 2202's IPA transmit and receive API. It also exposes a worker helper that copies the IPA readiness flag into the data layer.

## Revision and scope

2221 SHA-256 is `38442c83c692bb3bd05851d8f70e712ea52100072000e113ca1d9ab955a010ff`. Its bytes match commit `bf895b2ecb9529ba994ee5dbd1ecdc7dc16ae321`. Both added files match `plans/data/src2221` exactly.

2220 SHA-256 is `adda4660cc553d741f7c36e627b974874c65cdb0adf6037a525a15457784ae48`, the revision approved by audit5. Its added data files match `plans/data/src` exactly. I read audit4, audit5, the agreed interfaces in `docs/cellular-data.md`, and the IPA and modem README contracts under the workspace's top-level `plans` directory.

2202 SHA-256 is `2b1dfb705a74560a396550513b8082dcbc3105da31cb00cf7e81ff1def9a1555`. 2189 SHA-256 is `45db8e1d6939fa23793c2d42c855ff6390f89f922129918e73427907b2c51518`.

References to `bsd/dev` files use their resulting source line numbers from the patches. References to patch files use literal patch-file line numbers. This review covers the binding and the connected readiness and worker paths. It does not approve the complete IPA hardware implementation or an integrated boot image.

## Must fix before the integrated trial

### 1. Refresh can copy true after IPA has failed

`bsd/dev/data/sweet_data_ipa.c:45` trusts `sweet_ipa_ready()`. At `bsd/dev/modem/IpaData.inc:19`, that function reads only `ipa_txq.ready`. `IpaSession.inc:22` and `IpaSession.inc:29` return EIO on data-path or QMI failure without clearing that flag.

A concrete sequence is successful handshake, queue readiness true, then a failed GSI completion or loss of the bound IPA QMI service. `ipa_session_poll()` reports EIO, but calling `sweet_data_ipa_refresh()` at that point still publishes true. The 2202 runtime clears the queue flag at its out label, which is too late to support the header's promise that refreshing on any failure revokes readiness before further session work.

There is also a receive-order gap. 2189 drains QRTR at `patches/modem/2189-asteroids-modem-data-worker-hooks.patch:159` before its data poll at line 161. A DEL_SERVER for IPA can precede a data response in that same drain. IPA service removal does not revoke USIM readiness, and `sweet_data.c:440` sees the old copied IPA flag before processing that response. Refreshing only immediately before the later poll cannot prevent that response from advancing setup.

Exact fix: make 2202's queue-ready flag an authoritative operational latch. Clear it under `ipa_queue_lock` on every detected GSI, handshake, IPA service, transport or fatal failure and before endpoint shutdown. Do this before returning a failure or dispatching another consumer callback. Preserve the existing handshake-only transition to true and forbid revival once stopping or failed.

On the same worker, publish the false state to data immediately after clearing it. Check the bound IPA service identity after each QRTR control update and before another QRTR data callback can run. Refresh before data-capable receive dispatch, before service processing and before poll, including cleanup polling. Fix the overbroad ordering claim at `sweet_data_ipa.h:9` to describe this complete ordering requirement.

A stronger lifecycle guarantee is required from 2202. A separate exported function is optional if the existing queue latch is made authoritative. Merely checking handshake bits once in 2221, or adding a refresh after poll, does not fix revocation.

### 2. The supplied worker never binds or refreshes the adapter

`patches/modem/2189-asteroids-modem-data-worker-hooks.patch:150` leaves IPA installation as a comment before attach. Lines 112 and 161 call data poll without refreshing IPA state. The supplied 2202 and 2221 patches add no runtime call to either adapter entry point.

With these files alone, adding the adapter translation unit does not connect the driver. The TX hook remains absent and the data readiness barrier stays closed. This fails safely, but it cannot perform the proposed integrated trial. The documentation describes future wiring rather than executable wiring.

Exact fix: add the adapter header to the integrated modem runtime. For a `sweetdata=1` session, call `sweet_data_ipa_bind()` once after IPA preparation and before data attach, QRTR start and MPSS authentication. Require return zero before proceeding. A failure must abort the session for that boot and preserve its errno. Skip binding for raw-only and disabled data configurations.

Add refresh calls with finding 1's receive and failure ordering, plus a refresh before every normal and cleanup data poll. Keep the stopping latch terminal. Review the resulting merged runtime, because 2189 and 2202 both modify its attach, loop and exit paths.

### 3. AP-channel quiescence is still a placeholder

`patches/modem/2189-asteroids-modem-data-worker-hooks.patch:117` comments that IPA must quiesce before detach, then line 119 detaches immediately. In 2202, `ModemRuntime.inc` patch lines 72 through 75 clear queue readiness at exit but stop hardware only after `m_cleanup()` and verified modem stop. Clearing the software queue does not drain or stop DMA channels.

Audit4 requires unavailable IPA and drained AP channels before data detach, followed by SSCTL and PAS shutdown. The supplied pieces do not implement that order. Callback lifetime being permanent does not satisfy the DMA shutdown condition.

Exact fix: after bounded WDS cleanup and its outcome query, revoke IPA readiness and quiesce the AP packet channels before `sweet_data_detach()` and SSCTL. Propagate quiesce failure into the final runtime error, still attempt modem shutdown, and retain hardware backing and required power whenever DMA stop is uncertain. Keep PAS15 release and shared-resource cleanup subject to verified MPSS shutdown. Review that integrated ordering before a phone trial.

## Binding checks that pass

The TX wrapper at `sweet_data_ipa.c:13` exactly matches 2220's `int (*)(void *, const uint8_t *, uint32_t)`. Passing its buffer to `const void *` and widening its length to size_t are safe. Mapping every nonzero IPA errno to -1 matches 2220's accepted/refused convention. Queue-full and down-path errors remain refused frames. 2220 does not require distinct retry errors.

The RX wrapper at `sweet_data_ipa.c:21` exactly matches `sweet_ipa_rx_fn`. Its 8192-byte check precedes narrowing size_t to uint32_t, so large values cannot wrap into accepted lengths. The current limit fits uint32_t. The QMAP v1 static assertion matches WDA framing, and the advertised 8192-byte and 16-datagram limits match `SweetIpa.h`.

`IpaTxQueue.h` copies accepted TX bytes into owned storage before return. The static wrappers and NULL contexts have kernel lifetime. On RX, `sweet_data_ipa_rx()` parses synchronously, `sweet_data.c:157` and line 158 copy delivered headers and payloads into mbufs, and IPA reposts its buffer after the callback returns. No new borrowed pointer survives the call.

There is no new lock inversion. The ifnet output path releases `sd_lock` at `sweet_data.c:184` before TX at line 193. The command ACK calls TX at line 693 without that lock. `IpaData.inc:29` releases the IPA queue lock before invoking RX at line 30, so an ACK can acquire it without recursion. The refresh getter releases that lock before the data readiness store.

## Bind order and failure handling

`sweet_data_set_ipa()` refuses when sweetdata is disabled, attach already happened or a TX hook already exists. Mapping its generic -1 to EPERM is coarse but consistent with the adapter's errno return contract. Bind is deliberately one-shot, and a second call is not an idempotent success.

At `sweet_data_ipa.c:35`, an EBUSY receiver refusal occurs after TX installation succeeded. The TX hook remains installed and cannot be replaced through the current setter. This is a permanent partial bind for that boot. It is safe under finding 2's required terminal-abort rule because no data attach, readiness publication or modem session follows it. No rollback is needed for that bounded contract.

Ignoring the error would be unsafe. A later refresh could authorize a data session with TX installed while another receiver owns RX. Document the partial-bind result in `sweet_data_ipa.h:5` and test that the worker never attaches or refreshes after any bind failure. Reordering registration first only moves the partial-install problem to the other API.

## Readiness interpretation

Initial assertion is sufficiently strong in the reviewed 2202. `IpaSession.inc:31` is the only true setter, reached after successful data polling, hardware readiness, QMI completion and an empty runtime transmit queue. `IpaQmi.h:101` through line 105 require accepted initialization, registration, modem completion 0x35 and the completion indication. Firmware-only mode never sets the queue ready.

The problem is continued validity after assertion. A mutex-protected read of a stale latch does not prove that endpoints and the QMI relationship remain usable. Refresh-before-poll serializes a current false value correctly because `sweet_data_poll()` calls `sd_readiness()` before service changes and session poll. It cannot repair a stale true source or revoke readiness before earlier receive callbacks.

## Validation and decision limits

I verified patch identities and source mirrors with read-only Python comparisons and traced the actual callers and state setters. I did not edit implementation files, compile host code, build kernels, contact the phone or remove files. The only written artifact is this report.

Before re-review, add host regressions for receiver EBUSY after TX installation, successful-handshake then GSI failure, IPA DEL_SERVER followed immediately by a data setup reply, and stop with queued TX and active RX. Check that failed binding never reaches attach, readiness becomes false before another setup send, no callback takes the IPA lock recursively, and quiescence precedes detach.

Audit4's bounded runtime, independent deadman, single attach, serialized worker, raw-output restrictions and cleanup-result propagation remain required. Audit5's transaction fix does not remove these conditions. This review does not grant the modem owner's separate lifetime approval or approve IPA hardware operation.

Verdict: NO-GO for the supplied integrated trial. Fix findings 1 through 3 and review the frozen integrated runtime before any phone boot.
