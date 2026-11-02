# USB gadget for SSH from the Mac: patches 2160 and 2161

2026-11-03. Desk work only. I ran no adb, fastboot or dev.py, built no kernel, wrote nothing in either live kernel tree, and committed nothing. Nothing here has run on a phone.

Labels: **proven** means read in code, a log or a file, with the place named. **Inferred** means reasoning with no run behind it. Line numbers in `sweet_usb.c` refer to `work/b/bsd/dev/sweet_usb.c`, the file after both patches.

## Files

- `2160-asteroids-usb-gadget-shared.patch` adds `bsd/dev/sweet_usb_gadget.h` and reworks the shared Poco F1 plumbing. It changes nothing on asteroids by itself.
- `2161-asteroids-usb-gadget-levels.patch` adds levels 2 to 4 behind `sweetusb`. With `sweetusb` unset, 0 or 1 it behaves as before.
- `host/test_gadget.c` is the host test for the header.
- `host/syntax-check.py` compiles the patched file with the combined build's own flags.
- `host/usb-check.py` is the Mac side of a level 3 or 4 run, and `host/test_usb_check.py` is its mock test.
- `host/make-patches.py` regenerates both patches from `work/`.
- `build-usb-loop.sh KERNEL` makes the hands-free images `usb-l2`, `usb-l3`, `usb-l4` and `usb-l4hs` in `out/`.
- `selftest/usb.sh` is the dev.py self-test body.
- `SHA256SUMS` holds the hashes.

Both patches apply with `git apply`, which is what `dxb/build.sh:473` uses, on top of the combined kernel's `sweet_usb.c` (SHA256 4cd078e8..., identical in `xnu-local` and `xnu-combined`). No other patch in the series touches these two files, so their place after 2153 is safe. I checked that by applying both to a copy and comparing the result with `work/b` byte for byte.

## Levels

One boot-arg, `sweetusb=N`. The low nibble is the level, and +16 forces high speed. Every level first runs the existing level 1 probe (2043, 2062), and continues only when that probe completed.

| Arg | Level | What it does | How it ends |
|---|---|---|---|
| 1 | probe | Unchanged read-only probe | Returns |
| 2 | own | Gates, our DMA area, `GEVNTADR` moved to our buffer, ep0 commands, one CmdIOC event as a DMA proof. No `RUN_STOP` | Halted, logs `L2: PASS` or `FAIL` |
| 3 | enumerate | Level 2, then VBUS override, `RUN_STOP`, a vendor interface with no endpoints, so only ep0 moves data | Soft-disconnects 20 s after `SET_CONFIGURATION`, or after 120 s without it |
| 4 | ECM | Level 2, then the Poco F1's CDC-ECM function and `ue0` with 192.168.7.2/24 and fe80::5eff:fe77:ee02 | Stays up for the boot |
| 20 | ECM, high speed | Level 4 with `DCFG.DEVSPD` written to high speed before `RUN_STOP` | Fallback only |

The levels follow what the hardware needs. Level 2 is everything that must be right before the controller may touch the bus, and it can be tested with the core halted. Level 3 adds the link and ep0 DMA only. Level 4 adds bulk DMA and the network stack.

## The bring-up, step by step

