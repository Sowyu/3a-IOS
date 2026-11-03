# USB gadget for SSH from the Mac: patches 2160 and 2161, v5

2026-11-03. v5 makes the cable link usable for development sessions of 1 to 2 hours. It answers findings 2 and 3 of Sol's daily-use audit (`docs/usb-gadget-audit4.md`). Daily default use is still off. Sol's audit of v5 (`docs/usb-gadget-audit5.md`) is a GO for a controlled dev-session test boot.

## Why TX froze in ast145

ast145 passed level 4, then TX stopped for good. The log has one `stale completion 0x0000c046 on ep3 ignored` (`ios-asteroids/logs/ast145-usb-l4-v4d.txt:9299`). After it, TX stays at 2226 frames while the queue fills to 32, and RX keeps advancing (`:9551` to `:10450`).

`0xc046` is endpoint 3 (bulk IN), XferComplete, status IOC and LST. That is an ordinary successful completion, and v4 threw it away. The cause is a race between two threads:

1. Network output (`ud_if_output`, then `ud_tx_kick_locked`, then `ud_start_trb`) runs on a network thread under `ud.lock`. It sets `tx_busy`, issues STARTTRANSFER and marks the slot active in `xfer_active[3]` only after the command returns.
2. The event thread, on another CPU, judged the TX completion in `ua_trb_done()` without that lock.
3. When the host read the frame before the output thread had marked the slot active, the completion found the slot inactive and was dropped as stale.
4. `tx_busy` then stayed set, so no later frame ever started.

TX is the only transfer started outside the event thread, which is why only ep3 was hit. The window is a few microseconds wide, so it takes an interrupt or preemption of the output thread inside it. That fits one stale event in 2226 frames, at a moment of heavy boot activity (the touch driver powering up, many execs). The host harness reproduces it: on v4 the race test prints exactly `stale completion 0x0000c046 on ep3 ignored` and TX never moves again. That the phone hit this race is inferred from the code, the event word and the counters. The model reproduces it, but the log alone cannot prove it.

## v5 changes

Only `sweet_usb.c` changes. Line numbers are in `plans/usb/gadget/work/b5/bsd/dev/sweet_usb.c`.

1. The TX completion is judged under `ud.lock` (`ua_event`, `:2085`). The event thread waits until a start in progress is fully recorded.
2. Bounded TX progress check, `ua_tx_check()` (`:2161`), every 100 event-thread polls. A TX held for 2 s is settled with output admission closed, so nothing is copied into the TX buffer meanwhile. If its TRB is back from the controller (HWO clear), the transfer finished and only its event was lost: it is finished by the same test a completion passes. If the controller still owns it, ENDTRANSFER ends it, verified through GUCTL2's polling mode, and that frame is dropped. Only then is the slot used again. A failed end keeps the slot reserved and stops the gadget, as every other failed end does. Nothing clears `tx_busy`, HWO or `xfer_active` without one of those two proofs.
3. Every data teardown (alternate 0, `SET_CONFIGURATION(0)`, bus reset, disconnect) drops the queued TX frames after the ends succeed, so none reaches the next host (`ua_data_down`, `:1876`).
4. The plain-UDP live log is off unless `sweetusblog=1` (`:2534`). The Poco F1 path is unchanged.
5. The status line every 30000 polls ends with `stale N, TX lost N ended N`.

2160 v5 differs from v4 only in recording each TX start time in `ud_tx_kick_locked`. `plans/usb/gadget/v4-to-v5-delta.patch` is the v4 to v5 diff.

Host side:

- `usb-check.py 4 OUT --soak S --idle I` keeps data moving after the first 4 MiB checksum. Each round sends 1 MiB up with the phone's hash, 1 MiB of zeros down checked on the Mac, and one small command that must answer in 15 s. Some rounds come after I seconds with no traffic. Three failed rounds in a row end it.
- `usb-verdict.py` fails level 4 when two status lines in a row show TX unchanged with a full queue. It fails the real ast145 log: `TX frozen at 2226 frames with the queue full (txq 32)`.

## v5 desk checks

All ran on 2026-11-03.

1. `python3 -I host/kernel-harness.py b5`: 38 of 38. On b4, the 6 new tests fail. The mock now models lock exclusion and a TX that completes inside its own STARTTRANSFER. New tests: the race, a host that stops reading (TX ended after 2 s, then resumes), a lost TX event (finished by HWO, no end), a failed recovery end (stopped, slot never reused), 50 frames plus 10 s idle with no recovery, no log thread by default, and a reset that drops queued TX.
2. `python3 -I host/test_usb_check.py`: all pass, with 5 new soak scenarios and 3 new level 4 verdict cases.
3. `test_gadget.c`: 13741 checks, 0 failed. The header did not change.
4. Patch proof: on a copy of `research/xnu-combined/dxb/xnu/bsd/dev`, reversing 2161 v4 and 2160 v4 gives `work/a` exactly, and applying 2160 v5 and 2161 v5 then gives `work/b5` byte for byte.
5. The kernel built from `research/xnu-usb` with `--remake`. That tree now equals `research/xnu-combined` (the daily tree) except `sweet_usb.c`, which is `work/b5`. The kernel is `plans/usb/gadget/kernel-v5/kernel.development.sweet`, SHA256 a1bca511017b253efece8136b82bcf895692e182f823eccae84e5ae27a35375a. Its sweet_usb.c warnings match v4's.

## Sol's audit of v5

`docs/usb-gadget-audit5.md`: GO for one controlled level-4 dev-session test boot on the known Mac. Its two findings do not block the boot:

