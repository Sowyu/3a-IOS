# 2188 revision 2 offline flow diagnostic re-review

2026-11-03. GO for ONE guarded offline readiness RAM boot of frozen tag21881, subject to every launch and acceptance condition below. I extend the limited 2185 known-PAS-retention exception to this flow diagnostic only. Launch NOW is NO-GO while DATA owns the phone. M2 remains incomplete.

What this change does: 2188 reverses QRTR RESUME_TX endpoints and identifies the local receiving client in its payload. Revision2 lets QRTR retain an acknowledgment when the real worker queue is temporarily full, without permanently failing that worker.

## Scope and method

I read `3a-IOS/HANDOFF.md` first, the prior 2188 audit, 2187 re-audit, 2185 UIM exception, modem README and `3a-IOS/docs/modem.md`. There is no top-level `docs/modem.md`. I applied ponytail-review and unslop and reviewed the production path in `ios-asteroids/research/xnu-modem/dxb/xnu`, for one modem owner and the existing 120-second post-authentication worker.

No phone operations, SSH, builds, compilation, test-binary execution, source changes, deletion, uploads or delegates occurred. Read-only Python used `-B`. This report is the only file written. Existing build/test records and supplied ast165 observations are distinguished below from checks performed during this review.

## Findings and closure of the previous blocker

No blocking source finding remains within this exact offline scope.

`ModemRuntime.inc:250-260` now rejects lengths above512 with sticky `m.failed`. When its32-entry queue is full, only a52-byte V1 RESUME_TX can return refusal without setting failure. Ordinary DATA responses and other control traffic still fail on queue loss. No retry clears `m.failed`.

The discriminator checks length, version and type, rather than reparsing all acknowledgment fields. This is sufficient for the connected production callers: the callback is static, installed only in `m_glink_prepare`, and invoked only by QRTR `emit`. The only RESUME_TX emitter is `qcom_qrtr_poll`, which builds the complete fixed52-byte packet itself. Rmtfs, pd-mapper, UIM and SSCTL all send DATA and cannot obtain the exception by choosing their QMI payload. There is no raw packet submission or data hook in this production series.

`QcomQrtr.c:52-70` retains copied endpoint integers until queue acceptance, coalesces matching flows and bounds pending entries at16. It removes an entry only after `emit` accepts its packet. A seventeenth distinct blocked flow latches `resume_failed`, and the actual worker poller observes that failure. BYE clears obsolete pending/discovery state. Incoming DATA has header/size/local-node checks before client dispatch and acknowledgment.

`m_ipc_poll:371-380` services receive, drains TX until GLINK Again/error, then polls retained acknowledgments. Temporary ACK refusal therefore leaves the worker healthy until space returns. GLINK breakage, transport-send errors and lost ACK capacity remain fatal. The callback runs before acknowledgment generation, so retrying the acknowledgment does not deliver the received message to the client again.

I traced SSCTL eligibility through `m_shutdown_request:381-411`. Recoverable acknowledgment fullness no longer sets the failure bit that previously skipped SSCTL. Its other prerequisites, endpoint selection, stale-ack rejection, bounded request/wait and fresh response-plus-indication/ack checks remain intact. Ordinary SSCTL queue loss still fails and makes this diagnostic unacceptable.

Cleanup at453 onward always attempts owed PAS shutdown even if SSCTL, transport or acknowledgment capacity fails. Failed PAS returns before ownership reclaim, QMP-off, remaining vote release, unmapping or freeing. Firmware, metadata, arguments, shared rmtfs and RAM shadows remain backed until guarded reset. Ownership/QMP/vote uncertainty also retains backing. Shared CX/XO floors and the measured ready/handover MSS proxy release remain unchanged.

## Regression evidence and limits

I independently matched the saved `qrtr-worker.c` to its current generator plus the actual production `m_qsend_packet` and `m_ipc_poll` bodies. It links the real extracted QRTR implementation. Its confirm-requesting receive occurs with the queue full, followed by five held GLINK Again polls, drain, exactly one client callback and exactly one accepted acknowledgment, with no worker failure.

