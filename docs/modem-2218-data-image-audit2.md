# Combined 2218 ONLINE review v2

**GO for one actively supervised, bounded ONLINE trial** of the unchanged exact image through the corrected runner and replacement `integration-2218/manifest-v2.json`, using `proof-procedure-v2.txt`. This supersedes the restoration-only NO-GO in `sol-2218-data-image.md` for this replacement submission. It does not approve the original manifest.

I found no remaining blocking source, image, restoration or proof-procedure defect within this scope. This is approval of an experiment with the accepted retained teardown, not evidence that ONLINE registration, calls or packets already work. No phone operation was performed.

## Exact scope and admission

The reviewed replacement manifest has 145 artifact pins. I independently hashed every pinned file and all 56 production source entries, with zero mismatches. Its current pre-admission SHA256 is:

```
da30e04fef7c8cbc0a6faa36c4ec2e1b2a0590dee8ae4971b1b78279c373bdd4
```

Both manifests remain `approved=false`. The replacement attempt file is absent. Its final audit path names this report but is not yet in its hash map, so a final admission update is still required before launch.

The launch owner must pin this report's final bytes and set approval true only in the replacement manifest after establishing the conditions below. That admission update may add this audit hash and change the approval flag. No reviewed artifact or existing replacement pin may change. The original manifest remains unapproved and must never launch.

The exact image is `plans/modem/out/data-usb-2218-boot.img`, SHA256:

```
4e1409ee78555833882db2823e7c3ffefb77b7c738da9b57d2d7e580fad77bd0
```

The saved and live built kernels both match the pinned SHA256:

```
e6bc2136e5a87302331271164f7ac11c6c4725e3b1cae63143200337c12b2f84
```

The old and replacement pin maps preserve the source, kernel, image, arguments, profiles, helper, selftests and hardware evidence identities. The replacement changes the runner pin, replaces the proof-procedure pin, and adds the corrected runner test and prior report pins. I also confirmed that the public and plans runner/test copies match byte for byte.

I reran ordered reconstruction through 2218. All 56 reconstructed files equal the actual combined production tree. The fresh reconstruction is `plans/modem/series-check/1793706707481756000`. I reran `verify-image.py data-usb-2218`, which verified payload containment and exact equality of all ten packed segments against the live kernel after chained fixups. No kernel rebuild was needed or claimed for these host-only corrections.

I independently extracted and matched the exact 607-byte image arguments, checked unique keys and the required reset, deadman, selftest, thermal, fetchsync, USB, CPU and panel gates. Modem/IPA/data gates remain unchanged. Raw QMI export and probe-only admission remain excluded. The private carrier settings remain Start Network TLVs only, with no modem profile create/modify operation.

## Restoration correction

I read the actual diff from `integration-2218/runner-review1.py` to `tools/run-data-usb.py` and traced every restore caller. The sole caller now passes the pinned trial profile into `restore()`.

Restoration requires the same device in clean recovery with no devloop process. It reads the live bytes once for classification. Original bytes cause no PUT. Exact reviewed trial bytes permit restoration of the pinned original. Any third state raises before PUT, including an unrecognized configd rewrite or an owner's edit.

After a permitted restore, the runner reads the profile again and verifies original-byte equality and clean recovery. A failed write/readback or third-state refusal propagates out of the `finally` restore branch before `safe_release` becomes true, retaining the hold and lock. An unresolved conflict requires supervised recovery and a new decision about those bytes. This GO grants no overwrite exception for it.

I independently reproduced the previous conflict against both actual runner versions with host-only mocked recovery, reads and writes. The old runner issued one PUT for an owner's third-state file. The corrected runner issued zero PUTs and raised the expected refusal. The copied updated runner suite also passed recognized trial restoration, original no-op, unknown-state zero writes, failed post-write readback, same-device/clean-state checks, changed pins and approval refusal.

The initial original-profile check, actual recovered-root hook composition check, second identity validation, exclusive modem hold/lock, exclusive attempt record, `--no-replay` and `--boot-tries 1` remain intact. I regenerated the trial preferences from the pinned original and obtained identical bytes. Both standalone and captured-root selftest compositions independently match their pinned files.

## Accepted hardware prerequisites

I read the accepted setup, clean-root, firmware and handshake records and independently matched their result/raw hashes and applicable markers. I reran the actual host acceptance predicates against each capture. For the clean checker I suppressed only its acceptance-record write in memory, preserving the accepted record.

1. ast174 passes SMMU setup, disabled/private parking and backing release without PAS start, with clean roots and a normal 214-second completion.
2. ast176 passes benign journal replay and a clean final root without modem/IPA work, with a normal 214-second completion.
3. ast177 passes PAS15 init, memory, authentication and shutdown, intact guards, parking and release, with clean roots and a normal 214-second completion.
4. ast182 passes offline I3 with clean roots, stale=false and a normal 363-second completion.

For ast182 I rechecked the accepted image, checker, source-audit and marker identities against the actual files. Its checker returns `I3 complete; PAS4 unverified; IPA backing retained until reset`. The actual raw sequence establishes tables/SRAM and endpoints, PAS4 authentication, all four QMI handshake flags and I3 after queue drain. Offline success precedes AP quiescence, which precedes SSCTL, then modem/IPA retention and normal DONE.

The accepted checker forbids PAS15 shutdown and backing release in that retained branch. This closes the offline I3 prerequisite. Earlier selected-USIM READY remains M3 evidence. Neither capture proves ONLINE registration, WDS calls or data packets.

