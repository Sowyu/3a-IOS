# 2186 offline completion-reporting review

2026-11-03. GO for ONE bounded offline RAM boot of the exact 2186 candidate, subject to the launch and acceptance conditions below. Known PAS shutdown failure remains unresolved. Retention is a diagnostic outcome and cannot establish M2.

What this change does: 2186 enables completion reporting for one owned apps-RSC DRV2 active transaction under exact `sweetmbw=1 sweetmbwi=1`. It first requires positive evidence that INTID37 is accessible to nonsecure software and that delivery is disabled and inactive. Fresh completion, idle status and all seven command acknowledgments remain mandatory.

## Scope and method

I read `3a-IOS/HANDOFF.md` first and applied ponytail-review and unslop. I reviewed the candidate patch, production runtime and its callers, strict gates, bandwidth encoder, touch arbitration, shutdown/retention, saved host tests, DT, vendor RSC source, prior audits and ast158/160/161 evidence. The daily/USBv5 safety context remains the previously reviewed bounded dev-session configuration described in HANDOFF and the prior modem audits. This does not approve daily USB use or resolve its documented limitations.

No delegates, phone operations, builds, test compilation, implementation edits, deletion, uploads or partition writes were performed. This report is the only file written. Read-only Python checks ran without bytecode output. I independently ran `python3 -B plans/modem/tools/verify-image.py offline-usb-2186`.

## GIC evidence and offsets

DT `asteroids.dts:841` gives DRV0/1/2 at 0x17a00000/0x17a10000/0x17a20000 and SPI3/4/5 respectively. SPI5 is architectural INTID37. The GIC distributor is 0x17100000. For INTID37, index 1 and bit 5 correctly select IGROUPR at 0x84, ISENABLER at 0x104 and ISACTIVER at 0x304. Its priority is byte 1 of the word at 0x424. The 0x1000 mapping covers all four reads.

