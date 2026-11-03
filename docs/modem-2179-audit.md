# 2179 offline shutdown review

2026-11-03. Conditional GO for ONE offline RAM boot of exact modem patches 2170 through 2179 over the previously audited dedicated xnu-modem tree with USB v5, subject to the conditions below. M2 remains incomplete. UIM remains NO-GO until verified M2 and a fresh separate review. This grants no daily-use or repeated-boot approval.

What this change does: 2179 sends one fixed SSCTL shutdown request before PAS shutdown. It waits for a successful response plus a shutdown acknowledgment, keeps serving RAM storage during that wait, and falls back to the existing bounded SMP2P/PAS path when graceful shutdown is unavailable.

I read `3a-IOS/HANDOFF.md` first, the current `plans/modem/README.md` checkpoint, and `sol-2178-audit.md`. I applied ponytail-review and unslop. Review only: no build, compilation, test execution, phone access, delegation, upload or deletion. This report is the only file I wrote. Intended load is one bounded offline experiment on the established host and phone.

## Exact source checked

Production is `ios-asteroids/research/xnu-modem/dxb/xnu`. In memory, I reversed all ten numbered modem patches against production with unique exact text matches. All 71 hunks matched and all 20 newly added files reversed to empty. This establishes the numbered delta and integration hunks, not the identity of every unrelated base-tree file.

All ten patches match their public mirrors byte for byte. Patches 2170 through 2178 retain the preceding audit identities. The 2179 patch SHA256 is `80ee3f60018f3a47a9c15c3d40d59aacefc854f226e10b8519b58249dcaf013f`.

Production `bsd/dev/modem/ModemRuntime.inc` SHA256 is `ae6c9b198092ce8105d4848875f8c858d835d2ca3e0fbd624a8fb0e398f3fdf5`. The saved `baseline2179/ModemRuntime.inc` has the audited 2178 hash `c94de841755d05c250b5d2371c78efe10f06570d0aaab577678b3f7963ed43d7`.

Production `sweet_modem.c`, `ModemUim.c` and `QcomRpmh.c` retain their 2178 audit hashes. `sweet_usb.c`, `sweet_usb_gadget.h` and `sweet_reboot.c` match `plans/usb/gadget/work/b5/bsd/dev` byte for byte. USB C SHA256 remains `5695af3f994645c5e6597edac3995b3e1b0ff14d90adaf0ccd6b1eda47cd883e`.

## Evidence and mechanism

Ast153 does not pass M2. I checked `ios-asteroids/logs/ast153-modem-offline-2178/result.json`: selftest-fail, root clean before and after, stale=false, 626 seconds. The frame count in that result cannot establish fresh screenshots because the body never ran.

The console records FAIL preserve frames at line 3160, matching the repeated archive-name failure. Line 7236 announces service 43 at packed instance `0x1202`. Line 10088 clears the stop request after acknowledgment. All fifty calls at 10089 through 10138 return raw transport status `-3145800` and result zero. Line 10139 retains memory and power and 10140 reports final_error=5. Line 13384 records the 600-second deadman reboot. This is failed shutdown plus a harness failure followed by guarded recovery, not evidence of a modem crash. 2178 did not solve shutdown.

I inspected the supplied local primary Nothing sources. `reference/nothing-drivers_remoteproc_qcom_sysmon.c:180–208,319–401,465–490` defines shutdown request and indication 0x21, the standard result TLV, completion waiting, and modem legacy v1 instance zero or v2 matching the sysmon instance. `nothing-drivers_remoteproc_qcom_q6v5_pas.c:2502–2518,2921` connects volcano modem PAS4 to sysmon ID 0x12. `nothing-drivers_remoteproc_qcom_q6v5.c:256–278` skips the SMP stop dance after sysmon acknowledgment. PAS shutdown still follows in `qcom_q6v5_pas.c:1120` onward.

The local `ios-asteroids/research/asteroids-hw/asteroids.dts:7050` modem node maps shutdown-ack to SMP2P bit 7. These sources support the selected endpoint, command and ordering. They do not decode the raw secure-world error or prove that SSCTL fixes it. The candidate is deliberately stricter than the vendor acknowledgment path: it requires both a zero result/error response and bit 7 or an empty indication, within one ten-second polling interval.

