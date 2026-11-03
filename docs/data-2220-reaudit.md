# Patch 2220 re-audit

Reviewed on 2026-11-03. NO-GO for all three requested configurations as supplied. No phone boot or modem state change is approved by this report.

The patch SHA-256 is `6a98f2c123c101158c9e46448546a884e16b4565751e88b1490e212c3c3792ac`, matching the requested revision. All twelve added files match `plans/data/src` byte for byte. Source references below are relative to `plans/data/src`, equivalent to the added files under `bsd/dev/data`.

I read the previous audit first, then the revised sources, patch, host tests, local QMI schemas and the three integration documents. I used the review and plain-writing skills. I changed no implementation files, built no kernel and did not contact the phone. I wrote this report and host probe artifacts under `3a-IOS/tools/data/tests/out`.

## Configuration decisions

1. `sweetqmiraw=1` alone is NO-GO for the supplied callable interface. It sends nothing automatically, and the reset, profile-write and raw-handle holes are closed. However, a root request can still put the modem online before USIM or IPA readiness, including on this raw-only configuration where the data IPA hook cannot be installed. Blocking finding B1 applies. This is not approval to run the mutating raw commands.
2. `sweetdata=1` without `sweetdataonline` is NO-GO. The session sends NAS indication registration before readiness and accepts incomplete WDA capacity verification. If the modem is already online, it starts WDS calls without the online token, so the late-start cleanup defect also applies. B2 through B5 apply.
3. `sweetdata=1 sweetdataonline=1` is NO-GO. The initial three-part barrier is a substantial improvement, but readiness revocation can lag behind sends. Incomplete WDA verification and lost late handles remain. B2 through B5 apply.

These decisions assume correct wiring of the documented contracts. There must be eight QRTR local slots, unannounced service-zero client ports, one worker for all session/QRTR operations, selected USIM state 7 as the SIM prerequisite, and QMAP v1 without checksum offload. IPA must supply 8192-byte completed RX buffers, a 16-datagram limit, a TX wrapper that copies before returning, and a kernel-lifetime callback/context. RX must run on that same worker, outside the TX queue lock. These assumptions do not fix the defects below.

Shutdown must keep QRTR receive and poll running for the bounded cleanup, then make IPA unavailable and drain it before detach and modem shutdown. Unconfirmed cleanup must remain an explicit failure. The modem runtime's RAM-only EFS and partition safety approvals remain separate prerequisites. This patch review grants no approval to IPA hardware setup, provisioning changes or modem-runtime changes.

## Disposition of the ten prior blocking findings

### 1. Raw profile changes and resets: fixed

`QmiRaw.c:24`, `QmiRaw.c:27` and `QmiRaw.c:78` remove Reset and WDS Create/Modify/Delete Profile. WDA Set and DPM are denied too. The local schemas identify the remaining list entries as queries except the two explicitly validated DMS/NAS setters. The profile-writing codec helpers still exist in `QmiData.c`, but neither the raw dispatcher nor the automatic session calls them.

### 2. Dangerous optional fields and duplicate TLVs: fixed at the raw request boundary

`QmiRaw.c:56` rejects duplicate request TLV types. `QmiRaw.c:62` requires exactly one one-byte DMS mode TLV with value 0 or 1. `QmiRaw.c:70` admits only NAS mode preference and volatile duration, requiring a nonzero seven-bit preference mask and duration zero. Raw Start/Stop Network are absent from `QmiRaw.c:27`, so their autoconnect options cannot pass.

The phrase "duplicates refused everywhere" is accurate for raw requests only. Session responses still use first-match TLV parsing through `QmiData.c:68` and `QmiData.c:97`. See N5.

### 3. Lost raw reply permanently occupies the slot: partly fixed

