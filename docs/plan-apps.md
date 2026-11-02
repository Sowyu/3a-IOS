# Plan: apps and media (YouTube, texting)

Date 2026-11-02. Nothing here ran on the phone. Patches are in `plans/apps/`, unapplied, and all are untested on a phone. Evidence dumps are in `plans/apps/evidence/`.

Tags: **Proven** means I read it in the code, shared cache or a log. **Inferred** means reasoning with no run behind it. **Unknown** means no evidence. Times are agent time plus the boots listed, and the owner's time for boots is extra.

## Verdict

- YouTube at 480p with sound is possible and not close. The earliest honest date is after Wi-Fi (researched separately, 2 to 3 months in `daily-driver-gaps.md`) and the ADSP audio port (3 to 5 weeks). Expect low frame rate until the compositor improves.
- The CPU is not the problem. H.264 at 720p30 should cost well under one big core. The limits are the 10 to 12 frames/s compositor, GPU hangs, and no audio device.
- Texting works only through the web or a from-source client over Wi-Fi, and only while the app is on screen. There are no real push notifications. iMessage and SMS are out.
- The hotspot is impossible.

## What the evidence changed

1. **Safari gets past launch.** Proven: `logs/ast15-ux.txt:10014` execs MobileSafari with no crash, and it uses its container (`.../Application/3BDD9558...`). It then execs WebContent (`:10102`), Networking (`:10122`) and GPU (`:10128`). No WebKit process crashed in the remaining 60 s of that log. Boots ast16 to ast22 never launched Safari. No page load has been observed on this phone.
2. **WebKit's VP9 is switched off here by design.** Proven: `WebCore::shouldEnableSWVP9Decoder` at `0x1a32dd334` returns false when the system has a battery and no VP9 hardware (`evidence/vp9-gates.txt`). The fake battery makes that true. VideoToolbox's codec list has no VP9 entry at all. So H.264 is the path, and that is the format that has software decode.
3. **Software H.264 exists in the cache.** Proven: `/System/Library/VideoCodecs/H264SW.videocodec` exports `VCPAVCRegisterDecoder`, and VideoToolbox names it (`evidence/exports.txt`, `vt-strings.txt`). Whether it is chosen on this port and decodes correctly is Unknown. The grid HEIC corruption in `heic-decode.md` sits in the neighbouring HEVC software path, so correctness needs a check, and `sweetappcheck` makes one.
4. **`mediaserverd` is gone as a running daemon.** Proven: zero execs in ast15 to ast22. Its roles appear as `mediaplaybackd`, `audiomxd`, `videocodecd`, `mediaparserd` and `cameracaptured`, which all exec.

## 1. Safari, WebKit and Metal

| Part | State | Tag |
| --- | --- | --- |
| Launch and container | Works since the `sweetuicache` Safari fix | Proven, ast15 |
| WebContent, Networking, GPU processes start | Once each, no respawn loop in 60 s | Proven, ast15 |
| Page rendering | Never observed here. Poco F1 went blank from P3 surfaces; `mkadt.py:349` forces sRGB | Unknown |
| JIT | Never exercised. `CODE SIGNING: 429 ... removing execute access` appears right after WebContent starts (`:10105`) | Unknown, biggest Safari risk |
| Network process | `SWEET Sandbox: privilege 10001 denied` for mDNSResponder, dasd, geod, cloudd and others | Proven denial |
| WebGL, WebGPU | ANGLE and WebGPU need a Metal device, and apps have none | Inferred fail |

The 10001 denial is `PRIV_NET_PRIVILEGED_SOCKET_DELEGATE` (Proven, `bsd/sys/priv.h:110`). `sweet_sandbox_priv_grant` only grants through its entitlement table (`bsd/kern/sweet_amfi.c:454`). WebKit attributes sockets with a delegate, so this will look like a Wi-Fi bug if left alone. It needs a one-line kernel table entry, which I did not prepare because this task covers `tools` only.

How processes fall back today:

- Proven: only backboardd registers Chlorophyll in ast22 (`ast22-var/bgmtl.log`, PIDs 64, 689, 795, which also shows backboardd restarted twice).
- Proven: patch 0232's system-wide device needs `chlorophyll=1`, and current boot args omit it. The inject's allow list covers Mercury, Photos poster, iconservicesagent, SpringBoard and PosterBoard only.
- Inferred: `MTLCreateSystemDefaultDevice` returns nil in app processes, so UIKit apps draw with the CPU into IOSurfaces and backboardd composites them.
- Proven by symptom: Core Image's default path can die without a device (SBRendererService asserts in `-[NUDevice_iOS _newRendererWithCIContextOptions:error:]`, three times in ast22).
- Inferred: WebKit's page painting is CoreGraphics on IOSurfaces, which is CPU work, so ordinary pages and `<video>` do not need Metal. YouTube's mobile page should not need WebGL.

