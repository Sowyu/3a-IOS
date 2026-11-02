# USB gadget for SSH from the Mac: patches 2160 and 2161, v2

2026-11-03. Desk work only. I ran no adb, fastboot or dev.py, built no kernel, wrote nothing in either live kernel tree, and committed nothing. Nothing here has run on a phone.

v2 answers Sol's audit, `analysis/usb-gadget-audit.md`, which found levels 2, 3 and 4 NO-GO. The audited v1 patches stay here unchanged for diffing.

Labels: **proven** means read in code, a log or a file, with the place named, or established by a desk run that is named. **Inferred** means reasoning with no run behind it. Line numbers in `sweet_usb.c`, `sweet_ufs.c`, `sweet_reboot.c` and `sweet_usb_gadget.h` refer to `work/b2/bsd/dev/`, the files after both v2 patches.

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
