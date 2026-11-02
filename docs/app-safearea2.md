# App top inset, round 2: diagnostics and guard fix

Desk work only. No phone command was run.

## What the ast85 logs can and cannot say

`logs/ast85-bars-X1/log-before.txt` is the log of the boot before the run. dev.py pulls it at plans/devloop/dev.py:381 before booting the test image. The run's own log is `logs/ast85-bars-X1.txt`. Both show the same pattern. PosterBoard prints the full app path (ast85-bars-X1.txt:4483 to 4489, including `app safe area installed; target=50pt`), and Preferences, MobileCal, Calculator, MobileSafari and MobileNotes exec at lines 9109, 10128, 10257, 10355 and 10616 with no sweetsb line.

Proven: PosterBoard is a top-level /Applications app started through xpcproxy, and kernel patch 2058 injected the dylib into it, the guard armed and the hook installed. Proven: SpringBoard lines carry `|SpringBoard|` because of kernel patch 0048 (ios-asteroids/research/xnu-local/dxb/xnu/bsd/dev/mem.c:161 to 260). It prints /dev/null writes from processes named in boot-arg `sweetnull`, default `backboardd,SpringBoard`. That default string is in next-fix/kernel/kernel.development.sweet. Neither test image set `sweetnull`, so app stderr writes were dropped. The logs cannot separate "not injected" from "injected and silent".

## Candidates checked at the desk

1. Sandbox denying the guard write is ruled out. Kernel patch 0050 registers a permissive "Sandbox" policy, logged at ast85-bars-X1/log-before.txt:118 (`SWEET Sandbox: permissive policy registered`). Every check passes. Only container-path and extension ops fail with ENOENT.
2. A missing guard state directory is still possible, inferred from the 0050 ENOENT on container paths. If NSHomeDirectory() names a container whose Library/Caches does not exist, `writeToFile:atomically:` fails and the constructor returns before installing (old sweetsb.m "guard failed: cannot write state"). I fixed this. sweetsb.m:240 to 243 falls back to /private/var/tmp, which SpringBoard's own guard proves writable for mobile.
3. Injection into apps is likely but unproven. Apps exec the same way as PosterBoard, and the 2058 conditions (arm64e, path shape, no existing DYLD_INSERT_LIBRARIES) hold for /Applications/Preferences.app/Preferences. The new `loaded` line settles it.
4. A route that skips the hook is unlikely for UIKit. In UIKitCore only four call sites send `_sceneSafeAreaInsetsIncludingStatusBar:` (bl to stub 0x18b4f4f60): 0x189183ce8 in `-[UIWindow _normalizedSafeAreaInsets]`, 0x18a748488 in `_usableBounds`, and two helpers at 0x1892962f8 and 0x1894f4438 that read `window` then call it. `_normalizedSafeAreaInsets` (safearea-normalized.txt:21 to 28) passes includeStatusBar = NO when `_usesSceneSettingBasedSafeAreaInsets` is true, which is the iOS 26 route. The hook already handles both BOOL values. The helper at 0x1894f4388 also calls `_safeAreaInsetsConsultingAlternativeTopInsetIfApplicable`, a top inset source I have not traced. SwiftUI reads the hosting view's UIKit safe area, so it should follow too, but that part is inferred.

The most likely cause, by inference: the app path ran blind, and either the guard returned early on a missing container Caches directory or a per-call check skipped. The run below tells which one, line by line.

## Changes

- sweetsb.m:45 to 60 makes note() also write to /dev/null when stderr is not already /dev/null. Patch 0048 then prints the line whatever stderr is.
- sweetsb.m:238 prints `loaded pid=N exe=... home=...` first, so a silent app means no injection.
- sweetsb.m:240 to 253 adds the guard directory fallback and puts the path in the write failure line. Guard semantics are unchanged: one unfinished start per boot per process, healthy mark after 60 s.
- sweetsb_safearea.h:74 to 89 reports each early return once per process, with its value: hosted, window level, frame, scene, orientation, screen, unchanged top per BOOL.
- sweetsb_safearea.h:92 prints the `top X -> Y` line once per BOOL route.
- sweetsb_safearea.h:55 and 94 to 95 add a layout probe 2 s and 10 s after the first change. It logs the window safe top, the root view controller's view safe top, and the first UINavigationBar's y and height in window coordinates. It measures what layout used, so the run does not depend on frames.
- sweetsb_safearea.h:102 to 110 makes install report why it is off or waiting. SpringBoard will now print `app safe area off: not a top-level app executable` once. It still installs nothing there.

The inset rules are unchanged: compactAppInsets only lowers top, portrait full-screen only, nothing in SpringBoard.

## Build and checks

`bash build.sh safearea` built app-safearea/sweetsb-install.dylib, sha256 08f86f90d8a13ee7200aa2be1a928832a0f6ab61b85dd63d1fd5567b13674594, with no compiler warnings. The previous artifacts are kept in app-safearea/v1/. safearea-check.sh and stui-check.sh pass, and pacstrip finds no PAC code. check.sh stops at `git apply --check root-builder.patch` on root_tweaks18.py:1664. That step does not involve these files, and its guard check-host step ran before it.

Boot image boot/next-loop-sb-null-boot.img (sha256 f02af8761ba9c59cec4122bd475a86e2907941b889f73806145038e899e1ce74) is plans/devloop/out/next-loop-sb-boot.img plus `sweetnull=Preferences,MobileCal,Calculator,MobileSafari,MobileNotes`. That makes 606 of 607 bytes. Rebuilding the payload with the old args gave a byte-identical next-loop-sb.payload, so the kernel and ADT are the same. SpringBoard and backboardd are left out of the tee for this one run to fit the limit, so `|SpringBoard|` lines will be missing. dev.py --dry-run accepted the image's loop args. I built it in its own directory and did not run build-loop.sh, so plans/devloop/out is untouched.