`sweet_data.c:718` reserves COPYING before copyin. `sweet_data.c:747` uses an absolute deadline, and `sweet_data.c:765` frees the slot on every waiter completion, including timeout. Send failure at `sweet_data.c:547`, service loss at `sweet_data.c:498` and stop at `sweet_data.c:584` complete the waiter. Matching at `sweet_data.c:414` includes transaction, service, client and message.

Transaction retirement is not permanent. `sweet_data.c:738` wraps to 0x8000 after 32768 requests, and `sweet_data.c:465` resets that range on attach. An old response can then match a newer identical request. See N1. The original permanent-busy defect is fixed.

### 4. Raw client 1 and unowned raw handles: fixed

`QmiRaw.c:103` rejects every nonzero client. Raw Start/Stop Network are denied at `QmiRaw.c:27`. Raw-only operation therefore needs only PORT_CLIENT0 and cannot acquire an unmanaged WDS handle.

### 5. Shutdown finishes before an outstanding start returns: partly fixed

`CellSession.c:244` counts currently pending starts. `CellSession.c:395` records successful late starts during STOPPING and immediately advances cleanup. `CellSession.c:491` and `CellSession.c:499` prevent late non-WDS setup replies from restarting setup. However, `CellSession.c:553` discards starts on ordinary request timeout, after which their successful replies are dropped at `CellSession.c:489`. B3 remains blocking.

### 6. Failure and stop errors lose handles: partly fixed

`CellSession.c:73` routes failure through STOPPING. `CellSession.c:423` validates Stop Network and retains the handle on error, with retries at `CellSession.c:247`. Missing WDS produces an explicit unconfirmed-cleanup error at `CellSession.c:253` before bounded termination.

The timed-out-start hole remains under B3. Separately, a connected/reconfigure indication clears `c->up` at `CellSession.c:356` without CE_DOWN. Subsequent stop and finish can therefore omit CE_DOWN for a family already presented to en2. See N3. Normal successful stop now emits CE_DOWN.

### 7. SIM and IPA prerequisites absent: partly fixed

`sweet_data.c:521` combines USIM, IPA-ready and a TX hook, and passes actual format/capacity data at `sweet_data.c:523`. `CellSession.c:265` gates DPM and later setup on that record. However, `CellSession.c:145` registers NAS indications before this barrier, and `sweet_data.c:565` applies readiness changes after earlier receive/service paths can advance setup. B2 and B5 remain. Raw mode changes bypass the barrier entirely under B1.

### 8. WDA and family setup accept unusable replies: partly fixed

`CellSession.c:381` validates bind and family responses. `CellSession.c:412` requires an address for the requested family and the same IPv6 prefix restrictions as CellLink. `CellSession.c:503` requests Get Data Format when the three format fields are incomplete and fails if that query remains incomplete.

`CellSession.c:337` still treats the three format fields as sufficient when both downlink capacity fields are absent or zero. `QmiData.c:544` and `QmiData.c:545` collapse missing, undersized and explicit-zero capacity TLVs to zero. B4 remains blocking. Matching the optional settings family discriminator and rejecting unusable address classes remain non-blocking hardening under N4.

### 9. Shared indication sysctl output buffer: fixed

`sweet_data.c:788` allocates a private bounded snapshot for each call. `sweet_data.c:792` protects ring copying and `sweet_data.c:807` performs copyout after unlocking. The allocation is released at `sweet_data.c:808`. Concurrent readers no longer overwrite each other's snapshot.

### 10. Infinite retries and unrecoverable settings: partly fixed

`CellSession.c:155` and `CellSession.c:180` bound setup attempts. `CellSession.c:107` backs synchronous transport refusals off for one second. `CellSession.c:217` charges start attempts before send, and `CellSession.c:406` exhausts ordinary non-retryable QMI errors. `CellSession.c:206` limits settings attempts before stopping the acquired handle.