Plan for app Metal, in order:

1. Stay on the software path first. It is the only one with no new crash source for the compositor.
2. Give one named app a device only if it fails without one, by the existing recipe: `addlc.py` to link the inject, plus the allow list, plus `SweetAdrenoUserClient` in its entitlements (NOTES, iconservicesagent entry).
3. Do not turn on `chlorophyll=1` globally while hangs continue.

Risk of app Metal is high. Proven from `2005` and `2009`: there is one GPU address space (bank 0, ASID 2) shared by every client, and a recovery voids fences. So a bad app shader can hang the GPU and tear the home screen, and there is no isolation. Each client also runs a whole Turnip instance from the same 1.5 GiB page pool.

## 2. Video

The route, Inferred from the cache and matching the process list: `<video>` goes through AVFoundation and MediaToolbox, with `mediaplaybackd` hosting playback. VideoToolbox picks a decoder, which on this phone can only be H264SW for H.264. Frames arrive as `420v`/`420f` IOSurfaces and reach backboardd through a CoreAnimation layer.

| Codec | What the cache has | Usable here |
| --- | --- | --- |
| H.264 | H264SW.videocodec, H264H8 (hardware, inert) | Yes, Inferred |
| HEVC | VCPHEVC, AVD (hardware, inert) | Runs, wrong output on grid HEIC |
| VP9 | libvpx in libwebrtc, reached by `registerWebKitVP9Decoder` | Off, gate above. Only a testing override flips it |
| AV1 | AV1SW.videodecoder, and dav1d in WebCore's WebRTC decoder | Not advertised for hardware. No reason to use it |

`mediaplaybackd` crashes about once a boot. Proven: `ast22-linear.txt:18646`, `abort` from `objc_exception_throw`, and launchd restarts it. `app-crashes.md` traced the same path to an unrecognized selector, so the "CFPreferences" symbol in NOTES and `evidence/mpb-crash.txt` is a nearest-export artifact. The bt line stops at 10 frames, so the caller is Unknown. In ast2 and ast22 it follows `AudioConverterService` starting, which suggests an audio-related cause (a weak inference). Adding `sweetscan=mediaplaybackd` to the boot args prints the class and selector.

CPU numbers. Measured on this Mac (M5 Pro), ffmpeg decoding 600 frames (`evidence/host-decode-bench.txt`, synthetic test pattern with noise at YouTube-like bit rates):

| Clip | Mac, 1 thread | Estimate, one A720, ffmpeg |
| --- | --- | --- |
| H.264 480p, 1 Mbit/s | 1900 fps | about 200 fps |
| H.264 720p, 2.5 Mbit/s | 890 fps | about 100 fps |
| H.264 1080p, 4.5 Mbit/s | 480 fps | about 50 fps |
| VP9 720p, 2.5 Mbit/s | 880 fps | about 100 fps |

The estimate divides by 4.5 for the core (my assumption, not measured or sourced) and by 2 for real content. Apple's decoder may differ. Even at half this speed 720p30 uses about 60% of one A720 and H264SW has threads, so decode fits. All of this is Inferred until `sweetappcheck` prints the real rate.

The real limits:

1. **Compositor rate.** The owner measured 12.1 frames/s (ast21, NOTES). A video cannot look smoother than that, whatever the decoder does.
2. **YCbCr handling.** Proven: `bgmtl.m:2772` converts every two-plane YUV surface to BGRA in a scalar per-pixel loop on the CPU at each refresh and allocates its result. A 720p frame is 0.92 Mpixel. That is several milliseconds per refresh in backboardd (Inferred), on top of a slow frame. Sampling the two planes in a shader removes it.
3. **Hangs.** Three or more recoveries per boot void fences (Proven, NOTES). A hang during playback freezes the picture.

YouTube route, ranked:

