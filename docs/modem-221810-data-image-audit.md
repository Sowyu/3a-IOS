# Combined audit10 second ONLINE trial review

**GO for one second actively supervised, bounded ONLINE trial** of `plans/modem/out/data-usb-221810-boot.img`, through the pinned runner and `integration-221810/manifest.json`, subject to the final admission and live conditions below. This decision explicitly admits the additional automatic LOW_POWER request and the maximum logical mode-attempt sequence 0,1,0 in this exact image. I found no blocking source, image, runner or proof defect within that scope.

What this change does: after a valid positive refusal of the first ONLINE request, it attempts LOW_POWER once. Successful LOW_POWER acceptance and fresh mode-1 readback permit one final ONLINE attempt. Lost or malformed matched Set replies terminate the session.

This is approval of a bounded experiment with conservative retained teardown. It establishes no successful firmware transition, registration, WDS call or packet exchange. The consumed first-trial approval cannot authorize this image or another use of the old image.

## Exact identity and final admission

I independently hashed all 154 manifest pins, all 56 production source entries and all 41 cutoff entries. Every hash matched. The pre-admission manifest SHA256 is `fb1d52ed761ce08e4bda24ff692f14b2991dc1d5c160cf5b5f43584fef15755c`.

The exact image SHA256 is `5dc34bab4045c209c4c87e85758ffc5300100bc3515b918d3a1de00f08637b2c`. Both saved and live kernel hashes are `403877da7b664e8b7cc6153165c5bc08f545d9ecc93be7285b18745a6516f5b2`.

The changed 2220 patch is exactly `ea6bd5991f5be9c48265b06806a2181c7bc974eaee77218ce9385803a2ea00e5`. Unchanged 2221 is `4bdc74ce4ad45000ba2efe18bf45454169dcb6464483d5722c918892875e1e49`. Comparison with the first trial's 56-file source map finds only `bsd/dev/data/CellSession.c` and `CellSession.h` changed. All 56 archived old source files still match their old hashes.

I ran the required reconstruction:

```
TMPDIR=/tmp python3 -B plans/modem/tools/reproduce-ipa.py 2218 plans/modem/snapshots/data-audit10
```

It reproduced all 56 actual production files exactly in `plans/modem/series-check/1793708137577394000`. The ordered inputs preserve daily2130v4, 2140 through 2144, input2150/2151/2153/2152v4/2154, USB2160/2161v5, modem2170 through 2189v2, data2220/2221, then IPA2200 through 2218. The old default data snapshot does not establish this image's source identity.

`TMPDIR=/tmp python3 -B plans/modem/tools/verify-image.py data-usb-221810` passed payload containment and exact equality of all ten packed kernel segments after chained fixups. I inspected the retained full-build log ending in `done building xnu`. I did not rebuild the kernel or independently reproduce the compiler output.

I independently extracted the image arguments and matched the pinned 607-byte line, checked unique keys and the exact 900-second dev guard. The ONLINE, IPA, data, modem session and fresh readiness dependencies remain enabled. Thermal protection remains 3, USB remains 4, and raw/probe-only admission remains excluded. Private carrier values remain request TLVs, with no profile modification authorization.

At review completion, `approved=false` and the new attempt file is absent. This report must be pinned by its final bytes before the launch owner sets approval true. That final admission may add this report's pin and change the approval flag only after the live conditions are satisfied. Changing another reviewed artifact or existing pin requires renewed review. The consumed 2218 manifests must remain unusable.

## Integrated mode transition and limits

I read `3a-IOS/HANDOFF.md`, the actual previous v2 image report and source audits 9 and 10, then traced the integrated send, receive, advance, timeout and stopping paths. The source audits provide conditional source GO. This report supplies the separate exact-image admission for the additional state-changing LOW_POWER operation.

The initial ONLINE request still requires `sweetdataonline=1` and the fresh selected-USIM, IPA and required-service barriers. The attempt counters are charged before send and remain latched for the session. Only a valid positive QMI refusal of ONLINE1 permits the single LOW_POWER attempt. Malformed matched replies do not qualify as modem refusals.

`lowpower_ok` is set only by successful matched LOW_POWER acceptance. That path clears cached mode knowledge, so ONLINE2 requires fresh mode-1 readback. Fresh mode 0 may proceed directly to registration without ONLINE2. Duplicate or retired replies cannot create acceptance or another attempt because live transactions must match and transaction numbers are not reused.

The maximum logical attempt sequence is a prefix of 0,1,0. Transport refusal consumes the attempt even when no request reaches the modem. LOW_POWER refusal, ONLINE2 refusal, matched malformed Set replies and all three unanswered Set phases enter terminal cleanup. The shared builder permits modes 0 and 1 only. No reset, OFFLINE, persistent low-power, factory, provisioning or profile modification operation is added.

