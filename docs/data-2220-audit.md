# Patch 2220 safety audit

Verdict: NO-GO for all three requested enabled boots. No phone boot or modem state change is authorized by this report.

Reviewed on 2026-11-03. The patch SHA-256 is `9475a857e25b1a6da7f7177456abb0b1585bac17abfbe780b6da4e7345d7f94b`. All twelve added data source and header files in the patch match `plans/data/src` byte for byte. References below use the added files' line numbers under `bsd/dev/data`, rather than the patch's physical line numbers.

This was a static review. I read the patch, cellular-data.md, the host tests, the QRTR transport and QMI parser, the local libqmi wire definitions, the relevant HakiOS paths, and local XNU mbuf, sysctl and media definitions. I did not edit implementation files, build anything, run host tests, contact the phone, or write a partition. The only output written is this report.

## Boot decisions

1. With every gate absent, the currently unwired patch has no active data path. `sweet_data_attach` returns before allocating the mutex or registering ports, `sweet_data_poll` returns while unattached, and IPA RX returns while the session is disabled. No thread is created anywhere in this patch, and no ifnet is created without sweetdata=1.
2. Only sweetqmiraw=1: NO-GO after hook integration. The allow list admits profile writes and service resets. Raw timeouts can permanently occupy the only request slot, and raw-created network handles have no teardown owner.
3. sweetdata=1 without sweetdataonline: NO-GO. The session does not send DMS Set Operating Mode, but it does send DPM Open Port and WDA Set Data Format. If Get Operating Mode reports that the modem is already online, it can also start WDS calls. Missing readiness checks and broken teardown still apply.
4. sweetdata=1 sweetdataonline=1: NO-GO. The automatic DMS request is restricted to mode 0, but it can run without proof of SIM readiness or IPA handshake completion. The call cleanup and failure handling below also block this boot.

The all-gates-absent claim needs two qualifications. The sysctl OIDs are registered statically at sweet_data.c:656 even with no gates. Their handlers return ENOENT before touching the lock when disabled, but the names still exist and framework permission checks can reject a write before the handler runs. CTLFLAG_MASKED does not unregister an OID.

Also, sweet_data_set_ipa at sweet_data.c:506 is ungated. Calling it with every gate absent allocates a lock and changes callback state. Thus the stronger claim that every possible hook is completely inert without gates is false. Gate the setter before allocation if that exact guarantee is required. The current absence of hook callers is relevant and does not approve future wiring.

## Blocking findings

### 1. The raw allow list permits profile changes and resets

Locations: QmiRaw.c:20, QmiRaw.c:21, QmiRaw.c:23, QmiRaw.c:25 and QmiRaw.c:57.

WDS 0x27 and 0x28 are Create Profile and Modify Profile in the supplied libqmi definitions. Both are accepted without inspecting any TLV. A root caller can create or change the modem's stored profile configuration. RAM-only rmtfs does not make these commands compliant with the stated allow-list policy, and this review cannot prove that every internal modem persistence path passes through that server.

DMS, NAS and WDS message 0x00 is Reset in those same definitions. All three are accepted. These are service reset operations, and should not be described as a proven whole-modem reboot. They still contradict the raw interface's stated reset prohibition and can invalidate the session's client state. WDA 0x00 is also accepted without a corresponding message definition in the supplied WDA reference.

Exact fix: remove WDS 0x27 and 0x28 and remove 0x00 from every list. Deny every message whose exact schema and effects have not been audited. Add negative allow-list cases for each removed ID. The existing tests reject Delete Profile and Set Autoconnect Settings but never test the accepted Create Profile and Modify Profile messages.

### 2. Message IDs alone do not constrain dangerous optional fields

Locations: QmiRaw.c:45, QmiRaw.c:52 and QmiRaw.c:57. Related transport parser: bsd/dev/modem/QcomQrtr.c:141 and :160.

WDS Start Network 0x20 accepts arbitrary TLVs. Its reference schema includes Enable Autoconnect at TLV 0x33. That reaches an explicitly excluded behavior even though the separate Set Autoconnect Settings message is denied. Stop Network similarly accepts its optional Disable Autoconnect field without review.

DMS mode and NAS change duration validation examines only the first matching TLV. The common parser accepts duplicate types. A request containing mode TLV 0x01=0 followed by 0x01=4 passes the host check and transmits both. NAS duration 0 followed by duration 1 has the same ambiguity. The modem's duplicate-selection behavior is not established, so the host cannot guarantee the effective value.

