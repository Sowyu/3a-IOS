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

## Round 4: the status bar overlap (after ast99)

ast99 (logs/ast99-app-safearea3.txt) shows `vc route on` and `app vc safe area unchanged: <root VC> insets 50,0,34,0` in all five apps (lines 9192, 10204, 10340, 10462, 10731), with no `vc ... ->` line. So `_viewSafeAreaInsetsFromScene` was already 50, but the root views read 68 at 2 s and 10 s.

Proven from disassembly: `__UIPresentationControllerBaseContentInsetsForControllersAndViewInWindow` (UIKitCore 0x1892cc07c) calls `+[UIViewController _statusBarOverlapAndMarginInfoForView:inWindow:]` (0x1892ca648). That returns the overlap in top and the horizontal margins in left and right, which go out through pointers at 0x1892cc0ec and 0x1892cc0f4. The function then takes `fmaxnm` of that top and `_viewSafeAreaInsetsFromScene` top (0x1892cc12c). The overlap method reads `-[UIWindowScene _safeAreaInsetsForInterfaceOrientation:]` directly (0x1892ca798). When `_UIViewControllerModernContentOverlayInsetsPropagation` (0x1892ca7a4) is false, it uses `statusBarHeight` instead (0x1892ca7c4), minus the view's minY. Neither branch goes through the window. Inferred, not proven: one of those two gives 68, and the later pass the owner saw is the overlap's first use once scene or status bar info arrives.

Fix: the hook on `_viewSafeAreaInsetsFromScene` is removed, because it never fired. sweetsb_safearea.h now hooks the overlap class method (appOverlapInsets). It applies the window checks to the `inWindow:` argument: not hosted, level 0, 420x930 at 0,0, portrait UIWindowScene, canvas screen. Then it lowers only top, and only when 50 < top <= 80. Margins stay as they are, and nothing is ever increased. It covers both branches. The hook is optional, and the install line says `overlap route on` or `off`. The window hook, the guard and the SpringBoard exclusion are unchanged.

Timeline probe: from the first window change, the probe runs at 0, 250, 500, 1000, 2000, 4000 and 10000 ms. Each tick logs two lines per full-screen window. `app layout Nms` has the window and root view safe tops and the nav bar y. `app status Nms` has the status bar manager's frame, the raw scene top (`_safeAreaInsetsForInterfaceOrientation:1`), and the overlap before and after the hook.

Build: app-safearea/sweetsb-install.dylib, sha256 3049c912d307066f2542d34bf20f3fd4a0889d58092b3ecf3dea65e49227ef01, no warnings. The ast99 build is kept in app-safearea/v3. safearea-check.sh passes, with mock overlap cases for margins kept, nil window, level 10, landscape, a short frame, top 40, top 90, NaN and scope off. stui-check.sh passes. pacstrip finds no PAC code.

Run, from plans/, with the same boot image:

```sh
python3 devloop/dev.py statusbar-real/boot/next-loop-sb-null-boot.img /usr/local/lib/sweetsb.dylib="$PWD/statusbar-real/app-safearea/sweetsb-install.dylib" --test ui --note app-safearea4 --key 'sweetsb-real'
```

Success, per app:

```text
app safe area installed; target=50pt; portrait full-screen only; overlap route on
app overlap top 68 -> 50; view <root view class> target=50pt
app layout 2000ms: window ... safe top 50; root ... safe top 50; navbar y 50 ...
app status 2000ms: manager frame y 0 h <H>; scene raw top 68; overlap 68 -> 50
```

If the `app status` lines show the overlap already at 50 while the root view still reads 68, a fourth source is involved. The timeline then shows the tick where it changes.

## Round 5: hook the raw scene inset (after ast106)

ast106 logged `app overlap failed ABI: absent`. The cause was my error. `_statusBarOverlapAndMarginInfoForView:inWindow:` is a class method of UIPresentationController, not UIViewController. The method dump lists it under `CLASS UIPresentationController` at 0x1892ca648, with encoding `{UIEdgeInsets=dddd}32@0:8@16@24`. I had read its class off the wrong header in a dump that mixed many classes. The selector, the class-method lookup and the encoding were right, and the cache on this Mac matches the phone for this purpose. The probe hit the same lookup, which is why it printed `overlap nan -> nan`. Its `manager frame y 0 h 48` and `scene raw top 68` show that the status bar height is already right and only the scene inset is tall.

Fix: hook `-[UIWindowScene _safeAreaInsetsForInterfaceOrientation:]` (0x18918c360, `{UIEdgeInsets=dddd}24@0:8q16`). UIWindowScene is the only class in UIKitCore that implements it. All three readers that skip the window take their 68 from it: the window path (0x18918473c), the root view controller path in `_viewSafeAreaInsetsFromScene` (0x1892cbe9c), and the presentation overlap (0x1892ca798). The overlap hook is removed. The window hook stays, because it also covers the statusBarHeight merge.

