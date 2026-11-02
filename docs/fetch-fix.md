# Fetch synchronization candidate

2026-11-02. Built and verified in `EverIOS/fetch-fix`, copied from `label-fix` without images or existing output directories. Root cause and visual repair remain unproven. No phone interaction, adb, fastboot, commit, push, image modification or writes to existing build directories occurred. Existing Mesa objects were read-only linker inputs. Own temporary removals used `/usr/bin/trash`.

## Proven source findings

The premise about A810 using `FLUSH_PER_OVERLAP_AND_OVERWRITE` needs correction. In the linked tree, `freedreno_devices.py:1361` constructs A810 from `a7xx_base`, which sets `has_coherent_ubwc_flag_caches=True` at :822. The existing build's generated `freedreno_devices.h` maps chip `0xffff44010000` to `__info27`, whose property is also true. This holds without a runtime device-property override, which was not measured on the phone.

`tu_pipeline.cc:3752–3790` does implement `FLUSH_PER_OVERLAP_AND_OVERWRITE` for raster-order access, feedback loops or dynamic input attachments. However, its call sites at :4070 and :4333 are guarded by **not** having coherent UBWC flag caches. A810 normally bypasses them. `tu_pipeline_builder_parse_rasterization_order` at :4400 instead emits `GRAS_SC_CNTL.SINGLE_PRIM_MODE=FLUSH_PER_OVERLAP` for an ordered pipeline and `NO_FLUSH` otherwise.

`tu_cmd_buffer.cc:1184` makes that nominally GMEM draw-state group apply to SYSMEM, GMEM and BINNING on coherent-cache hardware. At :5530 and :5587 pipeline binding selects/emits this group. Thus this is pipeline/draw state, not one pass-wide mode retroactively installed for every draw. Switching from an ordinary pipeline to a fetch pipeline does not rewrite earlier draw groups. It does enable the fetch draw's overlap ordering, which is intended to order its fragments against preceding fragments. There is no separately emitted full CCU-to-UCHE barrier on this pipeline switch in the inspected path.

`tu_pipeline_update_rp_state` at :5474 accumulates `rp.sysmem_single_prim_mode`, but this is not a pass-wide register emission. Its consumers include the autotuner and pass-state merge. In particular, the flag helps prefer GMEM for expensive sysmem primitive ordering. It does not install OVERWRITE ordering on preceding ordinary draws. `tu6_emit_render_cntl<A8XX>` at :1068 is empty, as is A7XX's specialization. The A6XX `RB_RENDER_CNTL` code must not be used to explain A810 behavior.

For explicit synchronization, `vk2tu_access` maps shader input-attachment reads to UCHE reads plus CCHE reads around :5856. Colour output writes map to CCU colour writes around :5995. `tu_flush_for_access` records CCU clean plus pending invalidations and consumes those when the destination reads through UCHE. `tu6_emit_flushes` around :351 emits colour/depth clean before corresponding invalidates, UCHE cache events, bindless invalidation and the A7XX+ blit clean when requested. WAIT_MEM_WRITES, WFI and WAIT_FOR_ME follow when their bits are set. Ordinary draws run `tu_emit_cache_flush_renderpass`, but this only emits accumulated flags or debug-requested work. Raster-order fetch normally relies on SINGLE_PRIM_MODE instead of requesting the full flush for every draw.