The complete recovery cycle remains unbounded. A successful start resets `start_tries` at `CellSession.c:397`, even when settings never become usable. Successful stop clears drop/settings state at `CellSession.c:227`, allowing another full cycle forever. A permanently refused drop-stop also retries forever at `CellSession.c:189`. See N2. Malformed start replies and missing responses are also retried in addition to errors 0x46 and 0x0e, so the author's "only" claim needs that qualification.

## Remaining blocking findings

### B1. Raw ONLINE bypasses the required readiness evidence

Locations: `QmiRaw.c:62`, `sweet_data.c:547`, `sweet_data.c:634`.

A valid raw DMS Set Operating Mode request with mode 0 passes the allow list and goes straight through sd_send. Neither function checks USIM or IPA readiness. With only sweetqmiraw enabled, set_ipa refuses installation because sweetdata is absent, so this configuration cannot satisfy the documented data readiness barrier through these hooks.

Exact fix: remove DMS Set Operating Mode and NAS Set System Selection from the initial raw interface, leaving audited reads. If manual mutation must remain, dispatch it on the worker through the same current readiness checks and cleanup ownership as the session. Raw-only ONLINE must fail closed when those prerequisites cannot be established. Preserve explicit owner approval for each permitted mutation scope.

### B2. WAIT_READY sends a state-changing NAS registration

Locations: `CellSession.c:145`, `CellSession.c:264`, `QmiData.c:162`.

info() calls NAS Register Indications 0x03 with three enable TLVs before checking is_ready. This changes modem subscription state and contradicts the promised read-only barrier. The host test labels it read-only and explicitly permits it in `3a-IOS/tools/data/tests/data.c:466`.

Exact fix: move NAS indication registration into a step after readiness is established. Keep only actual DMS/NAS/WDS queries in info(). Change the test to reject NAS_REGISTER_INDICATIONS before readiness instead of listing it as allowed.

### B3. A timed-out start loses cleanup ownership

Locations: `CellSession.c:553`, `CellSession.c:489`, `CellSession.c:244`, `CellSession.c:395`.

A start is sent at time T. Stop begins at T+4000 ms. At T+5000 ms poll removes the pending starts, sees no handles and reports idle after just one second of shutdown. A successful start response arriving immediately afterwards is ignored, so no Stop Network is sent for its handle and no unconfirmed-cleanup error describes that start.

The same loss occurs before shutdown. A start timing out in CS_CALLS can later succeed after its transaction has been discarded, while a new start is attempted. The new STOPPING logic only owns starts still in the pending table.

Exact fix: preserve a separate record of every successfully queued start whose outcome is unknown, including its transaction and client, after the ordinary reply deadline. Match successful late responses against those records and stop their handles. Do not declare clean idle while such starts exist. During shutdown retain them through the full cleanup window, report unresolved starts as unconfirmed cleanup at the hard deadline, and prohibit new starts while an earlier outcome for that client is unresolved.

### B4. WDA verification does not establish receive capacity

Locations: `CellSession.c:332`, `CellSession.c:337`, `CellSession.c:503`, `QmiData.c:544`.

A success response containing raw-IP and the two QMAP fields, but neither aggregation limit, advances straight to ONLINE. Both capacity fields become zero and pass the upper-bound comparison. A Get Data Format response with the same omission also passes, so a buffer-safe negotiated maximum has not been established.

Exact fix: track presence and exact lengths for format and limit TLVs. Require positive confirmed downlink datagram and byte limits within 16 and 8192 for this contract. Query Get Data Format when any required field is missing from Set, and fail closed when Get still omits or zeroes a required field. Reject explicit mismatches and undersized required TLVs. Update WDA_OK test fixtures to include the limits and add missing/zero-limit rejection cases.

### B5. Readiness loss is applied after paths that can send

Locations: `sweet_data.c:438`, `sweet_data.c:503`, `sweet_data.c:562`, `sweet_data.c:565`, `sweet_data.c:574`.

