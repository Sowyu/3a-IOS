# Patch 2220 fourth audit

Reviewed on 2026-11-03. The patch SHA-256 is `28723c9bdc3d01119a3b0ae1a634c5e1cbeed81c1fc3c72d3aa0289ffb5a3660`, matching the requested revision. All twelve added source and header files match `plans/data/src` byte for byte.

I read the third report first and consulted both earlier reports. I verified that Git revision `f3689d3` contains the third audit's exact `65b6f8419d87a7114b566d658e34d88e7995f9398967425c6a501e4fb6164a2b` patch. I reviewed the complete patch difference from that revision, the affected callers, host tests and wiring contracts.

References below use source line numbers relative to `plans/data/src`. They also identify the added files under `bsd/dev/data`.

## Configuration decisions

1. `sweetqmiraw=1` alone is GO for one bounded read-only probe under the raw-output restrictions below.
2. `sweetdata=1` without `sweetdataonline` is GO for one bounded session trial under all conditions below. It can start calls if the modem is already online. It also changes NAS subscription, DPM and WDA state after readiness.
3. `sweetdata=1 sweetdataonline=1` is GO for one bounded session trial under all conditions below. This permits the automatic DMS ONLINE request after readiness.

These are conditional decisions about this exact data patch with the documented wiring. They do not approve an unreviewed integrated image, IPA hardware setup, modem lifecycle changes or provisioning. An unrestricted or extended session that can reach transaction exhaustion is NO-GO pending N1's fix.

## Conditions for GO

Keep the existing 120 s runtime deadline after authentication and the independent deadman. This report does not approve extending the probe lifetime. Keep the session far below transaction `0x7fc0`, the first reserved cleanup transaction, and perform one successful attach per boot. N1 remains a real defect outside this bounded scope.

Use eight QRTR local slots and unannounced service-zero ports. One modem worker must own QRTR receive, service changes, USIM readiness, session polling and IPA RX callbacks. Install the immutable IPA TX wrapper before data attach. Attach after QRTR initialization and other port registration, before QRTR start and MPSS authentication.

Readiness requires the selected primary GW USIM at app state 7, the completed IPA handshake, running endpoints and the installed TX callback. Revoke it immediately on UIM receive transitions, stale or fatal status, UIM endpoint loss or replacement, QRTR DEL_SERVER and GLINK loss. Serialize IPA readiness revocation on the same worker before further session work. Latch data_stopping before teardown and prevent readiness revival.

Use QMAP v1 with checksum offload disabled, 8192-byte completed RX buffers and at most 16 datagrams. TX must copy before returning, with kernel-lifetime callback and context. RX must supply actual completed bytes and run outside the IPA TX queue lock. Allow an already accepted frame to complete when a flow-disable command arrives.

Stop must keep receive and poll running through cleanup. Inspect `sweet_data_cleanup()` before detach. Treat both EIO and a still-pending EINPROGRESS at the owner's deadline as failure, preserve that failure in the final runtime result, and still attempt modem shutdown. Make IPA unavailable and drain its AP channels before detach, then perform SSCTL and PAS shutdown. Preserve the separately reviewed RAM-only EFS behavior and never write a partition.

Raw access may use only the audited standard read schemas. Do not log or save complete opaque responses, indication snapshots or unfiltered free-form modem text. No full IMEI, ICCID, IMSI or phone number may be exposed. N5 means this patch alone cannot guarantee that rule for unexpected vendor TLVs. A caller requiring unrestricted response export needs filtering first.

## Disposition of the requested findings

### B3 fixed

`QmiData.c:462` parses Start Network through the validated QMI envelope and result. `QmiData.c:470` requires exactly one four-byte result TLV and at most one four-byte handle TLV. `QmiData.c:475` rejects successful responses without a nonzero handle.

`CellSession.c:443` clears start_unknown only for a nonnegative parser result. Malformed replies retain ownership, block another start for that client and remain counted during STOPPING at `CellSession.c:268`. The matching late-response path at `CellSession.c:547` remains available after the ordinary pending entry is retired.

The supplied session_audit3 regression passes for malformed replies before and during stop, duplicate handles, late valid adoption and a valid failure result. My extra sanitized checks reject duplicate result TLVs and oversized handle TLVs. A start still unresolved at the five-second deadline now produces an explicit unresolved cleanup outcome.

### N1 partly fixed

`CellSession.c:96` refuses allocation at `CELL_TXN_RAW`. `CellSession.c:97` reserves the last 64 transactions and switches setup into STOPPING without allocating its triggering request. `CellSession.c:103` advances every successful allocation. SEND at `CellSession.c:130` and the direct start path at `CellSession.c:239` send nothing for transaction zero. The terminal-number reuse reported in audit3 is fixed.

A caller can still continue setup after that transition. In `CellSession.c:344`, a due NAS poll can trigger exhaustion. Execution then reaches `CellSession.c:350` and calls the IPv4 setup step even though the session is now STOPPING. If IPv4 is eligible to retry Start Network, `take_txn` now sees an ending session and hands it a reserved transaction. A new Start Network is sent during teardown.

I reproduced this with ordinary registration, bind and retryable start-failure responses, then seeded only next_txn at `0x7fc0` to reach the boundary without running for hours. The next poll entered STOPPING and sent Start Network with transaction `0x7fc0`. The new start remained owned and its subsequent successful response caused Stop Network, so this does not recreate B3's silent handle loss.