Exact fix: validate the complete request schema for each admitted mutating operation. Reject duplicate types, unknown types, incorrect lengths and values outside the audited set. Reject Start Network autoconnect TLV 0x33 and Stop Network autoconnect changes. For the initial raw boot, the smallest safe restriction is a documented read-only subset until each mutating schema has this validation.

### 3. A lost raw reply permanently blocks the raw interface

Locations: sweet_data.c:473, sweet_data.c:598, sweet_data.c:614 and sweet_data.c:383.

The slot follows 0 free, 5 copying, 1 queued, 2 sent, 3 answered, and 4 abandoned. State 5 correctly protects the static request buffer while SYSCTL_IN sleeps, and normal request/reply transitions use the mutex. Timeout in state 1 frees the slot. Timeout in state 2 leaves state 4 forever unless a late answer happens to arrive.

A modem that drops one reply makes all later calls return EBUSY for the rest of the boot. An oversized reply also cannot complete state 2. There is an additional send-error race: if the waiter times out while the worker is outside the lock in sd_send, the worker sees state 4 instead of 2 and does not release it even though transmission failed.

Exact fix: retire a timed-out transaction and release the slot on timeout, send failure, service removal and transport shutdown. Ignore retired replies. Do not reuse transaction numbers during that transport lifetime, or implement a quarantine with a justified maximum reply lifetime. Complete waiters with an explicit error on shutdown. Use an absolute deadline so unrelated wakeups cannot restart the full timeout. Match client, service and message as well as transaction on every completion path.

### 4. Raw-only client 1 has no registered receive port or handle cleanup

Locations: sweet_data.c:409, sweet_data.c:124, sweet_data.c:491 and QmiRaw.c:76.

The raw parser permits WDS client 1. The second QRTR port is registered only when session_on is true. On a raw-only boot, a client-1 request is sent from 0x4011 without a local receive registration. Its answer is unrouted, followed by the stuck state-4 failure above.

Raw WDS Start Network is also allowed while session_on is false. Its reply goes only to the raw waiter. The returned handle is never recorded in CellSession, and sweet_data_stop does nothing in this mode. Even with a session enabled, raw-created handles are outside the session's accounting.

Exact fix: register both ports whenever either feature can use client 1, or reject client 1 unless that port exists. Deny raw Start Network until raw handles are tracked and stopped by the worker on every shutdown path. A userland caller promising to send Stop Network is not a kernel teardown guarantee.

### 5. Shutdown can finish before an outstanding Start Network returns

Locations: CellSession.c:220, CellSession.c:228, CellSession.c:313 and CellSession.c:429.

If Start Network is outstanding and no handle has arrived, cell_session_stop immediately marks the session stopped because live is zero. A matching Start Network response can then install a handle and send Get Current Settings while the state remains stopped. A late settings response can even emit CE_UP during shutdown. No Stop Network is sent for that new handle.

Late DPM and WDA responses are also processed without a state check at CellSession.c:392 and :396. They can replace CS_STOPPING or CS_STOPPED with CS_WDA or CS_ONLINE. That can resume setup during teardown.

Exact fix: make stopping a terminal setup barrier. Cancel or drain pending setup operations before declaring stopped. Keep enough transaction state to recognize successful late starts and immediately stop their handles. During stopping, accept only cleanup completions and late handle acquisition for cleanup. Never emit CE_UP, advance setup, or initiate a new call after stop has begun. Preserve the overall five-second shutdown bound.

### 6. Failure and Stop Network errors lose live handles

Locations: CellSession.c:56, CellSession.c:251, CellSession.c:333 and sweet_data.c:499.

Service removal sets CS_FAILED without stopping existing WDS handles or lowering the interface. sweet_data_idle then reports true for CS_FAILED. A runtime following idle can shut down IPA and the modem without sending the required stops, even when only NAS or DMS disappeared and WDS remains available.

The WDS_STOP_NETWORK response path clears the handle without parsing the QMI result. An error response or a malformed response with a matching header therefore counts as a successful stop. Normal successful stop also clears c->up without CE_DOWN, leaving CellLink and link_up stale.

Exact fix: route failure through bounded cleanup, preserving the failure reason separately. Do not treat failed as idle while handles or starts still need cleanup. Validate Stop Network with qmi_response, retain the handle after an error, and retry within the shutdown deadline. Emit CE_DOWN for every presented family and lower en2 before teardown completes. If WDS itself is gone, report that cleanup could not be confirmed rather than silently treating the call as stopped.

