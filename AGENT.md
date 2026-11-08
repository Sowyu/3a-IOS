# Agent pickup

Read `HANDOFF.md` first, then this file.

## What this GitHub repo is

Portable source for iOS-on-Asteroids: docs, kernel/driver patches, host tools, and the live phone log in `HANDOFF.md`.

It does not contain IPSWs, boot images, partition dumps, or the 100 GB local trees under `/Users/aniko/Documents/Vibes/EverIOS/` (`ios-asteroids`, `backup-asteroids`, `plans/`, `next-fix`, and so on). Those stay on the Mac. Another agent can keep working from this repo plus that folder.

GitHub remote: `https://github.com/Sowyu/3a-IOS` (account `Sowyu`).

## Device

Nothing Phone (3a) Pro, codename asteroids, SM7635. Test device. USB ADB/fastboot on the Mac.

Last recorded boot (2026-11-07 morning): OrangeFox, then `boot_a` daily D (iOS 26.6.2). `boot_a` marked successful. No passcode left on the phone. Root had the Freeform daily driver from candidate E.

Flashing Android userdata will wipe the iOS root. Stock Android on these slots formats userdata. See `docs/plan-boot.md`.

## LineageOS

Unofficial only. LineageOS wiki has no official asteroids page.

- XDA 23.2 (SaalimQuadri / ZVNexus): https://xdaforums.com/t/rom-unofficial-lineageos-23-2-nothing-3a-3a-pro-asteroids.4783026/
- Earlier ZVNexus thread: https://xdaforums.com/t/rom-unofficial-asteroids-lineageos.4764784/
- Kernel trees: https://github.com/AKoskovich/android_kernel_nothing_sm7635

Maintainer flash paths (do not invent others):

1. `fastboot update lineage_asteroids-img.zip`
2. Flash `vendor_boot.img`, reboot recovery, sideload the OTA zip

Firmware note from ZVNexus: be on Nothing `B4.0-251118-1551` before a fastboot-package flash. OTA zips include firmware.

This machine had no USB device when this file was written (`adb devices` empty). Do not guess the phone is in fastboot.

## After a Lineage flash, to resume iOS

You need the local `backup-asteroids/` dumps and the recovery-capable `boot_a` image. They are not in this repo. Restore those with the existing OrangeFox / `tools/` scripts on the Mac, not from GitHub.
