# iOS 26 on the Nothing Phone (3a) Pro

Work in progress. iOS 26.6.2 runs on a Nothing Phone (3a) Pro (Asteroids, SM7635, Adreno 810) with Liquid Glass rendered on the phone's GPU. It is an experiment on a test device, far from a daily phone.

**Works, on the current build:**

- Home screen, Liquid Glass, Safari and the keyboard open, app launches
- Metal to Vulkan through Chlorophyll on Mesa Turnip
- Native-shaped 840x1860 canvas, no bars or zoom
- iPhone 11 status bar layout
- Journaled root on internal storage, loaded by `fastboot boot` from a Mac

**Does not work yet:**

- Wi-Fi, mobile data, sound, a real battery reading
- Cold boot without a Mac
- Metal in apps, so PencilKit drawing crashes
- Widgets, which stay on "Waiting..."
- Smooth animation: about 25 to 35 frames/s while animating

## What is here

- `docs/` holds the plans and bug reports. Start with `docs/roadmap.md`, though some of it has moved on. Each report separates proven from inferred.
- `patches/kernel/` has the XNU patches, on top of `nothing-sm7635-devs/ios-asteroids` branch `asteroids-ios26` through its patch 2049.
- `patches/driver/` has the Chlorophyll and Turnip changes.
- `tools/` has the hands-free test loop, the boot slot and journal scripts, and the status bar tweak.

## What is not here

No iOS images, firmware, shared cache, boot or partition dumps, or phone logs. They hold Apple's files or device identifiers. You need your own IPSW.

## Warning

Android boots after a normal reset on stock boot slots, and it formats `userdata`, which is where iOS lives. Read `docs/plan-boot.md` before flashing anything. Nothing here is tested beyond one phone.

## Credits

Built on `nothing-sm7635-devs/ios-asteroids`, Mesa Turnip and Chlorophyll. `rootstockos/UniXNU-asteroids` and HakiOS were reference points.