### 7. SIM and IPA prerequisites are absent from the session barrier

Locations: CellSession.c:21, CellSession.c:167, CellSession.c:193 and sweet_data.c:423.

B_ALL contains only DMS, NAS, WDS, WDA and DPM service presence. It says nothing about a present USIM application in state 7, running IPA endpoints, or the IPA Init Modem Driver completion. The session advances as soon as those five service announcements exist. sd.ipa_tx can still be NULL, which the CE_UP log explicitly permits.

The requested 32 packets and 16384 bytes of downlink aggregation are hard-coded. No IPA buffer capacity is supplied or checked. A transmit callback alone would not prove that the receive allocation can hold this aggregation size.

Exact fix: make the modem integration release a single explicit readiness barrier only after USIM readiness and the completed IPA handshake, before polling can advance the session. Pass actual QMAP format and receive capacities into configuration before that barrier. Clear readiness on service replacement or IPA failure. Do not send DPM, WDA, DMS online or WDS start before the required prerequisites are satisfied.

This is an integration blocker as well as a patch limitation. The hooks do not exist in the modem runtime yet, so their future ordering cannot be assumed from this patch.

### 8. WDA negotiation and WDS family setup accept unusable replies

Locations: QmiData.c:532, CellSession.c:401, CellSession.c:300, CellSession.c:309 and CellSession.c:328.

A successful WDA result with no format TLVs leaves all fields zero and passes the conditional mismatch checks. Explicit zero values also pass. The returned aggregation limits are never compared with the receive buffer capacity. This does not establish the format the IPA parser will actually receive.

WDS Set IP Family marks family_set unconditionally, even for an error or malformed response. Bind Mux errors are deliberately ignored. Get Current Settings accepts either address family, and the session marks a call up without requiring the configured family's address. A successful IPv6 decoder result with an unsupported prefix can reach CE_UP even though cell_link_set_v6 rejects it, and sd_event ignores that failure.

Exact fix: track format field presence and require verified raw-IP and the exact uplink/downlink QMAP format. If the modem omits echoes, perform and validate Get Data Format before proceeding. Check returned limits against IPA capacities. Validate Set IP Family and require the intended binding before start. Require matching-family usable settings, and report CE_UP only after the presentation accepts them.

### 9. Indication sysctl callers share an unlocked output buffer

Locations: sweet_data.c:629, sweet_data.c:638, sweet_data.c:652 and sweet_data.c:657.

sd_sysctl_ind builds its output in one static buffer while holding sd.lock, then drops the lock before SYSCTL_OUT reads that buffer. A second root caller can overwrite it while the first copyout sleeps or runs. The result can contain a mixture of two snapshots.

CTLFLAG_LOCKED means the handler supplies its own synchronization. It does not serialize callers. The request sysctl's static input buffer is protected by slot state 5, so it does not have this particular defect.

Exact fix: give each indication call its own snapshot buffer, on the stack if the verified kernel stack budget permits 2112 bytes, or in a bounded allocation. Copy ring data into that private buffer under sd.lock and copy it to userland after unlocking.

### 10. Several call paths retry forever or never recover

Locations: CellSession.c:144, CellSession.c:157, CellSession.c:328 and CellSession.c:411.

Bind Mux and Set IP Family have no attempt limit after missing replies. A synchronous send failure is retried on every worker poll because no pending entry or retry deadline is installed. That also affects Start Network, whose attempt counter advances only on a response error or an outstanding request timeout.

Get Current Settings is sent once after a successful start. If it times out or returns unusable settings, the handle remains live and call_step returns forever because c->handle is nonzero. Twenty start failures also leave CS_CALLS parked without an explicit exhausted outcome. The intended six-second, twenty-attempt start policy is only partially implemented.

Exact fix: charge an attempt and set a deadline for every attempted send, including transport failure. Bound bind, family and settings retries. Stop the handle and fail or retry deliberately when usable settings cannot be obtained. Expose exhausted retries as a terminal error. Retry error 0x46 according to the documented policy rather than retrying every modem error indiscriminately.

## Kernel and packet review

### Locking and callback lifetime

Session state, remote service lookup and QRTR sends are confined to the modem worker by the header contract. sd_output protects its static frame and CellLink access with sd.lock. The receive delivery path releases sd.lock before calling sd_input, and the local reply path does the same. I found no ifnet_input call under sd.lock.