Rules for the new hook (appSceneRawInsets): apps only, so SpringBoard never installs it. Portrait only, both for the asked orientation and for the scene's own. `_referenceBounds` must be 0,0 420x930, the screen must be the canvas, and only top changes, through the same compactAppInsets cap: 50 < top <= 80, never increased, left and right zero. The guard is unchanged. One difference from the window hook: this works per scene, so overlay windows in that scene (UITextEffectsWindow, alerts) also get top 50. That only lowers a top inset and does not affect the bottom. If it fails its ABI check, the window hook still installs and the line says `scene route off`.

The probe now prints `app status Nms: manager frame y .. h ..; scene top <original> -> <hooked>; presentation overlap <value>`, with the overlap read from UIPresentationController.

Build: app-safearea/sweetsb-install.dylib, sha256 0bb2503554c9ed12bf2a1319a7a7c9fc635c6a7d1fa3ec17d20c3625ba02726a, no warnings. The ast106 build is kept in app-safearea/v4. safearea-check.sh passes, with mock scene cases for landscape asked, landscape current, short bounds, external screen, top 40, top 90, NaN and scope off. stui-check.sh passes. pacstrip finds no PAC code.

Run, from plans/, with the same boot image:

```sh
python3 devloop/dev.py statusbar-real/boot/next-loop-sb-null-boot.img /usr/local/lib/sweetsb.dylib="$PWD/statusbar-real/app-safearea/sweetsb-install.dylib" --test ui --note app-safearea5 --key 'sweetsb-real'
```

Success, per app:

```text
app safe area installed; target=50pt; portrait full-screen only; scene route on
app scene top 68 -> 50; target=50pt
app layout 2000ms: window ... safe top 50; root ... safe top 50; navbar y 50 ...
app status 2000ms: manager frame y 0 h 48; scene top 68 -> 50; presentation overlap 50
```

How to read a failure: `app scene skip: bounds ...` means `_referenceBounds` is not 420x930, and the line gives the real value. If the scene top shows 50 while a root view still reads 68, the 68 comes from a source other than the scene inset. The overlap value on the same line then shows whether the presentation path is still the one raising it.

## Round 6: every app launch, and the home screen (2026-11-03)

Owner reports after v5: some apps still show the old gap, and the home screen has a gap above the top widget.

### Why some apps still had the gap

Every built-in app lives at `/Applications/<Name>.app/<Name>` (checked against the root manifest, including Photos, Files, App Store, Maps, Music and Weather), so kernel patch 2058 injects the dylib into all of them. The likely cause is the crash guard. It allows one unfinished start per app per boot, and a start only counted as finished after 60 s of main-queue time. A suspended app runs no timers. So an app that was opened, left and later killed, by the app switcher or by memory pressure, never got its healthy mark, and its next launch in that boot logged `guard blocked` and ran with the stock 68 pt top. The loop runs open each app once, which is why ast110 and every later run looked right: the non-hw frames of ast110, ast119, ast121 and ast131 all put the Calendar and Notes bar ink at 63.0 pt, against 81.7 pt stock (ast96, ast106). Inferred from the code, to be proven by the spacing run.

Fix: an app marks itself healthy after 3 s, or as soon as it goes to the background (`UIApplicationDidEnterBackgroundNotification`). An app that crashes within 3 s of launch is still blocked for the rest of the boot. SpringBoard keeps 60 s.

Not covered: app extensions (`.appex`) and arm64 third-party apps. 2058 never injects into them. Reaching them needs a kernel change and a Sol audit.

### The home screen

The home widget top is at 99.2 pt in every frame, stock and v5. `-[SBIconListView layoutInsetsForOrientation:]` (SpringBoardHome 0x1c17f27e0) returns the layout insets plus `additionalLayoutInsets`, and `getLayoutMetricsParameters:orientation:` (0x1c17f8e48) builds every icon position from the view bounds and that value. `sweetsb_home.h` hooks it in SpringBoard only: portrait, the canvas screen, icon locations `SBIconLocationRoot`, `SBIconLocationRootWithWidgets` and `SBIconLocationTodayView`. Top goes down 18 pt and bottom up 18 pt, so the grid moves up as a block. Row spacing, the dock, folders, the page dots and the Search pill keep their places. It skips an inset that cannot give up 18 pt.

App Library puts its search bar 3 pt below `-[SBHLibrarySearchController _windowSafeAreaInsets]`, the window's safe area (0x1c1a691d0, 0x1c1a6775c). An optional second hook lowers that top by 18 pt when it is a portrait notch top between 68 and 80. The lock screen is not touched.

Off switches for the home change: build without `-DSWEETSB_HOME_SHIFT=1`, or create `/private/var/tmp/sweetsb-home.off` and restart SpringBoard. `/private/var/tmp/sweetsb-app.off` does the same for apps at their next launch. SpringBoard's crash guard is unchanged: one unfinished start per boot, healthy after 60 s.

### Probe mode

