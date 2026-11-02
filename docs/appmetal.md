# Metal device for Freeform: trial plan

Date 2026-11-02. Nothing here ran on the phone. Patches are in `plans/appmetal/`, unapplied. Kernel code is not compiled; it passed a patch dry run, and its match logic passed a host test. The inject compiled for iOS.

Tags: **Proven** means I read it in the code, shared cache or a log. **Inferred** means reasoning with no run behind it. **Unknown** means no evidence.

## Verdict

- Injection and setup take hours: two small patches and one boot.
- Real drawing depends on one open question: whether compute kernels work on the A810. PencilKit runs one for strokes, and the only compute kernels the compositor has run on this GPU are `compute_average_luma` and `compute_sum_luma`. NOTES records them hanging (ast9, ast10) and producing garbage, and they are still skipped in ast47.
- If compute works, this is days of fixing small gaps. If compute is broken, it becomes a Mesa a8xx compute debugging job of weeks, and the trial can hang the GPU under the home screen.
- The home screen is not protected from this trial. The trial limits the damage to boots with the new boot-arg and to one hang per boot.

## What PencilKit asks for

Proven from the disassembly (`plans/appmetal/evidence/crash-site.txt`):

| Frame | Meaning |
| --- | --- |
| `PencilKit+0xb178` | Return address after `[descriptor setComputeFunction:fn]`, in the function at `PencilKit+0xb10c` |
| `PencilKit+0xb164` | `fn = [self->0x128 newFunctionWithName:@"paint_kernel" constantValues:... error:0]`, and 0x128 is the library |
| `PencilKit+0xabe0` | The "PreloadShaders" block, which calls `+0xb10c` |
| `PencilKit+0xabac` | `self->0x128 = [self->0x1a8 newDefaultLibraryWithBundle:... error:]`, and 0x1a8 is the device |
| `PencilKit+0x9938` | A small wrapper returns `MTLCreateSystemDefaultDevice`. The resource handler's init does not reject a nil device: its only `cbz` before the store tests `self` |
| `Metal+0x543b4` | `-[MTLComputePipelineDescriptorInternal setComputeFunction:]` raises "computeFunction must not be nil." as a release assertion |

So it is a nil device. The nil device gives a nil library, which gives a nil function, and the setter aborts. Not a missing pixel format or a tile shader. Proven for the chain of nil messages. The nil device itself is Inferred from `sweetappcheck.log` printing `system Metal device: none`, since the crash log has no register dump of it.

Chlorophyll would hit the same assertion if its library load failed, so `newDefaultLibraryWithBundle:` has to work. It is implemented at `next-fix/Chlorophyll/src/bgmtl.m:1372`.

## 1. Smallest way to give only Freeform a device

Use `plans/appmetal/2130-asteroids-app-metal-exec.patch`. It adds the inject to `DYLD_INSERT_LIBRARIES` at exec when `sweetappmetal=<name>` matches the executable's name exactly. The Freeform binary does not change.

- It only matches under `/Applications`, at the top level of a `.app`. SpringBoard, extensions and XPC services never match.
- With `sweetsb=1` it inserts both dylibs in one variable. Two variables would not work, so `sweetexecenv` cannot do this next to the status bar dylib.
- It costs 23 bytes of boot args. `sweetexecenv` would have cost about 75, and the 320-byte limit never comes into play.
- Boot args come to 561 of 607 for the trial, and 576 of 607 for the stub.

The inject patch, `plans/appmetal/inject-appmetal.patch`, accepts only `com.apple.freeform`, and only when the boot-arg names this program. It needs no new file on the root, because `/usr/local/lib/bgmtl/bgmtl-inject.dylib` already exists and a push can replace it.