I independently reran the audit10 supplemental scenarios against actual combined production session sources. They passed the three Set timeouts, late-reply rejection, acceptance plus fresh-read condition, readiness/service-loss/explicit-stop barriers at each Set phase, passive waiting and mode-prefix checks.

The 60-second ONLINE check is a soft local cutoff. I reproduced the outstanding Get sent at 56200 ms and answered at 61100 ms after stage entry at 1000 ms. Mode 3 fails, while mode 0 enters registration after 60 seconds. This accepted limitation adds no mode attempts and does not extend the independent 600-second modem runtime.

LOW_POWER read cadence still includes an extra immediate read before scheduled reads. Optional offline-reason and hardware-restriction fields default to zero when absent, so logged zero does not prove an explicit unrestricted response. The test comment identifying 0x34 as InvalidTransition remains inaccurate. These documented source-audit limitations do not block this bounded trial.

The first capture retained no numeric refusal code. Error 52, error 60 and a firmware root cause remain unestablished. HakiOS supplies an experimental rationale only. A LOW_POWER refusal on this firmware is an acceptable terminal experimental outcome, not permission to add a reset or stronger transition.

## Readiness and conservative retained teardown

The integrated owner synchronizes readiness and current service endpoints before routing a reply. `DataServices` revokes readiness before synchronizing changed services and granting fresh evidence. Required-service loss and readiness loss terminate setup. Per-control discovery remains in place, callbacks remain immutable, and restored readiness cannot restart a terminal session.

Worker exit revokes IPA readiness and queued traffic. `m_data_stop` revokes USIM readiness, begins WDS cleanup, pumps transport/session replies for at most five seconds, checks idle plus cleanup result, quiesces AP DMA, then detaches before SSCTL/SMP2P/PAS4 shutdown. A matching late valid WDS Start response during STOPPING is adopted so its handle can be stopped. Unknown Starts, malformed unresolved Starts and unstopped handles remain cleanup failures.

An already transmitted DMS mode request may take effect after local failure. WDS cleanup does not restore DMS mode. The same modem shutdown and retention contract therefore remains necessary for this new transition sequence.

When PAS4 stop is unverified, final GSI reset/deallocation is skipped. `ipa_cleanup(false)` retains backing, mappings and required power before any PAS15 shutdown or SMMU release. Failed AP quiescence also marks retention. With verified PAS4, PAS15 shutdown can still be attempted after an AP/GSI stop failure, but the retained flag prevents backing release.

Final backing release requires the verified stop path, safe GSI completion, PAS15 stop and safe SMMU release. Retention does not turn unresolved WDS ownership into successful cleanup. M2 remains incomplete even if this trial exchanges packets.

Command completion type 0x23 is measured. Data completion validation remains strict 0x22. Unexpected type 0x23 on data, invalid producer, pointer, channel, length or status must fail closed and revoke readiness. This approval grants no completion-format relaxation.

## Accepted hardware chain and first trial

I independently matched the accepted records against actual captures and reran the host acceptance predicates. The clean checker had only its acceptance-record write suppressed in memory to preserve the frozen evidence.

1. ast174 passed SMMU setup, guarded private parking and backing release without PAS start, ending normally in 214 seconds.
2. ast176 passed benign cleanup with a clean final root and no modem/IPA work, ending normally in 214 seconds.
3. ast177 passed PAS15 init, memory, authentication, shutdown, guards and backing release, ending normally in 214 seconds.
4. ast182 passed offline I3 in 363 seconds with clean roots and stale=false, classified as PAS4 unverified with IPA backing retained until reset.

The ast182 raw sequence supports table/SRAM setup, AP endpoints, PAS4 authentication, all handshake flags and I3 after queue drain. Offline success precedes AP quiescence, SSCTL, retained modem/IPA backing and DONE. Its accepted retained branch forbids PAS15 shutdown and backing release. No IPA production source changed for this second trial.

For ast183 I independently checked the actual result/raw/marker hashes, marker nonce, old image/audit identities, decoded nonblank 360x797 frame and restored original profile bytes. The result is selftest-done in 784 seconds with clean before/after roots and stale=false. This first trial ran audit8 data code.

Raw I3 occurs at 7628, DPM at 10236, WDA at 10243, mode 3 at 10245 and rejected ONLINE reply at 10246. Registration, WDS and packet proof were not reached. AP quiescence at 16305 precedes detach at 16308, idle=1/cleanup=0 at 16309 and SSCTL at 16310. PAS4/IPA retained backing appears at 16378/16379 and DONE at 16975. I found no GSI fault, invalid-completion or panic marker.