With `/private/var/tmp/sweetsb-probe.on` present when a process starts, every line also goes to `/private/var/tmp/sweetsb-real.log`, tagged `[exe pid]`, so no `sweetnull` list is needed. Writing a label to `/private/var/tmp/sweetsb-probe` makes the foreground app log its window, root view and nav bar tops plus every button in the top 140 pt, and makes SpringBoard log each visible icon list, its insets before and after, its first item's top, search fields and window safe areas. Apps also log each touch-down point and the view that took it, for the owner's reports of a dead Calendar "+" and of nav bar buttons that stay blurred after leaving a note. Without the switch file none of this runs.

### Build and checks

`bash build.sh safearea` built the ast152 dylib, sha256 480c5135f4f7bbc6334c31c1abaeb0dbda9aafbe5bd7e7036fcb2e0bb201d53a, with no warnings. v5 is kept in `app-safearea/v5` (0bb25035...). `safearea-check.sh` passes, with new mock cases: the shift for root, root with widgets and Today, none for the dock, folders, App Library pods, nil locations or landscape, a 10 pt top, NaN, an external screen and outside SpringBoard; the library hook for 68, 60, 90, 0, off canvas and outside SpringBoard. `stui-check.sh` passes, and pacstrip finds no PAC code.

### The run

`tools/devloop/selftest/scripts/spacing.sh` does both passes in one boot. It first prints the guard records the last boot left, then turns both fixes off with the switch files and restarts SpringBoard. It measures the home page, the Today view, pages 2 to 5 (App Library), Notification Center and 20 apps, taps Calendar's "+" and opens and leaves a note. Then it turns both fixes on, restarts SpringBoard and repeats everything. Last, it opens Settings, leaves it after 2 s, kills it and opens it again. It takes about 20 minutes, so it uses `boot/next-loop-sb-null30-boot.img` (sha256 28bc5a5c...): the v5 image with `sweetdeadman=600` changed to 1800 and nothing else, 607 of 607 bytes.

### ast152 results (spacing.sh, run by the phone queue)

ast152-spacing6: 63 frames, 0 GPU recoveries, 0 backboardd restarts, root clean. It ended `reset-unknown` after the last frame and before `DONE`, the same end HANDOFF records for 17 of 45 runs. The deadman was 1800 s.

Guard records from earlier boots: `sweetsb-real-MobileTimer.state` read `2 FACD971A-...`, so in that boot a Clock launch was blocked and ran stock. That is the cause the owner saw, now proven on one app.

Apps, nav bar top from the probe log, stock pass then fixed pass:

| App | Stock | Fixed |
| --- | --- | --- |
| Settings, Messages, Files (large title bars) | 68 | 50 |
| Photos, Mail, Music | 68 | 50 |
| Safari, Clock, Weather, App Store (no nav bar; window safe top) | 68 | 50 |
| Reminders, Journal, Passwords, Health | 68 (frames) | 50 |
| Calendar | 68 (frames) | 50 |
| Notes | 68 (frames) | 50 |
| Calculator, Camera | 68 (scene log) | 50 |
| Contacts | 68 (frames) | 50 (window and root) |

The frames agree: each before and after pair is offset by 15 px, which is 17.5 pt at this scale, so 18 pt within a pixel (Settings, Safari, Photos, Messages, Mail, Files, Contacts, Reminders, Calendar, Notes, Health). Full-bleed screens (Weather, Camera, Clock's stopwatch, the blank App Store and Maps) have no edge to compare.

A "Turn On Location Services" alert from Maps stayed up for the rest of each pass. The apps after it were never active, so their labelled probes and touches are missing, and the Calendar "+" and note checks did not happen. Their positions above come from the fixed pass's automatic timeline and the frames.

Home screen, from SpringBoard's probe:

- Page 1: insets 96,224.667 -> 78,242.667, first item 98 -> 80 pt (01-before-home1, 32-after-home1).
- Page 2: first item 96 -> 78 pt (03, 34).
- Today: insets 96,0 -> 78,18, first item 96 -> 78 pt (02, 33).
- Dock (804.3, first item 824.7) and Search pill (754.5) unchanged.
- App Library: unchanged, search text at 113.2 pt in both passes (04, 35). The hook returned 50 only from its second call on. App Library had already laid out once at SpringBoard start, before the grid hook checked the screen, and kept its 168 pt search bar frame. Fixed: each hook now checks the screen itself, with a host case for a library call that comes first.
- Notification Center (07, 38): unchanged and not touched, clock text at 105 pt in both.

Guard reopen: Settings opened, left after 2 s, killed and reopened: start=1 and safe top 50 (63-reopen-Preferences). Maps' first launch lived about 15 s but never wrote its healthy mark, and its next launch logged `guard blocked start=2`. Both healthy routes ran on the main queue, which Maps seems to have kept busy. Fixed: apps now mark themselves healthy on a background queue, 3 s after start.

Build after these fixes: sha256 5e5e8c45f07d41c92ccea20c598a93b05f61432f2a4618817391ee39120cd273. Host checks pass.

Next run: `tools/devloop/selftest/scripts/spacing2.sh`, about 9 minutes, with the same image and pulls. It covers the owner reports in both passes, App Library, and the reopen of Settings and Maps.