| Check | Result | Tag |
| --- | --- | --- |
| Binary slice | Freeform is thin arm64e. The inject is arm64, which dyld accepts with `ptrauth_disabled=1` (0131). backboardd is the same mix | Proven |
| `bgmtl.arm` | Missing from the root's `/private/var/db/bgmtl`, and a push cannot create it, so the boot-arg arms the app instead | Proven |
| Entitlements for the GPU user client | Not needed. `sweet_amfi.c` registers a permissive sandbox with only `mpo_policy_syscall` and `mpo_priv_grant`, and there is no IOKit open hook. Freeform has no user-client entitlement and needs no re-sign | Proven in the source, Inferred at run time |
| File access | Only Unix permissions apply, for the same reason. Freeform should run as mobile, and `/private/var/tmp` is world-writable | Inferred |
| Shader cache | `bgmtl.m:1105` uses `chlorophyll-cache/<uid>`. backboardd is uid 501 too (ast47 `u501`), so an app on that path shares its cache. The patch gives Freeform `chlorophyll-cache/freeform` so it cannot evict the compositor's shaders | Proven for the path, Inferred for eviction |
| Log | `bgmtl-app.log` of its own, because `bgmtl.log` may belong to another user. iconservicesagent wrote its state file but nothing to the log (NOTES:789) | Inferred |
| `TU_DEBUG` | Read from `bgmtl-bb.tudebug`, then `sweettu`, the same order as bgl-launch. The default in the inject matches | Proven by reading |
| `IR3_SHADER_DEBUG` | `nofp16`, or `sweetir3`, as bgl-launch does. PencilKit shaders use half floats throughout, so this flag matters here | Proven by reading |
| `CHL_FETCH_SYNC` | 0, or `sweetfetchsync`, and only with `nocb` and no `forcecb`, as bgl-launch does | Proven by reading |
| `BGMTL_TEXPOOL_MB` | 0, so no pool and no memory cost from it. backboardd's 192 MiB cap exists for its per-frame offscreens | Decision |

The app's dead-man and kill switch are its own, `bgmtl.off.com.apple.freeform` and its state file, from the existing per-bundle code. The global `bgmtl.off` also stops it.

Skipped: signing a new Freeform with `addlc.py`. Add it only if the insert route fails on this binary.

## 2. What else PencilKit and Freeform need

The metallib is 34 functions: 21 fragment, 10 vertex and 3 kernel (`evidence/pencilkit-metallib-functions.txt`). It ships as AIR, so Chlorophyll's translator can read it. `PencilKit_iOS.pipelinelib` only has Apple GPU binaries, and a forwarded `newPipelineLibraryWithFilePath:` answers nil, which the code at `+0xaab0` stores without checking.

| Need | State in Chlorophyll | Risk |
| --- | --- | --- |
| **Compute**: `paint_kernel`, `particle_kernel`, `sdfPen_kernel` write stroke vertices to buffers, then a render pass draws them | Implemented, never proven on the A810. Kernel to vertex fetch ordering is not exercised by anything else | **Highest. Hang or garbage geometry** |
| Framebuffer fetch: `uber_fragment` and `copy_framebuffer_fragment` read colour 0, 1 and 4, and PencilKit asks `deviceSupportsFramebufferFetch:` | Supported as input attachments (`air_nir_vk.c:140`). Colour 4 needs five attachments | Medium, Unknown |
| Function constants: 25 calls with `constantValues` | Translator handles `is_function_constant_defined` and its predicate | Low |
| AIR intrinsics: 35 kinds, all maths, conversions and `sample_texture_2d` (`evidence/pencilkit-air-intrinsics.txt`), no atomics or threadgroup memory | The names are in the translator's tables. `sample_texture_2d` goes through its generic parser, which I did not trace | Low. These shaders have not been compiled on a host |
| Tile functions, imageblocks | No tile or imageblock selector or AIR in PencilKit | None |
| `MTLSharedEvent` | PencilKit calls no event selector (`evidence/pencilkit-selectors.txt`) | None |
| Memoryless textures: storage mode 3 in 2 places | `bgmtl.m` has no memoryless case, so Inferred ordinary memory | Low, costs memory |
| Formats: BGRA8 (80), R8 (10), R32Float (55), MSAA sample counts | Native | Low. `metalLayerPixelFormat`'s value and the six-channel path are Unknown |
| `CAMetalLayer` / `MTKView`, `nextDrawable`, `presentDrawable:` | Works for clients: Mercury ran on Chlorophyll with its wallpaper composited by backboardd. The device wraps CA's IOSurface with `newTextureWithDescriptor:iosurface:plane:` at `bgmtl.m:2848` | Low. No new compositor path is needed |
| Unimplemented selectors | The device forwards them and answers nil or zero, and logs `UNIMPLEMENTED` | Finds gaps at run time |

Freeform's own code also links Metal, MetalKit, MPS, CoreImage and RealityKit. I did not trace those. They are Unknown past the drawing path, and 3D objects are out of scope.

