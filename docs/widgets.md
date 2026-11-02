# Why widgets say "Waiting…"

Best lead: LaunchServices (LS) has no usable record for the widget extensions. The text is SpringBoard's fallback when that lookup fails. It is not a chronod, RunningBoard, sandbox or Metal result. Why the records are missing is not settled. The two candidates are below, and pulling one file separates them.

## Proven

1. `Waiting…` is `WAITING_ICON_LABEL` in `SpringBoardHome.loctable`. No other string in the Chrono, WidgetKit, SwiftUI, WidgetRenderer or SpringBoard tables I scanned contains "waiting" in this sense.
2. The key has two users. `SpringBoardHome+0x6e20c` calls `SBHContainingBundleRecordForWidgetWithBundleIdentifier` (`+0xe1468`) and returns the label when that returns nil. `+0x2bb7d4` is an NSProgress label for `isWaiting` and has no widget input.
3. `+0xe1468` is `[[<class> alloc] initWithBundleIdentifier:id error:nil].containingBundleRecord`, so nil means no extension record or no containing app. The nil result is not cached, so the lookup retries and a later registration can clear it.
4. No appex under `/Applications/*/PlugIns` has an `exec` line in any `logs/ast*.txt`. 75 ExtensionKit names do. The one widget-named appex that runs, `MessagesSettingsWidgetKitExtension`, is in `/System/Library/ExtensionKit`.
5. The root has 59 widgetkit appexes in `/Applications` and 44 in `AppPlaceholders` (`plans/widgets/appex-points.tsv`). Calendar is `/Applications/MobileCal.app/PlugIns/CalendarWidgetExtension.appex`.
6. chronod's provider reports `3 received`, read by PosterBoard: `ast20-nofp16.txt:3924`, `ast22-linear.txt:12880`, `ast23-flushall.txt:3615`. I can't name the 3. ast24 has no such line, so the count after 2057 is unknown.
7. `sweetuicache.m` sends no `_LSBundlePlugins`. `__LSCreateRegistrationData` in CoreServices reads that key, and Procursus `uicache` contains it with `PluginOwnerBundleID` and `PluginKitPlugin`.
8. `sweetuicache` re-registers 30 of the 41 apps with widget appexes on first boot. The other 11 (Batteries, Siri, Screen Time and others) are not re-registered.
9. pkd's unfiltered `spotlight.index` + `fileprovider-nonui` query gets `Candidate plugin count from LaunchServices: 1` (`ast23-flushall.txt:1727`). The root has 17 such appexes outside `AppPlaceholders`, 8 of them under `/System/Library`, which `sweetuicache` never touches.
10. No WidgetRenderer surface appears in ast24: `exec` at line 7086, 8 AKS lines, no `SWEET SURF`, no crash. chronod's 25 surfaces are 58x58 and 128x64.

## Inferred

- The 2057 inference failed because entitlements were not the gate SpringBoard's label depends on. That fits fact 3, but no log shows chronod's own launch decision. chronod has never logged os_log on this port: its job has no console stderr and stays that way because of `analysis/hang-root-cause.md`.
- H1, `sweetuicache` wipes plug-ins: re-registering without `_LSBundlePlugins` replaces a seeded record with one that has none. Fits facts 4, 7 and 8. Fact 9 says it can't be the whole story unless pkd filters before its count.
- H2, the seed drops most appexes: `MIBundle _filterExtensionBundles:` logs `Ignoring extension ... due to missing extension cache entry`, and the gatherer logs `Ignoring app extension ... validation issue(s)`. This is what happened on iOS 16 (`docs/EXTENSION_POINTS_2.md`). `widgetkit-extension` is only in `extensions.plist`, not in the 62 `.appexpt` files, so a failed signed read would remove it. PosterBoard receiving 9 `posterkit.provider` extensions, also plist-only, argues against a failed read. lsd's os_log has never been captured, so this stays open.
- The white tile body has the same cause. Nothing proves that. The label proves only the name lookup.
- Ruled out as the current blocker, by facts 3 and 10 rather than by test: WidgetRenderer's render, Metal and archive rendering. No extension content reaches them.

## Answers

1. SpringBoard decides the text. It waits on an LS extension record. Next upstream is LS registration, then pluginkit and extension launch. The sandbox and `containermanagerd` show no widget failure in ast24, and widget containers existed in ast16c.
2. There is no explicit failure line because lsd and chronod are silent. Crashes in ast24 (SBRendererService, MessagesBlastDoor, mediaplaybackd) don't involve widgets. Sandbox denials name no widget process.
3. Fix prepared for H1, diagnostic prepared for H2: `plans/widgets/next-widgets.md`.

## Metal, later