The positive-priority argument is valid for this nonsecure kernel accessing a conforming GIC directly. Secure interrupt priority fields are inaccessible as nonzero values to nonsecure software. A nonzero observed priority therefore supplies the missing positive access evidence when IGROUPR reads zero. Zero priority remains ambiguous and is correctly refused unless the group bit itself supplies evidence. The candidate does not interpret hidden enable/active zeros alone as ownership proof. [Arm IHI0069](https://documentation-service.arm.com/static/67eaa4d098aa3c3b6eea7351) and [Arm IGROUPR register documentation](https://support.arm.com/documentation/ddi0595/2021-06/External-Registers/GICD-IGROUPR-n---Interrupt-Group-Registers) support these access restrictions.

The indexed primary specification text identifies the secure-priority restriction under section 4.8.2 in this revision, rather than the supplied 4.8.1 citation. The direct PDF fetch timed out, but Arm's indexed specification text and register documentation were available. This numbering difference does not invalidate the inference. It remains conditional on genuine nonsecure MMIO, rather than an arbitrary emulated distributor.

`ml_io_map` does not request page-table read-only protection. Here “read-only GIC” accurately describes the code's accesses: all use `m_rd`, and none use `m_wr` on this mapping. The new mapping is retained for the boot, including clean exits. This is one bounded mapping, not an unbounded per-request leak. No GIC pending clear, acknowledge, priority, routing, grouping or enable write is introduced.

## Ownership, races and reporting cleanup

The vendor version-3.0 table agrees with stride 0x2a0, command stride 24, IRQ enable/status/clear at base+0/4/8, WAIT/CONTROL/STATUS/ENABLE at +0x20/24/28/2c, and command message/address/data/status at +0x34/38/3c/40. Its active-TCS IRQ handling supports enabling reporting during synchronous requests. This supports the experiment but does not prove that masked reporting caused ast160.

The shared touch/modem mutex covers TCS selection, IRQ mask read-modify-write, clear, programming, completion and restoration. Selection requires idle plus zero control, command enable and wait state, and no visible completion. Only the selected bit is cleared. After enabling that bit, the worker proves its completion status clear before any command is triggered, preventing a stale hidden completion from passing as the new transaction.

Completion remains a fresh IRQ bit plus idle and issued/completed bits for every command. No idle-only or stale-command fallback was added. Success clears owned control/enable/wait and completion, then restores only the owned IRQ bit to its prior value while preserving other bits. Timeout logs all command statuses, masks the owned bit, leaves control/enable/wait programmed and sets uncertainty. `m_load` and PAS initialization cannot follow that failure. Touch can use another free TCS and cannot select the retained transaction.

2184's shared SN3 maximum remains byte-identical. With the strict bandwidth gate, later touch SN3 transactions retain x=y=16383. Other shared floors remain pinned until reset. No IPA/IP0 vote is added by the modem bandwidth path.

The GIC reads occur before acquiring the RSC mutex. That mutex does not reserve INTID37 against a separate GIC client or secure firmware changing its security/enable state. I found no INTID37-enabling client in the reviewed candidate sources. The platform FIQ setup enables timer/IPI redistributor interrupts and distributor group forwarding, rather than enabling this SPI. Asteroids touch polls and does not enable a GIC SPI. Legacy beryllium RPMh/WLAN code is not authorization to add a second live writer to this experiment.

This snapshot is acceptable for the frozen, single-worker diagnostic with no new interrupt consumer or power-management integration. It is not a general concurrency proof. A future INTID37 consumer or concurrent security-state reconfiguration invalidates this GO and needs actual interrupt ownership coordination. A second snapshot alone would not remove that race. Pending state is not checked or cleared at the GIC, so pending state must not be described as clean after the run. Delivery must remain disabled until the guarded reset.

No blocking finding remains within that narrow operating scope. Hardware latching, mask behavior and firmware cooperation remain unmeasured risks.

## Lifecycle and actual prior results

`m_cleanup` attempts SSCTL when available and falls back to the SMP2P stop request as needed. Every exit with `pas_owed` attempts secure PAS shutdown, including initialization/start failures. A pre-PAS exit owes no PAS command. The dependency refusal occurs before modem activity.

A nonzero PAS shutdown result returns before ownership reclaim, QMP-off, remaining vote release or backing unmap/free. Firmware, metadata, SCM arguments, shared rmtfs memory and RAM snapshots remain backed until reset. MSS proxy release at ready/handover is the previously measured behavior and is not a later release after failed secure stop. Runtime partition references remain reads/size queries and rmtfs writes remain RAM-only.

I independently checked ast161's local result and console. It returned normally after 363 seconds with root clean before/after, stale=false and one frame. Its completion nonce matches its installed script. PAS init/setup/authentication, ready/handover, MSS release, GLINK, QRTR, pd-mapper and RAM-rmtfs progress are recorded. Both UIM requests succeeded and report an installed USIM in state 1, not state 7. This establishes detected-card status only, with no data-readiness inference.

Ast161 records accepted SSCTL with response, indication and fresh shutdown acknowledgment at console line 11784. Exactly fifty cmd6 attempts return the known raw failure at lines 11785 through 11846, followed by retention at 11847, final_error=5 at 11848 and normal DONE at 12577. USB summaries show stale=0 and TX lost=0. I found no modem transport fatality in the recorded window. The early retention branch does not print the final transport/fatal summary, so this is log inspection rather than a measured zero summary.

Ast158 likewise ended with failed PAS and retention. Ast160 timed out its bandwidth transaction before PAS and ended with no PAS activity, incomplete cleanup and retained uncertain state. Neither is M2. The changed reporting experiment addresses a specific unmeasured completion mechanism and adds diagnostics, so this is not an unchanged repeat of ast160.

## Exact evidence and artifacts

All seventeen local/public modem patches 2170 through 2186 match byte for byte. I reconstructed all 21 added modem source/header files in memory from the complete ordered public series and compared them with production. I also verified the current saved bandwidth unit exactly composes the real touch and modem functions with the current mock and assertions.

The saved sanitizer record `logs/start-check-2186.txt` contains PASS results for strict gates, read-only storage references, lifecycle, joined SSCTL/QRTR/RAM-rmtfs/PAS cleanup, joined handover and the actual touch/modem bandwidth unit. Its assertions cover raw and masked reporting, unknown/enabled/active GIC refusal, map failure, hidden stale completion, missing new completion, partial acknowledgments and timeout retention. These are deterministic models, not hardware race or latching measurements. I did not rerun or compile them under the no-build restriction.

The full build log contains the completed install and `done building xnu`. Safe remake execution and exit 0 remain supplied process evidence. The independent image verifier confirmed all ten rebased segments and embedded payload. The embedded argument text matches the saved file, has unique keys and occupies 598 bytes.

Frozen SHA256 identities:

- Patch 2186 `11d302f12041c16080daf79c0a9a5b935d8297410cd5dabd0262fc96d4a94a67`.
- Runtime `83141c28abdbdda5acb1a365532a9269cc428d38444c684cda28b69a8270e44e`.
- Touch `7b5cf717cbc553a36a5bcfe19b996fc6866b05b091d6b2425734f14f74ad8534`.
- Kernel `86c29556f84b50b7c3dcebfc760eda854c5260cd201a645c8d21264cdbea0740`.
- Image `8062a2a6e3626978851f81d7ea0b4bbefb64a8a668eea130e2a0b72a241e17a4`.

Script SHA256 is `6d76b79a0cd4f59aa1143d71a33a5e990c1e091cb5f8555fe550ca1a4a57212d`. It exactly equals the current composed hook/body and its private nonce matches. Collision-safe frame preservation, checked screenshot and fifteen-second DONE flush remain present. Its 240-second body wait does not itself prove worker completion, which acceptance must check separately.

The payload enables offline mpss/glink/rmtfs/pdm/mstop/mho/mbw/mbwi, USB4, fbarm1, selftest1, deadman600, panicfb10, thermal3, keep/astwait and fetchsync. It contains no UIM, data, ONLINE, IPA, raw-QMI or TFTP gate. Both new reporting and bandwidth gates use the exact-single-token parser and default off.

## Launch and acceptance conditions

1. Parent must finish checking ast161, coordinate the shared phone hold and wait for every active phone-job PID. Immediately before launch require actual clean recovery, no active job and the exact frozen hashes, args, hook and fresh nonce. Change only the runner's current 2184 image/note references to 2186, preserve `--no-replay` and clean-recovery checks, and run once. The wants marker alone is not a lock. Keep the existing reset/deadman bounds and total duration below twenty minutes.
2. A GIC unknown/enabled/active/map refusal before bandwidth/PAS is a valid clean NO-START diagnostic if logs prove no PAS activity, completed cleanup and worker exit before fresh normal DONE. Prior SMEM setup, RAM ownership and proxy votes occur before this guard, so do not call it side-effect-free. A programmed bandwidth timeout is an incomplete retained-transaction outcome, stops further runs and requires evidence review. Never weaken completion admission or repeat unchanged.
3. If PAS starts, require bandwidth error=0/pinned=1/commands=7, ready/handover, successful MSS release, healthy GLINK/QRTR, pd-mapper and RAM-rmtfs progress, plus later successful touch SN3 maximum-floor evidence. Require accepted SSCTL and full actual PAS shutdown logs. Missing evidence, unexpected transport failure, changed PAS failure behavior or reset ends the experiment without progression.
4. For the known PAS-failure branch require explicit STOP UNVERIFIED retention and final_error=5 before DONE, with no subsequent reclaim, QMP-off, power release or backing free/unmap. This can accept the bounded diagnostic observation only. M2 requires actual PAS status=0/result=0, cleanup=ok, healthy transport/fatal summary and final_error=0, together with all lifecycle evidence. Keep the strict run checker strict.
5. Require normal fresh DONE, matching nonce, checked nonempty frame, stale=false and clean root before/after. Panic, fired deadman, reset-unknown, FAIL/SKIP, dirty root or missing worker completion fails acceptance. Keep firmware, raw logs and identifiers private. No automatic repeat or next stage follows any outcome.

No partition writes, especially modemst1/2, fsg, fsc, persist, EFS or modem_a, are authorized. No flash, slot change, stock boot, plain reboot, UIM query, provisioning, calls/SMS, ONLINE, IPA/data, raw QMI or extended session is approved. Changed source/image, another GIC/RSC user or broader integration requires fresh review.

Verdict: GO for this single guarded 2186 offline diagnostic. M2 remains blocked pending measured secure shutdown and complete cleanup. No boot was performed by this reviewer.