## The proving run

From plans/:

```sh
python3 devloop/dev.py statusbar-real/boot/next-loop-sb-null-boot.img /usr/local/lib/sweetsb.dylib="$PWD/statusbar-real/app-safearea/sweetsb-install.dylib" --test ui --note app-safearea2 --key 'sweetsb-real'
```

Root mapping: /usr/local/lib/sweetsb.dylib, an existing file, gets app-safearea/sweetsb-install.dylib. dev.py itself replaces renegade-firstboot.sh as usual. The ui test opens all five apps, as in ast85. No hook.sh change is needed because the probe logs the numbers.

Success, for each of the five apps, in logs/<label>-app-safearea2.txt:

```text
|Preferences| sweetsb-real: loaded pid=N exe=/Applications/Preferences.app/Preferences home=...
|Preferences| sweetsb-real: guard armed start=1; registering ObjC image callback
|Preferences| sweetsb-real: app safe area installed; target=50pt; portrait full-screen only
|Preferences| sweetsb-real: app safe area top 68 -> 50; statusbar=0 target=50pt
|Preferences| sweetsb-real: app layout 2s: window UIWindow safe top 50; root ... safe top 50; navbar y 50 h ...
```

How to read a failure:

- No `|App|` line at all means the dylib was not injected, or the app is not named in sweetnull.
- `loaded` with `guard failed` or `guard blocked` means the guard stopped it, and the line names the path.
- A `skip:` or `unchanged:` line and no `->` line means a per-call check kept the stock top, and the line gives the value.
- `top 68 -> 50` with a layout line still at 68 means layout reads another route. The next target is `_safeAreaInsetsConsultingAlternativeTopInsetIfApplicable`.

Rollback: put app-safearea/v1/sweetsb-install.dylib or the ordinary sweetsb-install.dylib back at the same root path.

## Round 3: the root view controller route (after ast97)

ast97 (logs/ast97-app-safearea2.txt) proves the dylib loads in all five apps (lines 10171, 11208, 11335, 11442, 11722) and the window hook changes `top 68 -> 50` with statusbar=0. The probes show every main window at safe top 50, but every root view controller's view at 68, with the nav bar at y 68. The guard did not need the fallback: every app reported home=/var/mobile, and no `not writable` line appears. The `skip: scene nil; top=0` lines come from windows before they join a scene. The `skip: window level 10; top=68` lines come from UITextEffectsWindow. Both are expected.

Proven from UIKitCore disassembly, not yet by a run: `-[UIViewController _viewSafeAreaInsetsFromScene]` at 0x1892cbb9c starts from `-[UIWindow _managedSafeAreaInsets]`, which only tail-calls `safeAreaInsets` (0x1891d7858) and gives 50. When the view controller's ivar flag bit 6 is set (`ldrh` then `tbnz w8, #6` at 0x1892cbc00), it branches to 0x1892cbe68. There it reads `-[UIWindowScene _safeAreaInsetsForInterfaceOrientation:]` with `_lastKnownInterfaceOrientation` directly, and the window hook never sees that value. The caller list for the method's stub 0x18b52f020 is `_setContentOverlayInsets:andLeftMargin:rightMargin:` (0x1891a6b4c, 0x1891a6b84), plus code at 0x18917ce5c and 0x1892cc07c that is not an ObjC method. The bit 6 flag is my inference for "root view controller", from the ast97 pattern.

Fix: sweetsb_safearea.h now also hooks `-[UIViewController _viewSafeAreaInsetsFromScene]` (appVCInsets). It takes the view's window from `viewIfLoaded.window` and applies the same checks as the window hook (appWindowFits): not hosted, level 0, frame 0,0 420x930, UIWindowScene in portrait, canvas screen. Then it applies the same compactAppInsets cap: top only, only when 50 < top <= 80, never increases. The hook is optional. On an ABI mismatch, the window hook still installs, and the install line says `vc route off`. SpringBoard is still out of scope. The guard is unchanged. A ponytail comment records one limit: a root view placed below the window top gets the cap, not the exact delta.

Build: app-safearea/sweetsb-install.dylib, sha256 3b87a987ac5731ee882a900033ac251916ea9d0f8e3b486b9246777cd2366b6a, no warnings. The ast97 diagnostic build is kept as app-safearea/v2 (sha256 08f86f90..., 72576 bytes). safearea-check.sh passes, with new mock view controller cases for an unloaded view, no window, level 10, landscape, top 40, top 90 and scope off. stui-check.sh passes. pacstrip finds no PAC code.

Run, from plans/, with the same boot image:

```sh
python3 devloop/dev.py statusbar-real/boot/next-loop-sb-null-boot.img /usr/local/lib/sweetsb.dylib="$PWD/statusbar-real/app-safearea/sweetsb-install.dylib" --test ui --note app-safearea3 --key 'sweetsb-real'
```

Success, per app:

```text
|Preferences| sweetsb-real: app safe area installed; target=50pt; portrait full-screen only; vc route on
|Preferences| sweetsb-real: app vc safe area top 68 -> 50; <root VC class> target=50pt
|Preferences| sweetsb-real: app layout 2s: window UIWindow safe top 50; root ... safe top 50; navbar y 50 h ...
```

If the root view still reads 68 with the `vc ... -> 50` line present, a later stage raises it again. The next candidate is `_statusBarOverlapAndMarginInfoForView:inWindow:` in the function at 0x1892cc07c, which takes the max of that value and `_viewSafeAreaInsetsFromScene` (fmaxnm at 0x1892cc12c).