Exact fix: reject every non-Stop-Network message in `issue` when ending the session, before transport send. Also return from the REGISTER/CALLS branch if ending immediately after its NAS SEND. The central guard covers multi-SEND callers such as the profile-settings loop at `CellSession.c:412`. Add a regression with an eligible IPv4 restart when the NAS allocation reaches the reserve. Assert that only Stop Network messages can be sent after the transition.

This is non-blocking for the bounded trial above. It blocks approval for operation that can reach exhaustion. The author's claim that all remaining sends are cleanup at that boundary is false.

### N4 fixed for the third report's requested changes

`QmiData.h:150` adds has_family. `QmiData.c:501` requires any family TLV to occur once with exactly one byte. `CellSession.c:377` compares every present value, including zero, to the call family. `QmiData.c:500` rejects duplicate IPv4-address and IPv6-address TLVs.

The supplied explicit-zero-family regression passes. My extra checks reject duplicate IPv4 addresses and an oversized family TLV. The existing address-class restrictions remain intact.

There is a smaller remaining hardening gap at `QmiData.c:500`. It ignores the address size results written into ok. A single oversized address TLV still passes the earlier minimum-length lookup, and a short address TLV can be ignored if the other address family is present. Exact fix: check each address count and its ok result separately before reusing ok. This is not an ownership or bounded-buffer defect and does not block this trial.

### N6 fixed for the supported layout

`Qmap.c:69` recognizes flow enable and disable only at exactly 16 bytes. Other request lengths that fit the reply buffer receive unsupported and leave flow unchanged. Header reserved-bit rejection remains at `Qmap.c:67`, and `sweet_data.c:682` checks the mux before calling the helper.

The supplied 16-byte ACK and 20-byte unsupported tests pass. My extra check exercises every length from 8 through 64 with sufficient output capacity and confirms that only length 16 changes flow. Oversized requests that cannot fit sweet_data's 64-byte reply buffer are dropped without changing flow, rather than receiving unsupported. This is a bounded-buffer refusal, not an ACK of a malformed command.

## New cleanup API

`CellSession.c:277` latches unresolved when the stop deadline expires with a live handle or unknown start. `CellSession.c:634` reports PENDING while active, UNRESOLVED for that latch or remaining ownership, and CLEAN otherwise. A later response cannot erase the latched deadline failure.

`sweet_data.c:618` maps these outcomes to EINPROGRESS, EIO and zero. Detach does not reset the session or session_on, so the outcome remains available after detach. Disabled data and failed port attachment return clean because no session ran. The distinction between terminal state and confirmed cleanup is now exposed to the modem owner.

The header's phrase "every WDS call was stopped with a successful reply" is too narrow. `CellSession.c:249` also retires a handle on a valid disconnected indication. CLEAN means no tracked live handle or unresolved start remains, without a prior unresolved deadline. Exact documentation fix: describe that ownership condition and include disconnected indications as confirmation. No implementation change is needed for this wording issue.

The modem README records a stop draft that treats every nonzero cleanup outcome as failure while preserving PAS shutdown. Its draft generator and worker test contain that query and the data_stopping latch. This supports the contract assumption. It is not an audit of a frozen integrated kernel.

## Remaining blocking findings

None for the three bounded configurations under the stated conditions. B3, the remaining blocker from the third audit, is fixed. N1 must be fixed before approving a session that can exhaust its transaction space. Unfiltered raw export remains outside GO because of N5.

## Other non-blocking findings and evidence

N5 remains open at `sweet_data.c:421`, `sweet_data.c:436` and `QmiData.c:68`. Raw response and indication exports still preserve opaque TLVs, and most response decoders still select the first result TLV. Profile, revision and operator text remain free-form. Exact hardening fix: export only audited fields, constrain log text and reject duplicate critical result TLVs in the shared response parser. The new start parser applies its own stricter ownership check.

The earlier one-attach, partial-registration and stale session-time limitations remain. Treat failed attach as terminal for that QRTR lifecycle. Poll promptly during teardown because readiness and service callbacks still precede the session time update at `sweet_data.c:572`. No other implementation changes since the verified audit3 revision were present beyond the transaction handling, start parsing, settings parsing, QMAP layout checks and cleanup API reviewed above.

The new USIM evidence is real. `plans/modem/logs/live-uim-usb-21871-20261103-181101.txt:51` reports primary_GW=0 and USIM_ready=1. Line 53 identifies slot 1, app 0, type 2, state 7. Later fresh polls repeat it. This establishes readiness in that offline diagnostic without requiring the provisioning draft. It does not replace fresh readiness checks in the integrated session trial or establish IPA readiness.

## Validation and limits

`sh 3a-IOS/tools/data/check.sh` passed with AddressSanitizer and UndefinedBehaviorSanitizer against patch-extracted sources. I added `3a-IOS/tools/data/tests/out/audit4-probe.c` and ran its sanitized executable. It reruns the existing suite, reproduces N1's remaining boundary defect, confirms ownership and cleanup of the unexpected start, and checks the additional parser and QMAP cases described above.

I edited no implementation files, built no kernel, contacted no phone and wrote no partition. Review artifacts and host binaries were written only under `3a-IOS/tools/data/tests/out`, plus this requested report. No files were removed.

The host suite excludes sweet_data.c. Kernel sysctl concurrency, actual hook ordering, ifnet behavior, kernel stack use, DMA operation and real session responses remain static-review limits. The GO decisions depend on the documented worker and lifetime contracts and separate approval of the integrated modem and IPA code.