1. **Mobile site in Safari, recommended.** A genuine browser handles YouTube's page logic, tokens and streams itself. I could not find what the site currently serves to iPhone Safari. MMS exists since iOS 17.1, and yt-dlp's guide says YouTube's iOS client gets HLS. Inferred: with VP9 and hardware absent the page falls back to H.264. It needs one captured session to settle.
2. **Yattee**, an open-source AGPL client for Invidious or Piped. It depends on someone running a working backend, and I could not confirm its current state. Try it second, built from source.
3. **The official app** is FairPlay-encrypted and is rejected by the installer. It stays that way.

FairPlay and HLS limits: ordinary YouTube videos are not DRM. DRM content (paid films, Netflix and the like) will not play here because it needs FairPlay and secure hardware, and working around that is out of scope. `fairplayd.H2` relaunches constantly (`ast22-linear.txt`, dozens of execs). It costs CPU and `quiet()` already throttles it when enabled.

**Minimum for 480p with sound:** Wi-Fi, the JIT working or tolerable, the 10001 grant, H264SW confirmed correct, `mediaplaybackd` stable, an audio device. A shader path for YUV is strongly wanted, not strictly required.

## 3. Texting

| Option | Can it work | Breaks | Tag |
| --- | --- | --- | --- |
| iMessage, FaceTime | No. Needs Apple activation and identity | | Inferred, and spoofing it is out of scope |
| SMS | No. The modem has no iOS path | | `daily-driver-gaps.md` section 12 |
| Telegram Web in Safari | Most likely first | No background delivery | Inferred |
| WhatsApp Web, Discord web | Likely with Request Desktop Website | Phone-sized layout, heavier JS, no background delivery. WhatsApp needs a primary phone | Inferred, not checked |
| Telegram-iOS from source | Plausible | Push, app groups, some Metal effects | Inferred |
| Signal-iOS from source | Harder | Registration, push | Inferred |

Dependencies:

- **APNs** will not work. `apsd` runs but no activation identity exists. Web push on iOS rides on APNs too (from memory, not checked). Clients must tolerate a failed registration. Telegram does in my memory, and Signal's registration waits on a push challenge token before falling back (DeepWiki summary only, verify against source).
- **Keychain** is probably fine. `securityd` runs once with no crash in ast22 (Proven), but no round trip has been tried. `sweetappcheck` tries one.
- **Secure Enclave** does not exist. Client code that demands it fails. `sweetappcheck` shows whether it fails cleanly. Telegram and Signal use it optionally as far as I remember (not checked).
- **Telegram-iOS build.** Proven from its README: it needs your own `api_id`, Xcode and `Make.py`, and its signing steps assume an Apple Development certificate. Here the installer re-signs ad hoc, so a throwaway profile or unsigned build path is what to try. Its `profiles` folder lists entitlements. Plan 1 to 2 days of build fighting.
- **FairPlay.** App Store copies cannot run. Only open-source builds, or the owner's own decrypted copies of apps they own, go on this phone. `sweet-install-ipa.sh` keeps refusing encrypted binaries.

Notifications without APNs: `usernotificationsd` runs (Proven, exec only). Local notifications from a running app should appear (Inferred). The workable design is a small launchd job holding one connection the owner controls, such as TDLib with their own Telegram account, which posts a local notification through a helper app. It is 3 to 5 days of work and only worthwhile once Telegram itself works. I did not prepare it. No part of it touches APNs.

## 4. Sideloading

State:

- Containers: `sweetuicache` containerises Safari only, so other apps get a nil `dataContainerURL`. Proven (`sweetuicache.m:63`). **Patch 0001 generalises this.** The registration keys it uses all exist in CoreServices (`IsContainerized`, `Container`, `EnvironmentVariables`, `GroupContainers`, `Entitlements`, `evidence/cs-audio.txt`). The group lookup symbol is exported by `libsystem_containermanager`. Whether apps read `HOME` from `EnvironmentVariables` here is Inferred, from how TrollStore's registration works as I remember it.
- Signing: Proven by NOTES and `docs/CSOPS.md`: AMFI is a stand-in, and ad-hoc signatures run. An ad-hoc sideload ran on the Poco F1 port on 2026-10-20. On this kernel it is untested, though first boot's own SweetRun.app registers and launches on this phone (ast22 `renegade-firstboot.log`).
- Delivery: `sweet-install-ipa.sh` needs SSH, and this phone has no USB network. **Patch 0002 adds `--prepare` and build-time sideload.** It also fixes a real bug: a name with a space made the remote `rm -rf` run on the wrong path.
- On-device installer: not prepared, because it needs a network to be useful. Later, a root job watching a drop folder, using the same steps as the host script minus pacstrip. pacstrip is Python and macOS `codesign` is unavailable, so arm64-only apps with a small on-device signer are the realistic limit. About 2 to 3 days. It is a root-running parser of downloaded files, so it needs care.
- Keyboard: `kbd`, `InputUI` and `inputanalyticsd` run (Proven). Typing into a text field on this phone has no logged result. Autocorrect quality is Unknown and will be poor if language assets are missing.
- App launch: `FBLaunchWatchdogScale` 20 is set, and the 300 s Settings stall has a root tweak. Launch reliability otherwise depends on the GPU hangs, and backboardd restarted twice in ast22.