The lazy lock initialization at sweet_data.c:90 is not concurrency-safe. Two first callers can allocate and publish different mutexes, defeating serialization. The stated setter contract, before the modem starts, can avoid this race only if the future wiring enforces that ordering. Use a statically declared lock group and mutex, or a proper once initializer, and handle allocation failure if dynamic allocation remains.

IPA callback use is inconsistent. sd_output calls ipa_tx while holding sd.lock at :178. A callback that synchronously enters IPA RX will deadlock when sd_deliver takes the same lock. sd_deliver's command ACK path at :523 reads ipa_tx and ipa_ctx without that lock, racing callback replacement in sweet_data_set_ipa. Merely copying those pointers under a lock would still need a lifetime guarantee before freeing the old context.

Both the command ACK and the data frame buffers have limited lifetimes. The ACK is a stack array. The data frame is a shared static buffer reused by the next output call. The IPA transmit implementation must copy or consume the bytes before returning. It must not retain either pointer as a queued DMA buffer.

Before integration, define and enforce that transmit contract, prevent reentrant callbacks under sd.lock, and drain in-flight users before clearing or freeing an IPA context. Use a private bounded frame buffer if transmit is moved outside the lock. These are unresolved integration conditions, not evidence of an already observed UAF in an absent IPA driver.

link_up is written by the worker and read by ioctl without synchronization. Publish and read it atomically or snapshot it under sd.lock. Also guard attach/reattach and shutdown publication. sweet_data_stop never clears attached, qr, service cache or raw availability. Reject new sysctls during shutdown, cancel waiters, drain callbacks and invalidate transport state before the runtime can release it. I cannot claim a QRTR-object UAF without the future runtime's lifetime rules.

### mbufs and buffer limits

sd_output checks the packet-header length before mbuf_copydata, copies the entire chain into a 1536-byte frame buffer, and frees the original mbuf once. QMAP's 4-byte or 8-byte prepended header fits in the removed 14-byte Ethernet headroom. The 400-byte reply buffer holds the largest generated DHCP or IPv6 response.

sd_input allocates a header mbuf and a cluster, checks MBUF_EXT and capacity, sets the receive interface and frame-header pointer, and removes the Ethernet bytes from the data view. Allocation failures free the owned mbuf. The current callers bound the IP length to 1522 bytes or a small local reply before sd_input, so its addition cannot overflow on those paths. For a defensive local invariant, check length against MCLBYTES minus CELL_ETHER before computing total.

The cluster and detached Ethernet-header layout agree with the local XNU KPI implementation inspected. This is not a runtime validation of the target kernel's receive path or BPF behavior.

### SIOCGIFMEDIA copyout

sweet_data.c:194 chooses the correct 32-bit or 64-bit user-pointer member. The local XNU media structures share the scalar field offsets used here. copyout supplies only a known four-byte media word and returns its error, so this is not an arbitrary kernel-memory disclosure.

It nevertheless overwrites ifm_count and copies one word whenever the pointer is nonzero, even when the caller supplied count zero or a negative count. Exact fix: save the incoming count, reject a negative count with EINVAL, report the available count as one, and copy at most one word only when the original count is positive and the user pointer is nonzero. Test count zero with a nonzero pointer and both pointer widths.

### Root checks and identifiers

Both handlers check kauth_cred_issuser before copying request or indication bytes. The RW request OID also lacks CTLFLAG_ANYBODY. CTLFLAG_MASKED hides routine enumeration, not access. I found no ordinary non-root path around the explicit handler checks.

The raw list denies the known full-identifier DMS reads, UIM slot status, PIN changes and SIM file operations. The session's DMS Get IDs decoder retains only the last four IMEI digits and logs only those digits. No full identifier is deliberately logged in the reviewed data code. The raw sysctl still returns whole modem responses and its indication ring stores any indication from six discovered services without a schema filter. That is not a guarantee against identity-bearing vendor extensions or unexpected indication types.

For the owner rule to be enforced at the boundary, return only audited response and indication fields, drop unknown TLVs and indication IDs, and never retain or export full identifiers. This review found definite persistent-operation and reset exposure, but did not establish a concrete full-IMEI, ICCID, IMSI or phone-number response through one of the known allowed schemas. Raw profile reads can also return credentials, which is additional sensitive data even though it is not one of those four identifiers.

### QMI codecs

