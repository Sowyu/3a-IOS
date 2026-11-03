# Sol review of the exact 2208 ONLINE data image

Reviewed 2026-11-03. **Conditional GO for one bounded ONLINE data-image trial after the mandatory prerequisites below.** No phone operation is authorized by this report alone. No new blocking defect was found in the reviewed modem/data integration, exact image, profile restoration path or bounded packet-proof design.

What this change does: the combined image prepares IPA, binds the real 2221 adapter, starts MPSS and permits 2220 to request ONLINE only after current USIM and IPA evidence. It exposes en2 for a bounded IPv4 packet proof and retains hardware backing when shutdown cannot be proved.

## Mandatory prerequisites

1. Obtain the separate fresh 2208 IPA source GO, explicitly closing the VMID/global-translation finding in `plans/ipa/sol-2207-source-audit.md`.
2. Complete a separately reviewed, successful PAS15-only trial with the corrected source and record its exact image, preparation, authentication and teardown/retention evidence.
3. Complete a separately reviewed, successful offline IPA/GSI/QMI handshake trial with working endpoints and the reviewed shutdown/retention policy before requesting ONLINE.
4. Only after those conditions hold may the owner record approval and this real report's hash in the manifest, revalidate every frozen artifact and consume the single attempt.
5. Preserve the exact image, arguments, scripts, profile, nonce, lifetime and proof procedure reviewed here, with no automatic retry or replay.

At review time these hardware prerequisites have not been established. The manifest correctly remains `approved=false` and contains no report hash. I have not changed it. Ast166 failed preparation, ast169 reset, and ast170 proved the corrected read-only survey only. None is PAS15, handshake or ONLINE approval. Any prerequisite failure or new source-review blocker suspends this conditional GO.

## Exact identity and validation

I read `3a-IOS/HANDOFF.md` first, followed by the dev-session, modem and cellular-data documentation, modem2189 re-audit, data2220 audits4 through7, data2221 re-audit and the IPA2207 source finding. I used ponytail-review and unslop. Source references below are under `ios-asteroids/research/xnu-modem/dxb/xnu` unless another path is stated.

I independently recomputed all69 hashes in `plans/modem/integration-2208/manifest.json` and all53 relative source hashes in `source-hashes.json`. Every hash matched. The public runner matches the reviewed `plans/modem/tools/run-data-usb.py` byte for byte. The supplied series log records the rebased prerequisite order and exact reproduction of19 IPA-touched files, including2207 and2208 at the requested hashes.

The kernel SHA256 is `1dcd7de654ed7b81ed721cf95a2cfa4f5cc07a8893fea87d2d7616216e21d869`. The boot-image SHA256 is `b141136242ee89934c78e29e78dc881d1800c499fa5a16153af7cd34a478dd8a`. Both supplied build logs contain `done building xnu`. I did not rebuild the kernel.

`python3 -B plans/modem/tools/verify-image.py data-usb-2208` freshly passed all10 packed segments against the kernel after chained fixups. An additional independent payload check verified unique containment in the decompressed image, ten unique segment addresses, descriptor bounds, the exact606-byte argument line and absence of duplicate keys or a raw-export gate.

Fresh ASan/UBSan lifecycle and datapath suites passed in `/tmp/sol2208-jqbsa0kn/host`. Their logs are `/tmp/sol2208-jqbsa0kn/check-lifecycle.sh.log` and `check-datapath.sh.log`. Freshly extracted tested modem/IPA source files matched the combined production source, including the worker after IPA hunks. Coverage includes the shared-TLB lease model, promoted complete-handshake/failure and active-RX stop cases, real2221 partial-bind failure, bounded cleanup and quiescence ordering. These remain host models.

The runner host check passed from an isolated copied hierarchy at `/tmp/sol2208-jqbsa0kn/runner`, with output in `runner.log`. The en2 profile check passed. Standalone script composition matches the frozen selftest, and recomposing the frozen root script preserves its exact contents. Actual live-root composition is deliberately deferred to the runner's prelaunch check.

## Integration safety

`ModemRuntime.inc:623-631` prepares IPA, installs the real2221 binding once, checks failure, attaches data and only then loads/authenticates MPSS. A partial bind failure cannot proceed to attach or authentication. Eight unannounced QRTR local slots and one serialized modem worker remain the integration contract.

`ModemRuntime.inc:366-376` refreshes UIM and IPA evidence before each QRTR receive, processes the control update, checks identities again and publishes discovery before the next packet. The normal worker also refreshes UIM unconditionally and publishes IPA state before data poll. This preserves audit7's revoke-before-service-sync ordering and prevents same-batch DEL/NEW or late setup replies from hiding an observed loss.

`IpaData.inc:21-25` makes revocation terminal and drops queued TX under the queue lock. `ipa_transport_sync` rejects failed, fatal, stopping, unready, failed-GSI and changed-peer states. Positive session readiness is accepted only in WAIT_READY, and the central session send guard permits only WDS Stop Network during teardown. I found no readiness revival or setup-send bypass in these paths.

`m_data_stop` latches stopping, revokes admission, pumps receive/poll work for at most5 seconds, checks cleanup, quiesces AP channels and detaches once. Cleanup error or failure to become idle remains failure even when shutdown is attempted afterward. RX callbacks run outside the IPA queue mutex, copied TX has bounded lifetime, and callback registration remains immutable.