sweet_data_usim(0) changes only sd.usim. An IPA-ready revocation changes only its atomic flag. Until sd_readiness runs, CellSession retains is_ready and can advance on a QRTR response or service notification. For example, revoke USIM readiness on the worker, then deliver the pending WDA success before the next poll. The session can query mode and send ONLINE or continue WDS setup using the old ready record.

Exact fix: apply readiness revocation on the owning worker before it processes any session receive, service notification or setup advance. Have the worker-side USIM hook revoke the session immediately, and serialize IPA readiness notifications onto that worker before further session work. Reorder poll so current time and readiness are updated before service callbacks can send. Once revoked, every subsequent advance must enter cleanup without issuing setup traffic.

## Non-blocking findings and limits

### N1. Transaction reuse weakens retired-reply matching

Locations: `sweet_data.c:738`, `sweet_data.c:465`, `CellSession.c:92`.

The raw transaction range repeats after 32768 requests, and the session range repeats after 32767 allocations. This is not a new arbitrary-memory-access path, but it invalidates the claim that retired replies can never match. Refuse new requests when a transport lifetime exhausts its range, or use a justified transport-generation/quarantine rule. A single attach per QRTR lifetime and an explicit request cap are acceptable release constraints for a bounded probe.

### N2. Retry limits do not bound the full failure cycle

Locations: `CellSession.c:189`, `CellSession.c:206`, `CellSession.c:397`.

Repeated start success followed by unusable settings resets the start budget on each cycle. Drop-stop refusal has no local deadline. Keep a failure budget through settings success, add a deadline for stopping an unusable call, and enter bounded failure cleanup when either is exhausted. The independent runtime hard deadline prevents an indefinite boot, so this is separate from the lost cleanup ownership in B3.

### N3. Reconfiguration can leave en2 presented after call teardown

Locations: `CellSession.c:356`, `CellSession.c:225`, `CellSession.c:235`.

After CE_UP, a connected/reconfigure indication clears c->up without withdrawing the presentation. Stop then skips CE_DOWN because c->up is already zero. Emit CE_DOWN when invalidating settings, or track presentation separately from settings readiness. The documented detach lowers and clears en2, limiting this defect to the interval before detach.

### N4. Settings validation still accepts unusable addresses

Locations: `CellSession.c:343`, `QmiData.c:475`, `QmiData.c:480`, `QmiData.c:485`.

A nonzero IPv4 address passes even if multicast or otherwise unusable. An all-zero IPv6 address with prefix 64 also passes, and the optional returned family discriminator is not checked. Reject unspecified, multicast and unsupported address classes and reject an explicit family that conflicts with the call. These cases affect usable connectivity rather than proving a kernel overflow.

### N5. Raw output remains opaque and session parsing remains first-match

Locations: `sweet_data.c:419`, `sweet_data.c:427`, `sweet_data.c:434`, `QmiData.c:68`.

The narrowed request and indication IDs remove the known identifier operations. I found no standard allowed schema that necessarily exports a full IMEI, ICCID, IMSI or phone number. However, raw responses and kept indications still return unknown TLVs unchanged, so this is not a schema-enforced guarantee against identifier-bearing extensions. Export only audited response/indication fields if the owner rule must hold for unexpected modem TLVs too, and reject duplicate response TLVs for safety-critical session replies.

The session logs only the IMEI suffix through `CellSession.c:448`. Profile names/APNs and operator text remain free-form modem strings in logs. They are not a proof that a modem can never place a sensitive string there. No packet bytes are deliberately logged.

### N6. Flow-command validation is partial

Locations: `Qmap.c:65`, `Qmap.c:66`, `sweet_data.c:663`.

The command helper now rejects headers shorter than eight bytes, handles enable/disable and marks unknown command names unsupported. It still accepts arbitrary reserved bits and longer payloads that fit the 64-byte reply buffer. Validate the exact supported command layout and reserved fields, then test malformed variants. The mux is checked by sd_deliver before changing tx_paused. An output call already past the pause check can still be in flight when disable arrives, so the IPA contract must permit that bounded in-flight frame.