1. Gates before any write, `ua_gadget` at `sweet_usb.c:1992`. The core must read as a DWC_usb3 (GSNPSID 0x5533xxxx) in device mode (GCTL PRTCAPDIR 2). The Phase 0 photo read 5533330A and GCTL 00102001 (proven, `3a-IOS/docs/plan-usb.md`, Phase 0 result).
2. SMMU gate, `ua_smmu_through` at `sweet_usb.c:1629`. SID 0x40 must match exactly one SMR, and that SMR must pass addresses through, either as S2CR bypass or as translation into a bank with SCTLR.M 0. Phase 0 saw SMR3 80000040, S2CR 3, bank 3 SCTLR E0, so M is 0 (proven, same table). The code never writes the SMMU. It also reads the SMMU output size (IDR2 OAS, 36 bits from IDR2 00005111), and the DMA area must lie below it.
3. Halt, `ua_halt` at `sweet_usb.c:1672`. `DEVTEN` 0, `RUN_STOP` cleared, wait up to 500 ms for `DEVCTRLHLT`. Phase 0 found it already halted (proven), so this should be a no-op write. The plan said to zero `GEVNTADR` here. I replace it with our own buffer instead, because a zero address would send a stray event to physical 0.
4. DMA area, `sweet_usb.c:2037`. `kmem_alloc_contig` with `KMA_PERMANENT`, so it is never freed. It is one canary page, the 16 KiB area, then another canary page. Every page's physical address is checked for contiguity, the end must lie below the SMMU output size, and the area must not overlap ABL's buffer. The level also logs whether ABL's 0xD017B000 lies inside the RAM our kernel manages (`gPhysBase`), which says how bad a stale ABL write would have been.
5. `GEVNTADR`, `GEVNTSIZ` (interrupt masked) and `GEVNTCOUNT` are programmed at `sweet_usb.c:2076` and read back. A mismatch stops. This happens long before `RUN_STOP`, as asked.
6. ep0 commands, then `SETTRANSFRESOURCE` on ep1 with CmdIOC at `sweet_usb.c:2094`. If the controller counts an event, it must be in our buffer. If the count is non-zero and our buffer is all zero, the controller wrote somewhere else, and the level stops. If no event comes in 100 ms, the level logs that the DMA write is not proven and goes on. I could not confirm from local sources that CmdIOC raises an event for this command (inferred from the DWC3 databook's description of CmdIOC). Level 3's SETUP packet proves the DMA write either way.
7. Level 3 and 4 only: address 0, `DEVTEN` 7, the ep0 SETUP TRB, then the VBUS override in QSCRATCH (`sweet_usb.c:2158`), then `RUN_STOP` (`sweet_usb.c:2162`). The override is the Poco F1's (proven to be the fix there, `ios-asteroids/NOTES.md:1014`, patch 0074). Mainline does the same on eUSB2 boards: `dwc3-qcom.c` sets `UTMI_OTG_VBUS_VALID | SW_SESSVLD_SEL` and `LANE0_PWR_PRESENT`, and `phy-snps-eusb2.c` never sets the PHY's own `VBUSVLDEXT0` (proven, mainline master read on 2026-11-03).
8. A polling thread, `ua_thread` at `sweet_usb.c:1903`, handles events every 1 ms like the Poco F1's. Every second it checks both canary pages.

### Speed

ABL left `DCFG.DEVSPD` at 4, SuperSpeed (proven, DCFG 0048080C). On the Poco F1 the same value with a USB 2.0 port linked at high speed and worked (`NOTES.md:986`). The DT says this phone's controller has a QMP combo PHY and `maximum-speed = "super-speed"` (`research/asteroids-hw/asteroids.dts:11494`, `:11529`), but whether the port is wired for SuperSpeed is unknown. So by default the code keeps ABL's speed and serves descriptors for whatever speed `DSTS` reports at connect done. High speed gets the Poco F1's USB 2.00 descriptors. SuperSpeed gets bcdUSB 3.00, ep0 512, 1024-byte bulk endpoints with companion descriptors, and a BOS. `sweetusb=20` writes `DEVSPD` 0 before `RUN_STOP`, without a soft reset. Whether DWC3 takes that without a soft reset is unknown. The Poco F1 notes claim it does not (`NOTES.md:986`, not tested there either).

### Teardown

`ua_stop` at `sweet_usb.c:1683` runs on any failed gate after the first write, at level 3's end, and on a canary hit, a bad event count or a refused TRB. It marks the gadget stopped so no transfer can start, ends every active transfer, sets `DEVTEN` to 0, clears `RUN_STOP` and waits for the halt, disables the endpoints, restores QSCRATCH, posts link-down, checks the canaries and logs one `ASTEROIDS USB STOP:` line. `GEVNTADR` stays on our buffer. The area is never freed, so a late write from the controller lands in memory we own.

### What 2160 changes in the shared code

- Every TRB, the setup packet and the notification get their own 64-byte line (`sweet_usb_gadget.h:20`). Before, four TRBs shared one line, and the setup packet shared one with the interrupt TRB. With non-coherent DMA, cleaning a TRB the CPU just wrote could write back a stale neighbour the controller had updated. The DT marks the controller `dma-coherent` (`asteroids.dts:11480`), but that is unproven through bank 3, so the code still cleans and invalidates (plan-usb.md Phase 3 step 3).
- DWC3 reads and writes are one plain `ldr`/`str` each (`sweet_usb.c:437`), because Gunyah cannot emulate an access with writeback (proven, plan-usb.md finding 1).
- `ud_start_trb` refuses a TRB that is not one of the five slots, or a buffer not inside one buffer region (`sweet_usb.c:497`). On asteroids a refusal stops the gadget.
- The TX pad uses the bulk max packet (`sweet_usb.c:792`), which is needed at SuperSpeed.

These also change the Poco F1 and sweet builds. I did not test them there.

## DMA gates against plan-usb.md Phase 3

| Plan gate | Where |
|---|---|
| Halt before any route or buffer change | Step 3 runs before the DMA area exists |
| Route checked, fault reporting visible | Read-only route check. No SMR is added, because SID 0x40 already has one, and there is no sGFSR logging |
| Dedicated pool, addresses checked before every register write | `GEVNTADR` through `ug_pa_ok`, every TRB and buffer through `ud_start_trb`'s gate |
| Canary page each side, checked after each stage | After the event test, every second while running, at stop |
| Explicit `dc civac` | `sweet_dma_sync_va` on every TRB, buffer, event read and canary check |
| High speed only, no PHY or repeater reset | No PHY, repeater or SMMU write anywhere. Speed differs: see "Speed" above |

What the gates cannot stop: a controller that ignores the new `GEVNTADR` and writes one more event to ABL's 0xD017B000. The level 2 event test detects that case and stops, but by then 4 bytes have been written there. The Poco F1 moved its event buffer the same way without a reset and got its events (proven, `NOTES.md:1014`). Stage 2 limits stray DMA to our VM's memory (inferred, plan-usb.md).

## Host side

### What the Mac does on its own

This Mac creates a network service with DHCP and IPv6 Automatic for every USB Ethernet interface it has seen (proven: `/Library/Preferences/SystemConfiguration/preferences.plist` has DHCP/Automatic services for `en7` "USB 10/100/1G/2.5G LAN" and `en3` to `en5`, all with default names). So the phone's ECM interface gets an fe80:: address without sudo (inferred, because this Mac has never seen the phone's interface). No DHCP server answers, so its IPv4 ends up self-assigned in 169.254/16 and is not used.

