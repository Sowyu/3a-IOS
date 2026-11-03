# Data 2221 and IPA 2205 re-audit

Reviewed on 2026-11-03. GO for including the frozen 2221 adapter with 2220 in one bounded integrated data trial, for either `sweetdata=1` or `sweetdata=1 sweetdataonline=1`, subject to audits 4 through 7 and the 2189 re-audit. All three blockers from the first 2221 report are fixed in the frozen integrated source. This is source integration approval, conditional on separate image and hardware approval before any phone boot.

What this change does: 2221 binds the data TX and RX hooks to IPA. The frozen 2205 supplies the actual worker binding, terminal readiness revocation, per-packet synchronization and AP-channel quiescence before detach.

## Scope and identities

I read `docs/data-2221-audit.md` first. I then read data audits 4 through 7, `docs/modem-2189-reaudit.md`, the frozen cutoff, the connected worker, IPA and data source, and the host fixtures. I used the ponytail-review and unslop skills.

I independently computed and verified all ten frozen patch SHA-256 values against `plans/ipa/cutoff-2206.txt` and the supplied request. Paths below are under `3a-IOS/patches`.

```text
modem/2189-asteroids-modem-data-worker-hooks.patch
9b4b570d07d9a5b02288f006e500a733bfdea0876acaa938ef5b7395497ed58f
data/2220-asteroids-data-qmi-session-and-link.patch
410ee41d9269ab7e6f97b1beb4c4550af28464b5503c5719790094bedc2ebdf8
data/2221-asteroids-data-ipa-binding.patch
4bdc74ce4ad45000ba2efe18bf45454169dcb6464483d5722c918892875e1e49
ipa/2200-asteroids-ipa-read-only-preflight.patch
eee590a673ad46eabbb272825d576b660122531437ad45898f58b5cf9fb49b9a
ipa/2201-asteroids-ipa-pas15-isolated-dma.patch
334ea045a2c066fa59fc48836312e2865d637127c59062f1857b7e24570ca9b9
ipa/2202-asteroids-ipa-gsi-qmi-data.patch
c54dc55bdeee05a74a0ac7f50c5e8bef30aada631e11e6ce6b49b67efb0ceef3
ipa/2203-asteroids-ipa-memory-read-only-survey.patch
0aec26665a2f71c227d6dd9bbf480d04b0934ae5b865bfc1297d9309c87b9c52
ipa/2204-asteroids-ipa-plain-mmio.patch
15b52570e717c5537435e490a1477fd97af632bac31298cdc8e2b43eb76e28b3
ipa/2205-asteroids-ipa-data-worker-integration.patch
479f4c1df57b857745b75262b8f4e9895d44e8d22e643c9fa6ef81beaf067590
ipa/2206-asteroids-ipa-unused-context-reset.patch
0e2795df6d0bd53aa9bd790eba558d261b8a098bd91cd893d08f6629811d9425
```

`python3 plans/ipa/tools/reproduce-cutoff.py` passed from the project root. It verified the copied modem/data owner bytes and reproduced all 19 IPA-touched files exactly in `plans/ipa/series-check/1793693582698175000`. It replayed the documented prerequisite order, the current modem series through 2189, data 2220/2221, then IPA 2200 through 2206. This verifies source reproduction, not a complete kernel build.

Modem source references below mean resulting `bsd/dev/modem` files in that reproduced directory, also identical to `ios-asteroids/research/xnu-ipa/dxb/xnu`. Data references mean `plans/data/src` and adapter references mean `plans/data/src2221`, corresponding to resulting `bsd/dev/data` files. The adapter mirror matches the patch-extracted tested adapter exactly. Tested data helper mirrors also match the extracted files.

## Original finding dispositions

### 1. Stale IPA readiness after detected failure is fixed

`IpaData.inc:24-25` sets a terminal latch, clears readiness and drops copied TX under the queue mutex. `IpaData.inc:22` refuses every later true request after that latch. GSI event and post failures revoke before returning at lines 28, 30, 35 and 48. RX callbacks still run outside the queue lock at lines 33-34.

`IpaSession.inc:22` revokes for worker failure, fatal state or stopping. Lines 24-30 check the QMI relationship before pumping the data path and revoke on either QMI or GSI failure. A lost bound service therefore cannot leave queued TX eligible for a later datapath pump.

`ModemRuntime.inc:570-573` checks worker state, endpoint readiness, GSI failure and the bound IPA service identity before refreshing the data evidence. `IpaQmi.h:57-65` makes missing, moved or ambiguous bound IPA service identity terminal. Receive synchronization at `ModemRuntime.inc:370-374` publishes that loss after QRTR control processing and before discovery or the next packet.

The adapter remains a simple evidence copy at `sweet_data_ipa.c:43-45`. It now reads an authoritative operational latch. It does not need its own duplicate handshake state machine.

