# Power and heat plan

Written 2026-11-02 from code, logs and the device tree. Nothing was run on the phone. Every prepared patch is **untested**.

Labels used below:

- **Proven** means a log, a user report on this phone, or code I read shows it.
- **Rootstock** means their logs on this exact phone show it (`ref/UniXNU-asteroids/porting.toml`), but our kernel hasn't run it.
- **Inferred** means my reasoning with no run behind it.

Patches and their description are in `plans/power/README.md`. Test boot-args are in `plans/power/boot-args.md`.

## Where things stand

| Area | State | Label |
|---|---|---|
| CPU DVFS | Our EPSS governor exists and ran on this phone. Fixed clocks are a boot-arg choice, not a missing driver. | Proven: `sweet_cpufreq.c`, `analysis/cpu-load.md` time-in-state |
| CPU firmware limits | EPSS has DCVSH interrupts in the DT. We don't take them. Whether firmware limits us at all without Linux is unknown. | Unknown |
| Temperature | Nothing in our kernel reads a sensor or throttles. Only the DT's numbers exist. | Proven by grep |
| GPU clock | One vote at boot, level 6 (763 MHz), held for the whole boot. | Proven: `sweet_a810.c:754` |
| GPU level changes | HFI votes at run time work on the same firmware, with a governor on by default. | Rootstock T39, T40c, T40e |
| Battery percent | The fake source is `publish(100, 4200, 0, true, true, false, true, false, false)`, logged as `static fallback, 100% 4200mV external=1 charging=0`. | Proven: `SweetBattery.cpp:369`, our logs |
| ADSP and charging | Our kernel never starts the ADSP. Whether the phone charges with it down has never been measured. | Unknown |
| Battery readings via ADSP | Rootstock's client is written for this phone. I checked its owner 32778, opcodes 0x04, 0x07, 0x30, 0x32 and property numbers against Nothing's `qti_battery_charger.c`, and they match. | Code match, Rootstock T38 |
| Screen off and on | The kernel has it, and it worked once with rc 0. **The current `exp-boot` base line sets `sweetpanel=0`, which turns it off.** | Proven: `ast4-gpu4`, `exp-boot/build.sh` |
| Brightness | Worked on `ast4-gpu4` with the backlight service, default off since a `push_surface` abort. That abort is in the CPU renderer's code path, which backboardd has left. | Proven, then inferred |
| Idle | Plain WFI only. `wfi=1` and no PSCI CPU_SUSPEND anywhere. | Proven: `osfmk/arm64/cpu.c:588` |
| Suspend | None. Only keys and touch polling threads and iOS timers run, so nothing sleeps deeply. | Proven: `sweet_keys.c:106` |
| Power off | Goes to fastboot on purpose, because a real power cycle boots Android, which formats the iOS root. | Proven: `sweet_reboot.c:339` |

## DCS commands checked against our device tree

The boot cmdline selects `qcom,mdss_dsi_rm69220_fhd_plus_120hz_vid_boe`, so this phone's panel is the **BOE** variant. Our driver's comment and rootstock's notes both say Visionox, and that label is wrong for this phone. I decoded that node at `asteroids.dts:22388`.

| Command | T37 and our driver | BOE node in the DT | Result |
|---|---|---|---|
| Brightness | `51 hi lo`, high byte first, 10 to 4095 | `bl-inverted-dbv`, `bl_ctrl_dcs`, min 0x0a, max 0xfff, `51 0d bb` at init | Match |
| Off | `28`, wait 120 ms, `10`, wait 100 ms | `28` delay 100 ms, `10` delay 100 ms, LP mode | Match. 120 ms is the Visionox delay and is harmless. Ours is HS mode, which worked on the phone |
| On | `11`, wait 80 ms, `29`, wait 10 ms | Last two of 143 `vid-on-commands`: `11` delay 80, `29` delay 10 | Match |

Sleep out without re-sending the other 141 commands worked on `ast4-gpu4` and at T37d, so the panel keeps its setup across sleep in. No change is needed here.

