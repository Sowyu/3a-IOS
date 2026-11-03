# Cellular data: the QMI session, QMAP and the iOS interface

Owner goal: SIM mobile data in iOS, routed like a wired Ethernet link. The data agent owns patches 2220 to 2239, the code above the modem lifecycle (modem agent, 2170 to 2189) and above IPA (IPA agent, 2200 to 2219). Source lives in `plans/data/src` until it becomes numbered patches in `patches/data/`.

Labels as in `docs/input.md`: **Measured**, **Code**, **Inferred**, **Missing**.

## ONLINE sequence (2026-11-03)

The modem and IPA agents follow this order. Each step names who does it and what must be true before the next one starts. Nothing in steps 5 to 9 runs before a Sol GO for that exact boot.

1. **Modem agent.** PAS 4 boot, SMP2P, GLINK, QRTR, rmtfs (RAM-only writes), pd-mapper. Keep the runtime alive for the whole data session instead of the current 120 s bound, and stop it only through the data agent's stop hook (step 10). Done when DMS 0x02, NAS 0x03, WDS 0x01, WDA 0x1a and DPM 0x2f are announced. ast151 already showed NAS, WDS and 45 more services.
2. **Modem agent.** UIM card status (2176). Data needs a present card with the USIM application ready (app state 7). PIN-locked or absent stops here.
3. **IPA agent.** IPA 5.2 clock and interconnect, firmware through PAS 0x0f, GSI rings, endpoint setup for AP WAN producer (endpoint 2, channel 5) and AP WAN consumer (endpoint 18, channel 9), then the IPA QMI handshake on service 0x31: Init Modem Driver 0x21 with real table ranges, then wait for the modem's completion 0x35. Done when the endpoints are running and the handshake completed. The modem must not go online before this, because it programs its filter and route rules during the handshake.
4. **Data agent.** DPM Open Port (0x2f, 0x20): one hardware data port, endpoint type embedded (4), interface 1, modem RX endpoint = our TX endpoint 2, modem TX endpoint = our RX endpoint 18. HakiOS found that without this the modem answers WDA, Bind Mux and Start Network with InvalidOperation.
5. **Data agent.** WDA Set Data Format (0x1a, 0x20) on that port: link layer raw IP (2), uplink and downlink aggregation QMAP (5) or QMAPv5 (9), downlink limits that fit the IPA RX buffers. The session refuses to go on if the modem echoes a different format.
6. **Data agent.** DMS Get Operating Mode. If not online and `sweetdataonline=1`, DMS Set Operating Mode online (0). The first integrated trial (2218, 2026-11-03) found the modem in OFFLINE (3), offline reason 0, and it refused ONLINE. So a refused ONLINE now gets exactly one LOW_POWER (1) step. The session re-reads the mode (three reads, 5 s apart) and sends ONLINE once more. Any further refusal ends the session, with the QMI error code and the hardware-restricted flag in the log. Without the gate the session waits and reports, and the raw tool can put the modem online by hand. Only online (0) and low power (1) are ever sent.
7. **Data agent.** NAS Register Indications (serving system, signal info, PLMN name), then Get Serving System every 5 s until registration state 1 and PS attach state 1.
8. **Data agent.** IPv4 first, one WDS client per family (IPv4 on client 0, IPv6 on client 1): Bind Mux Data Port (embedded, interface 1, mux 1), Set IP Family, Start Network. The APN and authentication come from `sweetapn=` and `sweetapnauth=pap|chap|none` and travel only in the Start Network request, so no stored modem profile is created or changed. For the owner's carrier, these are the carrier APN from the operator's public help page. Without `sweetapn` the modem's default profile is used. Before any start, the 3GPP profile list is read and logged read-only. The IPv6 call starts only after IPv4 is up, and is not tried when IPv4 cannot come up. Error 0x46 right after registration means the packet domain is still attaching: retry every 6 s, at most 20 times. Then Get Current Settings: address, gateway, DNS, MTU, IPv6 prefix.
9. **IPA agent, then data agent.** The IPA agent's RX and TX paths hand whole buffers to and from the data agent's QMAP layer (`qmap_deaggregate`, `qmap_frame`, mux 1). The data agent's interface presents them to iOS (below) and runs DHCP and router advertisements from the step 8 settings.
10. **Teardown, data agent first.** WDS Stop Network for every live handle, then the IPA agent drains and stops its channels, then the modem agent shuts the modem down. Every exit path runs these in that order.

### Agreed interfaces (2026-11-03)

