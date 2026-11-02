# SystemStatusUI correction after ast33

The previous implementation used the wrong Objective-C class name for the renderer that SpringBoard actually uses. The runtime classes are **`STUIStatusBarVisualProvider_*`, without a leading underscore**. The exported C functions have leading underscores in symbol listings, which must not be copied into Objective-C class names. ast33's “absent or ABI mismatch” combined two different failures and concealed this error.

The new dylib is built, PAC-rewritten and signed. `stui-check.sh` passes on the host. No phone command or devloop command was run. The previous report is preserved as `plans/statusbar-real/report-before-stui.md`; its claim that ordinary apps necessarily draw their own UIKit provider is superseded below.

## Cache decoding and selection

`stui-dump.py` now decodes the authenticated slide-v5 pointer's full 34-bit runtime offset. The old `dsctools/stubs.py` helper masked authenticated pointers to 32 bits. STUI class and class-data addresses exceed that range, so it read the wrong class metadata. This is not a Swift-specific provider ABI. The new reader also handles tagged relative method-list lists, including categories from other cache images, rather than silently dropping them. The shared tools were not changed.

The provider method dump is in `stui-methods.txt` and `stui-methods.json`. Concrete evidence:

- `STUIStatusBarVisualProvider_iOS +visualProviderSubclassForScreen:visualProviderInfo:` is at 0x2777f06ec, encoding `#32@0:8@16@24`.
- Phone uses the same signature at 0x2777f07b8; Split at 0x27784bccc; DynamicSplit at 0x2777f036c.
- FixedSplit `-notchRect` is at 0x277850b30, encoding `{CGRect={CGPoint=dd}{CGSize=dd}}16@0:8`.
- `STUIStatusBar -_prepareVisualProviderIfNeeded`, 0x2777c250c, calls the exported selection function at 0x2777c38f4 with `_effectiveTargetScreen` and `visualProviderInfo`, then stores the returned class in its own ivar.
- Selection caches the embedded screen's provider at unslid 0x280899940. Its cached-class check is 0x2777c394c and the store is 0x2777c3ae8.

The selector's screen argument is UIScreen, not a new STUI screen wrapper. `_effectiveTargetScreen` at 0x2777cb254 takes the explicit target, otherwise `window.screen`, otherwise UIScreen.mainScreen or the car screen. The class reference at 0x278671d90 resolves to UIScreen. `stui-screen.txt` and `stui-meta.txt` preserve this trace.

STUI FixedSplit's notchRect reads that screen's `_exclusionArea.rect`, scales each component by `_nativeScale/_scale`, and caches the rectangle. Thus forcing the class alone would still use the island rectangle. The local subclass override remains necessary and returns **96.5,0,227,32 pt** without changing UIScreen or the global sensor area. `stui-notch.txt` captures the independent STUI evidence.

STUI Split828 has the 48 pt height, 14 pt base font, 828 class native-width constant and 227×32 notch class constant. The previous point-scale reasoning remains conditional on the screen's effective corner radius and containing view. No 420/414 font or icon scaling is added.

## Preference override

STUI has its own override, but it does not read `UIStatusBarVisualProviderClassName` from com.apple.UIKit. At 0x2777c3958–3980 it reads:

`STUIStatusBarSettingsDomain.rootSettings.visualProviderSettings.visualProviderClassName`

It checks nonempty length, resolves the class, walks superclass protocol conformance, logs the override and caches the result. The settings domain subclasses **PTDomain**. Its domain group is `SpringBoard` and domain name is `Status Bar`; these are PrototypeTools domain labels, not proven CFPreferences suite names. `STUIVisualProviderSettings` supplies the property getter and setter.

A correctly persisted PrototypeTools override could select `STUIStatusBarVisualProvider_Split828` before the first cached lookup. The persistence schema, production enablement and loading path have not been established here, so writing a guessed plist key is not recommended. Even a successful class override would leave the original island notchRect unless geometry is corrected separately. Therefore a plain preference change is not a complete replacement for this candidate.

## What changed in sweetsb.m

The correct STUI names are used alongside the existing UIKit names. Both families retain exact screen, scale and method-ABI checks, original-method fallback, and a local Split828 subclass. Each missing class, failed method encoding, failed subclass creation, failed guard read/write and screen mismatch gets a specific one-line message. Installation and selection logs name the family separately.

`objc_addLoadImageFunc` observes already-loaded and future ObjC images. Crucially, its callback only coalesces a main-queue installation request; it does not call Objective-C runtime APIs while the runtime lock is held. This restriction follows the SDK header and Apple's [objc-os.mm implementation](https://github.com/apple-oss-distributions/objc4/blob/main/runtime/objc-os.mm). The installer runs synchronously outside that callback during the constructor as well.

If SpringBoard's STUI classes are genuinely absent, the constructor loads SystemStatusUI synchronously with `dlopen` and installs before returning to main. This avoids relying on a queued callback racing the first status-bar construction. The framework handle stays loaded. In other apps, the callback retries when their frameworks arrive; it never forcibly loads UIKit or SystemStatusUI into helper processes. Already-created provider instances/caches are not retroactively rewritten, so this requires a fresh process as the devloop test provides.