1. Medium: `dev-v5-test.sh` exited 0 even after a failed test. Fixed before the run: it now refuses a reused note, empties the checker's result file first, and exits 0 only when dev.py, the checker and the verdict all pass.
2. Low: the soak never leaves bulk IN pending, so the ENDTRANSFER branch of the progress check is tested only in the host model. Proving it on hardware needs a host fixture that stops the Mac's USB driver from reading, which needs more than this Mac allows without sudo. Open.

## Phone test, ast147 (2026-11-03 13:46 to 14:01)

`plans/usb/gadget/dev-v5-test.sh usb-dev-v5a`: image `usb-dev-t`, the dev image with `sweetdeadman=960`, and the Mac checker with `--soak 480 --idle 90`. Logs: `ios-asteroids/logs/ast147-usb-dev-v5a.txt`, Mac result `plans/usb/gadget/runs/usb-dev-v5a.txt`, runner log `/tmp/usb-dev-v5.log`.

Measured:

- The Mac saw the gadget 65 s after the checker started, ping6 worked at 67 s, port 22 at 75 s, ssh at 80 s, and the 4 MiB upload checksum matched at 84 s.
- The soak passed: 16 rounds both ways, each with 1 MiB up hashed on the phone, 1 MiB down and a small command. 4 rounds came after 90 s with no traffic. Rounds took 2.1 to 2.2 s, except one at 11.7 s (round 2, the small command took 1.0 s).
- `USBCHECK L4 PASS ... soak 480 s: 16 rounds both ways, 4 after 90 s idle, slowest 11.7 s`.
- `USBVERDICT L4 PASS phone and Mac agree (19 status lines; stale 0, TX lost 0, TX ended 0)`.
- The phone sent 23330 TX frames, 10 times as many as ast145 managed before its stall, with 0 stale completions and no recovery needed. The queue never stayed full.
- The self-test ended by itself at 664 s, the phone went back to fastboot after 695 s, and the root was clean.

dev.py exited 1 with `selftest-no-frames`. The self-test did finish (`SELFTEST: DONE`), but dev.py's classifier wants at least one frame, and the body took none. The body now takes one frame at the end. That is a shell-only change and was not rerun.

What this does not prove: the race is rare, so one clean run shows the fix works under this load but cannot show the race is gone. The 2-hour image `usb-dev` has not run for 2 hours yet.

## Dev session image

`plans/usb/gadget/out-v5/usb-dev-boot.img` (SHA256 d0e31cf5...), from `plans/usb/gadget/build-usb-dev.sh`. It is combo-ub on the v5 kernel: UBWC, keep plus astwait, `sweetthermal=3`, plus `sweetusb=4 sweetdeadman=7200 sweetselftest=1`, 597 of 607 bytes. To fit, it drops `keepsyms=1`, `sweettouchirq=3` (touch is polled) and `sweetkeys_ms=5` (keys at the default poll rate). That was still 611 bytes, so it also leaves out `sweetusblog=0`, which v5 makes the default. The live UDP log is off.

The root needs the same driver as combo-ub (`plans/lag/next-luma-ub`). Nothing is flashed.

How the owner uses it:

1. Put the phone in OrangeFox or fastboot and check no other dev.py run is going: `pgrep -f 'devloop/dev[.]py'`.
2. Run `python3 plans/devloop/dev.py plans/usb/gadget/out-v5/usb-dev-boot.img --test usbdev --note usb-dev-NAME --key 'ASTEROIDS USB|SWEET USB'`, with a fresh NAME each time.
3. About 80 s after the boot, connect with `ssh root@fe80::5eff:fe77:ee02%en8`. If en8 is not it, `ifconfig` shows the new Ethernet interface with `status: active`.
4. To end early, run `ssh root@fe80::5eff:fe77:ee02%en8 touch /private/var/tmp/usbdev.stop`. The phone goes back to fastboot within about 10 s, and dev.py pulls the log. Otherwise the session ends by itself about 115 minutes after boot.

The phone is busy for the whole session, so tell the modem agent first.

## What still blocks daily use

From audit 4, the dev image still has:

- Finding 1: admission needs a running deadman, so every session ends at a fixed time. Daily use needs its own admission policy.
- Finding 4: root SSH has no phone-side host or lock-state gate, and dropbear listens on every address, IPv4 and IPv6.
- Finding 5: no handling for physical replug, host suspend, a charger or an endpoint halt. CLEAR_FEATURE(ENDPOINT_HALT) still clears nothing.
- Finding 6: restart, panic and thermal reset do not quiesce the gadget first.
- Finding 7: no long-uptime or combined-load qualification, no event-overflow recovery, no independent watchdog.

Also open: the ENDTRANSFER branch of the progress check is unproven on hardware (Sol audit 5 finding 2), and no 2-hour session has run.

## Files (v5)