## Battery life estimates

All of these are inferred, with no measurement behind them, and could be off by a factor of two. They assume about 19 Wh, from the phone's public 5000 mAh rating. `SweetBattery`'s 5020 mAh is the Poco F1's figure.

| Stage | Setup | Screen on, light use | Screen off |
|---|---|---|---|
| Today | Fixed clocks, GPU at 763 MHz, `sweetpanel=0`, panel lit black | 2.5 to 4 h (5 to 8 W) | 5 to 8 h (2.5 to 4 W) |
| After phases 1 and 2 | Governor with floor, panel asleep | 4 to 6 h (3 to 5 W) | 11 to 19 h (1 to 1.8 W) |
| After phase 4 | GPU idle level, slower polling with the screen off, crash loops gone | 5 to 7 h (2.5 to 4 W) | 19 to 38 h (0.5 to 1 W) |
| After phase 6, if PSCI idle works | Core and cluster power collapse | About the same | 30 to 75 h (0.25 to 0.6 W) |
| Real suspend | Display engine off, DDR self-refresh, CX collapse | About the same | 5 to 15 days, and I don't expect to reach this |

The screen-off column is what limits daily use. It needs the battery readout before any of it can be measured.

## Phases

Time is agent time, not counting a boot and test by the owner.

### Phase 0: measure what exists, about 1 hour of owner time

Depends on nothing and has low risk.

1. Boot base args with `sweetpanel=1` (variant A), no kernel change.
2. Run the zero-code test from `analysis/daily-driver-gaps.md`. Read `/sys/class/power_supply/battery/{capacity,status,current_now,temp}` in recovery before and after a timed iOS session on a charger, then a timed session unplugged.

Verify: the screen goes dark on Power. Recovery percent shows charging and drain rate, which fills in the estimates above. Stop if the phone feels hot.

### Phase 1: heat safety, 1 to 2 hours once built

Depends on nothing. Do this first of the code work because fixed clocks currently run with no protection.

1. Add patch 2070, and 2071 once the base is stable. Build through the usual path.
2. Boot variant C (`sweetthermal=1`, log only) and check readings on the phone.
3. Run load until the hottest sensor stops rising, noting the temperature against how the back of the phone feels.
4. Set the limits from that. Then boot with `sweetthermal=2`, then the default `3`.
5. Force the shutdown path once with `sweetthermal_soft=50 sweetthermal_hard=115` and confirm a clean mount on the next boot.

Verify: `SWEET THERMAL:` lines, `kern.sweet_thermal` over SSH if USB exists, clock drops in `SWEET CPUFREQ: cluster... thermal ceiling`, and frame rate drop under forced load. `sweetthermal_on=50` tests throttling without real heat.

Risks:

- **A TSENS read could raise an SError**, as stage 12's bad pool address did under Gunyah. Android's Linux reads these same windows in the same VM (`iomem.txt`), so I think it's unlikely. The first read is 20 s after start with a log sync before it, and `sweetthermal=0` undoes it by boot-arg alone.
- **Reading wrong sensors or a wrong scale.** The layout comes from Linux `tsens-v2.c`, and mainline `milos.dtsi` agrees the first window is TM. Variant C finds this before it can act.
- **The clean shutdown path may panic.** Sheng logged a `vnode_rele_ext` panic in a low-battery shutdown. The panic path still goes to fastboot, and the 30 s timer covers a stall.
- **The guard ends in fastboot with the screen on, not powered off.** It stops the heat, but a phone left there drains flat, and the next power-on boots Android. Real power-off waits for the standalone boot work.
- **A kernel hang stops the guard too.** A hard hang at high clocks has only whatever firmware protection exists. I found no proof of the hardware's cutoff temperature. The DT's "reset-mon" trip at 115 C is only a name.

### Phase 2: stop wasting power when idle, half a day

Depends on phase 1, so lower clocks don't arrive before heat is watched.