### 2. Missing worker bind and refresh is fixed

`ModemRuntime.inc:623-629` prepares IPA hardware, calls the actual `sweet_data_ipa_bind()` once for sweetdata, checks its return, records successful binding and only then attaches data. This precedes firmware load and MPSS authentication at lines 630-631. QRTR start remains downstream of this initialization.

Any nonzero bind return jumps directly to cleanup. No retry, attach or data refresh follows a failed bind because `data_ipa_bound` and `data_attached` remain false. The adapter's EBUSY-after-TX-install case is explicitly terminal in `sweet_data_ipa.h:5-10`. `sweet_data_ipa.c:32-39` returns EPERM for TX refusal and preserves the RX registration errno. Cleanup can replace the final result with EIO if cleanup itself fails at `ModemRuntime.inc:644-646`.

Raw-only operation skips binding. `ModemRuntime.inc:591` rejects sweetdata without full IPA. Refresh runs before and after each receive at lines 370-373, before each normal data poll at lines 638-639, during cleanup at line 487, and on GLINK loss at line 380. It is guarded by successful binding and attachment at line 573.

### 3. Missing AP-channel quiescence before detach is fixed

`ModemRuntime.inc:483-490` latches stopping, revokes readiness and pumps Stop Network receive/poll work for up to five seconds. Lines 493-496 inspect cleanup, quiesce IPA, detach once and record failure if cleanup, idle confirmation or quiescence failed. `m_cleanup` then attempts SSCTL and PAS4 shutdown at lines 501-512 even when data cleanup failed.

`ipa_data_quiesce` at lines 575-579 revokes again, publishes false and retains IPA backing on uncertainty. `IpaHardware.inc:111-118` closes RX aggregates, waits for closure and stops owned AP channels. `IpaGsi.h:118-125` confirms stopped or allocated channel states without releasing channel/event ownership or ring backing.

The no-attach path also quiesces at `ModemRuntime.inc:482`, covering offline and partial-start exits. After verified MPSS shutdown, lines 645-646 perform final hardware stop and IPA cleanup. `IpaRuntime.inc:154-173` retains backing on unverified MPSS stop, failed PAS15 shutdown, retained/uncertain hardware state or failed SMMU restoration. Hardware retention and pinned power remain conservative even if AP quiescence succeeded but MPSS stop did not.

The exact shutdown sequence revokes IPA at exit and when stopping begins, earlier than the minimum requested revoke after cleanup. It then pumps Stop Network, queries cleanup, repeats revocation, quiesces AP channels, detaches, attempts SSCTL/PAS4, and permits final GSI/PAS15 cleanup only after verified MPSS stop. Early revocation blocks packet traffic while QRTR cleanup replies remain serviceable. It does not prevent WDS cleanup.

## Requested regression dispositions

1. EBUSY after TX installation is fixed and covered. `tools/data/tests/ipa.c:57-60` asserts a retained TX hook, missing RX and EBUSY. `tools/ipa/check-bind.py` generates `tools/ipa/tests/out/binding.c` from the actual worker bind block and real 2221 adapter, asserting no attach, load, refresh or bound flag after EBUSY. The enclosing runtime's failure exit was also checked statically.
2. GSI failure after a good handshake is fixed and covered in this review. The maintained `tools/ipa/tests/hardware.c:122-126` seeds readiness for its malformed-completion check. My supplemental `tools/ipa/tests/out/reaudit-2221.c:135-153` first completes the real 0x21/0x20/0x35/0x22 handshake and transport drain, queues TX and RX completion work, then injects a malformed GSI completion. It verifies EIO, terminal false readiness, dropped TX, no subsequent RX callback and false copied data evidence after worker synchronization.
3. IPA DEL_SERVER immediately followed by a data reply is fixed and covered. `tools/ipa/tests/hardware.c:112-120` completes a handshake, uses actual `m_greceive` and real QRTR control processing, then asserts terminal revocation and false data evidence before the following callback. Its data callback and discovery function are stubs. The real data-side revoke-before-sync behavior is separately covered by `tools/data/tests/services.c` and its passing suite, with production hook placement checked statically.
4. Stop with TX queued and RX active is fixed and covered in this review. `tools/ipa/tests/out/reaudit-2221.c:154-161` starts from a real good handshake, queues TX, leaves a completed RX event pending with active RX aggregates, latches stopping and calls the actual synchronization/quiesce helpers. It asserts false evidence, empty copied TX, closed aggregates, no callback and retained ownership of all stopped channels/events. `tools/ipa/check-worker.py:31-40` separately exercises actual `m_data_stop` with bounded pumping, cleanup errors, quiesce failure and detach ordering. `tools/ipa/tests/gsi.c:48-52` checks retention and refusal of further teardown writes after each channel-stop timeout.

These are host model checks. They do not prove physical DMA cessation or real modem response timing.

