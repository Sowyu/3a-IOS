# Ast149 completion review

2026-11-03. GO for ONE repeated read-only power probe with the same image and the collection changes below. Offline PAS remains NO-GO until a recorded clean repeat passes review. Ast149 does not establish clean selftest completion or M2/M3.

Read `3a-IOS/HANDOFF.md` first, then `plans/modem/sol-2177-audit.md`. Applied ponytail-review and unslop. Review only. This report is the only file written. No phone operation, build, delegation, upload or deletion. Unchanged exact 2170 to 2177 plus USB v5 and image identity are supplied premises, not newly certified here.

## Existing evidence

`ios-asteroids/logs/ast149-modem-power-probe.txt:6596 to 6625` records lookup success, 32-byte aux blocks and these active maps, matching the desk inference:

- CX `0x30000`, levels 0/16/64/128/192/256/288/320/336/384/400/416, request index 9, retained floor index 11.
- MSS `0x30060`, levels 0/56/64/128/192/256/320, bounded request and floor index 6.
- XO `0x300d0`, levels 0/32/80/128.

Lines 6632 to 6640 record hardware preflight error=0, no PAS start/shared-memory writes, end error=0 and no-PAS cleanup=ok. `ModemRuntime.inc:337 to 345` reaches this successful probe exit only after SMEM partition discovery, PAS-support query, RSC prerequisite and power inspection. The prior audit's limits on zero padding and XO validation still apply. This proves the active resource maps, not every trailing aux word.

The saved `result.json` records reset-unknown, 230 seconds, root clean, stale=false and one frame. The console records the probe step at line 9876, but no done step or `SELFTEST: DONE`. USB counters through line 10701 show stale=0 and TX lost=0. SSH success is also recorded by the parent in `plans/modem/README.md:121`.

## Findings that block advancement

1. Completion is unproven. The actual injected `renegade-firstboot.sh:65 to 70` sleeps 120, calls shot, emits the done step and calls finish. Its finish at lines 35 to 42 emits DONE, syncs, sleeps only one second and requests reboot. A pulled screenshot proves a file existed, and a clean root proves filesystem state at collection. Neither proves that finish ran or explains the reset. Lost final console persistence is plausible, not established.
2. A marker after ordinary shot can falsely claim success. `plans/devloop/selftest/hook.sh:25 to 29` converts screenshot failure into a console message and can return success. Check the actual bounded sweetshot exit and nonempty output before writing a success marker. A flat frame alone is not proof of screenshot failure.
3. The inherited cleanup violates the deletion policy. `hook.sh:42` and the injected script's line 44 run `rm -rf "$ST_OUT"` on the phone. The current policy supplies no phone exception. Preserve the old frame directory under a unique sibling name, check the move succeeded, then create the fresh frames directory. Do not run rm or substitute phone Trash. Simply leaving old PNGs in place would let dev.py count stale frames.

## Narrow collection change, without rebuilding

1. Keep `plans/modem/out/power-probe-boot.img` byte-identical. Change only the selftest composed and pushed by dev.py. Inspect that composed script before the run for preserved frames, explicit shot failure handling and the finish timing below. Keep the existing 120-second observation period.
2. Embed a fresh host-generated run nonce in this script. Write `/private/var/tmp/modem-power-probe-complete.txt` only after checked shot success and the final body step, with the nonce, `phase=body-complete`, frame path and shot status. Check marker write success. Never create it through SSH or treat its filename alone as evidence. A matching nonce prevents an older marker from passing without deleting it.
3. In this probe's composed finish path, retain DONE, sync and the bounded sweetreboot call, but increase the post-DONE sleep from 1 to 15 seconds. This places both marker and DONE before the flush interval. A sleep only before finish leaves DONE with its original one-second window. Sync and sleep improve collection opportunity but do not guarantee persistent kernel-console capture.
4. Run only through dev.py with `--test modem-power-probe --pull /private/var/tmp/modem-power-probe-complete.txt`. Its `collect` at lines 338 to 358 pulls extra paths after checking root state. Check the actual pulled marker bytes and frame, because collect ignores the extra-file pull return code. Do not widen the classifier to accept reset-unknown.
5. Require the fresh matching marker, checked same-run frame, recorded DONE and done step, no SELFTEST FAIL/SKIP, panic or fired deadman, normal unattended return, root clean and stale=false. Reconfirm the live tables, no-PAS cleanup=ok and healthy same-run USB. `handsfree.py:85 to 101` checks DONE before deadman, so inspect all termination evidence even if result says selftest-done. Missing final evidence keeps offline NO-GO.

## Scope after the repeat

Immediately before the one run, require fastboot or recovery and no active dev.py. A dirty pre-run root is a stop here: `dev.py:308 to 319` otherwise performs an automatic replay boot, exceeding the one-probe scope. Keep the whole run under 20 minutes and retain all prior safeguards, thermal=3, keep/astwait, UBWC and fetchsync.

Only `sweetmprobe=1 sweetusb=4` may enable these modem/USB stages. MPSS, GLINK, rmtfs, pd-mapper and UIM stay absent. No modem PAS, votes or shared-memory writes in this repeat. Read-only refers to the modem stage. The existing dev.py root-script update and requested completion file are the scoped root-file writes.

After a recorded clean repeat is reviewed, the prior audit permits the separate offline stage. M2 still needs ready plus handover, GLINK/QRTR, actual scoped rmtfs/pd-mapper responses and verified PAS shutdown/cleanup. Only a later M2 pass permits UIM and valid matched card/application status for M3. Continue toward M3 within those gates without routine approval. This review authorizes no phone operation by this reviewer and no automatic second repeat if evidence remains incomplete.

No partition writes, EFS changes, flash, stock boot, Android, other loop, slot changes, online/NAS/WDS/raw QMI, sudo or permanent deletion. Use dev.py only for the authorized parent run. Local writes stay in the workspace or `/tmp`; the requested phone root artifacts are the explicit exception. Keep identifiers, raw logs and firmware private. Not checked live: repeat completion, reset cause, flush reliability, PAS shutdown, M2 or M3.