1. Add 2072. Boot variant D, tuning the floors for smoothness and heat.
2. Compare against fixed clocks with the same five-minute swipe sequence from `analysis/cpu-load.md`, using frame rate and hottest temperature.
3. Retest brightness with variant E. If it holds, make `sweetpanel=2` the default.

Verify: time-in-state falls to the bottom states when the screen is off, and frame rate is within 10% of fixed.

Risks: clock writes are low risk. We write only a LUT index, EPSS firmware handles voltage, and our governor has made these writes on this phone in every `gov` session (Proven). Rootstock T75f found that a high floor helps gaps between animations (11.2 to 1.6 ms) with no help for the 8 ms under load, so don't expect this to fix the lag. The brightness risk is the old `push_surface` abort, which reboots via launchd. It's undone by boot-arg.

### Phase 3: charging and real battery, 1 to 2 weeks

Depends on phase 0's answer, and shares the ADSP start with audio and sensors.

1. Boot with 2073 (`sweetpowersurvey=1`, about 1 hour to read the output). Watch raw VBAT and IBAT with a charger on and off. This answers whether it charges under iOS without a recovery round trip.
2. If charging works with the ADSP down, a voltage-only percent is a useful stopgap. `SweetBattery::estimatePercent` already exists, but its curve is generic and sag-prone. Keep it marked estimated.
3. Port the ADSP start from `AdspProbe.cpp` and `AdspFirmware.c`, with `QcomSmem`, `QcomSmp2p`, `QcomGlink`, `QcomMdt` and the PAS calls we already have. Firmware comes from `modem_a` on LUN 4 through `sweet_ufs_lun_io`.
4. Port `ShengBattmgr.c` unchanged. It's clean C with no kernel calls, so it can be host-tested.
5. In `SweetBattery.cpp`, add an asteroids path that feeds `publish()` from the snapshot. Replace the Poco's 5020 constants with the gauge's own values, and report real temperature in place of the fixed 25 C. Keep the existing `publish` keys. iOS 26 boots with them, but I did not audit how powerd treats real values.
6. Add battery temperature to the guard. Above 50 C while charging, lower the charge limit through property 10 (`BATT_CHG_CTRL_LIM`) with `BC_BATTERY_STATUS_SET`.
7. Add a low-battery rule to the guard: a clean shutdown at or below 3% unplugged. Also add a VBAT floor that caps clocks, because of the brownout risk below.

Verify: percent matches recovery's reading within a few points, the status bar follows plug and unplug at once (Rootstock T38d did), and charge current reads positive.

Risks:

- **Starting the ADSP changes charging behaviour**, since its firmware then runs the charger. Rootstock ran it this way, on a phone that kept working, but I saw no record of a full charge cycle with temperature measured. Watch the first charge.
- **A boot failure from a wrong carve-out** is likely to show as an SError, and it's recoverable by boot-arg.
- **Brownout at low battery.** With high clocks and no BCL handling, a voltage droop could reset the PMIC. A reset then boots Android, which formats the root. On Android, BCL zones cut CPU and GPU when voltage drops, and nothing here does. This risk stays until step 7 and the standalone boot work are done. Don't run the battery low until then.
- **Sheng's low-battery shutdown panic** may repeat here, as in phase 1.

### Phase 4: GPU clock management and quieter idle, 2 to 4 days

Depends on phase 3 for measuring, and on the GPU hang work so results aren't confused.

1. Keep it off by default. Port `gpuDcvsThread` from `ref/UniXNU-asteroids/package/GpuPowerProbe.cpp`, starting with only its idle rule: level 1 (264 MHz) after 80 ms with no submissions, and back to level 6 on work. Our header already has the XOCLK busy counter, and `a810_ring.seqno` can stand in for the submission count.
2. Reuse the try-lock vote from 2071. Add a full governor only if measurements justify it. Rootstock's bound is floor 6 and ceiling 7, since T65-1050 changed nothing felt at a higher level.
3. With the display off, stop the vsync timer's work, slow the keys thread from 20 ms to 100 ms, and stop the touch poll. These are in `SweetFramebuffer.cpp:1032`, `sweet_keys.c:106` and `sweet_touch_asteroids.c`.
4. Stop the daemon crash loops (`bluetoothd`, `wifip2pd`, `fairplayd.H2`, about 8 execs per 10 s). This likely saves more screen-off power than step 1. It belongs to the stability area.