The temporary DynamicSplit allocation traps were removed. They redirected allocation without checking the screen and bypassed normal allocation behavior. Selection hooks now perform the substitution after checking the actual screen, and shared inherited Method objects are hooked only once. Original IMPs are saved before replacement.

The current one-unfinished-start-per-boot guard is preserved, including damaged-record rejection and per-executable app paths. The 60-second healthy timer starts after successful selection rather than merely after installation. The raw diagnostics use bounded `write` calls, with no delayed private-object diagnostics after clearing the guard.

## Which copy draws in apps?

The earlier assumption was wrong for the normal path in this cache. `UIStatusBar_Base +_implementationClass` at **0x189150860** checks `_isSpringBoard`. The non-SpringBoard branch at 0x18915092c returns **UIStatusBar_Placeholder**, confirmed against the class dump. SpringBoard takes a separate dynamically resolved implementation path. `UIStatusBarManager -createLocalStatusBar` at 0x189ecc3d4 simply returns nil.

The placeholder's `_requestStyle:partStyles:legibilityStyle:foregroundColor:animationParameters:forced:` logs that changing the status-bar instance directly is no longer supported. It is not the normal provider renderer. These findings, together with the ast33 absence of UIKit provider calls in SpringBoard and the recovered STUI rendering path, support **SpringBoard's STUI renderer for the visible system bar while ordinary apps such as Settings are foreground**. Neither an app-local UIKit provider nor an app-local STUI provider is required by that default path.

UIKit's `_UIStatusBar` and `UIStatusBar_Modern` still exist for explicit/local or specialized clients, and implementation-class overrides exist. That is why UIKit safety hooks are retained, but a lack of UIKit `choose` logs in an ordinary app is not by itself a failure. App-wide injection is not justified solely to fix ordinary foreground system bars; use the existing SpringBoard test route first. This does not assert that every specialized host or app follows the default branch.

## White text on Settings' white background

This is a separate style-selection or color-delivery fault, not an explanation for the class-name failure. STUI has actual dark-style support. `_effectiveStyleFromStyle:` at 0x2777d4830 returns explicit nonzero styles unchanged. For style 0, it reads `traitCollection.userInterfaceStyle`: dark appearance gives style 1, other appearances give style 2. The provider's `styleAttributesForStyle:` at 0x2777d4b4c uses white for style 1 and black otherwise when explicit colors are absent. Existing text/image color overrides take precedence.

The dylib now observes `_effectiveStyleFromStyle:` in both families and forwards the original result unchanged. It logs up to 32 distinct requested/resolved transitions, avoiding a burst of identical startup messages consuming the useful logs. It does not force black text everywhere, which would break the home and lock screens.

For the owner’s test, open light-mode Settings and look for `STUI style requested=... resolved=...` in SpringBoard. If it stays resolved=1, trace the foreground scene's style request, appearance traits and SpringBoard's status-bar scene/manager update path. If it resolves to 2 but the native capture remains white, inspect explicit STUI textColor/imageTintColor overrides and the foreground render/compositing path. If no new style transition appears, check the foreground scene's style update delivery and whether the same bar instance remains visible. Provider geometry alone is not a demonstrated color fix.

## Expected result and test

Expected family-specific sequence:

```
sweetsb-real: STUI ABI verified: selector #(@,@), notch CGRect()
sweetsb-real: STUI selector installed
sweetsb-real: STUI screen UIScreen native=840x1860 scale=2 nativeScale=2 embedded=1 fits=1
sweetsb-real: STUI choose Split828; notch=96.5,0,227,32
sweetsb-real: STUI notch override used
```

The concrete UIScreen subclass name can differ. A missing-class message followed by `STUI framework loaded before main` is acceptable; a terminal ABI failure or missing `choose`/`notch` log is not evidence of a layout fix.

The prediction remains approximately **clock x 27–59.5 pt, centre y 25 pt; battery right edge 401.5 pt, centre y 24.75 pt**, conditional on the reference corner radius and glyphs. At @2x these are clock x 54–119 px, centre y 50 px, battery right edge 803 px. This should visibly differ from ast33's clock x 117–190 px, y 64–89 px. These are predictions, not phone measurements. Canvas, home-screen grid, global safe areas and aperture configuration remain unchanged.

Built with `build.sh`. `stui-check.sh` tests real ObjC callback registration without runtime-lock reentry, simulated late-family availability, repeated installation, inherited shared methods, matching/external screen behavior, CGRect return ABI, original style forwarding and the current guard limit. Signature verification passes; PAC scan finds no remaining PAC instructions. The test uses mock classes on macOS and does not prove the iOS framework’s initialization behavior. No kernel or root-builder change was needed in this follow-up.

Run this yourself **from `plans/`**:

```sh
python3 devloop/dev.py devloop/out/next-loop-sb-boot.img /usr/local/lib/sweetsb.dylib="$PWD/statusbar-real/sweetsb-install.dylib" --test quick --note stui-split828 --key "sweetsb-real"
```

The filename/path is the existing user-provided boot image and file-only devloop route. I did not run it. Installation binary SHA-256 and the source/evidence file list are recorded in `plans/statusbar-real/STUI-SHA256SUMS`.