The test also asserts fatal ordinary queue loss, oversized callback input, GLINK broken state, transport-send error and propagation of latched acknowledgment-capacity failure. The separate real-QRTR flow test fills all16 pending entries, rejects the seventeenth, tests coalescing and checks exact reversed header/payload fields through a60-message peer model. The worker test injects QRTR receive directly and mocks GLINK polling/sending. It does not exercise the hardware GLINK FIFO or execute SSCTL after this same congestion sequence. SSCTL eligibility is established by the unchanged predicate and the healthy resulting state, supplemented by separate saved actual-SSCTL and joined cleanup tests.

Saved `start-check-2188-v2.txt` contains14 PASS records, `qrtr-worker-2188-v2.txt` one, and unchanged `uim-check-2188.txt` five. No ASAN/UBSAN error marker was found. The tests use sanitizer builds, but I did not compile or rerun them. The full build log reaches completed installation and `done building xnu`. Safe `--remake -j12` and exit0 are supplied execution evidence. Source-to-build provenance relies on that record.

## Connected safety boundary

2187 revision2 transaction retirement, discovery validation before cadence/reply admission, selected PRESENT/type2/state7 readiness and15-second freshness remain unchanged. UIM sends only empty seven-byte0x2f/0x47 requests, with card cap180 and slot cap3 inside the unchanged120-second worker. Eight local slots remain available, with client-only ports unannounced. No ONLINE, provisioning, PIN/APDU, slot switch, IPA, data hooks or raw-QMI interface is included.

The runtime storage calls remain `sweet_ufs_part_size` and `sweet_ufs_read_part`. Firmware loads from modem_a, and modemst1/2, fsc and fsg initialize RAM shadows. Rmtfs checks opened IDs, shared-window extents and every vector before copying. Writes stay in those RAM shadows. Logs export ICCID suffix4 only. Transport bytes and firmware remain private.

Strict gates reject absent, bare, duplicate, malformed and non1 tokens, and dependencies are checked before hardware setup. DT-derived addresses remain PAS4 MPSS0x8ac00000+0xe600000, SMEM0x81d00000+0x200000, rmtfs0xe1000000+0x280000, metadata0xe1400000 and arguments0xe1410000. XNU RAM starts at0x8c0000000, above4GB. There is no new PAS, ownership or power transition in2188 beyond acknowledgment integration and flow logging.

## Independently verified artifacts

All19 local/public patch pairs2170 through2188 match. In-memory reconstruction from that exact ordered public series matches all20 modem-directory files and `sweet_modem.c`, as do available extracted test inputs. Public2187 is revision2 with SHA256 `f1dd3d1fd6406fca105bf001123ecf0f2850ba3c51013e4684c0bd3a6db8790f`. Commit `7eefa98` contains the exact current2188 patch. The unrelated private2188 data-hook draft is excluded.

All six entries in `out/frozen-21881.txt` match current bytes. Key SHA256 identities are:

- Patch2188 `01d1979ea44147622f2aba7468ba22b475d306ba6128aa2835c853b9ec54e878`.
- Runtime `186e6528579d3ba36780b751d44a69038fd85aa874f84f93b42c8409b6b71142`.
- Kernel `261b54bb80766a08f4d0b66fd9df138ac44fe7c27ccc59431c75875e38e95dbc`.
- Image `0b0b058f9e62af6dbcb4cb0d46ec583a299f7f8571c62459e8fd683910f48c6c`.
- Script `299b2b436425a8480e432bb7ce9ee4d4d193ea1de34b284c4e37ed82f5f28a02`.

`python3 -B plans/modem/tools/verify-image.py uim-usb-21881` passed all10 rebased kernel segments and embedded payload verification during this review. The actual payload arguments equal the saved601-byte line with unique keys. They retain mpss/glink/rmtfs/pdm/mstop/mho/muim/mready=1, USB4, fbarm1, selftest1, deadman600 and thermal3. Bandwidth/IRQ and broader modem gates are absent.

