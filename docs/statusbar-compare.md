# Status bar: our port vs real iOS

Image: `statusbar-compare/side-by-side.png` (top bar of each source at 4 px per point, left-aligned, yellow lines every 50 pt).
Scripts: `statusbar-compare/scripts/` (`measure.py`, `run_all.sh`, `side_by_side.sh`). Raw numbers: `statusbar-compare/measurements.txt`.
No simulator, runtime download, adb, fastboot, phone, webcam or `say` was used. Nothing was committed.

## Sources

All sizes were checked against the device's native screen before use.

| Tag | Image | Pixels | Device and OS | Status bar contents | Trust |
|---|---|---|---|---|---|
| OURS-A | ast22 frame 300 (black background) | 840x1860, 2x, 420 pt | our port, iOS 26.6.2 | time, battery with "100" inside, no radios | native frame |
| OURS-B | ast19 frame 2792 (wallpaper) | 840x1860, 2x | same | same | native frame |
| REAL-11 | https://theapplewiki.com/images/applewiki/0/04/IOS_26_Field_Test.png | 828x1792, 2x, 414 pt | iPhone 11, iOS 26.1 beta 3 (page: https://theapplewiki.com/wiki/File:IOS_26_Field_Test.png) | no SIM: "SOS" with dotted bars, wifi, battery at 4% with the number inside | native size, matches iPhone 11 panel. Closest real match. |
| REAL-DI-18 | https://upload.wikimedia.org/wikipedia/commons/6/62/Seaborgium_table_bug.png (Commons "Seaborgium table bug.png", CC BY-SA 4.0) | 1179x2556, 3x, 393 pt | Dynamic Island iPhone of the 393x852 class. The OS is not stated. Uploaded 2024-10-27, so it is iOS 18 or earlier. | signal bars, "5G+", battery at 67% with the number inside | native size. Not iOS 26. |
| REAL-DI-17 | https://upload.wikimedia.org/wikipedia/commons/7/79/Private_Maps_Germany_northeast.png (Commons, CC BY-SA 2.0) | 1179x2556, 3x, 393 pt | iPhone 15 Pro, iOS 17 (stated in the file description) | signal bars, wifi, battery with no number | native size. Not iOS 26. |

Each download sits in its own folder under `statusbar-compare/refs/`, with a `SOURCE.txt` giving the URL.

Published size numbers:
- useyourloaf.com/blog/iphone-17-screen-sizes: iPhone 17 and 17 Pro are 402x874 pt @3x with a 62 pt top safe area and a 54 pt status bar. iPhone Air is 420x912 pt with a 68 pt top safe area and a 54 pt status bar. iPhone 11 is 414x896 @2x. It gives no status bar height for iPhone 11, 14, 15 or 16.
- A Use Your Loaf diagram, cited in developer.apple.com/forums/thread/715417, shows a 54 pt top area plus 5 pt on the iPhone 14 Pro. This is secondhand.
- The react-native-iphone-screen-helper package (pkgstats.com/pkg:react-native-iphone-screen-helper) lists a 59 pt "status bar" for the 14 Pro, 15 and 16 families. This is third-party and is really the safe-area inset.
- Our own `analysis/statusbar-re.md` has Split828 height at 48 pt and DynamicSplit height at 53.67 pt, from disassembly.

## Measurements in points

Method: `measure.py` thresholds the pixels in a window and groups columns into clusters. A cluster's height is its bounding box. Digit height stands in for the time's cap height. The REAL-DI-17 battery outline is gray on a map, so those numbers are read by eye from a 3x zoom and marked "~".