The captured runner log records recovery before launch and normal return to recovery. The private after-restore profile matches the pinned original exactly. The retained disposition record reports ownership released by 23:10:35 AEDT, about 17 minutes 11 seconds total, and no helper installation. That historical disposition does not establish current live phone state.

## Runner restoration and en2 packet proof

The runner is byte-identical to the runner admitted in the previous v2 review. I reran the current pinned runner suite with mocked host operations, including same-device clean recovery, pin/approval refusal, recognized temporary-profile restoration, original-byte no-op, third-state zero-PUT refusal and failed post-write readback.

Restoration permits PUT only when live bytes exactly match the pinned temporary profile. An unrecognized configd rewrite or owner edit is a third state and must be retained without PUT. Restoration failure propagates before release, retaining hold and lock for supervised recovery. This GO grants no overwrite exception.

I regenerated the exact temporary network profile from the pinned original. Both selftest compositions match, including the root composition from the actual ast183 captured root script and the new nonce. The runner must repeat that comparison against the actual live root before launch. Its second pin validation, exclusive lock, exclusive attempt creation, `--no-replay` and `--boot-tries 1` remain required.

Use the actual USB link for SSH, bridge100 when en8 is bridged and otherwise en8. Check the live host interface and leave Mac Sharing unchanged. Before every SSH operation verify the exact approved-image dev.py PID. Never reconnect or continue proof after that PID exits.

Withdraw the boot job's competing ue0 route/DNS state once with the pinned procedure while retaining USB SSH reachability. Keep private routes, interface state and before/after counters. Create the fresh nonce directory exclusively, use umask 077 and no-clobber upload, then read back complete bytes and SHA256 before chmod 0700 and one execution. Leave the helper inert afterward.

The pinned signed helper passed `codesign --verify --strict`. Its production entry fixes en2 and a numeric HTTP destination, sets and reads back IP_BOUND_IF, binds the en2 source and checks the connected source. It shares one monotonic ten-second deadline across connect/send/read and has no unbound fallback. Its localhost suite independently passed fragmented status, malformed/truncated response, missing-interface and total-deadline cases.

Only a successful bound HTTP exchange with supporting private captures counts as M6 packet evidence. USB Sharing traffic cannot establish cellular packets. DNS, TLS, Safari and iOS primary-route use require separate evidence. Restore the exact original profile from clean recovery before releasing ownership.

## Supervision, checks and boundaries

The modem runtime remains 600 seconds after MPSS authentication. Selftest uses a relative 660-second wait plus a 15-second finish delay, and its marker proves body completion only. The independent deadman remains 900 seconds.

The runner has no aggregate ownership deadline. Active supervision must keep all ownership strictly below 20 minutes, including waiting, boot, collection, recovery and profile restoration. The first trial's roughly 17-minute total leaves limited reserve. Stop proof early enough to preserve recovery time and do not begin or continue when that budget cannot be met.

Before final admission and launch, establish idle exclusive ownership, same-device clean recovery, unchanged original profile, matching live hook, unused attempt state, unchanged pins and sufficient recovery budget. This review performed no live checks. No phone action is authorized for the reviewer.

Stock signed MPSS/PAS4 and IPA/PAS15 firmware remains read-only through `sweet_ufs_read_part`. Rmtfs writes remain RAM shadows. No persistent modem, EFS, modemst1/modemst2/fsg/fsc/persist write, flash, stock Android boot, slot change or plain fastboot reboot is authorized. Two equivalent unexpected resets stop that approach.

Fresh isolated checks and logs are in `/tmp/sol-221810-h5zkgan7`. Actual production data/service-cache/2221 adapter tests, IPA datapath/hardware/worker/real2221 binding tests, IPA lifecycle tests, modem worker/gates/readiness/freshness/per-control discovery tests, the supplemental audit10 scenarios, current runner tests and helper tests all passed. C suites used warnings as errors and ASan/UBSan where supplied. Twenty-three extracted IPA/modem artifacts independently match actual production files.

I copied fixtures before compiling. The host fixture's runner test was stale against the four-argument restoration API, so I used a copy of the current pinned runner test against the actual runner. Copied harness adaptations set compiler/subprocess TMPDIR=/tmp and used explicit temporary directories under /tmp. Initial review-script path and unavailable Pillow assumptions were corrected, with frame decoding completed through the pinned stdlib PNG reader.

I preserved frozen/test outputs, old snapshots and accepted records. All review writes stayed in this workspace or /tmp. I made no implementation edit, manifest approval change, phone/SSH operation, commit, delegation, upload or file removal. Host models do not establish kernel concurrency, physical DMA cessation or firmware support for LOW_POWER. The GO remains limited to this one supervised second trial and the conditions above.