An unresolved WDS start or stop remains represented in CellSession and fails cleanup. Detach does not free IPA DMA backing. An unverified PAS4 stop returns before modem reclaim, and IPA cleanup retains its allocations, mappings and power when MPSS stop or hardware quiescence is uncertain. Final IPA release requires verified MPSS stop, hardware stop, PAS15 stop, SMMU restoration and intact guards. The owner's accepted PAS4-retention policy remains necessary, and M2 remains incomplete.

I checked2208's integration dependency without replacing its separate source audit. The current headers use distinct nonzero VMID leases, reject CLIENTPD/VMID16/EXID modes, recheck leases, verify private identity before maintenance and emit non-global leaves. The fresh shared-TLB model passes. This does not prove physical SMMU behavior or supersede the required fresh source GO.

## Storage and lifetime

The reachable firmware path uses `sweet_ufs_read_part("modem_a", ...)` through bounded FAT reads. MPSS uses PAS4 and stock split modem firmware. IPA uses PAS15 and the DT GSI reservation at0x9ea90000. The APPS geometry guard is the actual ID tuple `4c017e72/60000048/5111`, with36-bit address bounds.

`ModemRuntime.inc:441-442` reads EFS partitions into allocated images. `ModemRmtfs.c` bounds requests against those RAM images and the low-memory window, and implements writes as RAM copies. No modem/data/IPA call to a partition-write API was found. `sweet_ufs_read_part` saves/restores the selected LUN and calls only the read-block path. The general UFS write function bounds writes to the selected root partition, which remains userdata in this exact boot configuration. Root preferences, hook and evidence writes are separate authorized userdata writes. No modem_a, modemst1/2, fsg, fsc or persist write is authorized.

The exact arguments retain strict modem/data dependencies, sweetipa=1, sweetdata=1, sweetdataonline=1, sweetmsession=1, sweetusb=4, framebuffer arming and the independent900-second deadman. The worker permits600 seconds after authentication. The script uses a relative660-second sleep plus standard setup and15-second finish delay. Slow setup can consume that margin and fail the run, but does not extend the independent deadman. dev.py derives a1020-second return deadline, uses monotonic waits and receives one boot try, no replay and zero button wait.

## Runner and proof conditions

The runner validates approval and pinned bytes before taking ownership, waits for existing loop PIDs, takes the atomic phone lock, requires the same clean recovery device for preflight/restoration and checks the actual original profile and composed root hook. It revalidates artifacts and creates the exclusive attempt file before its sole dev.py invocation. The finally path restores original preferences, reads them back byte for byte and requires clean recovery before releasing ownership. Restoration uncertainty retains the hold and lock.

The original profile hash is `e8d0685f5b691028a7779bd2d481a537ed87574baeabd3799249efad3af4f368`. The en2 profile hash is `388a5a2ff448716ba60fcc61003fec56ea379e3c989d7336e8f08963cdc51aa2`. Keep the original intact. The new profile preserves ue0 link-local SSH while giving en2 DHCP precedence.

The runner's exit status is not the complete acceptance decision. dev.py can classify DONE with a frame while recording flat-frame or stale-root evidence separately. Require the exact fresh nonce marker, a decoded nonblank checked frame, `stale=false`, normal DONE, clean root before/after and elapsed boot time below20 minutes before recording success. A selftest result also does not prove ONLINE, WDS traffic or M6.

Follow the frozen proof procedure literally. Before every SSH operation, verify that the original live PID still names this exact image and stop when it exits. Keep Mac Sharing unchanged. Create the nonce directory exclusively with private permissions, upload the pinned sweeten2 under noclobber, compare full readback bytes and hash before execution, withdraw the bootjob route/DNS once with sweetnetcfg -d and retain interface/route/counter evidence privately.

The reviewed sweeten2 source fixes the target to1.1.1.1:80, verifies IP_BOUND_IF=en2, binds the selected en2 IPv4 address, checks the resulting local address and uses nonblocking I/O with a10-second monotonic deadline. Its one HEAD exchange proves only bounded IPv4 M6 when supported by the en2/IPA counters. DNS, TLS and Safari remain unproved. Keep the uploaded helper as an inert archive and never accept an unbound Safari request while a USB route exists.

Two equivalent unexpected resets stop the line. Retain uncertain hardware ownership until guarded recovery. No shutdown-only retries, calls, SMS, raw identifier export, flashing, Android boot or partition changes are covered.

## Limits and final decision

No implementation fixes are required by this bounded integration review. The unresolved hardware prerequisites above remain launch blockers. Source, runner, tool dependencies and evidence procedures must remain the reviewed versions throughout the attempt.

I did not operate the phone, invoke adb/fastboot/SSH, change Mac settings, install a profile, alter implementation or manifest files, commit, delegate, use sudo or remove files. Writes were limited to this report and isolated host-test artifacts in /tmp. Physical DMA cessation, actual PAS15 operation, kernel concurrency, modem responses and the separately reported signed-helper socket tests were not independently established here.

Verdict: conditional exact-data-image GO after the fresh2208 source GO and separately reviewed successful PAS15-only and offline handshake trials. Launch remains blocked until those conditions are satisfied.