These are the hooks in `bsd/dev/data/sweet_data.h` after the first Sol audit, plus the other agents' answers in `plans/modem/README.md` and `plans/ipa/README.md`. Each hook does nothing unless the boot args hold `sweetdata=1` or `sweetqmiraw=1`.

Modem runtime, all on its single worker thread:
- QRTR local capacity rises to 8: rmtfs, pd-mapper, UIM, SSCTL, IPA 0x4004 and 0x4005, data 0x4010 and 0x4011. Service-0 client ports are already unannounced, so no new QRTR API is needed.
- `sweet_data_attach(&qr)` after `qcom_qrtr_init` and after every other port is registered, and before `qcom_qrtr_start`.
- `sweet_data_poll(&qr, now_ms)` every worker iteration.
- `sweet_data_discovery(&qr)` after every QRTR control packet (NEW_SERVER, DEL_SERVER, BYE), before the next packet is dispatched. A service loss then reaches the session before any later reply from that service, even when the same endpoint is announced again in the same GLINK batch (modem 2189 audit, finding 1). Every worker entry point (poll, receive, discovery, a USIM change) runs one step, `data_services_step`: it revokes a lost readiness first, then syncs the service cache, then grants readiness if it holds. A service announced in the same step as a readiness loss can then never start setup on the old record.
- `sweet_data_usim(1)` once a selected USIM reports app state 7, and `sweet_data_usim(0)` when that evidence is lost. The 2185 run saw the USIM at app state 1 (initializing), so this needs a status poll on the modem side.
- Teardown: `sweet_data_stop()`, then keep pumping GLINK and QRTR and calling poll until `sweet_data_idle()` or 5 s have passed. Then read `sweet_data_cleanup()`: 0 means every WDS call was stopped with a successful reply, EIO means a handle or an unanswered start is unresolved, and EINPROGRESS means cleanup has not finished. Put EIO into the runtime's final error and still shut the modem down. Then drain IPA, then `sweet_data_detach()`, then SSCTL and PAS. A data-session gate may extend the 120 s probe lifetime, but the hard deadline and deadman stay.

IPA driver (its API: `sweet_ipa_transmit`, `sweet_ipa_register_rx`, `sweet_ipa_ready` in `bsd/dev/modem/SweetIpa.h`):
- `sweet_data_set_ipa(tx, ctx, 1, 8192, 16)` once, before the modem starts. Here `tx` wraps `sweet_ipa_transmit`, which copies the frame before it returns. The values are QMAP v1 with no checksum offload, 8192-byte receive buffers, and at most 16 datagrams per aggregate.
- `sweet_data_ipa_ready(sweet_ipa_ready())` whenever that changes.
- The `sweet_ipa_register_rx` callback calls `sweet_data_ipa_rx(buf, bytes)`.
- Endpoints for DPM: the modem receives on our TX endpoint 2 and sends on our RX endpoint 18.

Patch 2221 (`patches/data/2221-asteroids-data-ipa-binding.patch`, `bsd/dev/data/sweet_data_ipa.c`) is the adapter between the two sides, so neither agent has to write it. The modem runtime makes two calls:
- `sweet_data_ipa_bind()` once, after the IPA driver is prepared and before `sweet_data_attach`. It installs the data TX wrapper around `sweet_ipa_transmit` and registers the IPA RX callback with `SweetIpa.h`'s limits.
- `sweet_data_ipa_refresh()` every worker iteration before `sweet_data_poll`, and on any IPA failure. It copies `sweet_ipa_ready()` to the data side.

The adapter refuses to build unless `SWEET_IPA_QMAP_VERSION` is 1, and it drops receive buffers over 8192 bytes. 2221 needs 2202 and 2220.

Sol's 2221 re-audit with the IPA agent's frozen 2205 (`docs/data-2221-reaudit.md`) gives a GO for bounded source integration. All three earlier blockers are fixed, and all five host suites pass. That covers 2220 at 410ee41d and 2221 at 4bdc74ce on the cutoff in `plans/ipa/cutoff-2206.txt`. IPA hardware writes and any phone boot still need their own image approval. The first audit, for the record: Sol's 2221 audit (`docs/data-2221-audit.md`) passed the adapter itself: types, bounds, copy lifetimes and lock order. It is NO-GO for a trial until three things land in the IPA agent's integration draft (`plans/ipa/draft2204`):
- The IPA driver clears its ready latch the moment it detects a failure.
- The worker calls bind and refresh.
- The worker quiesces the AP packet channels before detach.

The adapter's own part is done:
- The header now says any nonzero bind result is terminal for the boot, because the TX hook may already be installed.
- It also says refresh must follow every IPA readiness change or IPA service loss, before any further QRTR dispatch.
- `tools/data/tests/ipa.c` tests the adapter against stubs of both sides: bind order and limits, the EPERM and EBUSY partial binds, errno mapping, the 8192-byte receive bound including a 64-bit length that would wrap, and refresh in both directions.