macOS has a built-in CDC-ECM driver that matches these classes (inferred, `ios-asteroids/analysis/usb-plan.md`). The Poco F1's ECM ran against a Linux host (`NOTES.md:1012`). This Mac has never recorded an interface with the host MAC 02:00:5e:77:ee:01, so the Mac side is unproven.

### Addresses

The phone side is fixed. `ua_if_inet6` (`sweet_usb.c:1967`) gives `ue0` fe80::5eff:fe77:ee02 through the same ioctls ifconfig uses. The IPv4 192.168.7.2/24 from the Poco F1 code stays. From the Mac, with no setup:

```sh
ifconfig -a | grep -B4 'ether 02:00:5e:77:ee:01'          # gives enN
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i ~/.ssh/id_ed25519 root@fe80::5eff:fe77:ee02%enN
nc -z fe80::5eff:fe77:ee02%enN 22
```

IPv4 to 192.168.7.2 would need 192.168.7.1 on the Mac, which needs sudo. The live UDP log (`sweetusblog`, Poco F1 patch 0077) goes to 192.168.7.1:5140, so it does not reach the Mac until IPv4 is set up. I skipped a DHCP server on the phone. It is only needed for IPv4, and it would be a new root binary.

### SSH server on the phone

No root change is needed.

- dropbear, dash as /bin/sh, toybox, `authorized_keys`, the host keys and the launchd job `org.sweet.dropbear` (`-p 22`, key only) come from `root_tweaks18.py` (`ssh()` at line 433). The current root build installed them (proven, `research/root-build-gpu17.log:179-184`). It took the key from `~/.ssh/id_ed25519.pub`, which has no passphrase and matches its private key.
- dropbear runs on the phone today (proven, `logs/ast100-ff-owner.txt:1313` and `:1416`, one exec, no restart).
- `-p 22` listens on IPv4 and IPv6 (inferred from the builder's comment at `root_tweaks18.py:525`, not seen on this phone).
- That the phone's root is the gpu17 build or a later one with the same key is inferred.

To change any of these, `06-hfs-put.py` can replace them all, since they exist: `/private/var/root/.ssh/authorized_keys`, `/usr/local/bin/dropbear`, `/System/Library/LaunchDaemons/org.sweet.dropbear.plist`. A new path, such as a DHCP server or a new launchd job, needs a root rebuild.

## Desk checks run

All ran on 2026-11-03 and all passed.

1. `python3 -I host/syntax-check.py b` and `... m`: both compile with the combined build's flags from `sweet_usb.o.json` (exit 0). Each shows the same 6 warnings as the untouched file, all in old Poco F1 code. A full `-c` compile to /tmp also passed. `gPhysBase`, `gPhysSize`, `sock_ioctl` and `pmap_find_phys` are in the built kernel's symbol table.
2. `cc -std=c11 -Wall -Wextra -Werror -I work/b/bsd/dev host/test_gadget.c -o /tmp/test_gadget && /tmp/test_gadget`: 13856 checks, 0 failed. It covers the cache-line layout, the address gates with wrap cases, the TRB words, and every descriptor at both levels and three speeds against the spec rules (lengths, interface and endpoint counts, companions only at SuperSpeed, max packets, BOS, strings). It also covers the whole ep0 request table, that no IN reply ever needs a zero-length packet, TX padding and RX length for every frame size at 512 and 1024, 100000 batches of event-ring arithmetic, and the canary check.
3. `python3 -I host/test_usb_check.py`: 8 of 8 scenarios. The Mac checker never calls sudo or networksetup, and every ssh call carries BatchMode, no host-key checking and the key.
4. `git apply` of both patches onto a copy of the combined `sweet_usb.c` gives `work/b` byte for byte.

The ep0 state machine, the network glue and the connect-done handling are the Poco F1's, proven there. They are not host-tested here.

## Boot-arg budget

The line is limited to 607 bytes.

| Image | Bytes |
|---|---|
| `combo-keep` today | 586 |
| `combo-keep` + ` sweetusb=4` | 597 |
| Loop base (`build-usb-loop.sh`, like `build-input-loop.sh`) | 537 |
| `usb-l2`, `usb-l3`, `usb-l4` | 548 |
| `usb-l4hs` (`sweetusb=20`) | 549 |

All other knobs have defaults, so no other arg is needed. `sweetusblog=0` would add 12 bytes, which fits the loop images but not combo-keep.

## Test plan, three boots

Each level is one hands-free dev.py run. Run the next one only if the previous one passed. Each needs the kernel built with 2160 and 2161 and then `plans/usb/gadget/build-usb-loop.sh KERNEL`. Copy `selftest/usb.sh` to `3a-IOS/tools/devloop/selftest/scripts/usb.sh` once. I did not put it there, since that directory belongs to the devloop tool.

Every image carries `sweetfbarm=1`, `sweetpanicfb=10`, `sweetdeadman=600` and `sweetselftest=1`. A panic, a gate failure, a hang in userland or the normal end all lead back to fastboot without a button. The self-test always ends with `sweetreboot fastboot`, whatever the USB result. Only a hard bus hang needs Power + Volume Down. The one new MMIO block is QSCRATCH, which no run has read yet. It is read at level 2, after a log sync, with every other read going to a block the level 1 probe reads earlier in the same boot.

Run them one at a time, from the EverIOS root. The Mac checker has no `devloop/dev.py` in its command line, so it cannot block `run-arms.sh`'s pgrep.

```sh
# Level 2, about 6 to 8 minutes
3a-IOS/tools/devloop/dev.py plans/usb/gadget/out/usb-l2-boot.img --test usb --note usb-l2 --key 'ASTEROIDS USB' --key 'SWEET USB'

# Level 3, about 8 minutes: the Mac checker first, in the background
mkdir -p plans/usb/gadget/runs
python3 -I plans/usb/gadget/host/usb-check.py 3 plans/usb/gadget/runs/l3.txt > /dev/null 2>&1 &
3a-IOS/tools/devloop/dev.py plans/usb/gadget/out/usb-l3-boot.img --test usb --note usb-l3 --key 'ASTEROIDS USB' --key 'SWEET USB'

# Level 4, about 8 minutes
python3 -I plans/usb/gadget/host/usb-check.py 4 plans/usb/gadget/runs/l4.txt > /dev/null 2>&1 &
3a-IOS/tools/devloop/dev.py plans/usb/gadget/out/usb-l4-boot.img --test usb --note usb-l4 --key 'ASTEROIDS USB' --key 'SWEET USB'
```

The checker waits up to 40 minutes for the gadget, which covers dev.py's recovery push before the boot, then exits on its own. Its last line is the verdict.

### Pass and fail lines

| Level | Pass, all of these | Fail, any of these |
|---|---|---|
| 2 | `ASTEROIDS USB L2: PASS`. Best case it says `event DMA proven`; `not proven` still passes | `ASTEROIDS USB L2: FAIL`, `ASTEROIDS USB GATE:`, `ASTEROIDS USB STOP:`, `panic(cpu` |
| 3 | `ASTEROIDS USB L3: PASS enumerated`; `ASTEROIDS USB STOP: level 3 hold time over; halted yes` ending `canaries intact`; `USBCHECK L3 PASS` in `runs/l3.txt` | `ASTEROIDS USB L3: FAIL`, `ASTEROIDS USB GATE:`, `canaries HIT`, `USBCHECK L3 FAIL` |
| 4 | `SWEET USB: ue0 fe80::5eff:fe77:ee02 up`; `ASTEROIDS USB L4: up`; `USBCHECK L4 PASS` in `runs/l4.txt` | `ASTEROIDS USB STOP:`, `ASTEROIDS USB GATE:`, `ASTEROIDS USB L4: FAIL`, `USBCHECK L4 FAIL`, `no SET_CONFIGURATION in 120 s` |

Level 2 also logs facts worth reading even on a pass: the QSCRATCH VBUS bits ABL left, whether ABL's buffer is inside our RAM, and the TX FIFO sizes.

### When it fails

- At level 3 with `FAIL no SET_CONFIGURATION` and 0 connects, the host never saw a pull-up. Read the `ASTEROIDS USB DIAG:` lines. In the eUSB2 `COMMON0`, bit 0 is `PHY_ENABLE` and bit 2 is `SIDDQ`, the power-down. `UTMI_CTRL5` bit 1 is `POR`. `HS_PHY_CTRL2` bit 2 is `USB2_SUSPEND_N` (offsets from mainline `phy-snps-eusb2.c`). A PHY that ABL powered down is the Poco F1's failure in eUSB2 form, and the fix would be a new patch with mainline's init order. The repeater's enable bit (`EUSB2_EN_CTL1` 0x46 bit 7 on PMIV0104) is not read. Add that read if the PHY looks alive.
- With connects but no configuration at speed 4, try `usb-l4hs`.
- At level 4, a `USBCHECK L4 FAIL interface` with an interface but no fe80 address means macOS did not configure it. That needs one owner action in the morning, not overnight.

Levels 3 and 4 could be merged to save a boot, but that gives up testing the link and the disconnect with only ep0 DMA in play.

## Skipped, add when needed

- No IPv4 on the Mac without sudo: add a DHCP server on the phone if the live UDP log or Mac NAT is wanted.
- `CLEAR_FEATURE(ENDPOINT_HALT)` is accepted with no `CLEARSTALL`, as on the Poco F1. Add it if macOS loses the first bulk packet after its driver starts.
- No reconnect after a level 3 disconnect, and no cable replug handling beyond the Poco F1's bus reset path.
- No full-speed ECM configuration. It stalls with a log line.
- No reads of the repeater or QMP PHY. Add them if level 3 fails.

## For the morning

Nothing needed sudo, approval or a system setting. Two steps are left to the owner by design.

1. Copy `selftest/usb.sh` into `3a-IOS/tools/devloop/selftest/scripts/`.
2. Build the kernel with 2160 and 2161 after a Sol audit. Use a fresh copy of the combined tree, not either live tree.