## Retained ONLINE teardown

I independently traced the unchanged worker exit, `m_data_stop`, `m_cleanup`, IPA cleanup/quiescence/final stop and `CellSession.c` stop and late-reply paths. The contract remains sufficient for this owner-accepted bounded trial.

Worker exit terminally revokes IPA readiness and queued traffic. `m_data_stop` revokes USIM readiness, starts WDS cleanup and pumps transport/session replies for at most five seconds. It checks idle and the cleanup result, attempts AP DMA quiescence before detach, and preserves failure in the final result.

A matching late valid WDS Start reply remains adoptable during STOPPING so its returned handle can be stopped. Malformed or unanswered Starts remain unresolved. Unstopped handles and unknown Starts count as cleanup failures. Retention does not convert WDS cleanup failure into success.

AP quiescence precedes SSCTL/SMP2P/PAS4 stop attempts. With PAS4 stop unverified, `m.stop_verified` remains false, final GSI reset/deallocation is skipped, and `ipa_cleanup(false)` retains memory, mappings and required power before any PAS15 shutdown or SMMU release. Backing must remain owned until guarded reset. Failed AP quiescence also marks retention.

Final backing release requires verified PAS4, successful final GSI stop, PAS15 stop and safe SMMU release. In the verified-PAS4 branch PAS15 shutdown can still be attempted after a failed AP/GSI stop, but the retained flag prevents backing release. The unverified-PAS4 branch never issues that shutdown.

Active-call WDS teardown and data-channel completion format remain ONLINE-specific unknowns. Command completions require measured type 0x23. Data completions remain strict 0x22. An unexpected data type 0x23, invalid producer, pointer, channel, length or status must fail closed and revoke readiness. This GO authorizes no relaxation. M2 remains incomplete even if packets succeed.

## USB selection and cellular proof

The only procedure change selects the actual Mac USB link. Use bridge100 when the USB interface is a member of that bridge, otherwise en8, after checking the live host interface. The brief's current absence of bridge100 during recovery is not a reason to change Sharing or assume the interface that will exist after boot. Sharing must remain unchanged.

Both choices use the existing key and the same exact-image live devloop PID guard before every SSH operation. Never reconnect after that selected PID exits. The unchanged pinned collector accepts both interface names. This selection controls SSH transport only. Cellular proof always uses the phone's en2.

The proof retains exclusive creation of the fresh private nonce directory, umask 077, no-clobber helper upload, complete byte and SHA256 readback before chmod 0700, and one execution. No overwriting or deletion is authorized. Withdraw the boot job's competing ue0 route/DNS state once with the reviewed command, while retaining ue0 SSH reachability. Keep private interface, route and before/after counter captures, helper output and exit status.

I read the actual helper source, checked its pinned binary and verified its signature with `codesign --verify --strict`. Its production entry point fixes en2 and the numeric destination. It sets and reads back IP_BOUND_IF, binds and verifies the en2 source address, and uses one monotonic ten-second deadline for connect/send/read. It requires a valid HTTP status line and has no unbound fallback.

Only a successful bound exchange with its supporting private capture counts as M6 packet evidence. An ordinary request over USB Sharing proves nothing about cellular packets. DNS, TLS, Safari and iOS primary-route use require separate evidence. Leave the helper inert afterward and restore original preferences from clean recovery before releasing ownership.

## Mandatory supervision and limits

The session deadline remains 600 seconds after MPSS authentication. The selftest waits 660 relative seconds and has a 15-second finish delay. Its marker establishes body completion only, not packet success or clean modem shutdown.

The runner has operation timeouts and the kernel has a 900-second deadman, but there is no aggregate host ownership deadline. Active supervision must keep total ownership strictly below 20 minutes, including waiting under the hold, boot, collection, return to recovery and exact-profile restoration. Reserve recovery/restoration time, stop proof work early enough, and do not start or continue if that budget cannot be met. The collector's own timeout is not an extension of this limit.

At launch, establish idle exclusive ownership, same-device clean recovery, unchanged original profile and hook, fresh attempt state and sufficient budget. This review does not establish live phone state. No flashing, slot changes, stock Android boot, plain fastboot reboot or persistent modem/EFS partition writes are authorized. Rmtfs writes remain RAM shadows. Two equivalent unexpected resets stop this approach.

## Independent checks and review boundaries

Fresh isolated fixtures and logs are in `/tmp/sol-2218-v2-6h9n67nb`. I copied the host fixture tree before compiling and ran IPA datapath, IPA lifecycle, data against actual combined production sources, the actual helper's localhost-only suite, and the corrected runner suite. All passed, with ASan/UBSan where supplied. Extracted IPA test headers/runtime independently equal production bytes.

Copied harness adaptations kept compiler/subprocess TMPDIR=/tmp and temporary-directory calls explicit under /tmp. The runner fixture used an isolated resolved temporary root for its local-artifact validation. Original fixtures and other agents' outputs were preserved. I also ran the independent old/new conflict reproduction, both hook composition checks, exact profile regeneration, image verification and source reconstruction.

All review writes stayed in the workspace or /tmp. I made no implementation edit, manifest approval change, phone/SSH operation, commit, delegation, upload or file removal. Hardware ONLINE behavior remains unmeasured by this review. The GO is limited to the single supervised trial and proof procedure above.