| Measure (pt) | OURS-A | OURS-B | REAL-11 (414, notch) | REAL-DI-18 (393) | REAL-DI-17 (393) |
|---|---|---|---|---|---|
| Time x span | 58.5-96.0 | 60.0-93.5 (frame reads "4:31", narrower "1") | 25.5-58.0 | 54.0-89.3 | 50.0-93.0 ("13:23", 5 chars) |
| Time left margin | 58.5 | 60.0 | 25.5 | 54.0 | 50.0 |
| Time digit height (cap height) | 12.5-13.0 | 12.5-13.0 | 12.0 | 12.0 | 12.3 |
| Time digit y span | 32.0-45.0 | 32.0-45.0 | 19.0-31.0 | 23.3-35.3 | 23.0-35.7 |
| Time centre y | 38.6 | 38.6 | 25.0 | 29.3 | 29.3 |
| Time width (4 digits plus colon) | 37.5 | n/a | 32.5 | 35.3 | n/a |
| Battery body x span | 330.0-352.0 | same | 370.5-394.5 | 334.7-359.7 | ~333.0-357.8 |
| Battery body w x h | 22.0 x 12.5 | same | 24.0 x 13.5 | 25.0 x 13.7 | ~24.8 x ~12.8 |
| Battery nub w x h | 1.0 x 3.5 | same | 1.5 x 4.5 | 1.3 x 4.0 | ~2 x ~4 |
| Battery right edge incl. nub | 354.0 | 354.0 | 397.0 | 362.0 | ~360.2 |
| Trailing margin (screen edge to nub) | 66.0 | 66.0 | 17.0 | 31.0 | ~32.8 |
| Battery centre y | 39.75 | 39.75 | 24.75 | 29.2 | ~29.2 |
| Percent display | number inside body, green fill, "100" | same | number inside body, gray fill, red sliver | number inside body, black/gray split fill | no number |
| Percent digit height | 6.5 | 6.5 | 8.5 | 8.0-8.3 | none |
| Wifi icon w x h | none | none | 16.5 x 12.0 | none | 16.3 x 12.3 |
| Signal icon w x h | none | none | "SOS" 13 x 6.5 plus dots, 16.5 x 11.5 together | 19.3 x 12.3 (bars) plus "5G+" 23.0 x 10.3 | ~17 x ~12.3 |
| Gap from wifi or last icon to battery | n/a | n/a | 4.5 | 6.0 (from "5G+") | ~7.7 (wifi to outline) |
| Gap from time right edge to first trailing item | 234.0 (to battery) | 236.5 | 269.5 (to SOS) | 190.7 (to signal) | 189 (to signal) |
| Top offset to top of time digits | 32.0-32.5 | same | 19.0 | 23.3 | 23.0 |

OURS-B differs from OURS-A only because the frame shows a different time ("4:31"). The battery numbers are identical.

Two ours-only observations:
- Our time centre (38.6) is within 0.3 pt of the centre of the sensor rect in `statusbar-re.md` (y 20 to 56.67, centre 38.3). That is an inference, not a measurement of the real island.
- Our battery fill is green. Real iOS draws green when charging. I can't tell the state of the port's battery from a frame.

## What differs in ours, largest first

1. Battery trailing margin is 66.0 pt. Real is 31.0-32.8 pt on Dynamic Island (+33 to +35) and 17.0 pt on iPhone 11 (+49).
2. Content sits low. Time centre is 38.6 pt against 29.3 on Dynamic Island (+9.3) and 25.0 on iPhone 11 (+13.6). Battery centre is 39.75 against 29.2 (+10.5) and 24.75 (+15.0).
3. Time left margin is 58.5 pt. Real is 50.0-54.0 on Dynamic Island (+4.5 to +8.5) and 25.5 on iPhone 11 (+33.0).
4. Percent digits inside the battery are 6.5 pt tall. Real is 8.0-8.5 (−1.5 to −2.0 pt, 19-24% smaller).
5. Battery body is 22.0 x 12.5 pt. Real is 24.0-25.0 x 13.5-13.7 (−2.0 to −3.0 wide, 8-12% smaller; −1.0 to −1.2 tall, 7-9% smaller).
6. Time is slightly large. Digit height is 12.5-13.0 pt against 12.0-12.3 (+0.2 to +1.0, up to 8%). "4:30" is 37.5 pt wide against 35.3 for "4:32" on REAL-DI-18 (+2.2, +6%).
7. Battery sits 1.15 pt lower than the time centre (39.75 against 38.6). In the references the two centres are within 0.5 pt of each other.
8. Battery nub is 1.0 x 3.5 pt. Real is about 1.3-1.5 x 4.0-4.5 (−0.3 to −0.5 wide, −0.5 to −1.0 tall). The body-to-nub gap is 1.0 pt, the same as real.
9. No signal or wifi icon, as expected without radios. Real wifi is 16.3-16.5 x 12.0-12.3 pt and real signal bars are 19.3 x 12.3 pt.

The real devices are at a different canvas width (414 or 393 pt against our 420). I did not rescale for that. Our physical panel scale-up (1.2857) is not in these numbers, which are in canvas points.

## Not obtained

- No real iOS 26 screenshot at 3x with a visible status bar and native size. The two Dynamic Island references are iOS 17 and about iOS 18, labeled above. One 1179x2556 iOS 26.3 alarm screenshot from Commons had no status bar and was discarded.
- No real home screen frame at native size. The iPhone 11 frame is an app screen with a white background.
- Apple's HIG layout page returned only its title through the fetch tool, so there is no Apple-published status bar table. The numbers above are from Use Your Loaf, a forum post and a third-party package, and none of them is a primary source.
- No press photos or App Store screenshots were fetched.
- The REAL-DI-17 battery and icon numbers marked "~" are by eye from a zoom, not a script.
- Ours were measured on two frames of the same session only.