## Kernel and packet checks

The static mutex declarations at `sweet_data.c:56` remove the lazy-initialization race. sd_output uses a private stack frame at `sweet_data.c:175` and calls IPA outside sd_lock at `sweet_data.c:193`. The command reply also has a private stack buffer. This is safe only under the documented synchronous-copy and kernel-lifetime callback/context contract. No new callback-drain or kernel-stack measurement is established by the host tests.

The indication allocation is bounded and private. Root checks remain before request copyin and indication export at `sweet_data.c:703` and `sweet_data.c:785`. The request slot protects its static copyin buffer. A stop during RAW_COPYING is detected after copyin, but `sweet_data.c:730` returns ENOENT rather than the claimed ESHUTDOWN in that branch. This is an errno mismatch, not the original stuck-slot defect.

SIOCGIFMEDIA now rejects negative counts and copies only when the original count is positive at `sweet_data.c:208` and `sweet_data.c:218`. link_up uses atomic access. sd_input checks cluster capacity before copying at `sweet_data.c:144`. I found no new direct frame-driven or raw-request-driven buffer overflow in these paths.

`CellLink.c:154` rejects fragmented DHCP and UDP lengths outside the bounded DHCP payload. `CellLink.c:225` validates ICMPv6 code and checksum for the handled RS/NS path. `CellLink.c:160` and `CellLink.c:242` return IP datagram lengths, and sd_output uses them for QMAP framing. These fixes are present. Inbound IP internal-length validation is still left to the network stack after the presentation's outer bounds check.

`sweet_data.c:365` emits an RA when IPv6 comes up. `sweet_data.c:567` refreshes it every ten minutes. There is no longer a router-lifetime-only failure caused by never sending periodic advertisements. With only IPv4 up, the no-v6 RA attempt returns zero and is retried on each poll because next_ra is not advanced. That is a small avoidable polling cost.

`QmiData.c:40` now checks the string bound before reading, and `QmiData.c:47` rejects an oversized QMI body. The APN buffer supplies the required accessible terminator byte. QMAP deaggregation remains bounded by the actual completed DMA length. The IPA worker must supply that actual length, not buffer capacity, and enforce its 8192-byte contract.

Attach/detach is improved but not a proven general reconnect API. Attach resets the raw slot and transaction range without waiting for an older sleeping sysctl caller, and detach does not unregister the QRTR ports. Require one attach per QRTR object lifetime for the first probe. Any restart within one boot needs a separately checked waiter/port/readiness reset sequence. The static OIDs still exist when gates are absent, and the USIM/IPA-ready setters still update flags, so documentation claiming every hook does nothing without gates is too strong.

None of the reviewed data files writes a partition or stores modem EFS. That does not validate a separate rmtfs implementation or guarantee modem-internal persistence behavior. The raw persistent-operation removals satisfy the concrete problems identified by the prior allow-list audit.

## Validation

`sh 3a-IOS/tools/data/check.sh` passed with AddressSanitizer and UndefinedBehaviorSanitizer. It reconstructed the tested codec/session sources from the public patches. These tests omit sweet_data.c, so they do not establish kernel sysctl concurrency, hook lifetime or receive/readiness ordering correctness.

I added a read-only host review driver at `3a-IOS/tools/data/tests/out/reaudit-probe.c`, using the existing test helpers and patch-extracted sources. The sanitized executable `3a-IOS/tools/data/tests/out/reaudit-probe` confirmed four cases: NAS registration before readiness, WDA acceptance without capacity TLVs, premature idle followed by an ignored successful start response, and missing CE_DOWN after reconfiguration then stop. All four probe assertions pass, confirming the defects described above.

The existing positive suite passing does not change the NO-GO decisions. B1 must be fixed for the raw configuration. B2 through B5 must be fixed for either session configuration before this revision can receive a boot GO.