Verify: screen-off current from the gauge, GPU `RECOVERY` count no worse than fixed level, and a power press wakes within about 300 ms.

Risks: a lower GPU clock lengthens jobs, and a hang recovery uses a timeout, so a slow job could be mistaken for a hang (`analysis/freeze.md` records that concern for 2050). Blocking HFI votes hold the GPU lock for up to 100 ms. A press shorter than 100 ms could be missed while polling slowly, so wake a faster poll on any activity, or take the PON interrupt (PDC pin 1, SPI 481, per rootstock).

### Phase 5: iOS-side thermal behaviour, 1 to 2 days, optional

Depends on phase 1. iOS doesn't know about any of the throttling. Find what `thermalmonitord` reads here and publish a level, so apps and the UI back off and the "too hot" screen can show. I found no notes on it, and the iOS 26 interface is unknown. Skip this until the rest holds.

### Phase 6: real idle states, 2 to 4 weeks, uncertain

Depends on phase 3, since it can't be judged without a current reading.

1. Probe first. Call PSCI `PSCI_FEATURES` for CPU_SUSPEND and log the answer. This is a small read-only step.
2. Try powering down CPU 7 alone while the screen is off, using XNU's CPU offline path and PSCI CPU_OFF. `SweetCPU.cpp` already has CPU_ON.
3. Then try CPU_SUSPEND with the DT's parameters (0x40000003 for core, 0x40000004 for rail, 0x41000044 for cluster) from `cpu_idle`. Resume needs XNU's reset entry, plus timer, GIC redistributor and PMU state restored. `IOCPU` and `start.s` reset paths exist for Apple hardware, so this is new work on our side.
4. System suspend: I don't consider it realistic for now. It needs display engine shutdown under Gunyah, RPMh sleep sets, and wake routing through PDC, and I found no working result for it in the repos here. UniXNU's sheng record mentions sleep and resume transitions only as a completion goal, and I didn't trace how far that got. Treat "screen off with everything idle" as the sleep state.

Verify: screen-off current falls, a wake from Power is under 1 s, and no lost timers or hangs after an overnight run.

Risks: this is where hangs are likeliest, and a resume bug shows as a dead phone needing a hard reset, which is **dangerous while a reset boots Android**. Do this only after standalone boot makes resets safe. Gunyah may also change what WFI costs. I expect it traps WFI and may already idle the physical core, which would shrink the gain from this phase, but I have no evidence either way.

## Dependencies on other areas

- A forced reset, a dead battery or a brownout all boot Android, which formats the root. The standalone boot work removes this risk. Phases 3 and 6 are much safer after it.
- USB device mode would let `sysctl` and live logs replace recovery pulls, but nothing here requires it.
- GPU hang fixes should land before phase 4.

## Biggest unknowns

1. Whether the phone charges under iOS with the ADSP down, and at what current.
2. What protects the chip if our kernel hangs at full clocks: the hardware cutoff temperature, and whether firmware limits run without Linux.
3. Actual power draw at each stage. Everything in the battery table is a guess until the gauge reads.
4. Whether TSENS and BCL reads behave the same from our kernel as from Android's under Gunyah, and whether BCL gives readings without its conversion-request write.
5. How junction temperature maps to skin temperature on this phone, which sets the real limits.
6. What Gunyah does on WFI and PSCI CPU_SUSPEND from a non-Linux guest.

## Skipped on purpose

- An "honest" `IOPMPowerSource` stub. Without a reading there's no honest percent to give, and any other fixed number misleads more than 100 does. The fake source already says `SweetBatteryFallback`. Add a real one in phase 3, once data exists.
- A brightness patch. The code exists and matches the panel, so this only needs the boot-arg retest.
- A GPU DCVS patch. It should wait for the hang work, and 2071 gives the part that safety needs.