The current hook/body composition equals the frozen script. Its nonce matches the private nonce file and differs from both21871 and rejected2188. The240-second wait, checked nonempty frame, collision-safe archive handling and15-second DONE flush remain required. I verified the rejected v1 patch/image are still preserved with their prior audit hashes. They remain NO-GO.

The runner freezes image/kernel/runtime/QRTR source/header/script, verifies current hook/nonce after waiting and passes `--no-replay --boot-tries 1`. It waits for DATA hold AND phone.lock absence before creating the cooperative modem hold, waits existing job PIDs, requires actual recovery and checks root cleanliness. It rechecks reservations/jobs before launch. Local marker inspection during review found DATA hold and phone.lock present, modem hold absent. I made no device contact. These markers must be rechecked later and grant no authority to interrupt DATA or IPA.

## Retention exception and exact conditions

The supplied ast165 evidence measured selected READY at10619/10621, exactly10 UIM replies including one slot reply, then silence and expiry11384. Fresh SSCTL acknowledgment11686 preceded50 raw-3145800 PAS failures, retention11743/final_error5, and normal DONE12413 in365seconds with fresh nonce, clean root before/after and recovery return. These observations establish the diagnostic precedent, not verified PAS stop.

This change targets a concrete explanation for that ten-response stall and adds bounded acknowledgment ownership without extending lifetime, changing power sequencing or exposing persistent writes. That justifies extending the limited exception for one measurement. It does not prove firmware compatibility or sustained readiness before that measurement.

1. Wait until DATA and IPA release the phone, both reservation/lock files are absent and no phone job is active. Acquire coordination through the reviewed runner and recheck actual clean recovery, all frozen hashes, exact arguments and fresh hook/nonce immediately before one tag21881 RAM boot. Keep `--no-replay --boot-tries 1`. Never interrupt an active reservation. While Internet Sharing remains enabled, any separately authorized collector must use bridge100.
2. Require successful PAS authentication, ready/handover, MSS proxy release, healthy GLINK/QRTR and pd-mapper/RAM-rmtfs progress. Require more than10 fresh accepted UIM responses and selected PRESENT/type2/state7 readiness refreshed through the remaining120-second worker, without readiness expiry. Record flow resumes/pending/failed/rx/tx/refused. Temporary refusal counts are acceptable only with eventual acknowledgment acceptance, no lost capacity, no unexplained pending backlog and no fatal state.
3. Inspect the entire shutdown window through worker completion. Require accepted SSCTL with fresh acknowledgment and every owed PAS attempt. For the known failure branch require the expected50 raw-3145800 failures, explicit STOP UNVERIFIED retention and final_error5, with no subsequent ownership reclaim, QMP-off, remaining power/vote drop or backing free/unmap. Unexpected shutdown behavior ends progression and needs review.
4. Require worker completion before normal selftest DONE under600seconds, matching fresh nonce, checked frame, stale=false, clean root before/after and recovery return. Reject unexpected reset, panic, fired deadman, fatal/transport error, lost ownership, missing guards/completion/nonce, FAIL/SKIP or dirty root. No automatic repeat follows success or failure.
5. Keep diagnostic acceptance separate from strict `check-run.py` PASS and M2. M3 is already complete. M2 requires actual verified secure-stop evidence. This GO authorizes no ONLINE, provisioning, IPA/data session, raw QMI, longer lifetime, partition writes, flash, stock boot or slot operations. Keep firmware, raw logs and identifiers private, suffix4 maximum in reports. Changed code/artifacts or an unexpected result requires fresh review.

Verdict: GO for exactly one frozen21881 offline flow/readiness diagnostic after ownership release, with the limited known-PAS-retention exception above. Immediate launch while reserved is NO-GO. No phone run was performed.