## 5. Hotspot

Impossible as a hotspot. It needs a cellular uplink and there is none. With Wi-Fi as the only uplink, the chip would have to serve and join at once through a driver not yet written, and the phone has nothing of its own to share. The useful direction is the reverse: this phone joins someone else's hotspot. USB tethering from a Mac is the only other network, and it needs the USB device-mode work.

## 6. Sound

Current state, Proven:

- `audiomxd` runs once with no crash (ast22).
- `root_tweaks18.audio()` moves `VirtualAudio.plugin` to `HAL.disabled`, moves the speech extensions aside, and turns off `StartRecorderServer`. No HAL plug-in remains, so there is no output device.
- CoreAudio scans `/System/Library/Audio/Plug-Ins/HAL`, `/Library/Audio/Plug-Ins/HAL` and `/Library/Apple/Audio/Plug-Ins/HAL`.
- CoreAudio also contains an `AMCP/Null` path and a `NullAudioDriverConfiguration` string. Whether it can stand in as a device is Unknown.

Shortest route to speaker sound:

1. Port the ADSP start from `ref/UniXNU-asteroids` (also needed for battery), 1 to 2 weeks.
2. Port GPR, AudioReach and TFA9873 setup, plus the kernel service behind the plug-in, 2 to 3 weeks.
3. Install `hal/ShengAudioPlugIn.c` (692 lines, BSD-2-Clause-Patent, plays through both speakers on iPadOS 17 per `porting.toml`) as a HAL plug-in. Its interface against iOS 26 is unproven, 2 to 3 days.

**Silent device first.** If AVPlayer stalls with no audio device, video stalls too, sound or not. `sweetappcheck`'s player step answers this. If it stalls, a plug-in with no kernel side that discards samples on a host-time clock gets video playing in 1 to 2 days, derived from the same file.

USB audio is not a stopgap. It needs USB host mode, role switching through the ADSP, and a USB audio driver this custom kernel lacks. That is more work than the speakers. Bluetooth audio needs Bluetooth (2 to 4 weeks) plus the same audio stack. The microphone remains broken upstream, so no voice notes or calls.

## 7. System services

Block daily use:

- Wi-Fi (other plan), the charging and boot traps (`daily-driver-gaps.md`).
- Audio, because of alarms and video.
- GPU hangs, and backboardd restarts, which end in launchd rebooting if repeated.
- Settings: the freeze fix needs confirming. Preferences execs five times in ast22, with no crash line.

Annoy without blocking:

- Clock alarms: `MobileTimer` never launched in the logs. They need audio, and the phone has no deep-sleep story. Unknown otherwise.
- Control Center tiles, widgets.
- MessagesBlastDoor's crash loop (33 lines in ast22) wastes CPU only.
- Location: `locationd` runs, with no GPS. Wi-Fi location needs Apple's service. Maps can browse only.

Do not matter for the goal:

- Camera. It stops video calling and QR login, though. WhatsApp Web's QR is scanned by the other phone, so it is fine.
- Apple ID and App Store: unavailable, and nothing here depends on them.
- Face ID, Touch ID: gone. Do not keep personal secrets on this phone, since data at rest is plain. A messaging login here is readable by anyone with fastboot access, so use a secondary account where possible.

## Phases

### Phase 1: one evidence boot. About 3 hours plus 1 boot. No dependencies.

1. Apply patches 0001 to 0003 from `ios-asteroids`.
2. Run the host checks, then `sh tools/sweetnet/build.sh` and `sh tools/sweetnet/appcheck-clips.sh`.
3. Build the Mac copy of `sweetappcheck` with the clang line in `test-sweetappcheck.sh`, run it on both clips, and record each `first luma` hash.
4. Rebuild the root with `RENEGADE_APPCHECK=1`, and add `/private/var/tmp/sweetappcheck.log` to the log pull.
5. Add `sweetscan=mediaplaybackd,MobileSafari` and `sweetsyserr=MobileSafari,com.apple.WebKit,mediaplaybackd` to the boot args.
6. Boot, tap Safari after 4 minutes, tap Notes, and pull logs.

