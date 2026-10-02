// [Switch] keys of the renderer area. Included by user/config_def.h inside its __SWITCH__ block.
// CONFIG_DEFINE_HIDDEN("Switch", type, Name, default, requiresRestart);
//
// Read once after the configuration is loaded (SwitchPerfInitRenderer, os/switch/perf/renderer_switch.cpp),
// so every key needs a restart. Performance-only switches: none of them changes what is drawn.

// The game's thread copies only the shader constant registers that changed for each draw (up to four runs per
// stage), instead of the whole span between the first and the last changed one.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSparseConstantCopies, true, true);
// A 16,384-entry texture descriptor heap (64 KB) instead of 32,768, which the driver can read from a hardware
// constant bank instead of memory. Textures beyond it would render black (logged once). Off until the
// "Texture descriptors: N in use at most so far" lines of long play sessions show Marathon stays well below it.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCompactTextureHeap, true, true);
// The port's full-screen copies (resolves and the gamma pass) draw one triangle over the target instead of that
// triangle plus a second one over half of it again.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSingleCopyTriangle, true, true);
// The render thread prefers core 1, away from the game's main thread on core 0 (it may still run on any core).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchHostThreadCores, true, true);
// The render thread keeps its last pipeline and sampler lookups for states it sees again.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPipelineLookupCache, true, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSamplerCache, true, true);
// The frame limiter sleeps until the frame's deadline instead of yield-spinning its last 2-3 ms.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFrameLimiterSleep, true, true);
// The lens-flare occlusion survey marks its counter with a plain store instead of an atomic add per pixel (the
// counter is only ever compared with zero).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSurveyPlainStore, true, true);
// The gamma pass takes its constants as push constants instead of loading them through a pointer.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchGammaPushConstants, true, true);
// The two frame timestamp queries are reset one at a time (3D engine) instead of together (copy engine).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchQueryResetPerQuery, true, true);
// Driver: vertex shader output components the pixel shader never reads are removed when the two are linked
// into a pipeline (NVK_LINK_VARYINGS, the driver's default), unless SwitchMesaEnvironment sets it itself.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLinkVaryings, true, true);
// Driver: ZCULL (the hierarchical depth test, which only skips work whose depth test would fail) in the "greater"
// direction (NVK_ZCULL=greater). The main 1280x720 pass draws with reverse Z (depth test greater-or-equal: every draw
// group of the perf5 draw profile), for which the driver's default "less" direction can cull nothing; the shadow passes
// (less-equal) lose their culling instead. Unleashed's default for the same reason. The image is the same either way.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchZcullGreater, true, true);
// Driver: the shader compiler marks Maxwell operand-reuse slots (NAK_DEBUG=reuse): the same instructions and results,
// fewer register-file reads. Unleashed's default for the game (round 14: 22.63 -> 22.49 ms at its hub). The pipeline
// cache entries of the other setting are not used (the driver's shader key includes it): shaders compile again once.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchOperandReuse, true, true);
// Environment variables for the Mesa/NVK driver, "NAME=value;NAME=value", set before the Vulkan instance is
// created (e.g. "NVK_SWITCH_VS_ONLY_VARYINGS=0"), after the keys above (so NVK_ZCULL or NAK_DEBUG here win).
CONFIG_DEFINE_HIDDEN("Switch", std::string, SwitchMesaEnvironment, "", true);