## Audit6 and audit7 ordering

The frozen ordering meets both contracts. `QcomQrtr.c:144-151` first applies each NEW_SERVER, DEL_SERVER or BYE to the remote table. `ModemRuntime.inc:371-374` then checks UIM identity, synchronizes/revokes IPA evidence and calls `sweet_data_discovery` before returning to the next GLINK receive callback. A DEL/NEW at the same endpoint cannot disappear inside a batch snapshot.

Before a QRTR data packet can invoke a data callback, the pre-receive synchronization at line 370 has refreshed IPA evidence. A failure detected by an IPA QMI callback is synchronized immediately after that packet at line 373. Before a normal data poll, `ipa_session_poll` handles detected GSI/QMI failure and lines 638-639 publish the resulting evidence before data work.

`DataServices.c:55-62` revokes a prior grant before synchronizing services, then grants only after the cache is current. The production receive, discovery, poll and USIM entry paths all use that helper through `sweet_data.c:510-513`. This closes audit7's case where the last missing service arrives in the same control packet that invalidates readiness.

`ModemRuntime.inc:483` makes stopping terminal for the worker. UIM positives are suppressed by lines 296-299, IPA positives by `IpaData.inc:22-25`, and session setup sends by `CellSession.c:114`. `CellSession.c:599-606` also refuses positive readiness outside WAIT_READY. Neither service reappearance nor a late good response can revive setup after teardown starts.

## Initial readiness definition

The definition at `plans/ipa/README.md:208` is met. `IpaHardware.inc:103-108` starts AP channels, posts RX buffers and completes table setup before setting hardware ready. `IpaQmi.h:73-90` validates the initialization response and the modem registration/completion requests. Lines 106-110 require the accepted initialization, registration, modem completion and successful enqueue of the AP completion indication.

`IpaSession.inc:30-32` additionally requires a successful data-path check and an empty outgoing transport queue before the only initial true transition. Queue fullness can later refuse TX with EAGAIN while readiness remains true. ONLINE state, NAS registration and WDS/IP connectivity are separate conditions. Firmware-only mode never grants IPA data readiness.

## Blocking findings

None for the bounded source-integration scope. No implementation fix is required before including these exact data patches in the separately reviewed trial image.

This finding list does not grant that image approval. The image must still pass its own review and build checks, including the IPA hardware changes.

## Non-blocking findings

1. `plans/ipa/README.md:210-220` still describes 2205 as a private draft, cites audit6 as its data baseline and says the frozen cutoff does not exist. The later cutoff section supersedes this history, but selecting the old paragraph alone gives the wrong revision. Mark those paragraphs historical and point directly to the frozen audit7 cutoff and hashes.
2. Maintained regression coverage is narrower than the claimed combined cases at `tools/ipa/tests/hardware.c:122-131`. It seeds readiness for GSI failure and does not leave a completed RX event pending in its queued-TX stop test. Move the supplemental handshake/failure and active-RX stop cases from `tools/ipa/tests/out/reaudit-2221.c:135-161` into the maintained suite so future runs preserve this coverage.

The earlier data parser/raw-output limitations remain governed by audits 4 through 7. This review adds no permission for unfiltered raw export or unrestricted operation.

## Validation and approval boundary

All five authorized suites passed freshly from `3a-IOS`: `sh tools/ipa/check-datapath.sh`, `sh tools/ipa/check-lifecycle.sh`, `sh tools/ipa/check.sh`, `sh tools/ipa/check-mmio.sh` and `sh tools/data/check.sh`. Host C suites used AddressSanitizer, UndefinedBehaviorSanitizer and warnings as errors. The MMIO suite compiled ARM64 accessors and checked plain load/store instructions and barriers. The supplemental re-audit probe also passed with the same host sanitizers.

Keep the guarded 600-second data runtime approved by the 2189 re-audit, exact independent `sweetdeadman=900`, required gates and harness deadline below 20 minutes. Keep one successful attach per boot, eight QRTR slots, one serialized worker, fresh selected-USIM evidence, immutable callback lifetime, QMAP v1 limits, bounded cleanup with error propagation, retention on uncertainty and RAM-only EFS behavior. The ONLINE variant remains a separately enabled data configuration under the same conditions.

I made no implementation edits, built no kernel and did not touch the phone. Writes were limited to this report, source-reproduction artifacts under `plans/ipa/series-check`, and host-test artifacts under `tools/*/tests/out`. No files were removed. Full-kernel compilation, kernel concurrency, physical DMA, actual handshake/session behavior and final image arguments were not verified here.

This review does not approve IPA hardware writes. The 2201/2206 SMMU and PAS15 work, 2205 hardware register programming, the final integrated image and any phone boot retain their separate review requirements. GO applies only to including frozen 2221 with frozen 2220 in the bounded integrated data trial after those requirements are met.