The underlying qcom_qmi_parse requires exact QMI body length and bounds every TLV before exposing it. QmiData.c validates response transaction, message and result encoding. Fixed-size decoders check minimum sizes before loads. The profile-list parser bounds each variable-length record, and copied strings are bounded and terminated. I found no reachable out-of-bounds copy in these decoder paths.

Optional undersized TLVs are often treated as absent rather than rejected. That is memory-safe but unsafe when callers confuse absence with an acceptable negotiation result, as in finding 8. Duplicate TLVs are accepted by the common parser and need strict handling at the raw mutation boundary.

qmi_put_string at QmiData.c:40 tests s[n] before n <= max. Its current APN caller supplies a terminated 101-byte array for max 100. As a public bounded helper, change the loop to test the bound before reading and define the required accessible input size. qmi_end also encodes its body into 16 bits without an explicit body-size check. Current 256-byte and 1024-byte callers keep it below that limit. Reject a larger body in the helper before writing the wire length.

### QMAP and Ethernet malformed input

Qmap.c:12 advances only after verifying header and payload fit the remaining supplied buffer. The 16-bit size cannot make at exceed length. Padding is checked before subtraction, mux shifts are guarded against values above 31, and short v5 checksum headers are rejected. I found no deaggregation read beyond the supplied DMA length. The caller must still pass the actual completed DMA byte count, not untrusted descriptor capacity.

Commands bypass the mux filter at Qmap.c:27 and qmap_command_ack accepts any command name with only two bytes present. That can acknowledge malformed or unknown flow-control commands and does not implement flow-disable behavior. Before a functional online boot, validate the known command structure and mux, implement the required flow-control action, and ACK only handled commands. Acknowledging every command is not a substitute for obeying modem flow control.

CellLink bounds Ethernet, ARP, IPv4 header lengths, IPv6 payloads and DHCP option advances before reading fields. DHCP and RA/NA output fit the fixed reply capacity. I found no direct frame-driven overflow in those paths.

Malformed protocol checks remain incomplete. DHCP ignores UDP length and IPv4 fragmentation. Neighbor discovery ignores ICMPv6 code and checksum. The modem input helper checks version and minimum size but leaves IP internal length validation to the stack. Reject inconsistent UDP/IP lengths and fragmented DHCP, require code zero and a valid ICMPv6 checksum, and trim forwarded Ethernet padding to the IP datagram length before QMAP framing. These are packet-correctness issues, not demonstrated kernel memory corruption.

IPv6 router advertisements are only generated in response to a router solicitation. No caller emits the advertised unsolicited RA on prefix availability, and no refresh timer exists. If IPv4 already raised the link before IPv6 becomes ready, the stack can finish its solicitations too early. A default-router lifetime also expires after 1800 seconds without refresh. Schedule an RA when IPv6 becomes usable and refresh it before expiry, outside sd.lock.

## Sequence and owner-policy result

The implemented broad order is service discovery, DPM, WDA, mode read, optionally mode set, NAS registration polling and per-family WDS setup. info() actually registers NAS indications and reads profiles before DPM. Card and IPA readiness are not represented, and WDA format verification is incomplete. It therefore does not yet implement the documented ONLINE sequence as a safety barrier.

Automatic DMS Set Operating Mode is reached only after cfg.allow_online, populated from the exact sweetdataonline token, is true. The automatic request is always mode 0. The shared builder refuses every value except 0 and 1. The raw gate intentionally allows online without sweetdataonline, but its duplicate-TLV hole defeats a strict effective-mode guarantee until fixed.

Exact-token ModemGate.h parsing fails closed for bare, malformed, duplicate and numeric-alias tokens. sweetapn alone and sweetdataonline alone do not start the session. None of the added data code opens a block device or implements partition or filesystem writes. The EFS RAM-only guarantee remains the responsibility of the separately approved modem/rmtfs runtime and cannot excuse the raw policy violations.

Stop Network is sent for both already-known handles on the happy path. It is not guaranteed for starts in flight, raw-created handles, service failures or rejected stop replies. The stop API is asynchronous despite the plan's wording that it returns after cleanup. Integration must call stop, continue transport receive and poll until idle or the five-second deadline, then drain IPA, then stop the modem.

No new boot should be attempted under any of the three requested enabled configurations until the applicable blockers are fixed and the complete hook wiring is reviewed. Host tests cover byte layouts and a happy-path fake modem, but omit sweet_data.c entirely and do not exercise the raw slot races, concurrent sysctls or shutdown interleavings identified here. Passing those existing tests would not change this verdict.