If widgets get past LS and then draw blank, Metal becomes the question. `sweetappcheck` proves no system device and CoreImage returning zero pixels, including its software context, so CoreImage has no working CPU path today. Whether WidgetRenderer can draw without Metal is untested.

Adding WidgetRenderer to Chlorophyll's inject would supply a device. It is not in the allow list (`NOTES.md:331`). The cost is another process on the GPU, with the hang recorded at `NOTES.md:677` when SpringBoard ran on it, a kill switch per process, and 3 to 6 existing GPU hangs per boot. Do not do this until a log shows WidgetRenderer drawing into a surface and failing.

# Update: the arm64e capability (ast26)

This replaces H1 and H2 above as the main cause. H1 still matters after the fix.

## Proven

1. All 519 `Ignoring app extension` lines in `ast26-gpu16first.txt` list `arm64e = 0` as the mismatch, with no other capability. That is 496 unique IDs.
2. That count is a lower bound. The log is exactly 4 MB and starts mid-seed at `com.apple.FMDMagSafeSetupRemoteUI`, so earlier apps wrapped away. `com.apple.Batteries.BatteriesWidget` is missing from the list for that reason, not because it passed. Its csstore count is 0.
3. Every readable appex `Info.plist` in the root (1022 of 1023) has `arm64e` in `UIRequiredDeviceCapabilities`. The 254 ExtensionKit-only IDs reach LS through `_scanExtensionKitDirectory` and aren't in the ignore list.
4. The chain is:
   - `MIBundle isApplicableToCurrentDeviceCapabilitiesWithError:` and `MICapabilitiesManager` report the mismatch.
   - libMobileGestalt's `arm64e` answer (`+0x26504`, tail call to `+0x12670`) is `CFStringCompare(MGGetStringAnswer("k7QIBwZJJOVw+Sej/8h8VA"), "arm64e") == 0`.
   - That key is `CPUArchitecture`: base64 of `md5("MGCopyAnswerCPUArchitecture")` matches.
   - `+0x1ede0` makes a string from `NXGetLocalArchInfo()->name`. libmacho calls `host_info(HOST_PREFERRED_USER_ARCH)` (flavor 12).
   - `osfmk/kern/host.c:412-414` returns `slot_subtype(master_id)`, set in `osfmk/arm64/cpu.c:731` to `CPU_SUBTYPE_ARM64_V8`.
5. The ADT, `hw.optional.*`, `hw.cpusubtype` and `sweet_amfi.c` are not in this chain. `kern.preferred_cpu_subtype` in `/defaults` is only read under `PREFERRED_USER_CPU_TYPE`, which only one `board_config.h` block defines, so an ADT property would not take effect here.
6. Flavor 12 callers in the shared cache (all 4306 images scanned for direct `bl` calls): libmacho and libMobileGestalt. `NXGetLocalArchInfo` callers: libMobileGestalt, TestFlightCore (a string), WebKitLegacy (`NXFindBestFatArch`). dyld has no such strings.
7. `CPUArchitecture` or its hash also appears in AXMediaUtilities, FindMyBase and GPUToolsTransport. Nothing else has it.

## Inferred

- `+0x1ede0` is the `CPUArchitecture` answer function. It is the only call site that fits, but I did not decode MobileGestalt's key table to prove the mapping.
- The MobileGestalt cache on the phone holds the old value. The file isn't in the root image, so it's generated on the device. It is keyed by OS build, not kernel, so a kernel-only change may not reach it.
- Nothing checks this value to decide whether PAC is real. Real PAC checks would use the process's own Mach-O subtype, `hw.optional.arm.FEAT_PAuth` or `hw.optional.ptrauth`, and those stay false. The literal string `arm64e` also appears in Bom, CoreMedia, CoreServices, CoreSymbolication, FrontBoardServices, libAppleArchive, libhwtrace, libLLVM, libswiftCore, MLIR_ML, VFX and XOJIT. I did not audit those. Most look like architecture name tables.
- After the fix a first-boot `sweetuicache` still re-registers apps without plug-ins, which likely wipes them (old H1). Use patch 0001 too on any flash.

## Render path after this

Expect the labels to change first. WidgetRenderer has no Metal device in ast26, but normal apps draw without one by sending layers to backboardd, so plain SwiftUI text and shapes have a fair chance. Likely problems are blank or partial tiles where a widget uses CoreImage (proven to return zero pixels here) or glass effects, a crash in anything that assumes a Metal device, and Weather or Stocks with no data because there is no network. Calendar and World Clock are the best first tests.

Another effect is more extension launches: 618 IDs return, including 130 diagnostics and 46 notification content extensions. Watch boot time, memory and GPU hangs.
