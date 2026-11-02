# Handoff: read this first

Written 2026-11-02 for whoever picks this up next. The phone is a test device owned by the repo owner. Update this file when the state changes.

## Where things stand

iOS 26.6.2 (23G90, iPhone12,1 base) cold boots from internal storage on a Nothing Phone (3a) Pro, with no Mac and no cable. Android cannot start from either slot, because iOS is flashed to `boot_a` and `boot_b`.

The kernel is local XNU 12377 with this repo's patches, on top of `nothing-sm7635-devs/ios-asteroids` branch `asteroids-ios26`. Compositing runs Metal through Chlorophyll on Mesa Turnip on the Adreno 810. The canvas is 420x930 pt at 2x, 840x1860 px, scaled to the 1080x2392 panel.

Fixed and confirmed on the phone:

- Icon tear, wallpaper corruption, UI restarts every 3 minutes, buffer leak
- Search band and most glass glitches, black bars while animating, zebra on app launch
- GPU hangs: 0 recoveries in 9 scripted runs, 7 back in the control run with the fix off
- Real iPhone 11 status bar with even corners, and hidden wallpaper host apps
- Widgets show real content where no internet is needed, such as Notes

Still open, most wanted first:

1. **Lag.** Animations run at 25 to 35 frames/s and look jittery. Frames run one at a time for a reason nobody has found yet. It is not the `waitUntilScheduled` wait (about 10 calls a run), the GPU clock, half-precision shaders or the display copy. See `docs/speed2.md`.
2. **Recovery access.** See below. It is the first thing to fix, since logs and file pushes depend on it.
3. **Boot slot countdown.** See below.
4. Wi-Fi, then a modem probe for mobile data. `docs/research-wifi.md`.
5. Freeform and PencilKit need Metal in apps, and compute on the A810 is unproven. `docs/appmetal.md`.
6. Sound and a real battery reading, which share the ADSP port. `docs/plan-power.md`.
7. Small ones: glass flicks white in places and stayed white in Calculator, Calendar widget stays a skeleton, app top spacing could still be tighter, only 24 GB of storage is visible, Nothing logo and the fixed 10 s wait in boot.

## Rules from the owner

- Never reduce Liquid Glass or blur. No bars, zoom, crop or stretch.
- A brick is acceptable to them as a last resort, but avoid it. They have explicitly asked to try the boot slot write below.
- Use the hands-free loop rather than asking them to test what a script can.
- Use `say` when you need them, and also say it in chat.
- No AI attribution in commits.
- Never put device dumps, phone logs, serials, firmware or Apple files in this repo. It is public.

## Danger

- Android formats `userdata` when it boots, which destroys the iOS root. Stock boot images must stay off the phone's boot slots unless you mean that.
- Never run `fastboot reboot` plain, `set_active`, or a lock state change.
- `userdata` holds the root. A root flash erases iOS state.

## Recovery access is broken, fix this first

`recovery_a` has no kernel. ABL takes the kernel from `boot_a`, and since that is ours now, both `fastboot reboot recovery` and the menu's Recovery mode boot iOS. `fastboot boot` of OrangeFox's own image fails because its kernel field is empty.

Tried: `recovery/ramboot/orangefox-ram.img`, the stock kernel plus OrangeFox's ramdisk, through `fastboot boot`. It loaded, showed nothing on USB, then the phone came back in iOS. Do not repeat it. A normal-mode boot of a recovery ramdisk probably gets `force_normal_boot` from ABL and then tries to start Android userspace, which is the path that can format `userdata`. That is inferred.

Next idea, not yet tried: flash that same combined image to `recovery_a`, so the partition carries its own kernel and ABL boots it in recovery mode. The stock image is backed up. This should also restore OrangeFox from the phone's own menu.

Without recovery there are no kernel logs, no file pushes, and the `tools/devloop/dev.py` cycle stops.

## Boot slot countdown

A flash clears `slot-successful` and sets 7 retries. ABL takes one per normal boot: power-on, hard reset, menu START. `fastboot boot` and Power + Volume Down cost nothing. At 0 the slot goes unbootable and ABL falls back to the other slot, then fastboot. Reflashing resets it. Each kernel flash clears it anyway. Details in `docs/bootslot.md`.

The owner has asked for the permanent fix: set `successful` with a one-byte write in both copies of the LUN 4 GPT from OrangeFox. Nothing has been written. Planned order:

1. Restore recovery access.
2. Save LUN 4 LBA 1..5 and 0xffffb..0xfffff. The backup GPT has never been read.
3. Run the tool's `check` on the real bytes. `tools/boot/mark_successful.py` has no phone code, so a small wrapper is needed. It refuses unless every byte and checksum matches.
4. Write the backup table first, then the primary, verifying each.
5. Read `slot-successful:a` in fastboot, then cold boot and confirm the count holds.

That table also lists `abl`, `xbl`, `tz` and `hyp`, and there is no EDL programmer, so a bad write could mean a repair service. Be careful, and read the whole of `docs/bootslot.md` first.

## How to work

- Boot-arg line limit is 607 bytes. `mkpayload.py` refuses more.
- `tools/devloop/dev.py IMAGE [ROOTPATH=FILE ...] --test ui --note NAME` runs one unattended cycle in about 5 minutes, once recovery works. Variants differ by one boot arg. Compare whole sets of runs, since home swipes alone swing about 10 frames/s.
- `tools/devloop/06-hfs-put.py` replaces existing root files from recovery, without a reflash. It cannot create new paths and refuses a dirty journal.
- Patches: `patches/kernel/` in number order on top of upstream patch 2049. `patches/driver/` on top of the fetch-fix tree. Build with `USE_KDK=1 tools/xnu-local-build.sh --remake -j12 26.5`.
- Current boot image args are in `tools/boot/next-flash-args.txt`.
- Root build: `tools/boot/build-root-example.sh`, about 7 minutes, then 3 for the journal, then about 9 to flash.

## Reports

Each one marks what is proven and what is inferred. Some earlier ones have been overtaken, so trust the newest and check code against claims. Some inferences were wrong today:

- The widget failure was not entitlements.
- Status bar work targeted UIKit's copy of the classes for a long time, while SpringBoard uses the `STUI` copy in SystemStatusUI.
- `waitUntilScheduled` was not the serial frame cause.

Start with `docs/roadmap.md`, `docs/speed2.md`, `docs/hangs.md`, `docs/blackbars2.md`, `docs/bootslot.md`.
