# 2173 first-start review

Verdict: NO-GO for the first actual MPSS/PAS start, including PAS-only operation. No phone or build operation was performed in this review. No implementation was changed.

This change connects the signed split-firmware loader to PAS4, proxy power, SMP2P and optional GLINK/QRTR/RAM-backed rmtfs. It runs for at most 120 seconds after authentication, then attempts graceful stop and PAS shutdown.

## Reviewed revision and evidence

Read `3a-IOS/HANDOFF.md` first, followed by `3a-IOS/docs/modem.md` and `plans/modem/README.md`. The requested `docs/modem.md` lives under `3a-IOS`. Reviewed 2173 atop 2170 through 2172 and the dedicated production source under `ios-asteroids/research/xnu-modem/dxb/xnu/bsd/dev/`.

The production `sweet_modem.c` resolves inside that dedicated tree and includes `modem/ModemRuntime.inc`. In-memory reconstruction matched `sweet_modem.c`, SMEM and GLINK against the ordered patch series. All thirteen newly added 2173 files matched the patch byte for byte. The touch change and build-file additions were also inspected.

2173 SHA256 is `3b0f4f1c13f30ab85316bec6d9024c0b614e1d4448134c93dcbfcab9b90ea7ba`. Production runtime SHA256 is `2e462466846ae84293977fd252050beac6d89f9a1b429efa90f336df5b3cf19a`.

`logs/build-2173-final.txt` now ends with `done building xnu` and final timestamp operations. Existing `logs/start-check.txt` and `logs/protocol-check.txt` report passing checks. I inspected their test sources and extraction script without rerunning compilers. The lifecycle test extracts the actual SCM retry and cleanup bodies, but mocks the actual RPMh, ownership, QMP and PAS operations. These checks do not establish a working hardware power sequence.

## Must fix

1. XO is neither acquired nor protected before PAS authentication.

   `ModemRuntime.inc:265-282` looks up only `cx.lvl` and `mss.lvl`, sends load_state, votes those rails and starts PAS. The same-phone DT explicitly supplies an `xo` clock at `asteroids.dts:7050-7056`. There is no XO enable, established inherited-vote guarantee, ownership record or restore path in this runtime.

   If the inherited APPS XO request is absent or changes during the probe, this sequence releases the modem without establishing its required clock. Another processor's current vote or the ADSP reference's bootloader inheritance does not prove the clock remains requested throughout this run. This blocks PAS-only as well as IPC boots.

   Implement and test a shared-vote-safe XO lifecycle before auth/reset, or establish a verifiable board invariant and refuse start when it cannot be established. Linux enables XO before authentication and uses `xo.lvl` enable value 3, with clock vote aggregation. Do not feed this enable value through the voltage-level lookup or blindly write zero on cleanup. Preserve inherited and other consumers' requests, serialize through the shared RSC lock, and retain uncertain TCS state on failure. References: [Linux PAS start](https://github.com/torvalds/linux/blob/master/drivers/remoteproc/qcom_q6v5_pas.c), [Linux RPMh clocks](https://github.com/torvalds/linux/blob/v6.12/drivers/clk/qcom/clk-rpmh.c).

2. GLINK permanently loses receive-completion acknowledgements under TX backpressure.

   `QcomGlink.c:222-236` delivers the complete packet, clears busy/fill and advances RX even when `command(RX_DONE...)` returns `QcomGlinkAgain`. No pending-completion state retries that command. This older 2172 core becomes reachable on hardware through 2173's `m_ipc_poll`.

   With insufficient TX space for the completion, the remote keeps that intent busy forever. Repeating this exhausts its usable intents and stalls QRTR/rmtfs despite the polling loop continuing. Existing handshake tests do not exercise this case. This blocks GLINK/rmtfs scope independently of finding 1.

   Retain and retry the completion before making the intent reusable, with exactly-once delivery to the client. Add a two-peer test that fills TX, receives a complete packet, frees TX and verifies completion delivery and subsequent intent reuse, both with and without negotiated reuse.

## Safety and protocol findings that passed source review

The hardware gates require one exact `name=1` token. Bare, duplicate and numeric aliases fail closed. `sweetmpss` gates the hardware worker. GLINK, rmtfs and pd-mapper have separate gates and dependency checks. No tftp, UIM, DMS online or data-call sender is present.

The reachable rmtfs storage chain is `m_shadow_prepare` to fixed-name size lookup and `sweet_ufs_read_part`, then heap snapshots. Its request handler copies between those snapshots and the bounded low shared window. Writes change only heap snapshots. Neither the handler nor its callbacks reach a UFS write API. This proves the new modem service's call path, not an isolation boundary against arbitrary authenticated modem firmware or unrelated kernel storage callers.

The four snapshots each have a 16 MiB cap. Vector extents use 64-bit arithmetic before bounded 32-bit copies. The shared window, metadata and indirect arguments do not overlap each other or the MPSS load reservation. The indirect-argument and metadata addresses avoid the above-4-GiB XNU allocation issue. Low-bank availability remains the supplied reservation assumption, not a new hardware measurement.

PAS4 IDs and argument packing, relocatable segment placement and bounded loader accesses are consistent with the inspected references. Ownership packing uses HLOS 3 and MSS_MSA 15 with RW permissions and a low indirect-argument page. SMP2P item IDs, processor IDs, named entries, bits and IPCC client/signals agree with the supplied DT. Polling discovers the remote FIFO after startup and bounds receive work.

The shared lock covers touch's `at_amc_locked` and modem active-TCS selection, programming and IRQ-mask updates. Both reject non-idle TCSs. A modem timeout leaves control/enable/wait state intact and cleanup avoids reprogramming uncertain votes. Modem cleanup touches only its CX/MSS resource addresses and selected TCS bit. Touch uses distinct VRM/BCM resources, including SN3, MC0 and SH0, so I found no path that explicitly clears those touch votes. This serialization does not supply missing XO vote aggregation or prove inherited CX/MSS baselines are zero.

Every ordinary error after PAS init is routed through cleanup, with shutdown owed before the init call. SCM busy and interrupted resumptions are bounded. PAS shutdown retries only -22, at most fifty times. If shutdown is unverified, memory and power are retained. Ownership failure also retains allocations. QMP runtime never calls abandon and waits for a pending packet before sending another. An incomplete cleanup is logged but its return is ignored by `modem_run`, so the external recovery/deadman bound remains essential.

## Exact allowed boot scope

GO remains limited to the previously audited read-only firmware preflight, subject to its existing image/build and recovery safeguards. Use explicit `sweetmodem=1` with all of `sweetmpss`, `sweetmglink`, `sweetmrmtfs` and `sweetmpdm` absent or explicitly zero. Leaving modem work entirely off is also allowed. This grants no phone operation in this review task.

No actual PAS start, rmtfs boot or tftp boot is authorized. After fixes, obtain a new Sol delegate audit of the exact patch and production source, followed by a successful matching build before any start image is used. A future approved image must retain the guarded devloop recovery path and an independently bounded deadman, with the whole run below twenty minutes. Require verified PAS shutdown and complete cleanup in the result. Ready alone is not M2 success without handover and the scoped service evidence.

Tftp requires observed need and a separate gate and audit. UIM, online mode, IPA and data calls remain excluded. Firmware and raw device evidence stay local. No partition write, flash, slot change, stock boot or Android restore is authorized.