The session does nothing that changes modem state until three things hold: the USIM is ready, the IPA is ready, and the transmit hook is set. Losing any of them sends it through cleanup.

## The iOS-facing interface

**Inferred, from prior ports.** configd does not need CommCenter for a wired route. On the Poco F1 port (iOS 16, ios-asteroids `NOTES.md` 2026-10-20) a kernel Ethernet ifnet `ue0` plus state written into configd's dynamic store (`State:/Network/Service/<id>/IPv4` with Router and InterfaceName, and `.../DNS`) made IPMonitor install the default route, and Safari loaded sites. HakiOS made configd's IPConfiguration plugin run DHCP on an ifnet with no IOKit object, through its `ipconfig_set_service` Mach call, then resolved names and fetched HTTPS with NSURLSession. configd also adopts an ifnet that has IOKit `IONetworkInterface` stand-ins (`SweetNetwork.cpp`), and wifid needs the `IONetworkStack` user client to stop waiting.

**Code, plan.** The cellular interface is an Ethernet ifnet with a made-up router. `plans/data/src/CellLink.c` answers ARP for every address, DHCP with the WDS address (a /24 around it, router at .1 or .2), WDS DNS and MTU, IPv6 neighbor solicitations for any target, and router solicitations with the bearer's /64 and RDNSS. All other unicast IP to the router goes to the modem. configd configures it like a USB adapter, through IPConfiguration DHCP. The packet path (QMAP and IPA) stays separate from this presentation, so a later `pdp_ip0` for CommCenter can take the same IP packets.

