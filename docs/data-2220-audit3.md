# Patch 2220 third audit

Reviewed on 2026-11-03. The patch SHA-256 is `65b6f8419d87a7114b566d658e34d88e7995f9398967425c6a501e4fb6164a2b`, matching the requested revision. All twelve added source/header files match `plans/data/src` byte for byte. Source references below are relative to that directory and also identify the added files under `bsd/dev/data`.

## Configuration decisions

1. `sweetqmiraw=1` alone is GO for a bounded read-only probe, subject to the wiring and schema conditions below. It sends no automatic session traffic and cannot issue the removed DMS/NAS setters or create WDS handles. This does not approve arbitrary vendor response exports.
2. `sweetdata=1` without `sweetdataonline` is NO-GO. B3 remains partly fixed. This configuration sends subscription, DPM and WDA changes after readiness and can start calls if the modem is already online.
3. `sweetdata=1 sweetdataonline=1` is NO-GO. The same B3 cleanup hole applies, including during shutdown. No state-changing session boot is approved by this report.

These are decisions about this exact data patch under the requested documented wiring. They do not approve a new modem runtime, IPA hardware setup, SIM provisioning, partition write or phone boot performed by this review.

## Scope and conditions

I read both earlier reports before reviewing the patch, sources, host tests, local QMI definitions and the three integration documents. I changed no implementation files, built no kernel and did not contact the phone. I wrote this report and host probe artifacts only under `3a-IOS/tools/data/tests/out`.

The integration must reserve eight QRTR local slots, preserve unannounced service-zero ports and give one modem worker ownership of QRTR, session receive, service changes, USIM updates and polling. Use selected USIM app state 7, never merely card presence or state 1. Install the immutable IPA wrapper before attach, with QMAP v1, checksum offload disabled, 8192-byte completed RX buffers and at most 16 datagrams.

The TX wrapper must copy before returning and retain a kernel-lifetime callback/context. RX must run on the worker outside the IPA TX queue lock and pass actual completion bytes. Readiness changes must be serialized with worker session work. The atomic IPA setter alone does not make a concurrent revocation and send indivisible.

Stop must keep pumping receive and poll for cleanup, then drain and disable IPA, detach data, and only then shut down QRTR and the modem. Preserve the independent hard deadline, deadman and explicit unconfirmed-cleanup failure. RAM-only EFS and the ban on partition writes remain requirements for the separately reviewed modem runtime.

The raw GO assumes the audited standard read schemas in the supplied local definitions. No known allowed schema necessarily exports a full IMEI, ICCID, IMSI or phone number. Unknown modem TLVs and free-form text are still opaque, as explained under N5. If the integration needs an unconditional guarantee for vendor extensions, it must filter exports before enabling the raw interface. No full response dumps or identifiers are approved for logs or storage.

## Disposition of B1 to B5

### B1 fixed

`QmiRaw.c:24` and `QmiRaw.c:25` exclude DMS 0x2e and NAS 0x33. Every admitted message is a read in the supplied schemas. The old setter validation branches at `QmiRaw.c:62` and `QmiRaw.c:70` are unreachable because the allow-list check rejects their IDs first.

Reset, profile writes, raw Start/Stop Network, WDA Set and DPM remain excluded. Client 1 remains rejected at `QmiRaw.c:103`. The raw-only configuration therefore cannot bypass readiness by changing operating mode or acquire unowned call handles.

### B2 fixed

`CellSession.c:146` sends only revision, suffix-decoded IDs, operator name, signal info and profile-list queries. Profile-settings follow-up at `CellSession.c:405` is also a read. NAS Register Indications moved to `CellSession.c:287`, after readiness and entry into CS_DPM, with a once-only flag.

This subscription is now before DPM Open Port, rather than after ONLINE as the sequence narrative says. That ordering meets the requested safety barrier. Its acknowledgment is not required for setup to proceed, so periodic serving-system polling remains necessary.

### B3 partly fixed and still blocking