Verify: the log ends `sweetappcheck: done`, or the last `==` line names the failing step. Risk: low, since it runs as a job and cannot harm the UI beyond CPU use for a minute. `JIT` failure ends the Safari path until fixed.

### Phase 2: WebKit shows local pages and video. 1 to 3 days plus 2 to 4 boots. Needs phase 1.

1. Build a 60-line WKWebView test app that loads a bundled HTML file with text, a canvas and a `<video>`. Install through `RENEGADE_SIDELOAD`. This also tests 0001 on a real app.
2. Read `SWEET SURF` for `w30r`/`b3a8` surfaces, the WebContent respawn loop from the Poco F1 notes, and the first `exec` crash.
3. Fix what appears: most likely `mediaplaybackd`'s selector, and a silent HAL plug-in if the player stalled.

Verify: an owner photo of text and moving video in the test app. Risk: medium, each fix is unknown.

### Phase 3: faster YUV. 2 to 4 days plus 2 boots. Needs phase 2, and coordination with the GPU work.

1. In Chlorophyll, import both planes as R8 and RG8 where the pitch allows, and convert in a shader.
2. Keep the CPU path as fallback.

Verify: backboardd frame time with the 720p clip against without. Risk: medium. Compositor rate itself belongs to the GPU plan and is open-ended.

### Phase 4: real web. 2 to 5 days plus boots. Needs Wi-Fi.

1. Add the 10001 entry to `sweet_skywalk_privs`, keyed on `com.apple.private.network.socket-delegate`.
2. Load a plain site, then `m.youtube.com`. Log which format the player requests, through WebInspector if it attaches, or through the Mac as a proxy.
3. Open Telegram Web and log in on a secondary account.

Verify: a short video plays silently, and a message goes both ways. Risk: high that TLS, memory or JIT limits appear only here. YouTube can also change its page at any time.

### Phase 5: sound. 3 to 5 weeks. Needs the ADSP port, shared with battery.

Steps as in section 6. Verify: a boot tone, then Safari's `<video>` audio. Risk: medium, since code exists for the same phone but on another iOS version.

### Phase 6: native clients and notifications. 1 to 2 weeks. Needs phases 2 and 4.

1. Build Telegram-iOS from source, sideload, run.
2. Add the notification job if the client is good enough to keep.
3. Add the on-device installer.
4. Try Yattee, then Signal.

Verify: receive a message while the app is in the background. Risk: medium for Telegram, high for Signal.

## Patches, most valuable first

1. `plans/apps/0003-sweetappcheck.patch` answers the most questions per boot. It depends on 0002.
2. `plans/apps/0001-sweetuicache-containers.patch`.
3. `plans/apps/0002-install-prepare-sideload.patch`.

Apply order is 0001, 0002, 0003. I applied them in that order to a temporary copy of the working tree and all three host checks pass. All three are written against uncommitted edits in `root_tweaks18.py` and `sweetuicache.m`, so a later change to those could need a refresh.

## Biggest unknowns

1. Does JavaScriptCore's JIT run on this kernel with PAC keys off? No process has tried it. Without it YouTube's page will be very slow.
2. Does H264SW get selected and decode correctly? HEVC's neighbouring path does not.
3. Does playback's clock run with no audio device?
4. Does a real page render at all? Nothing has been seen on this phone.
5. Can Wi-Fi bandwidth and stability hold a stream? That belongs to another plan.

## Sources

- [Telegram-iOS README](https://github.com/TelegramMessenger/Telegram-iOS)
- [yt-dlp PO Token guide mirror](https://git.cuqmbr.xyz/mirrors/yt-dlp/wiki/PO%20Token%20Guide.md)
- [Managed Media Source on iPhone, Radiant Media Player](https://payment.radiantmediaplayer.com/blog/at-last-safari-17.1-now-brings-the-new-managed-media-source-api-to-iphone.html)
- [Yattee on SourceForge mirror](https://sourceforge.net/mirror/yattee)
- [Signal-iOS push summary, DeepWiki](https://deepwiki.com/signalapp/Signal-iOS/10.1-push-notification-system)
- [signal-cli captcha registration](https://github.com/AsamK/signal-cli/wiki/Registration-with-captcha)