`ModemRuntime.inc:192–204` accepts replies only after a sent request, from the selected node/port, for message 0x21. Responses must match transaction 1 and contain exactly one standard result TLV. Truncated, duplicate or extra TLVs fail closed. QRTR routes replies to the registered client port 0x4003. Indications have no request transaction match and are endpoint/message bound, as appropriate for the empty shutdown indication.

`m_shutdown_request:276–308` permits only service 43, packed instance 0x1202 or 1. It rejects an already asserted bit 7, resets response/indication flags, sends once, and polls SMP2P plus the existing IPC transport. The IPC path continues RAM rmtfs and pd-mapper handling. Missing service, send failure, timeout or transport/fatal failure falls back. The ten seconds bounds polling with progress through those helpers, not arbitrary firmware stalls or synchronous logging latency.

`m_cleanup:348–374` always reaches actual PAS shutdown when owed, including a successful SSCTL handshake. Graceful acknowledgment only skips SMP stop. Failed PAS never permits reclaim, unmapping, freeing or rail release. Existing bounded retries and uncertain-vote retention remain intact. Successful PAS still requires ownership reclaim, QMP-off and MSS-only release for cleanup=ok. Shared XO and CX remain pinned until reset.

## Findings and coverage limits

1. Shutdown-time transport failure is not propagated independently to final_error. At `ModemRuntime.inc:429`, runtime rc is decided before cleanup. If IPC polling during SSCTL sets `m.failed` or SMP polling sets `m.fatal`, fallback PAS can still succeed and cleanup can return true, leaving final_error=0. This does not bypass PAS verification or release memory after a failed PAS. For this manually reviewed trial, reject advancement on any fatal or unresolved transport failure regardless of final_error. Before automatic milestone acceptance, preserve shutdown-time failure in the worker result and add a regression case.

2. The host suites test shutdown and cleanup separately. `tests/shutdown-prefix.c` stubs SMP and IPC polling, and `tests/lifecycle-prefix.c` stubs `m_shutdown_request`. They do not dynamically prove real RAM-rmtfs progress during SSCTL, delivery through the actual client-port registration, a real acknowledgment transition, or the joined failure-to-fallback path. Source review covers those connections for this single trial. A combined extracted-function test would close this gap before relying on the handshake as an established shutdown implementation.

No finding requires withholding this guarded diagnostic boot. Neither finding permits treating final_error=0, SSCTL accepted=1 or selftest DONE alone as M2.

I inspected passing `logs/start-check-2179.txt` and `logs/uim-check-2179.txt`. Coverage includes endpoint/instance selection, wire request, response transaction, malformed/truncated/duplicate TLVs, pre-request replies, stale high acknowledgment, response-only and indication-only timeout, send failure and fatal exit. Existing tests execute actual SCM/PAS and cleanup functions for transient/permanent vendor errors, fifty-call exhaustion, reclaim failure, stop clearing and retention. The graceful cleanup case checks that PAS still runs while SMP updates are skipped.

The generated `3a-IOS/tools/modem/tests/out/shutdown.c` and `lifecycle.c` match current production function extraction plus current wrappers exactly. Their SHA256 values are `58f466a78770ee563d1e111703cb33b0f3b556e643def0956e9c0b83b0971e16` and `54e14c1ed1efa9579f656569590380497243debb9e4d75d28f1c7ace73d3c99e`. Supplied passing logs are evidence from the parent, not tests rerun by this reviewer.

## Arguments and selftest

The default-off exact `modem_gate` is reused for sweetmstop. An absent, bare, duplicate or non-1 value cannot enable SSCTL. GLINK is required before any SSCTL work. No new partition write API or general QMI command surface is added.

I evaluated the argument construction without executing either builder. Offline produces 594 bytes with unique names, and UIM produces 606. Both preserve cpus=8, sweetfbarm=1, sweetpanicfb=10, sweetdeadman=600, sweetselftest=1, sweetthermal=3, keep plus astwait, UBWC, fetchsync and the daily graphics settings. The offline line contains no sweetmuim. It adds sweetmstop=1 and removes debug=0xc. The panic/deadman arming paths are independent of debug in the reviewed reboot code, and the panic hook runs independently in debug.c. Removing debug changes debugger behavior but does not remove these reset guards. Serial logging remains enabled. These are constructed lines, not verification of a newly packed image.