// Stage 2: how the game's D3D thread hands its render commands to the render thread. Only the moment they
// reach the render thread changes: it receives the same commands, with the same content, in the same order.
// The D3D thread is the thread that owns the device (D3DDevice_AcquireThreadOwnership): the main thread, or
// the loading thread while it draws the loading screen. Other threads send their commands directly, as before.
// Its commands are collected and handed over in batches: at draws, at the points where the game or the port
// waits for them (Present, texture unlocks, ImGui, a change of D3D thread) and when the batch is full.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchBatchRenderCommands, true, true);
// ... every few draws (half a batch) instead of after every draw. Needs SwitchBatchRenderCommands.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchBatchSeveralDraws, true, true);
// ... batches of 256 commands handed over at 128, instead of 128 and 64.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLargerCommandBatches, true, true);
// ... through a queue producer of their own instead of a lookup of the thread's implicit producer.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchRenderQueueToken, true, true);
// ... as the batch's buffer itself, which the render thread runs in place, instead of a copy into the queue.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchZeroCopyBatches, true, true);
// ... of up to 512 commands (handed over at 448) while the render thread sleeps waiting for work: every hand-over
// wakes it with a system call. Needs SwitchLargerCommandBatches.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchIdleRenderThreadBatches, true, true);
// The D3D thread does not send a render state or a sampler state again when the render thread already has that
// value (it applies them as plain assignments).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipRedundantRenderStates, true, true);
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipRedundantSamplerStates, true, true);
// The render thread submits, presents, waits for the GPU to finish the frame slot it reuses and acquires the next
// image by itself; Present only waits until the frame is recorded. Off until tested on the console: loading
// screens (the loading thread presents), docking and undocking, and frame generation on and off.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPresentOnRenderThread, true, true);
// With SwitchPresentOnRenderThread: Present does not wait for the render thread to record the frame either (UnleashedRecomp-NX
// round 11). The wait moves to the start of the next Present (where the render thread has had a whole frame of game work
// to finish); the shader-constant and vertex copies are double-buffered and the values the end of the frame reads are
// taken with its command. A frame that resizes still waits. perf5 profile: ~1.2 ms a frame of the game thread.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPresentWithoutRecordWait, true, true);
// A vertex/index buffer unlock on the D3D thread copies the buffer's bytes into the frame's command memory and the render
// thread writes them, byte-swapped, into the GPU buffer when it reaches the command: the same bytes, at the point the
// D3D thread's write waited for (WaitForPresentTail). perf5 profile: the GPU-memory write was ~2.7 % of the game thread.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchDeferredBufferUnlocks, true, true);

// Stage 3: pipelines and shader constants. Only how fast pipelines are made and how constants reach the GPU change.
// A persistent pipeline cache (cache/pipelines.bin next to the configuration), keyed by the driver the NRO links:
// pipelines compiled in earlier sessions are loaded instead of compiled again at their first draw. The driver only
// hands back binaries it compiled itself from the same shaders and state. Saved when a loading screen ends.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPipelineCache, true, true);
// ... also saved during play, at most once a minute (a save briefly holds the driver's cache lock).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPipelineCacheSaveDuringPlay, false, true);
// Draws without a colour target (shadow maps, depth passes) whose pixel shader can have no effect on depth or stencil
// are drawn without it, so the GPU skips their fragment shading. Never for the lens-flare occlusion survey.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchDepthOnlyWithoutPixelShader, true, true);
// Pixel shaders do not compute the colour channels that are neither written nor blended, alpha tested or used for
// alpha to coverage.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchTrimPixelOutputs, true, true);
// Vertex shaders do not compute the outputs the pipeline's pixel shader never reads (the driver already drops them
// when it links the two: SwitchLinkVaryings).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchTrimVertexOutputs, true, true);
// Shader constants are read from uniform buffers (hardware constant banks) instead of through pointers (memory
// loads), and the pointers are only pushed for the shaders that still read them.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchConstantsUBO, true, true);
// Constant uploads copy only the registers the draw's shaders can read instead of the whole 4 KB / 3.5 KB blocks.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchTrimConstantUploads, true, true);
// Draws without a fragment stage do not upload pixel shader constants (the next draw that has one does).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipUnusedPixelConstants, true, true);
// With SwitchConstantsUBO, the port's resolve copies do not make the next draw upload the vertex constants again.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCopyKeepsVertexConstants, true, true);

// Stage 4: resolves, clears, barriers and framebuffers. Only which GPU work makes the same image changes: every pixel
// every draw reads or writes, and every texture the game samples, holds the same value as before.
// Per-frame averages of resolve copies (and what triggered them), hand-overs, kept, owed and skipped copies, clears,
// barrier batches and framebuffer changes, written to stderr.log every 300 frames. Counting only.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchResolveStats, false, true);
// Barriers get access masks derived from the image layouts they move between instead of "all memory" on both sides:
// the waits stay, the cache flushes and invalidations they did for nothing go. Off until tested on the console (as in
// Unleashed): from a lens-flare occlusion survey to the end of the next frame, barriers keep the full masks.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPreciseBarriers, true, true);
// With SwitchPreciseBarriers: after a conditional survey draw, only the next barrier batch keeps the conservative masks
// (which make the survey's writes visible to every command after it), instead of every batch to the end of the next
// frame. The lens-flare survey runs every frame, so before this the precise masks never applied (perf5: 31 conservative
// batches of 30 a frame).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchPreciseSurveyBarriers, true, true);
// Depth targets get a hierarchical Z (ZCULL) plane (plume: depth targets without TRANSFER_DST). Off: Marathon has never
// run with it, it uses stencil (NVK programs ZCULL without looking at the stencil operations), and Unleashed lists ZCULL
// as the first suspect of its GPU loss in long play. The driver's own switch is NVK_ZCULL (SwitchMesaEnvironment).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchZcull, true, true);
// Draws that can write nothing (no colour channel, no depth, no stencil, not an occlusion survey) are not sent.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipNoOpDraws, true, true);
// A colour target left with pending resolves moves to the sampling layout together with the change of target, instead
// of in a barrier batch of its own in the middle of the next pass.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchEagerSampleTransitions, true, true);
// A depth resolve is not copied for draws that only test that depth buffer, nor at clears that do not clear it; the copy
// is made when anything could see the difference (the depth changes, the texture is bound or updated, Present).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchLazyResolves, true, true);
// A surface resolved into one texture and then cleared completely gives the texture its image instead of being copied
// into it (depth: only when the clear clears depth and stencil).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchResolveHandOver, true, true);
// Colour resolves still pending at the end of a frame stay pending (their textures keep reading the unchanged surface),
// so that the next clear of the surface can hand its image over.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchKeepResolvesPending, true, true);
// A surface resolved into one texture and then drawn into gives the texture its image, and the pixels the draw does not
// write get the old contents back (stencil marks and a fix-up copy), instead of the whole surface being copied first.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCoverageHandOver, true, true);
// ... without the marks and the fix-up when the draw is proven to write every pixel but a thin edge (full-screen passes).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchExactCoverage, true, true);
// A colour clear followed by a draw proven to overwrite every pixel of the target is not made (only its edges).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipOverwrittenClears, true, true);
// Draws that copy a surface's own pending resolve back into it are skipped (on, as in Unleashed; only one Sonic '06 pixel
// shader is a plain copy, so it matters little).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipRestoreDraws, true, true);
// The depth array textures (cascaded shadow maps) are D32_FLOAT instead of D32_FLOAT_S8_UINT: half the bytes copied into
// them and fetched from them; they never use stencil.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchDepthArrayTexturesD32, true, true);
// The same for the game's 2D depth textures (the depth resolves of the main depth: the copies at Present and before
// draws, 2.1 + 1.0 ms of the GPU-limited perf5 frame): the same float depth, half the bytes copied and fetched. Depth
// hand-overs and read-only depth sampling need equal formats and so do not apply to them (both ran 0 times a frame).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchDepthTexturesD32, true, true);
// perf7: the shadow cascades are drawn straight into the layers of a second image made like the cascaded shadow map,
// which then becomes the map's image (a swap, like a resolve hand-over), instead of being drawn into the shadow surface
// and copied into the map's slices: four 1024x1024 depth copies a frame (4.6 ms of the GPU-limited perf6 frame). The
// same draws and depth values; a cascade that needs more (a colour target, the stencil) goes back to the shadow surface
// with its content. Needs SwitchLazyResolves. A/B: off in config-presets/switch-ab-perf7-gpu-off.toml.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchCascadeAdoption, true, true);
// Draws that turn colour writes or the depth and stencil tests off keep the bound framebuffer (with nothing written to
// the attachment they leave out) instead of ending the render pass. Off until tested on the console.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchStableFramebuffers, true, true);
// Draws that only test a depth buffer while sampling its pending resolve attach it read-only and sample it directly
// instead of copying it first. Off until tested on the console.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchReadOnlyDepthSampling, true, true);
// Depth-only clears of a surface whose stencil holds one known value also clear the stencil to that value (the same
// texels, one full clear). Off: the gain is unmeasured.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchUniformStencilClears, true, true);

