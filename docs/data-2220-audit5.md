# Patch 2220 fifth audit

Reviewed on 2026-11-03. Verdict: GO carries over for the configurations approved by audit4, subject to its remaining conditions. N1 is fixed. I found no new blocking defect in this delta.

The data session no longer needs the 120 s runtime cap as protection against transaction exhaustion. A longer data-session runtime still requires the modem owner's separate lifetime approval, independent deadman, worker ordering and bounded cleanup. This report does not approve an integrated image or indefinite modem operation.

## Revision and scope

The reviewed patch is `patches/data/2220-asteroids-data-qmi-session-and-link.patch`, SHA-256 `adda4660cc553d741f7c36e627b974874c65cdb0adf6037a525a15457784ae48`. It exactly matches the patch in commit `1f91d89`. The patch in `c24d354` has audit4's SHA-256 `28723c9bdc3d01119a3b0ae1a634c5e1cbeed81c1fc3c72d3aa0289ffb5a3660`.

All twelve added source and header files match `plans/data/src` byte for byte. References below use line numbers in that source directory. I reviewed the complete patch delta, the test delta, every issue() caller, all shared response-parser callers and the connected receive, timeout and cleanup paths.

The author's delta account is accurate. The implementation changes are the issue() teardown guard, the return after a NAS poll enters teardown, and shared rejection of duplicate result TLVs. The two header changes describe existing cleanup behavior. The supplied tests add the reserve-boundary restart case and a duplicate-result rejection case.

## N1 is fixed

`CellSession.c:114` rejects every outgoing message during STOPPING, STOPPED or FAILED except WDS Stop Network. This check runs before the only transport send in the session. Both the SEND macro and the direct Start Network path use issue(), so setup cannot send through a second route.

`CellSession.c:346` returns immediately if the NAS poll's transaction allocation entered teardown. The eligible IPv4 restart from audit4 therefore never runs on that pass. The central guard also covers continued execution inside the profile-settings loop at line 414 when an allocation enters STOPPING.

Transactions still never wrap or enter the raw transaction range. take_txn() reserves the final 64 session transactions, returns zero for the setup allocation that reaches the reserve, and allows cleanup allocations while ending. The reserve can lose a few numbers to rejected builders in an already running multi-SEND loop, but that loop is bounded to four entries and does not threaten the five-second, two-call cleanup budget.

The supplied regression recreates the due NAS poll with an eligible IPv4 restart and an IPv6 handle. Only Stop Network is sent after the boundary. My additional four-profile reply test crosses the same boundary inside the multi-SEND loop and confirms that no profile-settings request escapes the guard.

## Paths relied on by earlier audits

Late-start adoption remains intact. The new guard restricts sends, not receive processing. `CellSession.c:443` still validates a Start Network reply, preserves start_unknown on malformed input and adopts a valid handle during STOPPING. The fallback at lines 544-552 still matches a late start after its ordinary pending entry has been consumed or timed out. stopping() then sends the permitted Stop Network request.

Stop Network retries remain permitted during STOPPING. A malformed or duplicate-result Stop reply leaves the handle tracked. The pending entry is consumed, and the existing retry cadence sends another Stop Network request. A valid success or QMI_ERR_NO_EFFECT still retires the handle. A valid disconnected indication also retains its existing cleanup meaning. An unresolved start or handle at the stop deadline still latches UNRESOLVED permanently.

The WAIT_READY stop shortcut remains valid. Before readiness, info() sends only read queries, with no call handle or unresolved start. stop() moves straight to STOPPED. Delayed profile-list replies cannot issue follow-up requests, and later readiness cannot restart the stopped session. I exercised this sequence separately.

Normal info replies remain accepted with one valid result TLV. Responses during teardown retain the earlier receive filtering. WDS profile replies are ignored in the stop phase, and non-WDS replies cannot advance setup. The guard does not alter the read-only information path while the session is active.

## Shared response parsing

`QmiData.c:68-80` now requires exactly one result TLV of type 0x02. The existing first-result length check still requires four bytes, and the result/error consistency checks are unchanged. Valid success, valid failure and NO_EFFECT retain their existing return values.

The new scan follows parse_any(), which calls qcom_qmi_parse(). That parser validates the exact envelope length and every complete TLV extent before the scan runs. Each scan step advances by at least three bytes, and the body length is limited by the 16-bit QMI header. I found no new bounds or termination defect.

All response decoders using qmi_response() inherit the stricter rule. NAS and WDS indication paths continue to use envelope validation without requiring a response result TLV. Start Network already enforced a singleton result in audit4, so its accepted reply set is unchanged. Rejecting malformed duplicate results elsewhere can cause the existing retry or failure behavior, including unresolved cleanup for an unconfirmed stop, but it does not silently release ownership.

This closes the duplicate-result portion of audit4's N5. It does not close N5's opaque raw export or free-form text concerns. The other non-blocking settings-TLV shape gap also remains unchanged.

## Runtime decision

Audit4's GO for one bounded raw probe, one bounded session with sweetdata=1, and one bounded session with sweetdata=1 sweetdataonline=1 carries over. Raw-output restrictions, one successful attach per boot, readiness revocation, eight local QRTR slots, serialized worker ownership, IPA contracts, cleanup-result propagation and shutdown ordering all remain conditions.

The data-session reason for staying far below transaction 0x7fc0 is removed. Reaching that boundary now ends setup and enters cleanup safely. A long session still has a finite transaction budget and will terminate at exhaustion. This patch does not provide transaction recycling or continuous service forever.

The 120 s cap can therefore be lifted for the data session within a separately approved modem runtime lifetime. Keep the independent deadman and the owner's hard stop deadline. Keep receiving and polling through data cleanup, preserve EIO or unresolved EINPROGRESS in the final runtime result, and still attempt modem shutdown. The raw-only probe approval is unchanged.

I did not perform the separate modem lifetime audit here. Its approval must cover the actual owner revision and requested duration. Nothing in this narrow data review overrides its limits, readiness-monitoring budget or shutdown requirements.

## Validation and limits

`sh 3a-IOS/tools/data/check.sh` passed with AddressSanitizer and UndefinedBehaviorSanitizer against patch-extracted sources. The supplied regression passes. The script extracts its QRTR dependency from the current modem patch directory.

I also compiled and ran `tools/data/tests/out/audit5-probe.c` with the same sanitizers. It reruns the supplied suite and checks the profile-loop boundary, WAIT_READY stop with delayed info, duplicate-result start ownership, late valid adoption during STOPPING, duplicate-result Stop rejection, Stop retry and NO_EFFECT cleanup. Both suites passed.

I changed no implementation files, built no kernel and did not contact the phone. Writes were limited to this report and host-test artifacts under `tools/data/tests/out`. No files were removed.

The host suite excludes sweet_data.c. Actual kernel concurrency, hook ordering, IPA DMA, ifnet behavior and modem responses remain outside this host validation. The separate runtime and integrated-image approvals remain necessary.