`CellSession.c:235` records queued starts separately from the pending table. `CellSession.c:223` prevents another start while its outcome is unknown. `CellSession.c:530` adopts matching late WDS responses by client, transaction and message, and `CellSession.c:263` counts unresolved starts through the five-second stop window.

However, `CellSession.c:434` clears `start_unknown` even when `wds_parse_start` returns -1. A matching malformed response, including QMI success without a usable handle, does not prove that the modem failed to start a call. `CellSession.c:440` then returns silently during stopping.

I reproduced stop with both starts outstanding, followed by success responses that omit their handles. Both unknown flags clear, the next poll reports clean STOPPED with no error, and a later valid success for the original transaction is ignored. No Stop Network is sent for that handle. This is the remaining boot blocker for both session configurations.

Exact fix: keep the unknown-start record when parsing returns a negative value. Retire it only after a fully validated success with a usable handle or an unambiguous validated failure result. During CALLS, block another start for that client and retain the existing 30-second unresolved policy. During STOPPING, retain it through the five-second window and report unconfirmed cleanup if unresolved. Reject duplicate critical result/handle TLVs before deciding that a reply resolves ownership. Add regression cases for malformed replies both before and during stop.

### B4 fixed

`QmiData.c:549` parses presence and exact four-byte lengths for the known format/capacity fields and rejects duplicate occurrences of those fields. `CellSession.c:356` requires all five necessary fields and positive downlink limits within the readiness record. `CellSession.c:548` requests Get Data Format when Set is incomplete and fails when Get is still incomplete or mismatched.

Under the documented readiness record this means no more than 16 datagrams and 8192 bytes. This does not mean every TLV in the entire WDA response is duplicate-checked. The shared result parser remains first-match under N5.

### B5 fixed under worker serialization

`sweet_data.c:580` applies USIM readiness immediately on the worker. `sweet_data.c:440` refreshes readiness before session receive, and `sweet_data.c:566` does so before service changes and polling. `CellSession.c:279` also fails an active setup when readiness is absent.

The host readiness test exercises CellSession directly, not these kernel hooks. The IPA setter at `sweet_data.c:656` only stores an atomic flag, so apply the worker serialization condition above. Poll still updates session time at `sweet_data.c:572`, after readiness and service callbacks, so prompt polling remains necessary for accurate cleanup deadlines.

## Disposition of N1 to N6

### N1 partly fixed

The raw range now refuses with ENOSPC at `sweet_data.c:727`, and successful attach is allowed once per boot at `sweet_data.c:449`. That closes raw wrap and reattach reuse. Session exhaustion at `CellSession.c:94` instead returns the same last transaction repeatedly without advancing `next_txn`.

I reproduced the final transaction being used again for the same client's Stop Network retry. Also, callers continue their current SEND after take_txn changes state to STOPPING, so the final setup operation can still be issued. The claim that session transactions are never reused is false.

This is non-blocking for the documented bounded probe, which must stay below session transaction exhaustion. Exact fix for unrestricted use: reserve a bounded cleanup range, return an explicit allocation failure before setup can consume it, and stop callers from building or sending when allocation fails. Allocate unique cleanup transactions too, and terminate with unconfirmed cleanup if those are exhausted.

### N2 fixed for the reported retry loops

`CellSession.c:458` resets the start budget only on usable settings and CE_UP. `CellSession.c:195` ends a drop-stop attempt after five seconds. The repeated successful-start/unusable-settings cycle no longer resets its budget.

Exhausting one family leaves the other usable family active. Any retained handle or unresolved start is still counted in eventual global stopping at `CellSession.c:261`. B3 is the separate case where that ownership is discarded incorrectly.

### N3 fixed

`CellSession.c:392` emits CE_DOWN before clearing the presented family on reconfigure. `sweet_data.c:369` withdraws that family's presentation and lowers the link when neither family remains. A later stop does not need a second CE_DOWN for an already withdrawn family.

### N4 partly fixed