Both builders and the archive hook match their public mirrors. The public modem-offline body intentionally retains @RUN_NONCE@ while the local body has a concrete nonce. The bounded archive suffix loop tries the original name and suffixes through 999, preserving previous generations without deletion. Exhaustion or move failure exits the script and relies on the armed deadman. The supplied `logs/frame-collision-check-2179.txt` passes three generations with the same RTC and PID.

The prepared `out/offline-script-2179.sh` contains the exact current hook/body composition. It checks screenshot exit and nonempty output, writes the fresh completion nonce after the done step, syncs and waits fifteen seconds after DONE before reboot. Its SHA256 is `d6b672bf2d082c46a675eb219a7e5e5a3f1ccc5c4b18945ccddbb6dc68026cc3`. The body marker proves body completion only, never modem success. Parent must compare the actual script sent by dev.py and collect its exact nonce and fresh frame.

## Conditions for the one boot

1. Confirm the matching safe --remake build exited 0 and no reviewed source changed. The existing build log now contains `done building xnu`; I did not establish the process exit status. The installed kernel currently hashes to `fefb2128fe9591307957f57d6ee1bca6982595c2e3469e75a5d0900d957e3189`. Verify every rebased kernel segment in the new offline RAM image against that exact successful build, the embedded payload and actual packed argument line, and record image/kernel hashes. No stale 2178 image or unrelated kernel delta is approved.

2. In the actual image require one each of sweetmpss=1, sweetmglink=1, sweetmrmtfs=1, sweetmpdm=1, sweetmstop=1 and sweetusb=4, all named reset/selftest/thermal/graphics guards above, unique names and at most 607 bytes. Require sweetmuim, sweetmprobe and sweetusblog absent. Inspect the final composed script, collision-safe archive block and fresh nonce before launch. A prepared UIM image is not permission to boot it.

3. Acquire /tmp/phone-modem-wants before waiting on other active phone-job PIDs, then independently verify actual mode and no active dev.py immediately before launch. Ignore the battery hold. Never interrupt owner iOS. Require checked clean recovery before this one boot, as the current runner does. Use dev.py only and keep the run under twenty minutes. Do not automatically replay a failed run.

4. Accept M2 only from same-run ready plus handover before shutdown, GLINK/QRTR, scoped actual RAM rmtfs and pd-mapper responses, actual PAS shutdown status=0/result=0, STOP verified PAS with cleanup=ok and final_error=0. On graceful success require the matching zero SSCTL response and a new bit-7 acknowledgment or valid indication. On fallback require stop-request clearing and retain its timeout evidence for review. Require successful owed reclaim/QMP-off/MSS release and no fatal, unresolved transport/canary failure or unhealthy USB. Ready/handover may clear during successful shutdown, so use the earlier same-run observations.

5. Require normal selftest completion, recorded done step and DONE, matching fresh nonce/frame, clean root and stale=false. FAIL/SKIP, panic, fired deadman, STOP UNVERIFIED, incomplete cleanup, nonzero final error or missing evidence fails advancement. Retain memory and power on unverified PAS, return only through guarded reset/recovery, and obtain fresh evidence review for any additional modem boot.

## Scope and safety

Owner authorization to continue toward M3 does not waive the M2 gate. After verified M2, a separate review must approve the exact UIM-enabled image and bounded read-only status requests. Service discovery, successful SSCTL or a clean filesystem cannot establish SIM status. No UIM request is approved by this report.

No partition writes, including modemst1/2, fsg, fsc, persist or modem_a. Modem partition snapshots are read into RAM and rmtfs writes affect RAM only. No flashing, stock kernel, plain fastboot reboot, set_active or Android. No TFTP, IPA, online mode, NAS/WDS or raw QMI bridge. No sudo or writes outside the workspace or /tmp, and removals only through /usr/bin/trash with absolute paths. Keep firmware, raw logs and full identifiers out of public material.

Not checked live: final packed 2179 image, successful build process exit, hardware SSCTL/PAS shutdown, M2 or M3. Verdict: conditional GO for the single offline trial above. UIM remains NO-GO pending verified M2 and fresh review.