## 3. Risk to the compositor

Off without `sweetappmetal=Freeform`: the kernel adds nothing, and the inject's new branch does nothing. The replaced dylib does load in backboardd, and its behaviour there changes only in the name of the log variable.

With it on, the home screen is exposed:

1. **Hangs**. One GPU address space (bank 0, ASID 2) serves all clients, on one ring, with no preemption found. A bad kernel stalls compositor frames until the watchdog fires (`sweetgpufast=1000`, or 5 s with no progress).
2. **Recovery**. A recovery voids fences for all clients, so half-drawn compositor targets get written back (the torn icons in NOTES). ast47 already had 5 recoveries with no app on the GPU, so a recovery during the trial does not by itself blame Freeform.
3. **Lost GPU**. A lost GPU makes backboardd write `bgmtl-bb.off`, and the rest of the boot runs CA's CPU renderer, which NOTES records crashing. Three crashes in 60 s make launchd reboot, and NOTES says a reboot on this phone can land in Android, which formats userdata. bgl-launch's hold reduces this and does not remove it.
4. **Memory**. Each client carries a whole Turnip instance and draws on the same pool (1.5 GiB by `plan-apps.md`). Also each client's GEM table is allocated in the kernel at `GEM_MAX` entries. Freeform's real use is Unknown. A big canvas could push backboardd into failed allocations.
5. **Address leaks across clients**. A client's bad pointer can read or fault in another client's BOs. Same as the existing allow-list clients.

What the patch does about it: the app's watchdog exits it at once when the kernel's recovery count changes or the GPU is lost, and writes its kill switch. So it gets at most one hang per boot, and later launches of Freeform run without a device and crash on drawing, as today. This does not stop the first hang.

## 4. Fallback that stops the crash

`sweetappstub=1` with the same boot-arg registers no device and makes the setter ignore nil. No GPU client exists, so the compositor carries no new risk. Strokes do not appear. It is Inferred to reach a usable board: other assertions may follow that I did not find, and a canvas with no renderer may behave badly.

Software path: I found none. PencilKit's stroke, tile and image rendering all go through Metal textures and pipelines (`evidence/pencilkit-selectors.txt`). `isMetalAvailable` guards some entry points, including text attachment registration. I did not read its implementation, because the ObjC method map did not finish in the time limit, so I cannot say what it returns on a phone with no device or whether forcing it to NO would hide the canvas cleanly. That would be the better fallback if it works.

## Test

One boot, about 30 minutes of the owner's time. Every step is the owner's.

1. Run `plans/appmetal/check.sh`.
2. In the kernel tree, apply `2130-asteroids-app-metal-exec.patch` with `git apply`, then build.
3. In `next-fix/Chlorophyll`, apply `inject-appmetal.patch` with `patch -p1`, then rebuild `bgmtl-inject.dylib` with `next-fix/build.sh`.
4. Replace `/usr/local/lib/bgmtl/bgmtl-inject.dylib` on the root.
5. In `plans/devloop`, apply `build-loop-appmetal.patch` with `patch -p1`, then run `build-loop.sh` to get `next-loop-appmetal-boot.img`.
6. Boot it, let the home screen settle, open Freeform, make a new board, and draw one stroke with a finger. Allow up to 90 s for the first shader compile.

The boot arg is `sweetappmetal=Freeform`.

| See | Means | Next |
| --- | --- | --- |
| Console has `SWEET EXEC: appmetal inject`, `bgmtl-app.log` has `BGMTLDevice registered` | Injection works | Keep drawing |
| No registered line | Insert or dyld refused it, read the log | Use `addlc.py` |
| Stroke appears, no RECOVERY | The trial works | Try a large canvas and other tools |
| No crash, stroke invisible, no RECOVERY | Compute or fetch output is wrong | Run with `/private/var/tmp/bgmtl.faildump` and `bgmtl.trace` |
| `DEAD-MAN ... a GPU recovery` or a RECOVERY line right after the stroke | Compute hangs the A810 | Stop, boot `next-loop-appstub`, treat compute as a Mesa job |
| New abort in Freeform | A gap in Chlorophyll | Read `UNIMPLEMENTED` in `bgmtl-app.log` |

To go back, boot any image without the boot-arg.