**Missing.** A first proof uses the USB link to the Mac as the uplink, with the same configd path (DHCP from the Mac's Internet Sharing on `ue0`). It is a check of the presentation layer only and does not block the modem path.

## en2 proof procedure (desk-ready, not run)

The proof must not be able to pass through `ue0` or the Mac.
1. Make the boot preferences with `tools/data/phone/en2-prefs.py ORIGINAL OUT`. It refuses any input but the root's original `preferences.plist` (SHA-256 e8d0685f...f368, kept in `plans/data/backup`). Its output adds a DHCP service for `en2`, first in the service order, and keeps `ue0` for SSH with no router, no DNS and only an IPv6 link-local address. `--check OUT` verifies a built file. Push it with `dev.py` as in ast167.
2. Internet Sharing stays off for the whole run, with no `bridge100`. Without it the Mac forwards nothing, so `ue0` has no uplink even by accident.
3. On the phone, first run `/usr/local/bin/sweetnetcfg -d`, so the boot job's 192.168.7.1 state cannot compete.
4. Evidence to record:
   - `sweetdhcp state` names `en2` as the primary interface.
   - `netstat -rn` has its default route on `en2` and none on `ue0`.
   - `ifconfig en2` shows the DHCP address from `CellLink.c`.
   - `ifconfig en2` and `ifconfig ue0` byte counters before and after a Safari page load show the page bytes on `en2`.
   - The kernel log has `SWEET DATA` call-up lines.
5. Put the original `preferences.plist` back byte for byte and check its hash before ending the session.

**Missing.** A tool that fetches over a socket bound to `en2`, with monotonic timing, is not built.

## Raw QMI from userland

**Code, plan.** `sysctl kern.sweet_qmi`, present only with `sweetqmiraw=1` and only for root. One sysctl call sends a QMI request (service, client 0 or 1, timeout up to 10 s) and returns the matching response. Transactions 0x8000 and up belong to the raw tool, so they never collide with the session. An allow list (`plans/data/src/QmiRaw.c`) refuses anything that returns a full identifier, touches PINs or SIM files, resets or reflashes the modem, or persists past a modem restart. A boot with this gate on needs a Sol GO, like any boot that can change modem state.

## Status

**Code.** Patch 2220 adds `bsd/dev/data/`:
- `QmiData.c`: QMI encoders and decoders for DMS, NAS, WDS, WDA and DPM.
- `Qmap.c`: QMAP v1 and v5 framing and deaggregation, and flow-control commands.
- `CellLink.c`: the Ethernet presentation.
- `CellSession.c`: the session state machine.
- `QmiRaw.c`: the raw allow list.
- `sweet_data.c`: the kernel glue (two QRTR client ports, `en2`, `kern.sweet_qmi` and `kern.sweet_qmi_ind`, and the hooks above).

Every gate is off by default.

**Review.** The first Sol audit (`docs/data-2220-audit.md`) was NO-GO with ten blocking findings. The revised 2220 addresses each one:
- The raw list is now read-only, apart from strictly checked DMS mode and NAS mode-preference requests. Duplicate TLVs are refused, and only client 0 may be used.
- The raw slot always frees on timeout, send failure, service loss and shutdown, and replies to retired transactions are dropped.
- Stop is a terminal barrier. It also stops handles that arrive late from starts already in flight.
- Failures go through cleanup.
- A Stop Network reply is checked. The handle stays until a stop succeeds.
- A readiness barrier covers the USIM and the IPA.
- The data format is checked, with Get Data Format when the modem's echo is incomplete, and it must fit the IPA limits.
- Family and settings replies are checked, so a call only comes up with a usable address for its own family.
- Every send attempt is charged, and every retry is bounded.
- Each indication read gets its own buffer.
- The lock is static, and the IPA transmit hook is called outside the lock.
- `SIOCGIFMEDIA` honours the caller's count.
- DHCP and ICMPv6 inputs are checked: fragments, UDP length, code and checksum.
- Ethernet padding is trimmed before QMAP framing.
- An unsolicited RA goes out when IPv6 comes up, refreshed every 10 minutes.

The second audit (`docs/data-2220-reaudit.md`) found five blockers and six smaller problems, all now fixed:
- The raw interface is reads only, so no raw request can put the modem online or around the barrier.
- NAS indication registration waits for the barrier.
- A start past its reply deadline keeps ownership of any late handle, blocks new starts for its client, and holds cleanup until it resolves or the 5 s deadline reports it.
- WDA must confirm positive downlink limits within the IPA's 16 datagrams and 8192 bytes. Duplicate or wrong-length format TLVs are refused.
- A readiness loss applies on the worker before any receive, service change or poll can send.
- Transactions are never reused in one attach, and attach is allowed once per boot.
- The start budget runs until a call comes up, and an unusable call that will not stop is ended.
- A reconfiguration withdraws the presented family first.
- Settings must carry a usable address of the call's own family.
- QMAP commands need clear reserved bits.

The third audit (`docs/data-2220-audit3.md`) gave a conditional GO for `sweetqmiraw=1` alone. Both session configurations were still NO-GO on one blocker: a malformed Start Network reply cleared the start's unknown record. That is now fixed:
- Only a valid success with one handle, or a valid failure result, settles who owns a start. Duplicate or wrong-sized result or handle TLVs count as malformed.
- A malformed reply keeps the start unresolved. It still blocks new starts, still counts for cleanup, and a later valid answer is still adopted.

Smaller fixes from the same audit:
- The last 64 session transactions are kept for Stop Network, and no number is ever reused.
- A family TLV must be exactly one byte, and any present value, zero included, must match the call.
- Flow-control commands must be exactly 16 bytes.
- `sweet_data_cleanup()` gives the modem's stop hook a clean or unresolved outcome.

The live 2187 run measured the selected USIM at app state 7 with no provisioning step.

**Sol GO.** The fourth audit (`docs/data-2220-audit4.md`, patch hash 28723c9b) gives a conditional GO for one bounded trial each of `sweetqmiraw=1` alone, `sweetdata=1`, and `sweetdata=1 sweetdataonline=1`. Its conditions:
- the documented wiring
- the 120 s runtime deadline and deadman
- one attach per boot
- treating `EIO` or an unfinished `EINPROGRESS` from `sweet_data_cleanup()` as failure
- no export or logging of whole raw responses

Its remaining open finding, N1, blocked sessions long enough to exhaust transactions. That is now fixed:
- In teardown only Stop Network can be sent, and the NAS poll returns when it reaches the reserve.
- Every response must carry exactly one result TLV.
- The cleanup comment now covers disconnected indications.

Sol's delta check (`docs/data-2220-audit5.md`) confirms N1 is fixed and the GO carries over to patch hash adda4660. The service-cache fix for the modem 2189 audit's finding 1 (patch hash 8a0b901c) also keeps the GO (`docs/data-2220-audit6.md`), on the condition that the modem runtime calls `sweet_data_discovery()` after every QRTR control packet. The revoke-sync-grant step (patch hash 410ee41d, `docs/data-2220-audit7.md`) keeps it too. The APN-authentication and IPv4-first revision (patch hash f331702b, `docs/data-2220-audit8.md`) keeps it too, and audit8 accepts it as the replacement for 410ee41d in the integration cutoff once the cutoff and its copied snapshot record the new bytes and the reproduction passes. 2221 stays at 4bdc74ce. Lifting the data session's 120 s cap now depends only on the modem runtime's own lifetime approval. Every other audit4 condition stays. Still open, and not blocking: raw responses and modem text are exported as they come (N5), and the shape checks on settings TLVs are partial.

**Measured, host.** `tools/data/check.sh` rebuilds the source from the public patches and passes under AddressSanitizer and UndefinedBehaviorSanitizer. The session tests cover:
- Nothing beyond read-only queries before the barrier.
- A stop with starts in flight.
- Late DPM answers after a stop.
- Loss of a service, of readiness and of WDS during a live call.
- Refused and unanswered stops.
- Send refusals backing off.
- WDA mismatches and oversized limits.
- Family and settings refusals.
- Terminal start errors, and the 20-try limit.

`sweet_data.c` itself has no host test. It needs kernel APIs.

**Measured, compile.** All six kernel files compile against the xnu-12377 headers with the modem tree's exact flags. The only remaining warnings come from XNU's own macros. There is no full kernel build or link yet.

**Missing.** A Sol image approval for the integrated kernel (modem 2189, data 2220/2221, IPA 2200 to 2206), working IPA hardware (PAS15, SMMU, GSI), then one bounded trial with `sweetdata=1 sweetdataonline=1` and the en2 proof procedure below.

## USB proof of the presentation path

The plan was to show iOS routing real traffic through a kernel Ethernet ifnet with no kernel change, using `ue0` and the Mac's Internet Sharing as the uplink. `tools/data/phone/sweetdhcp` asks configd's IPConfiguration for DHCP on an interface, dumps the store, resolves names, fetches pages and opens URLs. Two runs on 2026-11-03 used the existing USB dev image, with nothing flashed and the root left clean.

**Measured, ast163.**
- Before sharing, configd already had `ue0` as its IPv4 primary, with a default route via 192.168.7.1 and DNS 1.1.1.1 and 8.8.8.8, from the state keys `sweetnetcfg` writes.
- With sharing on (bridge100 at 192.168.2.1, en8 a member), `sweetdhcp dhcp ue0` returned status 0, and `ue0` took the lease 192.168.2.2 from the Mac.
- configd then added `default via 192.168.2.1` itself (`SWEET NET: configd route add 0.0.0.0 gw 192.168.2.1`). Right after, iOS set its clock from the network (`SWEET RTC: time set 1793688922`, the correct time). That is real IPv4 traffic from iOS to a time server through the Mac.
- Before that route appeared, a name resolved through the Mac's DNS (IPv6 answers only), and an HTTP fetch failed because only the IPv6 route existed.

**Measured, ast164.**
- With the clock already right, the root's preconfigured `ue0` service (Manual 192.168.7.2, router 192.168.7.1) stayed primary.
- Writing that service's state with the DHCP values did not stick, because IPConfiguration rewrote it from the preferences file. Switching the preferences file to DHCP needs a configd restart, and the root has no `killall`.
- No page loaded, and Safari was not tested. The original preferences file is back, byte for byte.

**Measured, ast167 (2026-11-03 18:26): the proof.**
- Before boot, `dev.py` replaced the root's `preferences.plist` with a copy whose `ue0` service (5EE7C0DE) has IPv4 ConfigMethod DHCP: 957 -> 806 bytes, undo file in the run directory.
- With Internet Sharing on (bridge100 at 192.168.2.1, en8 a member), configd's IPConfiguration ran DHCP on `ue0` by itself and took 192.168.2.2.
- configd's global state then named `ue0` the primary interface for IPv4 (router 192.168.2.1) and IPv6. DNS was the Mac (192.168.2.1, plus its IPv6 link-local), and the default route was via 192.168.2.1.
- The owner browsed several sites in Safari on the phone over the cable.
- After the run the original `preferences.plist` was put back byte for byte (SHA-256 prefix e8d0685f5b691028a777). The run ended `selftest-done`, root clean.

So configd makes a DHCP-configured kernel Ethernet ifnet the primary route, and Safari loads real pages through it, with no IOKit object, no CommCenter and no kernel change. The cellular `en2` takes the same path: `CellLink.c` answers the DHCP that IPConfiguration sends.

**Follow-up, low priority.** YouTube in Safari showed "An error occurred" while other sites loaded. It might be IPv6 (the Mac advertised an IPv6 router but routes only IPv4), QUIC/UDP through Internet Sharing, or DNS64/NAT64 detection. Not investigated.

**Harness bug.** The `usbdev` self-test hook computes uptime as the wall clock minus boot time. A network clock jump ended ast163's session at once ("time over at 11950000 s"). Any session that reaches the internet hits this, so the hook needs a monotonic uptime. That hook belongs to the USB/devloop owner.
