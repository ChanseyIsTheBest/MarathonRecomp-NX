# Switch performance changes

Performance work for the Switch build that does not change what is drawn or what the game does: no graphics
setting, resolution or shader output changes. Most of it is a port of UnleashedRecomp-NX's Switch performance work
(its `docs/SWITCH-PERFORMANCE.md` and `SWITCH-PERFORMANCE-AUDIT.md` describe each change and why it is exact),
adapted to Marathon; the rest is new, written for Marathon. Nearly every change has a key in the `[Switch]` section
of `sdmc:/switch/MarathonRecomp/config.toml`.

Measured on the console (the user's tests): perf3 was ~20 % faster on the CPU side and ~80 % on the GPU side than the
GitHub NRO; at the CPU-limited benchmark (Soleanna load-in spot, CPU 1020 MHz, draw-profiler preset) perf4 ran 47 FPS
(game thread ~20.2 ms of work per frame), perf5 54 FPS (~17.3 ms) and perf6 55 FPS (16.84 ms of work plus 1.37 ms of
waiting in Present: perf6's Present without the record wait never ran, see perf7) and perf7 60 FPS, the frame cap
(16.1 ms of work, no wait in Present; from here on CPU savings show in the frame limiter's share, not the FPS); at the
same spot GPU-limited (CPU 2397 MHz, GPU 230 MHz, memory 2733 MHz) perf6 ran 40.3 FPS and perf7 39 FPS (40.5 with its
cascade adoption off, 29 with every GPU change of rounds 1-7 off). `docs/SWITCH-OPTIMISATIONS.md` catalogues every
change with its key, default, files and origin.

- [Config keys and presets](#config-keys-and-presets)
- [What is on by default](#what-is-on-by-default)
- [What is off by default, and how to test it](#what-is-off-by-default-and-how-to-test-it)
- [Build-time changes](#build-time-changes)
- [Profilers and diagnostics](#profilers-and-diagnostics)
- [Not ported, and why](#not-ported-and-why)
- [Review](#review)

## Config keys and presets

Every key is read once at startup (a change needs a restart). When the game saves its options it writes only the
`[Switch]` keys whose value differs from their default (`user/config.cpp`), so a later build's defaults apply to the
rest. Each test package (`dist/switch/MarathonRecomp-perfN/config-presets/`) ships presets to paste as the `[Switch]`
section (a file can have only one):

| Preset | What it is |
|---|---|
| `switch-safe.toml` | turns the riskier runtime changes off (the conservative perf2 defaults), keeping logging and the profilers |
| `switch-verify.toml` | the verification switches, `SwitchResolveStats` and the GPU pass profiler, for one check session |
| `switch-verify-cpu.toml` | `SwitchVerifyNativeVectorMath`: the native vector math runs next to the game's code and is compared |
| `switch-draw-profiler.toml`, `switch-no-profiler.toml` | the draw profiler on (the benchmark setting); the profilers off (clean FPS) |
| `switch-ab-perf8-gpu-off.toml` | perf8: cascade adoption off, the draw profiler on as in the benchmark |
| `switch-ab-sound-thread-core2.toml` | perf8: the sound thread on core 2 again (perf7's placement, which had audio gaps), to confirm the fix |
| `switch-frame-log.toml` | one frame's commands in `stderr.log` once a minute, five times, for the next round's GPU work |
| `switch-ab-all-gpu-off.toml` | every renderer option of rounds 1-7 that changes the GPU's work off, the draw profiler on |
| `switch-ab-gpu-scheduler.toml`, `switch-ab-gpu-subtiling.toml`, `switch-ab-zcull-less.toml` | driver tunings and the ZCULL direction, for an A/B |

The comment above each key in `MarathonRecomp/user/switch/config_*.inl` says what it does.

## What is on by default

### GPU (from Unleashed)

| Change | Key |
|---|---|
| Shader constants through uniform buffers (hardware constant banks) instead of 64-bit pointers; pointers pushed only for shaders that still read them. Marathon's conditional-survey buffer owns descriptor set 4, so the constants use set 5 | `SwitchConstantsUBO` |
| Constant uploads copy only the registers the shaders read; draws without a fragment stage upload no pixel constants | `SwitchTrimConstantUploads`, `SwitchSkipUnusedPixelConstants` |
| Persistent pipeline cache `cache/pipelines.bin`, keyed by the linked driver and the generated shaders, saved in the background when a loading screen ends, checksummed and size-capped. Marathon compiles every pipeline on the render thread the first time it is used, so this matters more than it did for Unleashed | `SwitchPipelineCache` |
| Background pipeline compiler threads restored (Marathon had them commented out) for variants nothing waits for | — |
| Depth-only draws (shadow maps) built without the pixel shader when it cannot affect depth or stencil (632 of 638 pixel shaders qualify; never the lens-flare occlusion survey) | `SwitchDepthOnlyWithoutPixelShader` |
| Unread vertex outputs and unused colour channels not computed | `SwitchTrimVertexOutputs`, `SwitchTrimPixelOutputs` |
| Alpha-test early-out, and sinking of the arithmetic that only feeds kept pixels into it (190 pixel shaders; each one is proven equivalent by a symbolic checker when the shader cache is generated, and the generation fails otherwise) | `SwitchAlphaTestEarlyOut`, `SwitchAlphaTestSink` |
| Blended pixels that leave an 8-bit target unchanged discarded before the blend (never with depth/stencil writes or alpha to coverage) | `SwitchSkipTransparentPixels` |
| `max(a, a)` emitted as `a`, consecutive same-predicate instructions in one `if`, 32-bit swap masks (translator) | — |
| Resolve copies avoided: lazy depth copies, hand-over of the image at clears and at draws (stencil marks + fix-up copy, or none when a draw provably covers the target), colour resolves kept pending over Present, clears that the next draw overwrites skipped, draws that write nothing skipped, early sample transitions. Marathon's depth is D32+S8 and its game uses stencil and conditional surveys, which every condition accounts for; an "owed copy" mechanism keeps the lazy paths exact with Marathon's swizzled depth/L8 views | `SwitchLazyResolves`, `SwitchResolveHandOver`, `SwitchKeepResolvesPending`, `SwitchCoverageHandOver`, `SwitchExactCoverage`, `SwitchSkipOverwrittenClears`, `SwitchSkipNoOpDraws`, `SwitchEagerSampleTransitions` |
| Full-screen copies draw one triangle instead of 1.5 | `SwitchSingleCopyTriangle` |
| Shadow-gather pipelines without the runtime check, once gathers are on | `SwitchShadowGatherSpecialization` |
| plume: redundant binds filtered, host-visible buffers persistently mapped, no thread-local vectors in hot paths | — |

### GPU (new for Marathon)

| Change | Key |
|---|---|
| Cascaded shadow-map array textures as D32 instead of D32+S8 (they never use stencil): half the bytes written by the cascade copies and fetched by every shadow tap | `SwitchDepthArrayTexturesD32` |
| Vertex attribute half-swap masks as a specialization constant from the vertex declaration instead of a shared-constant read per attribute per vertex | `SwitchVertexSwapSpecialization` |
| Lens-flare occlusion survey writes `1` instead of an atomic add per pixel (the counter is only compared with zero) | `SwitchSurveyPlainStore` |
| Gamma pass constants as push constants instead of pointer loads | `SwitchGammaPushConstants` |
| Frame timestamp queries reset one at a time on the 3D engine instead of a copy-engine fill | `SwitchQueryResetPerQuery` |
| Resolve copies no longer force a vertex-constant re-upload | `SwitchCopyKeepsVertexConstants` |

### CPU (from Unleashed)

| Change | Key |
|---|---|
| Render commands batched on the game's D3D thread (several draws per hand-over, larger batches, zero-copy batch buffers, own queue producer, bigger batches while the render thread sleeps). Marathon renders from a loading thread too: ownership follows the device's Acquire/ReleaseThreadOwnership | `SwitchBatchRenderCommands` and the four keys after it |
| Render and sampler states the render thread already has are not sent again | `SwitchSkipRedundantRenderStates`, `SwitchSkipRedundantSamplerStates` |
| Shader constants kept in host byte order, only changed runs copied, swap/compare/store in one pass | `SwitchSparseConstantCopies` |
| Render-thread pipeline and sampler lookup caches | `SwitchPipelineLookupCache`, `SwitchSamplerCache` |
| Render thread prefers core 1 | `SwitchHostThreadCores` |
| Render queue spins ~1,000 times before sleeping instead of moodycamel's 10,000 (`RenderQueueTraits`, `gpu/video.cpp`) | — (always) |
| Native code for type_info comparison/typeid and the D3D constant setters (addresses found by matching Marathon's recompiled code) | `SwitchNativeRtti`, `SwitchNativeShaderConstants` |
| UI aspect-ratio modifier lookups cached | `SwitchModifierCache` |
| Game audio through `audout` from the pump thread instead of SDL's starvable audio thread, bit-identical samples | `SwitchAudioOut` |
| Critical sections and spin locks take the thread id from the caller instead of a thread-local read | — |
| The final critical-section leave drops a full fence its sequentially consistent store and load already order (round 12) | `SwitchLeanCriticalSectionLeave` |

### CPU (new for Marathon)

| Change | Key |
|---|---|
| Native `__RTDynamicCast` for single inheritance with a memo (1,157 call sites) | `SwitchNativeDynamicCast` |
| Native zlib for the archive loader's decompression (Marathon's counterpart of Unleashed's native LZX), checked against all 21,064 compressed entries of the game's 93 archives on the PC | `SwitchNativeInflate` |
| Native `_stricmp`, `strncpy`, `strchr` and the word/byte `memcpy` (overlapping copies run the game's code) | `SwitchNativeCrtStrings` |
| Guest TLS without `-mtp=soft` thread-pointer calls | `SwitchFastGuestTls` |
| Fewer file-system IPCs for file sizes and attributes | `SwitchFewerFileQueries` |
| Frame limiter sleeps to the deadline instead of yield-spinning its last 2-3 ms | `SwitchFrameLimiterSleep` |
| Vibration values not re-sent when unchanged | `SwitchVibrationDedupe` |
| Guest heap: power-of-two rounding with `bit_ceil`, outside the heap lock | — |
| `LOG*` macros do no formatting work when logging is off | — |
| XMA output ring offsets published with release/acquire | — |
| A lost wake-up in the guest dispatcher (generation advanced outside its mutex) fixed | — |

## Keys that needed a console test

Since perf3 every one of these defaults to on; perf3, perf4 and perf5 ran with them on the console. Only three keys
are off: `SwitchShadowGather` (its verify session showed magenta: not exact on the X1), `SwitchThreadIdealCores` and
`SwitchIndexedConstantsFromMemory` (A/B only). If something goes wrong, `switch-safe.toml` turns the riskier ones
off; then enable them a few at a time.

| Key | What to watch |
|---|---|
| `SwitchTextureSizeConstants` | one session with `switch-verify.toml` (menus, Soleanna, a stage, a cutscene): no magenta pixels |
| `SwitchCompactTextureHeap` | `stderr.log` "Texture descriptors: N in use at most" stays well below 16,384 |
| `SwitchPresentOnRenderThread` | loading screens, docked/handheld switching, long play |
| `SwitchSleepingGuestLocks` (+ `SwitchFastCriticalSections`), `SwitchFastEvents`, `SwitchGuestSpinBeforeSleep`, `SwitchSemaphoreWakeCount`, `SwitchRelaxedAtomics`, `SwitchTargetedDispatcherWakeups`, `SwitchCriticalSectionSpin` | audio keeps playing through a stage's first in-engine event and a pre-rendered movie (Unleashed's gate for guest-synchronisation changes) |
| `SwitchAudioThreadCores`, `SwitchXmaEventWait` | the same audio test, plus music and cutscenes |
| `SwitchPreciseBarriers`, `SwitchStableFramebuffers`, `SwitchReadOnlyDepthSampling`, `SwitchUniformStencilClears`, `SwitchSkipRestoreDraws` | A/B with `SwitchResolveStats`, compare screenshots, 30+ minutes of play |
| `SwitchClipDistanceSpecialization` | water reflections, geometry crossing the screen edges |
| `SwitchSurveySlots` | lens flares (Wave Ocean, Kingdom Valley) |
| `SwitchZcull` | long play; Unleashed lists ZCULL among the suspects of its GPU losses |
| `SwitchCascadeAdoption` (perf7) | shadows at Soleanna, in a stage and in cutscenes (A/B with `switch-ab-perf7-gpu-off.toml`); `stderr.log`'s `cascades:` line of the resolve stats (`switch-verify.toml`) |
| `SwitchJobWorkersOffMainCore`, `SwitchSoundThreadOffMainCore` (perf7) | the audio test above; FPS A/B with `switch-ab-perf7-cpu-off.toml` |

## Build-time changes

`tools/build-switch.sh` builds the NRO; see `docs/BUILDING-SWITCH.md` for every option.

- **Guest fences**: the recompiled code's `sync`/`lwsync`/`eieio` are real AArch64 fences. The base port emitted
  nothing, which was only safe on one core, but libnx lets every thread run on any of the application's cores.
- **Direct calls** between recompiled functions (151,618 calls in perf5; 430 fewer than perf4 because three more
  functions are native hooks), so GCC can inline them; hooked functions are never bypassed (checked in the ELF after
  every link).
- **FPSCR mode tracked across labels**, constant vector permute tables, `vperm` as two NEON instructions.
- `-fno-math-errno -fno-trapping-math -fomit-frame-pointer` for the recompiled code; `-ffixed-x16` everywhere,
  because the fault emulation (`os/switch/exception_switch.cpp`) resumes a faulting guest access through x16. Its
  decoder now covers every load/store form the compiler emits. `tools/switch-check-fault-emulation.py --all-functions`
  on the debug ELF (a manual step after each build, not run by `build-switch.sh`) finds none it cannot emulate.
- **LTO** (`SWITCH_LTO=1`), **PGO** (`SWITCH_PGO=generate`, play, copy `sdmc:/switch/MarathonRecomp/pgo` to `pgo/`,
  then `SWITCH_PGO=use`), `-O2` and `-fipa-pta` options as in Unleashed.
- `SWITCH_EXACT_FMA=1` (off): exactly the guest's fused multiply-adds are fused, as on the Xbox 360. Off, the build
  rounds like the base port; on, the last bit of some float results changes compared with it.
- Stamps so unchanged steps are skipped; the three perf patches (`patches/*-switch-perf.patch`) are required.

### Code generation options (`tools/switch-codegen-pass.py` and XenonRecomp)

Register locals and plain guest memory come from Unleashed's XenonRecomp settings and its round 8; the codegen-pass
options and the fewer mode switches from its rounds 11-14.

Applied to XenonRecomp's output before the direct-call pass; `ppc/` is regenerated when any of them changes. A
function with double-precision or vector floating-point arithmetic is left exactly as XenonRecomp wrote it, so no
fused multiply-add can move.

| Build option | Default | What it does |
|---|---|---|
| `SWITCH_INLINE_MEMCPY` | 1 | fixed-size guest memmove/memset calls (size loaded as a constant right before) as inline builtins |
| `SWITCH_LEAF_LOCALS` | 1 | functions that call nothing keep the context's registers in locals (little effect with Marathon's volatile memory) |
| `SWITCH_CONST_VMX_TABLES` | 1 | `lvlx`/`lvrx`-style shuffles with a constant mask row as one NEON `TBL` |
| `SWITCH_INLINE_FP_COMPARE` | 1 | `fcmpu`/`fcmpo` branchless and always inlined (a compare rounds nothing) |
| `dcbt`/`dcbtst` | always | the game's cache hints become `__builtin_prefetch` of the same address |
| `SWITCH_REGISTER_LOCALS` | 1 | CR, CTR, XER and the reservation as C++ locals, no LR/MSR copies (-25 % loads/stores of the recompiled code) |
| `SWITCH_PLAIN_GUEST_MEMORY` | 1 | guest memory accesses not `volatile`; loops keep a compiler barrier (validated on hardware in Unleashed) |
| `SWITCH_NARROW_BARRIER` | 1 | with plain memory: the loop barriers cover guest memory only, not the context |
| `SWITCH_FEWER_MODE_SWITCHES` | 1 | Unleashed round 14: moves, stores, conversions and compares whose result is the same in either FPCR flush mode keep the known one (exact helpers `PPCDoubleToFloatBitsAnyMode`, `compareBits` in `ppc_context.h`). Not `lfs`/`lfsx`/`frsp`: with Unleashed's `PPCFloatToDoubleAnyMode` GCC no longer knew those values were floats, and `tools/switch-fp-check.py` found 7 functions whose fused multiply-adds changed (single-precision products narrowed and fused before); 3-4 % of the game thread and ~7 % of the workers' work was in `msr fpcr` lines in the perf4 profile |
| `SWITCH_WIDE_DFORM` | 1 | register + displacement accesses (0-4095, any from r1) as 64-bit addresses: the register is added to `base` once and the displacements fold into the loads/stores. A 32-bit wrap past 4 GB lands in a 64 KB guard after the window and the fault handler emulates it at the wrapped address (`WrapGuestByte`) |
| `SWITCH_CALL_LOCALS` | 1 | perf7: functions that call keep the registers in locals between calls: before each call every register written since the last one is stored back, after it every register is read again, at each return the written ones are stored back. Only without floating-point products. The register save/restore helpers are inlined there as the same loads and stores. perf8: the functions are chosen from their compiled code (`MarathonRecompLib/config/call_locals.txt`): listed where the locals form has fewer loads and stores than the context form and spills at most 8 more stack slots (5,559 functions; of perf7's 978, chosen by profile, only 229 qualified: the stores before each call and the reloads after it outweighed what they saved) |
| `SWITCH_FPSCR_LOCALS` | 1 | perf7: in those and in leaf functions without floating-point products, the FPSCR's host mode word is a local too: the same switches at the same points; a second check of the mode in a loop finds it set |
| `SWITCH_PREFETCH_HINTS` | 1 | perf7: host prefetches of the scene tree walk's child and sibling nodes (`sub_82594D30`), read with guest loads of fields the walk reads anyway |
| `SWITCH_FP_LEAF_LOCALS` | 1 | perf7: leaf locals for the floating-point leaves in which no plain product reaches an addend on any path (15 functions) |
| `SWITCH_FP_NO_CONTRACT` | 1 | perf7: the hot floating-point functions where the previous ELF has exactly as many fused instructions as guest fused operations and as many plain multiplies as guest separate multiplies (`MarathonRecompLib/config/fp_no_contract.txt`) in locals, compiled with `-ffp-contract=off` (pragma) and their guest fused operations spelled fused: the same operations rounding the same way, now in registers. perf8: the count includes the callees GCC inlined, and a function stays listed only where its locals form has fewer loads and stores (49 functions; most of perf7's 145 had more) |
| `SWITCH_FP_INT_LOCALS` | 1 | perf8: the other floating-point functions in `call_locals.txt` keep their integer registers in locals (as leaves or between calls) while their floating-point and vector registers stay in the context as XenonRecomp wrote them, so GCC sees the same floating-point data flow: each was compiled both ways with the same fused, multiply and add counts, and fewer loads and stores (120 leaves, 688 calling functions; 8.8 % fewer loads and stores in them) |
| `SWITCH_FP_SINGLE_MODE` | 1 | perf7: Havok's MOPP query (`sub_828844E8`, the game thread's hottest function, 5 %) as above, and its vector arithmetic in the scalar code's FPCR mode instead of switching to flush-to-zero around it: its 43 `msr fpcr` leave the hot paths. The same results for every operand and result that is not a denormal (below 2^-126) |

perf4 and perf5 were built with `SWITCH_REGISTER_LOCALS`, `SWITCH_PLAIN_GUEST_MEMORY`, `SWITCH_NARROW_BARRIER` and
`SWITCH_WIDE_DFORM` on, and perf5 with `SWITCH_FEWER_MODE_SWITCHES`; since perf6 those are the defaults, and the
first line of `stderr.log` lists the code generation options `ppc/` was written with.

### perf5: from the perf4 profile (benchmark spot: GPU 6.5 ms, game thread ~20 ms of work per frame)

| Change | Key | Profile share it targets |
|---|---|---|
| NEON byte-swapped copy for vertex/index buffer unlocks and the render thread's swapped uploads (`CopyByteSwapped`, `gpu/video.cpp`); the old loop re-read the buffer's size and source after every 4-byte store into uncached GPU memory | — | `UnlockBuffer<uint32_t>` 5-7 % of the game thread (9 % in stages) |
| Native 4x4 matrix product `sub_82168C48` / `sub_82272648` and box projection `sub_825A1490` (`os/switch/perf/native_vector_math.cpp`): the same simde operations in the same order with the vector registers as locals, no stack copies; checked against the recompiled bodies on 200,000 random cases on the PC (NaNs, denormals, zero w) and in the compiled code (the same fused and unfused instructions) | `SwitchNativeVectorMath`, `SwitchVerifyNativeVectorMath` | ~15 % of the game thread |
| Fewer FPCR mode switches | `SWITCH_FEWER_MODE_SWITCHES` | 3-4 % of the game thread |
| Hot-function list: the 600 hottest functions of the perf4 profile without double or vector FP (`MarathonRecompLib/config/hot_functions.txt`) | `SWITCH_HOT_FUNCTIONS` | instruction cache, all threads |
| Targeted dispatcher wakeups (Unleashed round 14) | `SwitchTargetedDispatcherWakeups` | wake-up system calls on every event set/semaphore release; the audio pump's dispatcher mutex |
| Critical sections spin ~2 µs before the kernel wait (Unleashed round 14) | `SwitchCriticalSectionSpin` | the game thread's kernel waits for the sound thread's critical sections |
| `SwitchShadowGather` **off**: the perf4 verify session showed magenta on texel boundaries (the X1's gather snaps coordinates differently from point fetches) | `SwitchShadowGather` | correctness |

### perf6: from the perf5 profile (two research agents, every candidate checked against the code)

| Change | Key / option | Profile share it targets |
|---|---|---|
| Present without the record wait (Unleashed round 11): double-buffered command memory, brightness and viewport taken with the command, a resizing frame still waits | `SwitchPresentWithoutRecordWait` | ~1.2 ms a frame of the game thread waiting in Present |
| Buffer unlocks snapshotted into the command memory on the D3D thread, written byte-swapped into the GPU buffer by the render thread | `SwitchDeferredBufferUnlocks` | `CopyByteSwapped` ~2.7 % of the game thread |
| Box projection: the reciprocal chain on one lane (scalar `fdiv`, `fmaf` in the guest's operand order, `fneg`), splatted; bit-identical on 200,000 PC cases with the ARM build's fusion | `SwitchNativeVectorMath` | `ProjectBox` 4.2 % |
| Event waits spin ~2 µs before sleeping | `SwitchEventSpin` | 2.1 ms a frame of job waits |
| Heap commits on absolute 16 MB boundaries, the unaligned head split off, 2 MB-aligned backing (4 KB fallback) | — | TLB misses on heap objects |
| Register save/restore helpers through the codegen pass (wide D-form from r1, leaf locals) | codegen pass | ~1.2 % of the game thread |
| Full byte reversal of lvx/stvx as a constant permutation in integer code (`PPC_VECTOR_REVERSE`): lvx/stvx copies fold to plain loads and stores | `SWITCH_CONST_VMX_TABLES` | paired `tbl` copies |
| Job workers prefer cores 1-2 (A/B) | `SwitchJobWorkersOffMainCore` | job waits |
| ZCULL in the "greater" direction: the main pass is reverse Z | `SwitchZcullGreater` | main pass |
| NAK operand reuse (Unleashed's default for its game) | `SwitchOperandReuse` | every shader |
| 2D depth textures as D32_FLOAT (copies at Present and before draws) | `SwitchDepthTexturesD32` | 3.1 ms of depth copies in the GPU-limited segment |
| One conservative barrier batch per survey, carrying an explicit survey-buffer barrier (source stages primed once), instead of every batch to the end of the next frame | `SwitchPreciseSurveyBarriers` | 31 conservative batches a frame |
| Refusal counters for kept-over-Present colour resolves and read-only depth sampling | — | diagnostics |

Checked and deferred: cascade adoption into the shadow-map array (GPU-01, ~4.3 ms in the GPU-limited segment), owed
depth copies aliased or carried (GPU-02/03), register locals across calls (CPU-02) and what comes with it, stencil-less
depth surfaces, predicted exact gather, clears as load ops, SampleLevel(0) taps. Rejected: the lfs denormal guard and PGO
until their effect on fused multiply-adds is established.

### perf7: from the perf6 profile (CPU 1020 MHz: 55 FPS; GPU 230 MHz: 40.3 FPS)

The perf6 log showed the game thread still waiting 1.37 ms a frame in Present: the wait of the record path (the
disassembly of `Video::Present` puts it on `g_recordedCommandList`). `g_needsResize` is set by the first swap chain
creation and by the video options, and nothing in Marathon cleared it (Unleashed clears it in its render director hook),
so every frame took the record wait and perf6's `SwitchPresentWithoutRecordWait` never ran.

| Change | Key / option | Profile share it targets |
|---|---|---|
| `g_needsResize` cleared once Present has recomputed the viewport (its only readers on Switch recompute the same values until the swap chain or a video option changes, which set it again): Present without the record wait runs at last | `SwitchPresentWithoutRecordWait` | 1.37 ms a frame of the game thread |
| Registers in locals between calls in 978 profile-hot functions, the save/restore helpers inlined there, the FPSCR mode word local | `SWITCH_CALL_LOCALS`, `SWITCH_FPSCR_LOCALS` | context loads and stores (25-30 % of the hot functions' instructions) |
| 145 hot floating-point functions in locals without contraction (exact: the perf6 ELF fused exactly their guest fused operations), 15 floating-point leaves where no product reaches a sum | `SWITCH_FP_NO_CONTRACT`, `SWITCH_FP_LEAF_LOCALS` | ~20 % of the samples in recompiled code (animation, CRI mixer, physics) |
| Havok's MOPP query in locals, without contraction, its vector code in the scalar mode (no `msr fpcr` pairs on its hot paths) | `SWITCH_FP_SINGLE_MODE` | 5.1 % of the game thread |
| Box projection: the matrix rows and the flush mode set once, the eight corners as independent chains, folded in the guest's order (bit-identical on 200,000 PC cases, registers and stores included) | `SwitchNativeVectorMath` | 3.5 % of the game thread, workers |
| Prefetch of the scene tree walk's child and sibling nodes | `SWITCH_PREFETCH_HINTS` | 1.8 % of the game thread |
| The engine threads started at `sub_825866A8` prefer cores 1-2 (perf6's A/B, now on), the sound thread (`sub_8255B848`, the CRI mixer) core 2 | `SwitchJobWorkersOffMainCore`, `SwitchSoundThreadOffMainCore` | 2.1 ms a frame of the game thread waiting for them |
| The shadow cascades drawn straight into the layers of a second image made like the cascaded shadow map, which becomes the map's image when it is next used (a swap, as a resolve hand-over): no cascade copies (three cascades: one copy for the slice not drawn); a cascade that needs more goes back to the shadow surface with its content | `SwitchCascadeAdoption` | four 1024x1024 depth copies a frame, 4.6 ms of the GPU-limited frame |

The GPU side has fewer exact changes left: the remaining copies are made because a later draw writes the surface (the
depth copy before a draw), are colour copies whose draw blends (no hand-over), or are made at Present into textures of
another format. Not done: owed depth copies aliased (three copies of 0.1 ms each at 230 MHz), the gamma pass as a
lookup table (texture-bound, slower), `SampleLevel(0)` taps (the same TEX rate on Maxwell), predicted exact gather
(needs an exhaustive test on the console), stencil-less main depth (asynchronous pipeline variants needed), clears as
load ops and per-pass ZCULL direction (NVK changes).

### perf8: from the perf7 logs (CPU 1020 MHz: 60 FPS, the cap; GPU 230 MHz: 39 FPS, 40.5 with cascade adoption off)

The perf7 logs showed three things. Cascade adoption lost 0.9 ms of GPU time: the game binds the shadow map before its
third shadow pass, which then draws on top of the second cascade without a clear, so the map was adopted after two
cascades (two fill-in copies) and the third went back through the shadow surface (a restore copy and a normal copy):
four copies instead of three. The game thread's CPU-limited work went from 16.84 to 16.06 ms, but compiled both ways,
most functions perf7 had put in locals did more loads and stores than in the context form. And the session had 23
audio gaps (perf6: none): the sound thread had moved to core 2, where it time-sliced with the busy engine threads.

| Change | Key / option | What it targets |
|---|---|---|
| Cascade adoption v2: the map takes its held slices at the first draw that samples it (the translator now records each shader's declared samplers in its cache entry), not when it is bound; a cascade that keeps drawing after its resolve continues in the next layer (one transfer copy within the cascade image; where its resolve goes is learned the first frame), so all three cascades are held; layers already equal to the map's slice are not filled in again; the copies between the cascade image and the map are transfer copies. Expected: one layer copy instead of three shader copies (3.45 ms at 230 MHz), plus the cheaper D32 draws | `SwitchCascadeAdoption` | shadow-map copies |
| Locals chosen per function from the compiled code (call locals, integer locals in floating-point functions, the no-contract list), instead of by profile rank | `SWITCH_CALL_LOCALS`, `SWITCH_FP_INT_LOCALS`, `SWITCH_FP_NO_CONTRACT` | loads, stores and spills in every thread's hot code |
| Integer registers in locals in floating-point functions, their floating-point registers left where they were (exact: the same fused, multiply and add counts, checked per function) | `SWITCH_FP_INT_LOCALS` | the engine threads' Havok code and the sound thread (the bottleneck of the heavy stretches) |
| The sound thread back on core 0 | `SwitchSoundThreadOffMainCore` = false | audio gaps |
| UI modifier cache: 4,096 two-way entries instead of 512 direct-mapped (UnleashedRecomp-NX round 15) | `SwitchModifierCache` | `FindCsdModifierEntry`, 0.3 % of the game thread |
| Hot-function list: the perf4 list plus the hottest of the perf7 profile without floating-point products (890) | `SWITCH_HOT_FUNCTIONS` | instruction cache, all threads |
| A PGO training build (instrumented, no LTO) next to the test build | `SWITCH_PGO=generate` | the profile for a `SWITCH_PGO=use` build |

Looked at and not done: the native ADX decoder of UnleashedRecomp-NX round 15 (Marathon's sound thread's hottest
function is a CRI filter step, not the ADX decoder); scalar reciprocal square roots of dot products (11 sites in
Marathon, 816 in Unleashed); copying only the locked range of a vertex buffer (three of the four lock sites lock the
whole buffer); the gamma `pow` skip at gamma 1 (exact for the 8-bit input, but the default brightness gives 1/0.85);
owed copies decided by sampled slots (no copy of the GPU-limited frame was made at a bind); dead copies decided at
submit (needs the frame log first); the "BurnoutBlurFilter" passes (2 ms) are the game's own full-screen copy draws.

### perf9: window size per console mode, handheld GPU profile after undocking (not performance changes)

perf8's CPU and GPU changes measured faster on the console. perf9 adds three things the user asked for:

- **Window size per mode** (`sdmc:/switch/MarathonRecomp/resolution.txt`, written with the defaults when missing:
  `docked=1080`, `handheld=720`; any height from 480 to 1080, the width following for 16:9, or `WIDTHxHEIGHT`). The
  game renders at the window's size (`app.cpp` gives it the viewport when it starts), so this sets its resolution:
  docked now renders 1920x1080 by default instead of 1280x720 (2.25 times the pixels). The size is taken at start,
  from the console's mode then: the game makes its render targets once, so docking or undocking while playing keeps
  that size (the console scales the picture) and the other mode's size applies from the next start.
  UnleashedRecomp-NX makes its swap chain again at the new mode's size when the default display resolution changes,
  because its game remakes its render targets; upstream MarathonRecomp marks that "TODO: implement buffer resize".
  `MarathonRecomp/ui/game_window.cpp`.
- **460.8 MHz handheld GPU profile after undocking**: `SwitchHandheldGpuBoost` (on) used to act only when the game
  started in handheld mode; a thread now requests it whenever the console is in handheld mode.
  `MarathonRecomp/os/switch/perf/perf_switch.cpp`.
- **Frame Generation's description** says not to use it at a 30 FPS cap (all six languages).

### 1.0.3: the release build

perf9's code with profile-guided optimisation (`SWITCH_PGO=use`, the user's training session with the perf9
training build, 257 `.gcda` files) and LTO. The defaults are the release ones: every optimisation the test builds kept
on stays on; logging (`SwitchLog`), the CPU and GPU profilers (`SwitchCpuProfiler`, `SwitchSlowFrameProfileMs`,
`SwitchGpuPassProfiler`, `SwitchGpuSlowFrameMs`) and the stall watchdog (`SwitchStallWatchSeconds`) are off, and the
FPS overlay and the handheld GPU profile no longer open `stderr.log` by themselves, so neither `stderr.log` nor
`MarathonRecomp.log` is written (`crash.log` still is, on a crash). A newly written `resolution.txt` holds
`docked=900` and `handheld=720`. Version 1.0.3 (`MarathonRecomp/res/version.txt`).

The profile is committed in `pgo/` (the folder `SWITCH_PGO=use` reads by default; UnleashedRecomp-NX keeps its profile
the same way). To rebuild 1.0.3:

    SWITCH_PGO=use SWITCH_LTO=1 SWITCH_LTO_JOBS=4 JOBS=6 SWITCH_BUILD_ID=1.0.3-pgo NVK_ROOT=<Mesa install> bash tools/build-switch.sh

The profile-guided compiles of the recompiled code need much more memory than plain ones: `JOBS=12` ran out of memory
with 28 GB (another build running at the same time), `JOBS=6` did not. Only `SwitchPerfInitDiagnostics()` (changed for
the release defaults after the training session) has a profile that no longer matches its code; GCC ignores that one.

Checks of the 1.0.3 ELF: all 221 hooks called, none bypassed (21699 functions called directly); the fault-emulation
scan finds 0 accesses the handler would not emulate and 0 uses of x16 in the 37270 recompiled-function symbols.
`tools/switch-fp-check.py` perf9 -> 1.0.3: 543 of the 993 floating-point functions have different counts, 293 of them a
different share of fused operations. Profile-guided optimisation inlines, unrolls, peels and duplicates code where the
training run spent its time (patterns such as 7 fused -> 14 fused are a block or callee present twice), which changes the
counts without changing results; whether any of these functions also gained a fusion of a separate guest multiply and
add (a rounding change) has not been checked instruction by instruction. If a difference in behaviour from perf9 shows
up, that check (the line table maps each fused instruction to its guest instruction) or a 1.0.3 build without
`SWITCH_PGO=use` (perf9's floating-point code exactly) settles it.

## Profilers and diagnostics

The test builds defaulted them to on (logging, the CPU and GPU pass profilers, the slow-frame reports, the overlay,
the stall watchdog and the handheld GPU profile). Since 1.0.3 logging, the profilers, the slow-frame reports and the
stall watchdog default to off; the overlay and the handheld GPU profile stay on. They only write files. `SwitchLog = true` writes `stderr.log` and `MarathonRecomp.log`; its
first lines name the build and list every `[Switch]` key.

| Key | Output |
|---|---|
| `SwitchGpuPassProfiler` | `[gpu passes]` every 300 frames: GPU time per render pass and resolve-copy group, per-frame counters of every renderer change above, the game thread's frame breakdown, CPU use per thread, audio gaps |
| `SwitchGpuDrawProfiler` | the most expensive draws of the most expensive passes, by shader pair and state |
| `SwitchCpuProfiler` | hottest code addresses per thread every 30 s; `tools/switch-cpu-profile.py stderr.log --elf <debug ELF>` names them (recompiled functions as `sub_XXXXXXXX`) |
| `SwitchSlowFrameProfileMs` (25 in the test builds, 0 since 1.0.3) | with the CPU profiler: every sample of the game thread in frames where it worked at least that long, running or waiting, with its callers ("main in slow frames"); the per-core split and wait stacks of Unleashed's round 12 profiler |
| `SwitchGpuSlowFrameMs` (25) | turns the pass profiler on; `[gpu slow frames]`: the passes (and with the draw profiler, `[gpu slow draws]`: the draw groups) by the GPU time they add in frames at or over the threshold, slow vs all frames, and the three slowest frames whole |
| `SwitchFrameLog` | one frame of render-thread commands, once a minute |
| `SwitchStallWatchSeconds` | `[stall]` dumps when no frame comes for that long, `[hitch]` summaries |
| `SwitchShowProfiler` | the built-in profiler window from the start (no F1 key on the console) |
| `SwitchResolveStats` | per-frame resolve, hand-over and clear counts |
| `SwitchOverlayFps` | FPS and resolution for Status Monitor / SaltyNX |

`tools/switch-gpu-profile.py` turns the pass and draw reports into tables. A crash (CPU exception or lost GPU)
always appends a report to `sdmc:/switch/MarathonRecomp/crash.log`. Keep the `.debug.elf` of every NRO you test.

## Not ported, and why

- Unleashed's four round-10 renderer changes (dead copies, carried clears, skipped depth clears, early depth
  transitions): long play with them lost the GPU.
- Display defaults (docked/handheld resolution scale, window sizes): not performance changes.
- Skinning (`mrgHasBone`) specialization: Sonic '06 has no vertex booleans. Reverse-Z pass skip: no reverse Z.
- Quad-uniform alpha-test sinking (slower in Unleashed), native LZX (Marathon's archives use zlib: see native inflate).
- Unleashed's PGO profile and hot-function list: they belong to Unleashed's code. Marathon needs its own profile.

## Review

After implementation, every area was reviewed adversarially and each finding was checked by a second reviewer
trying to refute it. 18 findings were confirmed (none critical or high) and fixed, among them: guest-memory copies of
the new native hooks no longer go through newlib `memcpy` (which uses x16, see above), native zlib matches the game's
zlib on null pointers, the pipeline cache is checksummed and capped, and a released-but-still-bound resolve target
keeps reading what the base port read.