// Stage 5: what the translated shaders (XenosRecomp-switch-perf.patch) compute differently, chosen per pipeline by
// specialization constants. Every variant computes the same values as the code it replaces.
// The shadow maps' 2D-array sizes (and 2D sizes) are read from the shared constants instead of queried from the texture
// for every fetch: the same integers, converted to float the same way. Off until one SwitchVerifyTextureSizes session
// on the console shows no magenta (the tables must be right on every path that binds a texture).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchTextureSizeConstants, true, true);
// Debug: keep the queries and draw magenta wherever a size in the shared constants differs from them.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyTextureSizes, false, true);
// The cascaded shadow maps' 2x2 point-fetch filter is read with one gather wherever that returns the very same texels
// (point-filtered, single-mip, power-of-two shadow maps). OFF: the perf4 SwitchVerifyShadowGather session on the console
// (2026-10-01) showed scattered magenta pixels over Soleanna's shadowed ground: on the X1 a gather picks another texel
// than the point fetches at some coordinates on texel boundaries (the hardware's coordinate snapping, which
// tfetch2DArrayGatherExact does not model), so the image is not identical. The GPU is not the limit at the benchmark
// spot (6.5 ms of 20).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchShadowGather, false, true);
// Debug: do both and draw magenta wherever a gather differs from the point fetches it replaces.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVerifyShadowGather, false, true);
// With SwitchShadowGather: pipelines whose shadow-map slots are gatherable for the draw are built without the check
// (built in the background; the generic pipeline draws until then).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchShadowGatherSpecialization, true, true);
// Alpha-tested pixel shaders skip the arithmetic between their alpha output and the alpha test for the pixels the test
// discards.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchAlphaTestEarlyOut, true, true);
// ... and also move the arithmetic (and shadow gathers) that only feeds the colour into that early-out, so discarded
// pixels skip it too (every such shader is checked equivalent when the shader cache is generated).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchAlphaTestSink, true, true);
// Blended pixels that would leave an 8-bit target unchanged (transparent or black, depending on the blend) are
// discarded before the blend, which saves reading and writing the target. Never where the pixel writes depth, stencil
// or alpha to coverage.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSkipTransparentPixels, true, true);
// Vertex shaders get the vertex declaration's half-swap masks as specialization constants instead of reading them from
// the shared constants for every attribute of every vertex (the declaration is part of the pipeline already).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchVertexSwapSpecialization, true, true);
// Pipelines drawn with the clip plane off get vertex shaders without the clip distance output, so the GPU has no user
// clip plane to test. Off until compared on the console (water reflections, geometry crossing the screen edges).
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchClipDistanceSpecialization, true, true);
// The lens-flare occlusion surveys each get a fresh slot and a number of their own instead of a counter zeroed by a
// copy before each survey (a copy-engine transfer, barriers and a render pass split per survey). Off until lens
// flares are compared on the console.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchSurveySlots, true, true);
// A/B: bone palettes and other per-vertex-indexed constant arrays read through memory (L1) instead of the constant
// bank (with SwitchConstantsUBO). Which is faster depends on the scene; off keeps the constant bank.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchIndexedConstantsFromMemory, false, true);

// Stage 6: measurement. None of these changes what is drawn; their reports go to stderr.log, which they turn on.
// The GPU time of every render pass (a timestamp at every framebuffer change and run of resolve copies), averaged over
// 300 frames, with per-frame counts of the renderer's work and of what its optimisations saved, where the presenting
// thread spends its frame and each thread's CPU use. The timestamps cost some GPU time.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchGpuPassProfiler, false, true);
// With the pass profiler (turns it on as well): frames whose GPU time reached this many milliseconds get totals of their
// own, per pass (and per draw group with SwitchGpuDrawProfiler), printed as "[gpu slow frames]" next to the same passes
// over all frames, with the three slowest frames whole (0 = off).
CONFIG_DEFINE_HIDDEN("Switch", int32_t, SwitchGpuSlowFrameMs, 0, true);
// ... and of every draw, grouped by shader pair and state, for the most expensive passes (turns the pass profiler on;
// costs more GPU time). Shaders are named so that NVK_SHADER_STATS=1 (SwitchMesaEnvironment) lines can be matched.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchGpuDrawProfiler, false, true);
// The render thread's commands of one frame, in order, once a minute (five times): framebuffers, clears, resolves,
// copies, hand-overs, barriers, surveys and draws.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchFrameLog, false, true);
// The built-in profiler window (frame, GPU and present times, memory) over the game from the start: the console has no
// F1 key to open it.
CONFIG_DEFINE_HIDDEN("Switch", bool, SwitchShowProfiler, false, true);
