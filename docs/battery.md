# Real battery readings

B1 has code and passing host checks. No battery milestone has been measured on the phone. iOS still uses the existing fake battery source.

## Code

Patches 2190 and 2191 add an off-by-default read-only ADSP survey with sweetbprobe=1. It reads SMEM partition 0/2, SMP2P443/429, GLINK478/479/480 and command-db resources lcx.lvl, lmx.lvl and xo.lvl. It sends no messages, starts no remote processor and changes no charger state. The parser helpers are copied from the modem agent's bounded implementations after 2178.

The DT declares remoteproc-adsp@03000000 with qcom,volcano-adsp-pas, remote PID2 and channel PMIC_RTR_ADSP_APPS. Its protection domains are tms/servreg and msm/adsp/charger_pd. Rootstock's same-phone ADSP implementation uses PAS1 and PAS36, carveouts 0x99300000 + 0x2800000 and 0x99280000 + 0x80000, respectively.

Sol found lifecycle failures in the reference ADSP code and automatic USB current-limit writes in ShengBattmgr. These cannot be copied into the requested read-only implementation. The sender must reject USB_SET 0x33 and all other unapproved messages. Battery-control writes in the older power plan are excluded.

## Measured

Host sanitizer checks pass for the exact patched source extracted from the patch. They exercise off/invalid/duplicate boot arguments, both mapping failure paths, valid and corrupt SMEM, cached and uncached extents, SMP2P state, command-db resources and unchanged mapped buffers. A simulated remote metadata change verifies that the uncached extent is read once. Run `sh tools/battery/check.sh` from this repository.

## Inferred

Starting ADSP should make its charger service available. Read-only messages alone do not prove that starting the firmware leaves charging behavior unchanged.

## Missing

The full kernel build, live B1 logs, ADSP lifecycle, PMIC channel startup, property reads and SweetBattery publication remain unverified. No ADSP-start approval exists. No partition has been written and no file has been installed on the iOS root by this work.

Sol rejected the first version because permanent MMIO mappings were later unmapped and SMEM metadata was reread after validation. Patch 2191 fixes both. Sol re-audit approved the corrected B1 source. A final remake and exact image review remain required before a test boot.
