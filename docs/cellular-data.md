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
6. **Data agent.** DMS Get Operating Mode. If not online and `sweetdataonline=1`, DMS Set Operating Mode online (0). Without the gate the session waits and reports, and the raw tool can put the modem online by hand. Only online (0) and low power (1) are ever sent.
7. **Data agent.** NAS Register Indications (serving system, signal info, PLMN name), then Get Serving System every 5 s until registration state 1 and PS attach state 1.
8. **Data agent.** Per IP family, one WDS client each (IPv4 on client 0, IPv6 on client 1): Bind Mux Data Port (embedded, interface 1, mux 1), Set IP Family, Start Network with the APN (`sweetapn=`, else the modem's default profile). Error 0x46 right after registration means the packet domain is still attaching: retry every 6 s, at most 20 times. Then Get Current Settings: address, gateway, DNS, MTU, IPv6 prefix.
9. **IPA agent, then data agent.** The IPA agent's RX and TX paths hand whole buffers to and from the data agent's QMAP layer (`qmap_deaggregate`, `qmap_frame`, mux 1). The data agent's interface presents them to iOS (below) and runs DHCP and router advertisements from the step 8 settings.
10. **Teardown, data agent first.** WDS Stop Network for every live handle, then the IPA agent drains and stops its channels, then the modem agent shuts the modem down. Every exit path runs these in that order.

### What the data agent needs from the other agents

From the modem agent:
- `QCOM_QRTR_MAX_LOCAL` raised from 4 to at least 6. rmtfs, pd-mapper and UIM use three slots; data needs two client ports (0x4010 for client 0, 0x4011 for the IPv6 WDS client).
- A way to add a client port that does not announce NEW_SERVER (today `qcom_qrtr_add_service(q, 0, 0, ...)` announces service 0). Until then the data clients register the way UIM does.
- Three hooks in the runtime's worker: `sweet_data_attach(&m.qr)` after `qcom_qrtr_init`, `sweet_data_poll(&m.qr, now_ms)` each loop, and `sweet_data_stop()` before the modem stops, which returns when every Stop Network is answered or after 5 s.
- The NEW_SERVER and DEL_SERVER events (or the existing remote list) for DMS, NAS, WDS, WDA and DPM, so the session can start and fail closed when a service leaves.
- A long-running mode: the 120 s bound stays for probes, with a separate gate for a data session.

From the IPA agent:
- The QMAP version the endpoints are programmed for. Linux's IPA 5.2 table turns checksum offload on, which means QMAPv5 with a 4-byte checksum header. The first data boot is simpler with checksum offload off and plain QMAP. The data agent's code supports both; the IPA agent picks, and WDA then asks for the same.
- The RX buffer size and aggregation byte and packet limits, so WDA's downlink limits never exceed them.
- Two calls: a transmit of one framed buffer on endpoint 2, and an RX callback per completed buffer from endpoint 18, both outside interrupt context.
- Confirmation of the endpoint numbers 2 and 18 for DPM from the live hardware.

## The iOS-facing interface

**Inferred, from prior ports.** configd does not need CommCenter for a wired route. On the Poco F1 port (iOS 16, ios-asteroids `NOTES.md` 2026-10-20) a kernel Ethernet ifnet `ue0` plus state written into configd's dynamic store (`State:/Network/Service/<id>/IPv4` with Router and InterfaceName, and `.../DNS`) made IPMonitor install the default route, and Safari loaded sites. HakiOS made configd's IPConfiguration plugin run DHCP on an ifnet with no IOKit object, through its `ipconfig_set_service` Mach call, then resolved names and fetched HTTPS with NSURLSession. configd also adopts an ifnet that has IOKit `IONetworkInterface` stand-ins (`SweetNetwork.cpp`), and wifid needs the `IONetworkStack` user client to stop waiting.

**Code, plan.** The cellular interface is an Ethernet ifnet with a made-up router. `plans/data/src/CellLink.c` answers ARP for every address, DHCP with the WDS address (a /24 around it, router at .1 or .2), WDS DNS and MTU, IPv6 neighbor solicitations for any target, and router solicitations with the bearer's /64 and RDNSS. All other unicast IP to the router goes to the modem. configd configures it like a USB adapter, through IPConfiguration DHCP. The packet path (QMAP and IPA) stays separate from this presentation, so a later `pdp_ip0` for CommCenter can take the same IP packets.

**Missing.** A first proof uses the USB link to the Mac as the uplink, with the same configd path (DHCP from the Mac's Internet Sharing on `ue0`). It is a check of the presentation layer only and does not block the modem path.

## Raw QMI from userland

**Code, plan.** `sysctl kern.sweet_qmi`, present only with `sweetqmiraw=1` and only for root. One sysctl call sends a QMI request (service, client 0 or 1, timeout up to 10 s) and returns the matching response. Transactions 0x8000 and up belong to the raw tool, so they never collide with the session. An allow list (`plans/data/src/QmiRaw.c`) refuses anything that returns a full identifier, touches PINs or SIM files, resets or reflashes the modem, or persists past a modem restart. A boot with this gate on needs a Sol GO, like any boot that can change modem state.

## Status

**Code, desk only.** QMI encoders and decoders for DMS, NAS, WDS, WDA and DPM (`QmiData.c`), QMAP v1 and v5 (`Qmap.c`), the Ethernet presentation (`CellLink.c`), the session state machine (`CellSession.c`) and the raw allow list (`QmiRaw.c`). Host tests are next. Nothing has run on the phone and no patch is numbered yet.