`CellSession.c:368` rejects the requested IPv4 classes and all-zero, link-local and multicast IPv6 prefixes. A nonzero conflicting family is rejected. However, an explicit family value zero is treated as absent because `CellSession.c:370` tests the value rather than presence.

I reproduced usable IPv4 settings with family TLV 0x2b equal to zero reaching CE_UP. `QmiData.c:483` also treats an undersized family TLV as absent and accepts oversized ones. Non-blocking fix: track family presence, require exactly one byte, and compare every present value directly to the call family. Reject duplicate family/address TLVs too.

### N5 open, with WDA field checking improved

`sweet_data.c:421` copies complete raw responses and `sweet_data.c:436` retains complete selected indications without filtering their TLVs. Unknown extensions can therefore reach root unchanged. `QmiData.c:68` still takes the first result TLV, and `QmiData.c:97` uses first-match field lookup for most other session decoding.

The standard forbidden identifier commands remain excluded, and session IDs are suffix-only at `CellSession.c:488`. Operator/profile/revision text remains free-form at `CellSession.c:413`, `CellSession.c:484` and `sweet_data.c:329`. Do not claim unconditional identifier exclusion for unexpected content. Filter exported response/indication fields and log text if that stronger guarantee is required. B3 makes duplicate handling blocking specifically where it resolves start ownership.

### N6 partly fixed

`Qmap.c:67` rejects nonzero reserved bits in the eight-byte command header. `sweet_data.c:666` checks mux before changing flow state. The helper at `Qmap.c:65` still accepts extra payload bytes for known commands as long as they fit the reply buffer, and echoes them.

This remains non-blocking layout hardening. Validate the complete supported flow-command layout and extension lengths before changing flow state. The TX contract must still allow an already accepted in-flight frame when disable arrives.

## Smaller notes and retained fixes

The copyin errno note is fixed at `sweet_data.c:739`. Closure detected after otherwise successful copyin returns ESHUTDOWN. An actual copyin/parse error keeps its own errno.

The RA retry note is fixed at `sweet_data.c:306`. The next refresh advances even when IPv6 is down. IPv6 CE_UP still sends an immediate advertisement at `sweet_data.c:365`.

The attach/detach note is fixed for the one-successful-attach-per-boot contract at `sweet_data.c:449` and `sweet_data.c:486`. Detach closes raw access, clears presentation and drops the QRTR pointer at `sweet_data.c:618`. It does not unregister ports or implement reconnect. A failed partial port registration at `sweet_data.c:459` is not rolled back, so treat attach failure as terminal and discard that QRTR lifecycle rather than retrying attach on it.

The shared indication snapshot fix remains at `sweet_data.c:801`. Static locks, private output buffers, IPA TX outside the data lock, bounded mbuf copies, media-count validation, DHCP fragment/UDP checks, ICMPv6 checks and trimmed IP lengths remain present. Static sysctl OIDs still exist without gates, and readiness setters can record flags, so the documentation's claim that every hook does nothing without gates remains too broad.

No reviewed data implementation writes a partition or stores EFS. Codec helpers for profile mutation still exist but have no automatic-session or raw-dispatch caller. Their presence does not authorize their use.

## Validation and limits

`sh 3a-IOS/tools/data/check.sh` passed with AddressSanitizer and UndefinedBehaviorSanitizer using patch-extracted sources. The included session_reaudit cases confirm ordinary timed-out-start ownership, incomplete/zero WDA rejection, direct session readiness revocation and reconfiguration withdrawal. They miss malformed-start ownership, terminal transaction reuse and explicit family zero.

The additional sanitized `3a-IOS/tools/data/tests/out/audit3-probe` passed its reproduction assertions for all three remaining cases. Its source is `3a-IOS/tools/data/tests/out/audit3-probe.c`. These assertions confirm defects in the current code rather than verifying the proposed fixes.

The host suite excludes sweet_data.c. Kernel sysctl concurrency, hook ordering, ifnet behavior, kernel stack budget and real modem responses remain static-review limits. The raw GO is conditional as stated above. Both session configurations need the B3 ownership fix before another state-changing boot can receive GO.