The Vulkan contract includes ordering across draws for overlapping fragments when raster-order attachment access is enabled. Chlorophyll enables both `rasterizationOrderColorAttachmentAccess` and `dynamicRenderingLocalRead`, and uses GENERAL layouts. A framebuffer-local colour-output-write to fragment-input-read memory barrier with BY_REGION is therefore an available alternative under these features, subject to framebuffer-space stage/access restrictions. No such Vulkan barrier was added here. See the [Khronos local-read proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_dynamic_rendering_local_read.html) and [raster-order sample](https://docs.vulkan.org/samples/latest/samples/extensions/rasterization_order_attachment_access/README.html).

## Mesa main comparison

Compared the linked sources with local `gpu-fix2/mesa-main`, HEAD `1d4b5ef4a5562b6e2dba2cb5aacd60787979ba86`. The relevant primitive-mode choice and coherent-cache gate are unchanged. No later fix for A810 sysmem raster-order fetch was identified in this snapshot or its available history. This is not a claim about every upstream commit after that snapshot.

`c88b6ebd`, “tu: Fix bindless descriptor invalidate sequence”, adds the A8xx SP_CS_BINDLESS_INVALIDATE and SP_GFX_BINDLESS_INVALIDATE writes. The actual linked tree already contains these writes. Applying that fix again would accomplish nothing.

Later main changes include `10b1ef46`, implementing SUBPASS_SLICE_FENCE, `cc1cf17c`, using SUBPASS fences for by-region dependencies, and `99200197`, using them for GMEM invalidation. These improve explicit dependency handling and GMEM coherence. They are not evidence of a correction to the implicit sysmem raster-order path under investigation. The by-region change is broader than simply calling the whole series “GMEM-only”.

## Implemented candidate

Only `sweetfetchsync=0|1` is accepted. Absent, empty, oversized or invalid input selects 0. Mode 2 is deliberately unsupported. The launcher uses `has_arg` and resets `CHL_FETCH_SYNC` before accepting an exact value, preventing inherited environment values from enabling it. Default 0 preserves the baseline and tomorrow's separate flushall/syncdraw experiment until hardware evidence exists.

Chlorophyll tracks potential colour writes by attachment mask within each render encoder. A successful nonempty direct or indexed draw marks the bound attachments whose pipeline write masks are nonzero. Before a draw whose fragment shader fetch mask intersects those prior writes, mode 1 calls a private Turnip helper exactly once and clears the tracked mask. The fetch draw then contributes its own potential writes, so successive writing fetch draws each synchronize. No geometric overlap test is attempted. Depth-test rejection, clipping and shader-discard can cause conservative extra syncs because host state cannot know actual fragment writes.

The private `chl_turnip_fetch_sync` helper forces that render pass to sysmem and records the existing ALL_CLEAN, ALL_INVALIDATE, WAIT_MEM_WRITES, WAIT_FOR_IDLE and WAIT_FOR_ME sequence in the draw command stream. Turnip chooses rendering mode after recording the pass, so this also covers its earlier draws. The established `sysmem` boot already chooses this mode. Unlike a global `flushall,syncdraw` setting, the extra sequence is only inserted before dependent fetch draws. It uses the driver's existing cache emitter and leaves its pending state conservatively intact.

This choice avoids ending rendering. Every colour attachment, depth/stencil aspect and MSAA sample remains in the same rendering instance. Original load, clear, store and resolve actions remain byte-for-byte unchanged. There is no intermediate DONT_CARE store, repeated resolve or forced-clear resume. Pipeline, dynamic state, vertex/index bindings and descriptors remain bound. Cache invalidation does not unbind descriptor state. No restoration sequence is needed because this helper issues only cache events/waits and changes the eventual pass-mode selection.

Fetch-free backdrop passes do not call the helper or force sysmem through this change. Liquid Glass, vibrancy, blur, 192 MB pool, read-back fixes, restart policy, flip, nofp16 default, capture v2 and previous switches remain. The existing capture-only tear probe is unchanged, including its restricted pass eligibility and LOAD resume. A probe boundary can still influence an experiment, so omit `sweetprobe=1` for normal A/B observations.

`rebuild-chl.py` now compiles both local `chl_vk.c` and local `turnip/tu_cmd_buffer.cc`, substituting exactly those two objects at link time. Explicit baseline-source builds still use the original driver object and reproduce the old library byte-for-byte. The helper is hidden, introduces no exported API and is resolved internally. No Mesa source or build tree outside `fetch-fix` was changed.

## Checks and cost

`bash build.sh` passed, including the baseline reproduction, all inherited host tests, iOS arm64 compilation, signing and dependency inspection. `python3 verify.py` passed and checks the private helper is not unresolved, the prior public exports remain, install names/dependencies are unchanged and libdrm is byte-identical. No build remains running.

`test/fetch-check.py` compiles the actual C predicate and launcher parsing into a small host test. It checks no prior-write sync, disabled/no-fetch cases, disjoint attachment masks, four consecutive writing fetch draws producing four syncs, exact flag values, invalid values and oversized input. Source assertions check both draw hooks, unchanged attachment-action translation, no end/begin in the helper, and the inherited probe's LOAD resume. The candidate has no resume path to execute. These tests are not a GPU validation run or proof that A810 honors the requested events.

Cost is at most one added full flush/wait sequence per qualifying draw, zero additional rendering instances, zero added draws and zero additional resolves. For an illustrative frame with 16 qualifying label draws, that is 16 sequences per frame, 960 per second at 60 fps or 1,920 per second at 120 fps. Other fetch effects can increase that count. No honest millisecond or bandwidth estimate is possible without device timings. A command-recording counter logs `[chl] fetch-sync count=N interval=Ts` to stderr at the next qualifying sync after each ten-second interval. It is mutex-protected across recording threads and measures recorded syncs, not completed frames. Idle periods can make the reported interval exceed ten seconds.

## Boot experiments

Install the new `fetch-fix/out-metal` payload through the owner's existing root-assembly workflow first. Old root images do not contain these binaries. No new root image or kernel boot image was created.

1. Tomorrow's global diagnostic control: `sweettu=sysmem,noubwc,nocb,nolrz,flushall,syncdraw sweetfetchsync=0`.
2. Candidate alone: `sweettu=sysmem,noubwc,nocb,nolrz sweetfetchsync=1`.
3. Unmodified synchronization control: `sweettu=sysmem,noubwc,nocb,nolrz sweetfetchsync=0`.

Keep the owner's other board/system boot arguments. nofp16 remains the default. Do not combine the global diagnostic flags with mode 1 when trying to attribute an improvement to selective synchronization.

## Inferred and undetermined

Stale attachment-0 fetch remains consistent with the label mask observation and shader algebra. The source proves the selected ordering mechanism and candidate event sequence, not that the hardware returns stale values. No register/packet capture, device validation, frame timing, actual sync count or tomorrow's global diagnostic result was available. A successful candidate would implicate inter-draw ordering/cache visibility, but its broad cache flush would not isolate CCU visibility from descriptor/cache behavior. It does not repair possible ordering failures between primitives inside one draw.

The copied main snapshot contains no identified matching fix. Actual device feature overrides and exact A810 cache/event semantics remain unverified. The change is a bounded diagnostic repair candidate, not a proven upstream driver fix.

Patch: `EverIOS/fetch-fix/fetch-fix.patch`. It applies over label-fix and adds the modified driver source as a new file. The smaller `fetch-fix/turnip-fetch.patch` shows only the driver change against the linked Mesa source. Full SHA256 values for patch, changed sources and payload are in `fetch-fix/SHA256-fetch.txt`. Build and verification logs are `fetch-fix/build.log`, `BUILD-REPORT.txt` and `VERIFY.txt`.