- `2160-asteroids-usb-gadget-shared-v5.patch` and `2161-asteroids-usb-gadget-levels-v5.patch`, made by `host/make-patches-v5.py` from `work/a`, `work/m5` and `work/b5`. Hashes in `plans/usb/gadget/SHA256SUMS-v5`.
- `v4-to-v5-delta.patch`, the v4 to v5 diff of `sweet_usb.c`.
- `build-usb-dev.sh`, `dev-v5-test.sh`, `selftest/usbdev.sh` (dev.py's copy is `plans/devloop/selftest/scripts/usbdev.sh`).
- `kernel-v5/` and `out-v5/` stay in `plans/usb/gadget`, not in this repo.

---

The rest of this file is the v4 description.

## v4

2026-11-03. Desk work only, no phone. v3, under Sol's level 4 re-audit, is unchanged:

- The `-v3.patch` files match `SHA256SUMS-v3`.
- The v3 host tools, self-test, builder and README are copied unchanged in `v3/`.
- v4's hashes are in `SHA256SUMS-v4`.

## What ast107 and ast108 showed

Level 2 passed on the phone (`ios-asteroids/logs/ast107-usb-l2.txt`).

Level 3 failed (`ast108-usb-l3.txt`): `FAIL no SET_CONFIGURATION in 120 s: 1 connects, 1 resets, 10 setups`, followed by a verified stop. The cause is the Mac's accessory security, not the gadget and not its descriptors. This Mac's kernel log for that run (`log show`, no sudo) says so (proven):

```
01:48:18.250 (IOUSBHostFamily) usb-drd2-port-hs@02100000: AppleUSBHostPort::enumerateDeviceComplete_block_invoke: enumerated 0x1209/0001/0103 (iOS 26 asteroids (USB level 3) / 1) at 480 Mbps
01:48:18.250 (AppleCredentialManager) ACMTRM: _onEnterRestrictedMode: [CACHE MISS] (reason=4).
01:48:18.250 (AppleCredentialManager) ACMTRM: _onEnterRestrictedMode: rejecting cache miss (trigger not allowed).
01:48:18.250 (IOUSBHostFamily) usb-drd2-port-hs@02100000: AppleUSBHostPort::enumerateDeviceComplete_block_invoke: device functionality blocked by transport restrictions
01:48:18.250 (IOUSBHostFamily) iOS 26 asteroids (USB level 3)@02100000: IOUSBHostDevice::start_block_invoke: device will not be registered for matching
```

macOS enumerated the device, then refused to match any driver to it, because the device was not in its cache of approved accessories and it was not allowed to ask. Nothing ever sends SET_CONFIGURATION to a device no driver matched. The phone's other USB identities pass the same port with no restriction over the last two days: fastboot `0x18d1/d00d "Android"` 131 times, and OrangeFox `0x18d1/4e11 "A059"` 54 times. They were approved before. This was the only restricted-mode event in three days.

The guess about class 0 was reasonable, but the level 3 descriptor already had bDeviceClass 0 and bDeviceSubClass 0. That is exactly what `AppleUSBHostCompositeDevice` matches (its Info.plist on this Mac: `IOProviderClass IOUSBHostDevice, bDeviceClass 0, bDeviceSubClass 0`). The driver never got the chance.

### (a) What the hardware path proved

Proven by both logs:

- The link came up at high speed: the phone's `connect done #1, speed 0 (high)` and the Mac's `at 480 Mbps`.
- One bus reset was handled.
- SET_ADDRESS worked.
- Nine IN control transfers completed with data and status stages. They were the device descriptor, strings 5, 1 and 3 (each at length 2 and then in full), and the configuration descriptor at 9 and at 18 bytes.
- The bytes arrived intact: the Mac logged the product string `iOS 26 asteroids (USB level 3)` exactly as our pool held it. That is controller DMA out of our pool, read correctly by a host.
- The stop was verified: halted, transfers ended, event buffer ours, canaries intact, sGFSR 0.

Not proven:

- SET_CONFIGURATION handling.
- Any OUT data stage. The Mac sent none.
- The timed stop after configuration. The stop ran on the 120 s no-configuration path.

So the level 3 criterion is right as a USB criterion. The Mac blocked it before the gadget could meet it, and it cannot pass on this Mac until the accessory is approved.

### (b) The smallest change, and level 4

No descriptor change gets a SET_CONFIGURATION from this Mac without an approval. Any identity macOS has not approved before is blocked in the same way. That includes level 4, which would have failed the same way tonight.

Once approved, both levels have a matching driver (proven from the drivers' Info.plists on this Mac):

- Level 3 and v4's level 4, with device class 0: `AppleUSBHostCompositeDevice` sets the configuration.
- v3's level 4, with device class 2: `AppleUSBCDC.kext`'s `CDCCompositeDevice` (bDeviceClass 2) would have set it.
- Then `AppleUserECM.dext` matches the interfaces `2/6/*` and `10/0/0`, which are exactly the ECM interfaces we describe.

That these drivers work end to end with this gadget is inferred.

## v4 changes

1. One device identity at levels 3 and 4, so a single approval should cover both, `sweet_usb_gadget.h` (2160 v4). Both levels now have bDeviceClass 0/0/0, bcdDevice 1.01, product string 2 "Sweet iOS (USB Ethernet)", manufacturer "ios-sweet" and serial "0001". Only the configuration differs. The level-3-only string is gone. Level 4 moves from device class 2 to class 0, which `AppleUSBHostCompositeDevice` configures. Which fields macOS's approval cache is keyed on is unknown. If it covers the configuration descriptor, level 4 needs its own approval, and nothing is lost. `sweet_usb.c`, `sweet_ufs.c` and `sweet_reboot.c` are byte-identical to v3, and 2161 v4 differs from 2161 v3 only in its Subject line.
2. `host/usb-check.py` recognises the block. It scans the kernel log with `log show` (no sudo) for `blocked by transport restrictions` since the run started, and ends with `USBCHECK L<n> FAIL BLOCKED by macOS accessory security ...` instead of a generic configuration failure. Run against this Mac's real log, it finds the ast108 line in 2 s and nothing after it. It accepts the v4 identity (bcdDevice 0x0101) as well as the v2 and v3 per-level values. At level 3 it requires the configured interface to be the vendor class 255.

## v4 tests

All ran on 2026-11-03.

1. `cc -std=c11 -Wall -Wextra -Werror -I work/b4/bsd/dev host/test_gadget.c -o /tmp/tg && /tmp/tg`: 13741 checks, 0 failed. New: the same device descriptor and the same strings at levels 3 and 4 at every speed, class 0/0, AppleUserECM's interface classes present, level 3's vendor interface. Against v3's header, the three "same device descriptor" checks fail.
2. `python3 -I host/test_usb_check.py`: 13 checker scenarios and 8 verdict cases pass. New: v4 identity at levels 3 and 4, and BLOCKED at levels 3 and 4. All four fail with `--checker v3/host/usb-check.py`.
3. `python3 -I host/kernel-harness.py b4`: 31 of 31. The kernel code is v3's.
4. `syntax-check.py b4` for the three files and `m4`: exit 0, with the same 6, 77 and 9 inherited diagnostics.
5. `git apply` of both v4 patches onto the combined tree's three files reproduces `work/b4` byte for byte.

## What this means for the runs

Hands-free levels 3 and 4 need this Mac to accept the gadget's identity. Either of these needs the owner and is outside my rules:

1. One attended run. Have the Mac unlocked and someone at it when a level 3 boot with the v4 identity (or v2's, for level 3 only) enumerates, and allow the accessory when asked. macOS then caches it, as it did for fastboot and adb. Whether it prompts at all depends on the "Allow accessories to connect" setting, which I did not read. "Trigger not allowed" at 01:48 fits a locked Mac.
2. Or set System Settings > Privacy & Security > Allow accessories to connect to "Always". That needs an admin password, and it weakens a security control, so it is the owner's decision.

After either one, rerun level 3 with `usb-check.py` and `usb-verdict.py` as before. If it passes, level 4 (after Sol's GO on v3, with v4's identity) should need no further click, unless the cache is keyed on the configuration.

---

The rest of this file is the v3 description.

## v3

2026-11-03. Desk work only. I ran no adb, fastboot or dev.py, built no kernel, wrote nothing in either live kernel tree, and committed nothing.

v3 answers Sol's re-audit, `analysis/usb-gadget-audit2.md`. That audit gave level 2 GO, level 3 GO after level 2 passes, and level 4 NO-GO. v2 is what runs on the phone tonight, and it is unchanged:

- The `-v2.patch` files still match `SHA256SUMS`.
- The v2 host tools, self-test, builder and README are copied unchanged in `v2/`, and still match their hashes in `v2/SHA256SUMS`.
- v3's hashes are in `SHA256SUMS-v3`.

Line numbers below refer to `work/b3/bsd/dev/`, the files after both v3 patches. The v2 description further down still holds except where this section says otherwise.

## v3, finding by finding

| # | Re-audit finding | v3 change | Where | Test, failing on v2 and passing on v3 |
|---|---|---|---|---|
| 1 | Alt 0 does not exclude TX: output starts TX in alt 0, and teardown can let a packet be copied into a TX buffer the controller owns | A data output admission gate, `ud.tx_closed`, guarded by `ud.lock` (see the list below) | `sweet_usb.c:441`, `:804`, `:833`, `:1849`, `:1981`, `:1781`, `:2315` | "R1 alt 0", "R1 configured but alt 0", "R1 output interleaved with alt-0 teardown", "R1 alt 1 again" (liveness) |
| 2 | The readiness refusal came after the read-only probe | Levels 2 to 4 also refuse before the probe's first access when recovery is not armed. The check inside `ua_gadget` stays | `sweet_usb.c:209` | "R2 recovery not armed: the probe makes no access either" |
| 3 | Request validation incomplete | The class branch accepts only SET_ETHERNET_PACKET_FILTER (0x21, 0x43) to interface 0, with no data, when configured. Standard requests are checked for wValue, wIndex and wLength as chapter 9 defines them, and for the Address and Configured states. The kernel tracks the Address state | `sweet_usb_gadget.h:332`, `:343`, `sweet_usb.c:1940`, `:1881` | 21 new header checks, 15 of which fail on v2's header. Harness: "R3 malformed SET_CONFIGURATION configures nothing" |
| GUCTL2 note | ENDTRANSFER on a pending SETUP may not complete (Linux) | `ua_stop` leaves a SETUP TRB waiting for the host to the halt, as `dwc3_gadget_soft_disconnect` does. It still ends an ep0 data or status stage and every data endpoint | `sweet_usb.c:1788` | "R4 stop with a pending SETUP: halt covers it, stop verified". The old F3 stop-failure test now fails an ep1 data stage's end instead |
| F7 note | The verdict matched substrings across boots | `usb-verdict.py` judges only the log after the last `ASTEROIDS USB L<n>: start`. On v2's script, "an old passing boot, then one that went quiet" gives a false PASS | `host/usb-verdict.py` | 3 new verdict cases |

How the gate in finding 1 works:

- Every teardown closes it under `ud.lock` before it ends any transfer: alternate 0, `SET_CONFIGURATION(0)`, bus reset or disconnect, and `ua_stop`. It stays closed through alt 0.
- `ud_if_output` drops a packet while the gate is closed.
- Before copying a byte, `ud_tx_kick_locked` checks the gate, `ud.gate`, `ud.stopped` and that the TX slot is not owned by the controller.
- `tx_busy` is cleared only after the TX end succeeded with the gate closed.
- The gate reopens under the same lock only in alternate 1, configured, with no gate failure, and then sends what is queued.
- All command cleanup stays outside the lock.
- The gadget starts with the gate closed. On the Poco F1 it is never closed, so that path keeps its behaviour.

Finding 1's tests run the real `ud_if_output` and `ud_tx_kick_locked`, which the harness now extracts too, along with the real probe entry. A lock hook runs one network output just before each lock acquisition during the host's SET_INTERFACE(1, 0), 12 positions in all. Lock 2 is Sol's case. The mock flags any packet copy into `UD_TX` while the controller owns ep3. On v2, TX ends up active after the teardown at all 12 positions, and position 2 shows the copy into a live TX.

### v3 desk checks

All ran on 2026-11-03.

1. `python3 -I host/kernel-harness.py b3`: 31 of 31 pass, plus the boot-order check.
2. `python3 -I host/kernel-harness.py b2`: the same 25 earlier tests pass, and all 6 new tests fail.
3. `python3 -I host/kernel-harness.py b`, v1: only the 3 baselines and one other test pass.
4. `cc -std=c11 -Wall -Wextra -Werror -I work/b3/bsd/dev host/test_gadget.c -o /tmp/tg && /tmp/tg`: 13916 checks, 0 failed. With `-I work/b2/bsd/dev`, 15 fail.
5. `python3 -I host/test_usb_check.py`: 9 checker scenarios and 8 verdict cases, all pass.
6. `python3 -I host/syntax-check.py b3 FILE` for `sweet_usb.c`, `sweet_ufs.c` and `sweet_reboot.c`, and `m3 sweet_usb.c`: all exit 0, with 6, 77 and 9 inherited diagnostics as before.
7. `git apply` of both v3 patches onto copies of the combined tree's three files reproduces `work/b3` byte for byte. `sweet_reboot.c` and `sweet_ufs.c` are the same as v2's.

The mock's locks still do not block. The interleaving tests stand in for another thread with one call at a chosen point, so they cover ordering, not real concurrency.

### Left for later

- No watchdog for a bus stall that freezes every CPU. That is unchanged and shared by every level.
- The Mac's IOUSBHostInterface key names are still my assumption. They only matter for level 3's host half. Level 4's checker uses ifconfig, ping6 and ssh.
- The daily image is not approved for level 4, and its missing deadman still refuses level 4.
- `CLEAR_FEATURE(ENDPOINT_HALT)` with no `CLEARSTALL`, and no reconnect after a stop, as in v2.

### For tonight's v2 runs

Use v3's `host/usb-verdict.py` for the level 2 and 3 verdicts too. It is host-only, needs no kernel change, and only judges the last run of the level in the pulled log. With v2's script, an earlier passing boot in the same log can vouch for a later quiet one.

---

The rest of this file is the v2 description.

## Files

Patches:

- `2160-asteroids-usb-gadget-shared-v2.patch` holds the shared plumbing and the new header. It changes nothing on asteroids by itself.
- `2161-asteroids-usb-gadget-levels-v2.patch` holds levels 2 to 4, the boot order in `sweet_ufs.c` and the readiness check in `sweet_reboot.c`.
- `2160-asteroids-usb-gadget-shared.patch` and `2161-asteroids-usb-gadget-levels.patch` are the audited v1, kept for diffing.

Host tools in `host/`:

- `kernel-harness.py STAGE` runs `test_kernel.c`, the audit findings as tests. It cuts the real gadget functions out of a stage's `sweet_usb.c` and runs them against a mock DWC3.
- `test_gadget.c` tests the header.
- `test_usb_check.py` tests the Mac checker and the verdict script. `--checker PATH` runs it against another checker.
- `usb-check.py` is the Mac side of a level 3 or 4 run.
- `usb-verdict.py` combines the phone log and the Mac result into one verdict.
- `syntax-check.py STAGE [FILE]` compiles a file with the combined build's own flags.
- `make-patches-v2.py` regenerates the v2 patches from `work/`. `make-patches.py` made v1.

Also here:

- `build-usb-loop.sh KERNEL` makes the hands-free images.
- `selftest/usb.sh` is the dev.py self-test body.
- `SHA256SUMS` holds the hashes.
- `work/a` has the base files, `work/b` the audited result, `work/b2` the v2 result, and `work/m2` the state after 2160 v2 alone.

Both v2 patches apply with `git apply`, which is what `dxb/build.sh:473` uses, to the combined tree's `sweet_usb.c`, `sweet_ufs.c` and `sweet_reboot.c`. These three files are identical in `xnu-local` and `xnu-combined`. No other patch in the series touches them. I applied both v2 patches to copies, and the result matches `work/b2` byte for byte.

## What v2 changes, finding by finding

Each finding has at least one test in `host/test_kernel.c` (or `test_usb_check.py` for F7) that fails on the audited v1 and passes on v2. See "Desk checks" for the runs.

| # | Audit finding | v2 change | Where | Test |
|---|---|---|---|---|
| 1 | Recovery armed after USB | `sweet_usb_probe()` now runs after `sweet_reboot_asteroids_init()`. Levels 2 to 4 refuse before any MMIO unless `sweet_reboot_handsfree_ready()` says the reason is armed and read back, a panic resets into fastboot, and the deadman thread runs | `sweet_ufs.c:1319`, `sweet_reboot.c:555` and `:539`, `sweet_usb.c:2224` | "F1 boot order", "F1 recovery not armed" |
| 2 | Missing DMA proof passes | The CmdIOC event must be exactly one event at offset 0 of our buffer, endpoint 1, EPCMDCMPLT, status 0, command 2, with every other word still zero. Anything else stops the level, and levels 3 and 4 never reach `RUN_STOP` | `sweet_usb_gadget.h:117`, `sweet_usb.c:2334` | "F2" x3 |
| 3 | No single safe stop | Admission refuses anything but the Phase 0 state before any write. The DMA area is allocated before the first write. `ua_stop` ends every transfer, halts, and either verifies the stop or panics into the armed fastboot reset. Its log says whether the event buffer is ours, ABL's or UNVERIFIED. Level 3 passes only on a verified stop | `ua_admit` `:2187`, `:2252`, `ua_stop` `:1757`, panic `:1783` | "F3" x5 |
| 4 | ENDTRANSFER completion assumed | GUCTL2[14], the DWC_usb3 3.10a+ polling mode, is set and read back before anything can be ended. If it does not stick, the level stops. A failed ENDTRANSFER keeps the slot reserved and stops the gadget. A completion whose TRB the controller still owns is ignored as stale | `:2293`, `ua_end_transfer` `:1737`, `ua_trb_done` `:1993`, `:2036` | "F4" x3 |
| 5 | SET_INTERFACE overwrites a live notification | Selecting the same alternate again restarts nothing. The notification starts only when none is active. Alternate 0 and `SET_CONFIGURATION(0)` end the data transfers. In strict mode `ud_start_trb` refuses a slot that is not the endpoint's own or is still active. Requests are now checked for direction, recipient and state | `:1943`, `ua_data_down` `:1824`, `:508`, `sweet_usb_gadget.h:340` | "F5" x3, header checks |
| 6 | Command failures do not stop | A STARTTRANSFER failure stops the gadget and keeps its slot reserved (`:530`). The thread does the stop, never the TX path under `ud.lock`. A failed first SETUP stops before QSCRATCH and `RUN_STOP`. Configure, connect-done, SETSTALL and `RUN_STOP` failures all stop | `:2374`, `:2401`, `ua_configure` `:1878` | "F6" x5 |
| 7 | Host disappearance is not proof | `usb-check.py` level 3 needs macOS to have configured the device (an IOUSBHostInterface of 1209:0001) before the device leaves. `usb-verdict.py` needs both halves, including the kernel's `configured by the host`, `halted yes, transfers ended yes, event buffer ours (read back)` and `stop verified` lines | `host/usb-check.py`, `host/usb-verdict.py` | "L3 seen, never configured, then gone", verdict x5 |
| 8 | MMIO exposure understated | Level 2 no longer reads QSCRATCH, GUCTL1 or the FIFO sizes. Before the first access to each register group that no earlier boot touched, `ua_mmio_first` logs its address and syncs the log. See the table below | `ua_mmio_first` `:1625` | "F8" |

Also from the audit's assessment: the SMMU's sGFSR is now logged after the DMA proof and at stop. The probe reads the same register.

## MMIO exposure per level

"Read before" means the level 1 probe (2043, 2062) reads that exact register earlier in the same boot. Every other row gets an `ASTEROIDS USB MMIO: first access to ADDR+LEN` line, synced to the persistent log, before its first use.

| Level | Registers | Access | Read before? |
|---|---|---|---|
| 2 | DWC3 GSNPSID, GCTL, DCTL, DSTS, GEVNTADR/SIZ/COUNT | read | yes |
| 2 | SMMU id, SMR, S2CR, bank 3 SCTLR, sGFSR | read | yes |
| 2 | DEPCMD and parameters ep0 to ep5, 0x0a60c800+0x60 | read, write | no, announced |
| 2 | DCFG, DCTL, DEVTEN, DALEPENA, 0x0a60c700+0x24 | write | read only, announced |
| 2 | GUCTL2, 0x0a60c19c | read, write | no, announced |
| 2 | GEVNTADR/SIZ/COUNT, 0x0a60c400+0x10 | write | read only, announced |
| 3, 4 | QSCRATCH HS_PHY_CTRL, SS_PHY_CTRL, 0x0a6f8800+0x34 | read, write | no, announced |
| 3, 4 | eUSB2 PHY 0x3c to 0x94, diagnostics only | read | yes |

Level 2 therefore makes new accesses to three DWC3 register groups and new writes to two more. All of them are inside the DWC3 block, whose reads Phase 0 proved. Level 3 adds QSCRATCH.

A plain `ldr`/`str` avoids Gunyah's writeback aborts (plan-usb.md finding 1). It cannot stop a forbidden access from aborting, or a stalled one from hanging. An abort leaves the CPU running, so the panic reset brings the phone back to fastboot. A bus stall that freezes every CPU is not recoverable without a watchdog, and I did not enable `sweetgwdt` (unproven, audit finding 1). In that case the last log line names the register group, and the phone needs Power + Volume Down. The announcements only say where it happened.

## Levels

One boot-arg, `sweetusb=N`. The low nibble is the level, and +16 forces high speed. Every level first runs the existing level 1 probe and continues only when it completed. Levels 2 to 4 also need recovery armed, so they run only on images with `sweetfbarm=1`, `sweetpanicfb=N` and `sweetdeadman=N`. The loop images have all three. The daily `combo-keep` has no deadman, so it refuses level 4 until that rule is relaxed after level 4 passes.

| Arg | Level | What it does | How it ends |
|---|---|---|---|
| 1 | probe | Unchanged read-only probe | Returns |
| 2 | own | Readiness, admission, SMMU route check, DMA area, `DEVTEN` 0, GUCTL2[14], `GEVNTADR` to our buffer with readback, ep0 commands, the exact CmdIOC proof. No `RUN_STOP` | Halted. `L2: PASS owned` only with the proof |
| 3 | enumerate | Level 2, then the first SETUP, the VBUS override with readback, `RUN_STOP`, a vendor interface with no endpoints | A verified stop 20 s after `SET_CONFIGURATION` and `L3: PASS`, or a FAIL line |
| 4 | ECM | Level 2, then CDC-ECM and `ue0` with 192.168.7.2/24 and fe80::5eff:fe77:ee02 | Stays up, or stops with a FAIL line |
| 20 | ECM, high speed | Level 4 with `DCFG.DEVSPD` written to high speed before `RUN_STOP` | Fallback only |

### Order at level 2

1. Readiness, before any MMIO.
2. Admission, reads only. The core must be a DWC_usb3 in device mode with `RUN_STOP` 0 and `DEVCTRLHLT` 1. `GEVNTSIZ` must have its interrupt mask set, `GEVNTCOUNT` must be 0, and no endpoint may have a command in flight. Phase 0 read exactly this (proven, plan-usb.md Phase 0 result). Anything else is refused untouched.
3. SMMU route check, reads only, unchanged. SID 0x40 must match one SMR that passes addresses through.
4. DMA area: contiguous, permanent, below the SMMU output size, clear of ABL's buffer, with a canary page each side. Still no write.
5. First writes: `DEVTEN` 0, then GUCTL2[14] with readback, then `GEVNTADR`, `GEVNTSIZ` and `GEVNTCOUNT` with readback.
6. ep0 commands, the last one with CmdIOC. The exact proof.
7. `L2: PASS owned`.

Whether this core raises a command-complete event for SETTRANSFRESOURCE while halted is still unknown (inferred from the DWC3 databook's CmdIOC description). If it does not, level 2 fails closed with `CmdIOC proof: count 0 ... wrong event count`. That costs one boot, and the proof stage would need a redesign. The event's field layout is from Linux `core.h` (`struct dwc3_event_depevt`, `DEPEVT_PARAMETER_CMD`). GUCTL2 0xc19c bit 14 and its polling meaning are from Linux `core.h` and `gadget.c` (`dwc3_stop_active_transfer`, "As of IP version 3.10a"), read on 2026-11-03. The core is 3.30a (Phase 0 GSNPSID 5533330A).

### Speed

Unchanged from v1. The code keeps ABL's `DEVSPD` 4 and serves descriptors for whatever speed connect done reports. The audit notes this goes beyond the plan's high-speed-only gate. The Poco F1 linked at high speed with the same `DEVSPD` on a USB 2.0 port (`ios-asteroids/NOTES.md:986`). Whether this phone's port is wired for SuperSpeed is unknown. `sweetusb=20` forces high speed without a soft reset, and whether DWC3 takes that is unknown.

## Host side

This part is unchanged from v1 apart from F7.

- macOS gives each new USB Ethernet interface DHCP and automatic IPv6 by itself (proven for four earlier adapters in `/Library/Preferences/SystemConfiguration/preferences.plist`). So the ECM interface should get an fe80:: address with no sudo (inferred).
- The phone is always fe80::5eff:fe77:ee02, so `ssh root@fe80::5eff:fe77:ee02%enN` needs no setup.
- dropbear, the key and the launchd job are already on the root (proven, `research/root-build-gpu17.log:179-184`), and dropbear runs at boot (proven, `logs/ast100-ff-owner.txt:1313`). No root change is needed. `06-hfs-put.py` can replace those files, since they exist. New paths need a root rebuild.
- `usb-check.py` uses no sudo and never prompts. ssh runs with BatchMode, no host-key checking, `UserKnownHostsFile=/dev/null` and `~/.ssh/id_ed25519`, which has no passphrase.
- The level 3 configured check reads IOUSBHostInterface nodes with `ioreg -r -c IOUSBHostInterface -l`. I could not see such a node on this Mac tonight, because no USB device was attached. The key names `idVendor` and `idProduct` on interface nodes are inferred. If they differ, level 3's host half fails, and the kernel half still shows the truth.

## Desk checks run

All ran on 2026-11-03.

1. `python3 -I host/kernel-harness.py b2`: 24 of 24 tests pass, plus the F1 boot-order check.
2. `python3 -I host/kernel-harness.py b`, the audited v1. The 3 baselines pass, which shows the mock drives the real code. All 21 finding tests fail there, and so does the boot-order check. Each fails for its own finding, and the DMA model reports violations such as `ep5 STARTTRANSFER while its transfer is active` and `ep0 STARTTRANSFER while its transfer is still ending`.
3. `python3 -I host/test_usb_check.py`: all pass, 9 checker scenarios and 5 verdict cases. With `--checker` pointed at the v1 checker, "L3 seen, never configured, then gone" fails: v1 reports PASS.
4. `cc -std=c11 -Wall -Wextra -Werror -I work/b2/bsd/dev host/test_gadget.c -o /tmp/test_gadget && /tmp/test_gadget`: 13895 checks, 0 failed. New since v1: the CmdIOC proof forms, one slot per endpoint, and request validation.
5. `python3 -I host/syntax-check.py b2 FILE` for `sweet_usb.c`, `sweet_ufs.c` and `sweet_reboot.c`, and `m2 sweet_usb.c`: all exit 0. The diagnostics match the base files: 6, 77 and 9, all inherited.
6. The existing `plans/usb/host/check_failsafe.py`, from a /tmp copy with its patch step off, run on v2's `sweet_reboot.c`: "all scenarios pass", same as the base file.
7. `git apply` of both v2 patches onto copies of the three combined-tree files reproduces `work/b2` byte for byte.

The mock DWC3 is a model I wrote from the register descriptions this code uses. It shows that the control logic follows its rules. It shows nothing about the hardware.

## Boot-arg budget

Unchanged. The line is limited to 607 bytes. The loop images `usb-l2`, `usb-l3` and `usb-l4` are 548 bytes, and `usb-l4hs` is 549. `combo-keep` plus ` sweetusb=4` would be 597, but level 4 refuses there without the deadman.

## Test plan, three boots

Each level is one hands-free dev.py run. Run the next one only if the previous verdict is PASS. First, build the kernel with both v2 patches and run `plans/usb/gadget/build-usb-loop.sh KERNEL`. Copy `selftest/usb.sh` to `3a-IOS/tools/devloop/selftest/scripts/usb.sh` once.

Any failure in the gadget code now ends in fastboot without a button. A refusal or gate failure returns to an orderly boot, and the self-test then reboots into fastboot. A panic, including the one for a core that will not halt, resets into fastboot after 10 s. A hang in userland hits the deadman. The one exception is a bus stall that freezes every CPU, described above.

```sh
# Level 2, about 6 to 8 minutes
3a-IOS/tools/devloop/dev.py plans/usb/gadget/out/usb-l2-boot.img --test usb --note usb-l2 --key 'ASTEROIDS USB' --key 'SWEET USB'
python3 -I plans/usb/gadget/host/usb-verdict.py 2 ios-asteroids/logs/<run>-usb-l2.txt

# Level 3, about 8 minutes: the Mac checker first, in the background
mkdir -p plans/usb/gadget/runs
python3 -I plans/usb/gadget/host/usb-check.py 3 plans/usb/gadget/runs/l3.txt > /dev/null 2>&1 &
3a-IOS/tools/devloop/dev.py plans/usb/gadget/out/usb-l3-boot.img --test usb --note usb-l3 --key 'ASTEROIDS USB' --key 'SWEET USB'
python3 -I plans/usb/gadget/host/usb-verdict.py 3 ios-asteroids/logs/<run>-usb-l3.txt plans/usb/gadget/runs/l3.txt

# Level 4, about 8 minutes
python3 -I plans/usb/gadget/host/usb-check.py 4 plans/usb/gadget/runs/l4.txt > /dev/null 2>&1 &
3a-IOS/tools/devloop/dev.py plans/usb/gadget/out/usb-l4-boot.img --test usb --note usb-l4 --key 'ASTEROIDS USB' --key 'SWEET USB'
python3 -I plans/usb/gadget/host/usb-verdict.py 4 ios-asteroids/logs/<run>-usb-l4.txt plans/usb/gadget/runs/l4.txt
```

The `<run>` log name is the one dev.py prints, under `ios-asteroids/logs/`. The verdict line is `USBVERDICT L<n> PASS` or `FAIL <reason>`, and the exit code is 0 only on PASS.

What the verdict requires is in `usb-verdict.py`. In short:

- Level 2 needs `CmdIOC proof: count 4 ... proven` and `L2: PASS owned`.
- Level 3 needs the proof, `L3: configured by the host`, the `ASTEROIDS USB STOP: level 3 hold time over; halted yes, transfers ended yes, event buffer ours (read back) ... stop verified` line, `L3: PASS`, and `USBCHECK L3 PASS` from the Mac.
- Level 4 needs the proof, `L4: configured by the host`, `ue0 fe80::5eff:fe77:ee02 up`, `L4: up`, no `STOP:` line, and `USBCHECK L4 PASS`.
- Any `ASTEROIDS USB GATE:`, `L<n>: FAIL`, `stop NOT verified`, `did NOT halt`, `canaries HIT` or `panic(cpu` is a FAIL.

If level 3 fails with `no SET_CONFIGURATION` and 0 connects, read the `ASTEROIDS USB DIAG:` lines. In eUSB2 `COMMON0`, bit 0 is `PHY_ENABLE` and bit 2 is `SIDDQ`, the power-down (offsets from mainline `phy-snps-eusb2.c`). A PHY that ABL powered down needs a new patch.

## Skipped, add when needed

- No IPv4 on the Mac without sudo. Add a DHCP server on the phone if the live UDP log or Mac NAT is wanted.
- `CLEAR_FEATURE(ENDPOINT_HALT)` is accepted with no `CLEARSTALL`. Add it if macOS loses the first bulk packet.
- No reconnect after the level 3 stop. `ud.stopped` is final for the boot, and only a panic follows a failed stop.
- No hard-hang watchdog. Revisit after `sweetgwdt` has its own audited test.

## For the morning

Nothing needed sudo, approval or a system setting. Left to the owner by design:

1. A fresh Sol audit of the v2 patches.
2. Copy `selftest/usb.sh` into `3a-IOS/tools/devloop/selftest/scripts/`.
3. After a GO, build from a fresh copy of the combined tree, not either live tree. The kernel in `research/xnu-usb` was built from v1 and must be rebuilt.

## Levels 3 and 4 pass (2026-11-03)

The owner set System Settings > Privacy & Security > Allow accessories to connect to "Always". On "Ask for new accessories", macOS blocked the device with "device functionality blocked by transport restrictions" and never showed a prompt (ast142).

**Measured:**

- **Level 3, ast141:** the Mac enumerated and configured the device, and it left by itself 20 s later. Verdict: `USBVERDICT L3 PASS phone and Mac agree`.
- **Level 4, ast145:** `ssh root@fe80::5eff:fe77:ee02%en8` returned `uname` and `id` (uid 0). A 4 MiB random blob piped through `sha256sum` on the phone matched the Mac's hash. ping6 over the cable took 0.6 to 10 ms. Verdict: `USBVERDICT L4 PASS phone and Mac agree`.

Two checker bugs, both fixed in `tools/usb/host/usb-check.py`, with tests in `test_usb_check.py`:

1. macOS gave the phone's ECM interface the address `ether 02:00:00:00:00:00`, not the `iMACAddress` 02:00:5e:77:ee:01 that the gadget sends (ast143). The checker now also accepts the single Ethernet interface that did not exist when it started. The interface came up as en8 with link active and an IPv6 link-local address.
2. The kernel brings the link up about 15 s into the boot, but dropbear starts with launchd about a minute later (ast144: refused). The checker now waits up to 150 s for port 22.

**Daily kernel:** 2160 v4 and 2161 v4 apply cleanly on the daily tree (`research/xnu-combined`, with 2130 v4, 2140 to 2144 and 2150 to 2154). The kernel is `next-combined/kernel-daily-usb` (sha256 565dfd76...). The image is `combo-ub-usb` from `tools/boot/build-combined.sh`, which is combo-ub plus `sweetusb=4`, 605 of 607 bytes. Sol's audit 3 covers one isolated test boot only, so a daily-use audit (`docs/usb-gadget-audit4.md`) comes before the owner gets it.
