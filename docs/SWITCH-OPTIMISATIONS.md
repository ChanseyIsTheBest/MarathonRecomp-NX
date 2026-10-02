# Switch optimisations: the complete catalogue

Every performance change of the Switch build of MarathonRecomp-NX, checked against the code of branch `switch-perf`
(uncommitted work on top of `eb59c3f`). The tree described here is the perf8 source; the rows perf6, perf7 and perf8
added or changed say so in their Status column. `docs/SWITCH-PERFORMANCE.md` is the shorter change list and
`docs/BUILDING-SWITCH.md` the build guide; where they and the code disagree, this file follows the code and lists the
difference under [5. Discrepancies found](#5-discrepancies-found).

- [Legend](#legend)
- [1. Overview](#1-overview)
- [2. Catalogue](#2-catalogue): [CPU code generation](#21-cpu-code-generation-build-options),
  [CPU runtime](#22-cpu-runtime-kernel-threads-synchronisation-audio-file-io),
  [native replacements](#23-native-function-replacements), [renderer CPU side](#24-renderer-cpu-side),
  [GPU](#25-gpu-renderer-shader-translator-driver-and-plume), [memory](#26-memory),
  [diagnostics](#27-diagnostics-and-profilers), [build and tooling](#28-build-and-tooling),
  [correctness changes made alongside](#29-correctness-changes-made-alongside-not-optimisations), [counts](#210-counts)
- [3. Tried and turned off, rejected or not ported](#3-tried-and-turned-off-rejected-or-not-ported)
- [4. Verification tools and checks](#4-verification-tools-and-checks)
- [5. Discrepancies found](#5-discrepancies-found)

## Legend

**Paths.** Repo-relative. Short names used in the tables:

| Short name | Path |
|---|---|
| `video.cpp` | `MarathonRecomp/gpu/video.cpp` |
| `imports.cpp` | `MarathonRecomp/kernel/imports.cpp` |
| `perf/` | `MarathonRecomp/os/switch/perf/` |
| `config_X.inl` | `MarathonRecomp/user/switch/config_X.inl` (runtime keys) |
| XenonRecomp patch | `patches/XenonRecomp-switch-perf.patch` (line numbers are lines of the .patch file) |
| XenosRecomp patch | `patches/XenosRecomp-switch-perf.patch` (idem) |
| plume patch | `patches/plume-switch-perf.patch` (idem) |

**Defaults.** For a runtime key, the literal default of its `CONFIG_DEFINE_HIDDEN("Switch", type, Name, default, ...)`
line, cited as file:line. The tree is in the **"all on" test state** (`MarathonRecomp/user/config_def.h:116-117`): 88 of
the 106 `[Switch]` keys default to `true`, including logging, the profilers, the overlay and the handheld GPU profile; 13
default to `false`; 5 are numbers or strings. Many key comments still say "off until ..." (see
[section 5](#5-discrepancies-found)). For a build option, the default in `tools/build-switch.sh:92-100`.

**Origin.** `U R1-3`: UnleashedRecomp-NX, a change its `docs/SWITCH-PERFORMANCE.md` describes before its round 4 (in
"Notes on each change"). `U Rn`: Unleashed round n. `U final`: its final build. `New`: written for Marathon;
`New (cf. U Rn)`: a Marathon take on an Unleashed idea.

**Status.** `on` / `off` / `A/B`: the current default. `perfN-M`: on in the test builds perfN to perfM, all of which
the user ran on the console (perf2 booted; perf3 to perf7 were measured, see [1.3](#13-measured-results); perf8 is built and
not yet measured). "Not in a test build": never run on the console. "Build-only": a compile-time option, no runtime key.

## 1. Overview

### 1.1 What the work is

MarathonRecomp is a static recompilation of Sonic the Hedgehog (2006): XenonRecomp turns the Xbox 360 executable's
PowerPC code into C++ (`MarathonRecompLib/ppc/`, 36,993 functions in the current tree), XenosRecomp translates the
game's Xenos shaders into SPIR-V, and the port (`MarathonRecomp/`) runs both on the Switch through libnx, plume and a
statically linked NVK (Mesa) Vulkan driver. The performance work touches four layers:

- **Code generation**: XenonRecomp (XenonRecomp patch), a text pass over its output (`tools/switch-codegen-pass.py`), the
  direct-call pass (`tools/switch-direct-calls.py`) and compiler options (`CMakeLists.txt`,
  `MarathonRecompLib/CMakeLists.txt`, `MarathonRecomp/CMakeLists.txt`), chosen with the `SWITCH_*` variables of
  `tools/build-switch.sh`.
- **Runtime**: guest kernel, threads, audio, file I/O and native replacements of hot guest functions, each behind a
  `[Switch]` key of `sdmc:/switch/MarathonRecomp/config.toml`.
- **Rendering**: the renderer (`video.cpp`), plume (plume patch) and the shader translator (XenosRecomp patch).
- **Diagnostics**: logs, CPU and GPU profilers, stall watchdog, crash reports, verification modes.

Most of it is a port of UnleashedRecomp-NX's Switch work (its rounds 1 to 14), adapted to Marathon; the rest is new.
Every key is read once, after `Config::Load()`, by `SwitchPerfOnConfigLoaded` (`perf/switch_perf_init.cpp`: diagnostics,
codegen, kernel, native, audio, renderer, in that order) into plain globals; a change needs a restart. `Config::Save`
writes only the `[Switch]` keys that differ from their default (`MarathonRecomp/user/config.cpp:889`). The first two
lines of `stderr.log` name the build and list every key with its value.

### 1.2 Constraints

- **Pixel-identical image.** Every pixel a draw reads or writes and every texel the game samples holds the same value
  (`config_renderer.inl:92-93`); every shader variant computes the same values as the code it replaces
  (`config_renderer.inl:142-143`).
- **Identical behaviour.** The render thread receives the same commands with the same content in the same order
  (`config_renderer.inl:39-40`). A native replacement leaves guest memory above the stack pointer, r1 and the return
  value exactly as the recompiled code does, and runs the recompiled code for anything it does not reproduce with
  certainty (`perf/native_hooks.h:9-13`).
- **Identical floating-point rounding.** The default build keeps GCC's default contraction, as the base port does
  (`SWITCH_EXACT_FMA=0`). The code generation pass leaves every function with double-precision or vector floating-point
  arithmetic exactly as XenonRecomp wrote it (`tools/switch-codegen-pass.py:34-39`); `tools/switch-fp-check.py` compares
  the fused multiply-adds of those functions between two ELFs.
- **No visual setting changes.** Resolution, display defaults and shader output are untouched
  (`docs/SWITCH-PERFORMANCE.md:191`). `SwitchHandheldGpuBoost` changes the handheld GPU clock only.
- Two runtime keys are not strictly identical to the base port, and say so: `SwitchRelaxedAtomics` gives the game
  PowerPC's own (weaker) ordering for its 54 `stwcx.`/`stdcx.`, and `SwitchCompactTextureHeap` would render a texture
  black if more than 16,384 descriptors were ever in use.

### 1.3 Measured results

Only these measurements exist (all on the user's console):

| Build | Result | Where it is recorded |
|---|---|---|
| perf3-allon against the GitHub NRO | about 20 % better on the CPU and 80 % on the GPU (the user's estimate) | user report |
| perf4-profiler | 47 FPS at the user's CPU-limited benchmark (Soleanna load-in spot; CPU 1020 MHz, GPU 1305 MHz, memory 2733 MHz; `switch-draw-profiler.toml` preset); game thread ~20.2 ms of work per frame | user report; `dist/switch/MarathonRecomp-perf4-profiler/drawprofiler.log` ("game thread 20.21 ms working", e.g. lines 3147 and 16478) |
| perf5 | 54 FPS under identical conditions; game thread ~17.3 ms of work per frame | `dist/switch/MarathonRecomp-perf5/stderr.log`, `[gpu passes]` blocks, "game thread per frame: 17.30-17.33 ms working" (lines 9198-9426, 18091-18471, 26511-26739) |
| perf6 | 55 FPS under identical conditions; game thread 16.8-16.9 ms of work plus 1.34-1.39 ms waiting for the render thread in Present per frame. 40.3 FPS at the same spot GPU-limited (CPU 2397 MHz, GPU 230 MHz, memory 2733 MHz) | user report; `dist/switch/MarathonRecomp-perf6/analysis/cpu-limited-segment.log` ("game thread per frame", lines 10, 87, 164); `dist/switch/MarathonRecomp-perf6/analysis/gpu-limited-segment.log` |
| perf7 | 60 FPS at the CPU-limited spot, the frame cap (game thread 16.06-16.29 ms of work, 0.00 ms waiting for the render thread in Present, ~0.5 ms in the frame limiter); GPU-limited 39 FPS (GPU frame 25.2 ms), 40.5 FPS with `switch-ab-perf7-gpu-off.toml` (cascade adoption off, 24.3 ms) and 29 FPS with `switch-ab-all-gpu-off.toml` (every GPU change of rounds 1-7 off, 37.9 ms); 23 audio gaps over the session (perf6: none) | user report; `dist/switch/MarathonRecomp-perf7/profiler.log` (lines 3479-10913: "game thread per frame"; 121754-126113: `[gpu passes]` at 25.2 ms), `gpu round 7 off.log` (3374-7605), `gpu all off.log` (3651-3758) |

From perf4 to perf5 that is +15 % FPS and -14 % game-thread work, from perf5 to perf6 +2 % FPS and -3 % work (derived
from the rows above); the Present wait perf6 was meant to remove was still there (perf7 found why, see 1.4). Profile shares
quoted in the docs (cited as such, not measured here): at the benchmark spot the GPU frame is ~6.5 ms
(`docs/SWITCH-PERFORMANCE.md:153`), and the game thread spends "about 16 ms computing, 2.3 ms waiting for the worker
threads' jobs, 0.9 ms for the render thread" (`dist/switch/MarathonRecomp-perf5/README-TEST.txt:5-7`).

### 1.4 Test builds

| Build | Build ID / options line | What it added |
|---|---|---|
| perf2 | `perf2-20261001-1902-lto` | Everything `docs/SWITCH-PERFORMANCE.md` lists as on by default (the Unleashed port and the new Marathon changes) and its build-time changes (guest fences, direct calls, FPSCR mode across labels, constant permute tables, `vperm` on NEON, recompiled-code flags, `-ffixed-x16`), with LTO. Conservative runtime defaults: the 20 runtime keys perf3 turned on were off (perf3's `config-presets/switch-safe.toml` lists them), as were logging and the diagnostics. |
| perf3-allon | `perf3-allon-20261001-1955-lto` | The same code with every implemented runtime key on (shadow gathers included), plus `SwitchLog`, the SaltyNX overlay, the handheld GPU profile and a 1 s stall watchdog; from here on `Config::Save` writes only non-default `[Switch]` keys (README-TEST.txt:4-12). |
| perf4-profiler | `perf4-profiler`, options `lto=4 fixed_x16 plain_guest_memory` | Unleashed rounds 11-14 code generation (register locals, plain guest memory, narrow loop barriers, wide D-form, leaf locals, VMX `TBL`, inline FP compare, inline fixed-size memmove/memset, `dcbt` prefetches), the lean critical-section leave, the CPU slow-frame and GPU slow-frame reports; profilers on by default. |
| perf5 | `perf5`, options `lto=4 fixed_x16 plain_guest_memory` | NEON byte-swapped buffer copies, native matrix product and box projection, `SWITCH_FEWER_MODE_SWITCHES`, the 600-function hot list, targeted dispatcher wakeups, critical-section spin; `SwitchShadowGather` off. |
| perf6 | `perf6`, options `lto=4 fixed_x16 plain_guest_memory reglocals fewer_modes leaf_locals vmx_tables fp_compare inline_memcpy narrow_barrier wide_dform hot_functions` | From the perf5 profile (two research agents, 31 candidates, each checked in the main session): present without the record wait, deferred buffer unlocks, scalar reciprocal chain in the box projection, event spin, 2 MB-aligned heap commits, save/restore helpers and the constant lvx/stvx reversal through the codegen pass, ZCULL "greater", NAK operand reuse, 2D depth textures as D32, one conservative barrier batch per survey, refusal counters; job workers off core 0 as an A/B. See `docs/SWITCH-PERFORMANCE.md` (perf6). |
| perf7 | `perf7`, options `lto=4 fixed_x16 plain_guest_memory reglocals fewer_modes leaf_locals vmx_tables fp_compare inline_memcpy narrow_barrier wide_dform call_locals fpscr_locals prefetch_hints fp_leaf_locals fp_single_mode fp_no_contract hot_functions` | From the perf6 profile, everything in one build: Present without the record wait made to run (`g_needsResize` was never cleared, so perf6 waited every frame), call locals in 978 hot functions, FPSCR locals, 145 floating-point functions in locals without contraction, floating-point leaf locals, the MOPP query in one FPCR mode, the box projection as independent chains, scene-walk prefetches, the engine and sound threads off core 0, cascade adoption into the shadow-map array. See `docs/SWITCH-PERFORMANCE.md` (perf7). |
| perf8 | `perf8`, options as perf7 plus `fp_int_locals` | From the perf7 logs and UnleashedRecomp-NX round 15: cascade adoption v2 (adopted at the first draw that samples the map, by the shader sampler masks the translator now records; the third shadow pass continued in the next layer; layers known equal not filled in; transfer copies), locals chosen per function from the compiled code (call locals in 5,559 functions, integer locals in 808 floating-point ones, the no-contract list cut to 49), the sound thread back on core 0, a 4,096-entry UI modifier cache, 890 hot functions. See `docs/SWITCH-PERFORMANCE.md` (perf8). |
| perf8-pgo-generate | `perf8-pgo-generate`, options `fixed_x16 plain_guest_memory ...` (no LTO), `SWITCH_PGO=generate` | The perf8 code instrumented for profile-guided optimisation: it writes `.gcda` files to `sdmc:/switch/MarathonRecomp/pgo/` every 3 minutes, for a later `SWITCH_PGO=use` build of the same code. Slower than perf8 by design. |
| perf9 / perf9-pgo-train | `perf9`, options as perf8; `perf9-pgo-generate` (no LTO, `SWITCH_PGO=generate`) | Window size per console mode (`resolution.txt`), the handheld GPU profile also after undocking, the Frame Generation note; the matching training build. |
| 1.0.3 (release) | `1.0.3-pgo`, options as perf8 plus LTO and `SWITCH_PGO=use` | perf9's code with profile-guided optimisation from the user's perf9 training session (257 `.gcda` files); release defaults: logging, the CPU and GPU profilers and the stall watchdog off (no `stderr.log`, no `MarathonRecomp.log`), every optimisation on as in perf9; `resolution.txt` written with docked 900p and handheld 720p. |

**Reproducing perf4/perf5.** (Since perf6 the defaults of `tools/build-switch.sh` are this code generation, and the
first line of `stderr.log` lists it.) Those builds used non-default code generation options: `SWITCH_LTO=1 SWITCH_LTO_JOBS=4
SWITCH_REGISTER_LOCALS=1 SWITCH_PLAIN_GUEST_MEMORY=1 SWITCH_NARROW_BARRIER=1 SWITCH_WIDE_DFORM=1`, as recorded by
`MarathonRecompLib/ppc/ppc_codegen_stamp.txt` (`register_locals=1`, `fewer_mode_switches=1`),
`MarathonRecompLib/ppc/codegen_pass.txt` (`SWITCH_WIDE_DFORM=1 ... SWITCH_NARROW_BARRIER=1`) and
`build/switch-app/generated/switch_build_id.h` (`lto=4 fixed_x16 plain_guest_memory`). `tools/build-switch.sh` with its
defaults builds different code.

## 2. Catalogue

### 2.1 CPU code generation (build options)

| Optimisation | What it does | Why it is exact | Key / option and current default | Files | Origin | Status |
|---|---|---|---|---|---|---|
| Direct calls | Calls between recompiled functions call `__imp__sub_X` instead of the weak, `noinline` `sub_X` alias, so GCC and LTO can inline them (151,618 calls to 21,699 functions in the current `ppc/`). | The same body; any address named in `MarathonRecomp/` or `MarathonRecompLib/config/` keeps the alias, the indirect-call table is untouched, and the linked ELF is checked for bypassed hooks. | `SWITCH_DIRECT_CALLS=1` | `tools/switch-direct-calls.py`; `tools/build-switch.sh:455-469, 624-630` | U R1-3 | on; perf2-5 |
| Profile-hot functions | The functions with the most samples, among those without double or vector FP, are `PPC_HOT_FUNC_IMPL` (`__attribute__((hot))`, grouped in `.text.hot`): the 600 of the perf4 profile and, since perf8, 290 more of the perf7 profile that have no floating-point product either (890). | An attribute only; FP functions are left out because it can change GCC's unrolling and with it its multiply-add choices; the first 600 keep it for the same reason. | `SWITCH_HOT_FUNCTIONS=1` (`tools/build-switch.sh:92-100`); list `MarathonRecompLib/config/hot_functions.txt` | `tools/switch-direct-calls.py:144-175`; XenonRecomp patch:1087-1089 | U R8 (mechanism), U R14 (FP filter); the list is Marathon's | on; list filled in perf5 (empty before), extended in perf8 |
| Link-time optimisation | Whole-app `-flto` with balanced partitions and `SWITCH_LTO_JOBS` LTRANS jobs. | Optimisation only (for the opt-in exact-FMA mode LTO is not exact: `docs/BUILDING-SWITCH.md:168-177`). | `SWITCH_LTO=0`, `SWITCH_LTO_JOBS=2` | `CMakeLists.txt:114-118`; `thirdparty/lsfg-vk/lsfg-vk-common/src/vulkan/vulkan.cpp` (LTO link fix) | U R1-3 (test NROs since U R7) | off; all test builds used it (perf4/5: 4 jobs) |
| Profile-guided optimisation | `generate`: instrumented build that dumps `.gcda` files every 3 minutes to `sdmc:/switch/MarathonRecomp/pgo/`; `use`: build with them (a profile hash is put on every compile line). | Changes only optimisation decisions. | `SWITCH_PGO=` (empty), `SWITCH_PGO_DIR=pgo` | `MarathonRecomp/CMakeLists.txt` (PGO block); `perf/pgo_switch.cpp`; `tools/build-switch.sh:98-104, 245-247` | U R9-R10 | used for 1.0.3 (profile from the perf9 training build); off by default in `tools/build-switch.sh` |
| `-O2` | All code, or only the recompiled code, at `-O2` instead of `-O3` (instruction cache). | Optimisation level only. | `SWITCH_O2=0`, `SWITCH_RECOMP_O2=0` | `CMakeLists.txt:121-127`; `MarathonRecompLib/CMakeLists.txt:205-208` | U R1-3, U R10 | off; not in a test build |
| `-fipa-pta` | Whole-program points-to analysis at compile and LTO link. | Optimisation only. | `SWITCH_IPA_PTA=0` | `CMakeLists.txt:129-133` | U R10 | off; not in a test build |
| Recompiled-code math flags | `-fno-math-errno -fno-trapping-math` for `MarathonRecompLib` (one `fsqrt` instead of a libm check per square root; if-conversion). | The guest never sees errno or FP flags; `-frounding-math` stays; the fused operations per function were checked unchanged. | always | `MarathonRecompLib/CMakeLists.txt:37-51` | U R1-3 | perf2-5 |
| `-fomit-frame-pointer` | The recompiled code gets x29 as a register (the app keeps frame pointers for crash reports). | Register allocation only. | always | `MarathonRecompLib/CMakeLists.txt:53-56` | U R7 | perf2-5 |
| Plain guest memory and loop barriers | Guest loads and stores are plain instead of `volatile`, so GCC can forward, pair, schedule and drop dead accesses; `PPC_LOOP_BARRIER()` sits on every backward branch, backward switch-table edge and tail call. | PowerPC orders nothing without `sync`/`lwsync`/`eieio` or a reservation, and those stay fences; wait loops still re-read memory at the barrier; MMIO accesses stay volatile; the atomics stay compiler barriers. | `SWITCH_PLAIN_GUEST_MEMORY=0` (CMake option, define `PPC_PLAIN_GUEST_MEMORY` for `MarathonRecompLib` only) | XenonRecomp patch:51-91 (barriers; tail-call barrier), 1091-1130; `MarathonRecompLib/CMakeLists.txt:71-76` | U R8; tail-call barrier New | off; perf4-5 |
| Narrow loop barrier | With plain memory, the barrier clobbers guest memory only (an asm with a 4 GB memory operand), not `PPCContext`, so guest registers stay in host registers through loops. | Every guest access is still reissued and kept in order across it; the context is thread-private. Functions with double or vector FP keep the full barrier. | `SWITCH_NARROW_BARRIER=0` (forced 0 without plain memory, `tools/build-switch.sh:117`) | `tools/switch-codegen-pass.py:326-329, 358-359`; XenonRecomp patch:1101-1123 | U R11 | off; perf4-5 |
| Register locals | CR fields, CTR, XER and the reservation become locals of each function; no LR/MSR bookkeeping (XenonRecomp's own `*_as_local`/`skip_lr`/`skip_msr`, which `Marathon.toml` keeps off). | The game reads none of them before writing it, nor after a call without writing it again (apart from code that never runs); r12 and r14-r31 stay in the context. | `SWITCH_REGISTER_LOCALS=0` (`XENON_RECOMP_REGISTER_LOCALS=1`) | XenonRecomp patch:20-42; `MarathonRecomp/kernel/memory.cpp:190-223` (traps without lr/ctr) | New as a build option (Unleashed sets those options in `UnleashedRecompLib/config/SWA.toml:8-15`) | off; perf4-5 |
| Leaf locals | In a function that calls nothing, every context register it uses (r, f, v, cr, ctr, xer, reserved, lr, msr) is a local, read at entry, written back at every return if it may have been written (5,582 functions). | The same values at return; the FPSCR stays in the context; FP-sensitive functions are skipped. | `SWITCH_LEAF_LOCALS=1` | `tools/switch-codegen-pass.py:112-174` | U R11 | on; perf4-5 |
| Wide D-form | Register + displacement accesses (displacement 0-4095, or any from r1) as 64-bit addresses (`PPC_LOAD_U32_D`...): the register is added to `base` once and the displacements fold into the accesses (596,884 accesses). | It differs only when the 32-bit sum wraps past 4 GB; such an access lands in a never-committed 64 KB guard after the window and the fault handler emulates it at the wrapped address. | `SWITCH_WIDE_DFORM=0` | `tools/switch-codegen-pass.py:226-247`; XenonRecomp patch:1211-1225; `MarathonRecomp/kernel/memory.h:19-23` | U R11; wrapped-address emulation New | off; perf4-5 |
| Constant VMX tables and `TBL` | `VectorMaskL/R` and the shift tables are `alignas(16) constexpr` (fixed rows fold, no reload after guest stores); shuffles indexed by a `VectorMaskL/R` row become one NEON `TBL` (`PPC_VECTOR_TABLE`, 16,889 lines). | `TBL` returns 0 for an index of 16 or more, as the x86 shuffle's sign bit does; those rows hold only 0x00-0x0F or 0xFF. | `SWITCH_CONST_VMX_TABLES=1` (the `TBL` rewrite; the constant tables are always on) | XenonRecomp patch:1303-1349; `tools/switch-codegen-pass.py:250-279` | U R11 | on; tables perf2-5, `TBL` perf4-5 |
| `vperm` on NEON | `simde_mm_perm_epi8_` as one `BIC` and one two-register `TBL` instead of two shuffles and a blend. | The same bytes ("tested exhaustively against this simde path", patch comment). | always (AArch64) | XenonRecomp patch:1356-1371 | New | perf2-5 |
| Inline FP compare | `fcmpu`/`fcmpo`'s CR update branchless (`__builtin_isless`...) and always inlined. | A compare rounds nothing; the same four bits, unordered included. | `SWITCH_INLINE_FP_COMPARE=1` (define in `ppc_config.h`) | XenonRecomp patch:1275-1295; `tools/switch-codegen-pass.py:355-357` | U R12 | on; perf4-5 |
| Inline fixed-size memmove/memset | A call of the hooked memcpy/memmove (`sub_826DF680`, `sub_826DE940`) or memset (`sub_826DFD40`) right after `li r5,N` becomes `__builtin_memmove`/`__builtin_memset` of N bytes (N up to 64 / 256), before leaf locals (527 calls). | The hook made the same copy with the same pointers and set r3 to its low 32 bits, as the line does; `memmove` keeps overlaps exact; only sizes GCC copies inline (no library call, which would use x16). | `SWITCH_INLINE_MEMCPY=1` | `tools/switch-codegen-pass.py:177-223` | U R14 (the inline part only) | on; perf4-5 |
| `dcbt`/`dcbtst` prefetches | The game's cache-touch hints become `__builtin_prefetch` of the same guest address (276 places; XenonRecomp emitted nothing before). | A prefetch never faults and changes no value or order. | always | XenonRecomp patch:263-275, 1132-1144 | U R13 | perf4-5 |
| FPSCR mode across labels | The flush mode at a label is what every branch into it agrees on (a fixpoint, up to 8 passes) instead of unknown after every label, so fewer mode checks and switches. | A mode is assumed only where every way in agrees; unknown otherwise, as before; mid-asm hooks reset it. | always (off only with `SWITCH_CLASSIC_CODEGEN=1`) | XenonRecomp patch:82-91, 659-664, 852-892 | U R8 | perf2-5 |
| Fewer FPCR mode switches | While the VMX (flush-to-zero) mode is known, moves (`fmr`, `fabs`, `fnabs`, `fneg`, `lfd(x)`, `stfd(x)`, `stfiwx`), `fcfid`, `fctidz`, `fctiwz` keep it, `stfs(x)` uses `PPCDoubleToFloatBitsAnyMode` and `fcmpu` `compareBits`; while the FPU mode is known, `vcfsx`/`vcfux`/`vctsxs`/`vrfin`/`vrfiz` keep it. | Each result is the same in either mode (bit operations or exact helpers); `lfs`/`lfsx`/`frsp` are excluded (see section 3); an unknown mode changes nothing. | `SWITCH_FEWER_MODE_SWITCHES=1` (XenonRecomp's own default is 0) | XenonRecomp patch:115-133, 291-460, 921-933, 1024-1078, 1254-1273 | U R14 (level 1 only, narrower) | on; perf5 (perf4 profile: 3-4 % of the game thread in `msr fpcr` lines, `docs/SWITCH-PERFORMANCE.md:148`) |
| Relaxed guest atomics | `stwcx.`/`stdcx.` as relaxed compare-and-swaps (`PPCStoreConditional32/64`) instead of `__sync` ones with a full barrier each (54 sites). | Not identical to the base port: it gives the game PowerPC's own ordering; the guest's fences stay and each CAS stays a compiler barrier. | `SwitchRelaxedAtomics` = true (`config_codegen.inl:9`) | XenonRecomp patch:473-484, 545-556, 978-1015; `perf/codegen_switch.cpp` | U R7 | on; perf3-5 |
| Save/restore helpers through the pass | XenonRecomp's register save/restore helpers (`__savegprlr_N`, `__restgprlr_N`, the FPR and VMX ones) get the code generation pass like the game's functions: wide D-form from r1 and leaf locals. | The same loads and stores. | with `SWITCH_WIDE_DFORM`, `SWITCH_LEAF_LOCALS` | `tools/switch-codegen-pass.py:100` | New | on; perf6-7 (perf5 profile: ~1.2 % of the game thread) |
| Constant `lvx`/`stvx` reversal | The full byte reversal around every `lvx`/`stvx` (`VectorMaskL` row 0) as `PPC_VECTOR_REVERSE`, a constant `__builtin_shuffle`, so GCC folds the two reversals of a copy through registers into a plain load and store. | The same 16 bytes. | `SWITCH_CONST_VMX_TABLES=1` | `tools/switch-codegen-pass.py:716-753`; XenonRecomp patch:1356 | New | on; perf6-7 |
| Call locals | In calling functions the context registers are locals: before each call the ones written since the last call (a dataflow over the function's labels and gotos) are stored, after it every one is read again, at each return the written ones are stored. The 36 register save/restore helpers are inlined there as their loads and stores. perf8: the functions are chosen from their compiled code (`MarathonRecompLib/config/call_locals.txt`, no LTO, the build's flags): listed where the locals form has fewer loads and stores than the context form and spills at most 8 more stack slots; 5,559 functions. | The context holds the same values at every call and return. Refused for a floating-point product, `setjmp`/`longjmp`, a mid-asm hook, a call that is not a statement of its own, or any other use of `ctx`. | `SWITCH_CALL_LOCALS=1` | `tools/switch-codegen-pass.py:344-526` | New (cf. U R11 leaf locals) | on; perf7 (978 functions chosen by profile), perf8 (chosen by code: only 229 of perf7's 978 qualified) |
| FPSCR mode word as a local | In those functions and in leaf functions without floating-point products, the FPSCR's host mode word is a local too, stored and read again with the registers. | The same mode switches at the same points; the local only lets GCC see that a second check finds the mode already set. | `SWITCH_FPSCR_LOCALS=1` | `tools/switch-codegen-pass.py:313-341, 367-526` | New | on; perf7 |
| Prefetch hints | The scene tree walk (`sub_82594D30`) prefetches its child and sibling nodes (`__builtin_prefetch` of the pointers at +28 and +20 of the node, read with guest loads of fields the walk reads anyway). | A prefetch never faults and changes no value or order. | `SWITCH_PREFETCH_HINTS=1` | `tools/switch-codegen-pass.py:598-619` | New | on; perf7 (perf6 profile: 1.8 % of the game thread) |
| Floating-point leaf locals | Leaf locals for the 15 floating-point leaf functions in which no plain product (`fmul`/`fmuls`, followed through moves, `frsp`, `fsel`, `fdiv` and `fsqrt`) reaches an addend (`fadd`/`fsub` or the B operand of a fused operation) on any path. | Each guest fused operation is one expression either way, and with no other product meeting a sum there is nothing else GCC could fuse, whether the registers are locals or context fields. | `SWITCH_FP_LEAF_LOCALS=1` | `tools/switch-codegen-pass.py:169-261` | New | on; perf7 |
| Floating-point functions without contraction | Hot floating-point functions (`MarathonRecompLib/config/fp_no_contract.txt`) in locals, as leaves or between calls, compiled with `-ffp-contract=off` (a pragma around each) and their guest fused operations spelled as explicit fused operations (`__builtin_fma`, `vfmaq_f32`); their FPSCR mode switches stay where they were. perf8: 49 functions, those whose locals form has fewer loads and stores than the context form with the same fused, multiply and add counts. | Listed only where the previous ELF has exactly as many fused instructions as guest fused operations and as many plain multiplies as guest separate multiplies (perf8: counting the callees GCC inlined), i.e. GCC fused exactly the guest's operations; without contraction and with those explicit, the same operations round the same way (checked per function: no-LTO objects before and after, and `tools/switch-fp-check.py` against the previous build). | `SWITCH_FP_NO_CONTRACT=1` | `tools/switch-codegen-pass.py:529-572` | New | on; perf7 (145), perf8 (49) |
| Integer locals in floating-point functions | The floating-point functions in `call_locals.txt` (120 leaves, 688 calling functions) keep their integer registers (r, cr, ctr, xer, the reservation, lr, msr) in locals while their floating-point and vector registers and the FPSCR stay context fields, written and read exactly where XenonRecomp wrote them. | GCC sees the same floating-point data flow (the integer fields lie at other offsets of the context, which it tells apart); each listed function was compiled both ways with the same fused, multiply and add instruction counts; `tools/switch-fp-check.py` checks the fused counts of the linked ELF. | `SWITCH_FP_INT_LOCALS=1` | `tools/switch-codegen-pass.py` (`INTEGER_KINDS`, `localized_name`, the `SWITCH_FP_INT_LOCALS` paths) | New | on; perf8 (8.8 % fewer loads and stores in those functions; the engine threads' Havok code, the sound thread) |
| MOPP query in one FPCR mode | Havok's MOPP query (`sub_828844E8`, the game thread's hottest function) as above, and its vector arithmetic in the scalar code's FPCR mode instead of switching to flush-to-zero around it: its 43 `msr fpcr` leave the hot paths. | The same results for every operand and result that is not a denormal (magnitude below 2^-126): flush-to-zero changes only those. **Not bit-exact for denormal vector values** (risky, accepted for this round). | `SWITCH_FP_SINGLE_MODE=1` | `tools/switch-codegen-pass.py:529-582` | New | on; perf7 (perf6 profile: 5.1 % of the game thread) |

### 2.2 CPU runtime (kernel, threads, synchronisation, audio, file I/O)

| Optimisation | What it does | Why it is exact | Key / option and current default | Files | Origin | Status |
|---|---|---|---|---|---|---|
| Fast guest TLS | `KeTlsGetValue`/`KeTlsSetValue` (1,967 call sites) find the thread's value vector at a fixed offset from the thread pointer instead of through a `thread_local` (two `__aarch64_read_tp` calls with `-mtp=soft`). | The values stay where they are; a thread without a vector, or an index past its end, takes the old path; checked once at start-up. | `SwitchFastGuestTls` = true (`config_kernel.inl:7`) | `imports.cpp:1431-1480`; `perf/kernel_switch.cpp` | New | on; perf2-5 |
| Thread id from the caller's context | `RtlEnterCriticalSection`, `RtlTryEnterCriticalSection` and the spin-lock acquires take r13 from the caller's context instead of the `g_ppcContext` thread-local. | The same value (`g_ppcContext` is that context). | always | `imports.cpp:977-991, 1146-1160`, hooks at the end of the file | U R10 | perf2-5 |
| Sleeping guest locks | Critical-section waiters sleep on the owner word (`svcWaitForAddress`, woken by the final leave, 1 ms safety timeout) and spin-lock waiters sleep 10 µs, instead of `yield()`, which never lets a lower-priority owner run on that core; a weak CAS that failed on a free section retries at once. | The same compare-and-swap, owner and recursion values; only the waiting changes. | `SwitchSleepingGuestLocks` = true (`config_kernel.inl:16`) | `imports.cpp:885-960, 1012-1036` | New (Unleashed's kernel waits on the address without a key) | on; perf3-5 |
| Fast critical sections | With sleeping locks, the final leave skips its `svcSignalToAddress` when nobody waits (waiters count themselves in `LockCount`, which the guest never reads). | The waiter counts itself before the kernel compares the owner, the leaver clears the owner before it reads the count, full fences in between; the 1 ms timeout bounds any miss. | `SwitchFastCriticalSections` = true (`config_kernel.inl:21`) | `imports.cpp:906, 958-975, 1024-1032` | U R9 | on; active perf3-5 (on but inactive in perf2) |
| Lean critical-section leave | The final leave drops the full fence between its sequentially consistent store of the owner and load of the waiter count. | Those two are already ordered (single total order; `STLR` then `LDAR`). | `SwitchLeanCriticalSectionLeave` = true (`config_kernel.inl:24`) | `imports.cpp:913, 958-975` | U R12 | on; perf4-5 |
| Critical-section spin | A critical section another thread holds is watched (reads only) for ~2 µs before the kernel wait. | The same acquiring compare-and-swap. | `SwitchCriticalSectionSpin` = true (`config_kernel.inl:27`) | `imports.cpp:924, 1022, 1102-1120` | U R14 | on; perf5 |
| Spin before sleep (spin locks) | Guest spin locks watch the lock word for ~2 µs before their yield or sleep. | The same compare-and-swap. | `SwitchGuestSpinBeforeSleep` = true (`config_kernel.inl:33`) | `imports.cpp:918, 1128` | U R10 | on; perf3-5 |
| Fast events | Event sets and semaphore releases (and the dispatcher) skip their notify system calls when nobody waits (waiters counted under the object's mutex). | A counted waiter is in the wait or checks its condition under the mutex before it sleeps. | `SwitchFastEvents` = true (`config_kernel.inl:36`) | `imports.cpp:40-45`, `Event`/`Semaphore` | U R9 | on; perf3-5 |
| Semaphore wake count | A release of N units wakes N waiters (one notify each) instead of all of them. | Which waiter wins was always up to the scheduler; a woken waiter takes its unit under the mutex. | `SwitchSemaphoreWakeCount` = true (`config_kernel.inl:38`) | `imports.cpp:46-49, 259-290` | New (cf. U R14 `SwitchSemaphoreWakeOne`) | on; perf3-5 |
| Targeted dispatcher wakeups | Only an event or semaphore that a `KeWaitForMultipleObjects` call waits on advances the dispatcher generation and wakes those calls (the audio pump's); every other set/release skips the global mutex and wake-up. | A waiter registered before the signal is notified, one registered after it sees the new state; resets never satisfy a wait. | `SwitchTargetedDispatcherWakeups` = true (`config_kernel.inl:30`) | `imports.cpp:50-58, 2038-2090` | U R14 | on; perf5 |
| Ideal cores (A/B) | A game thread starts on core n/2 for the Xbox 360 hardware thread n its `SetThreadIdealProcessor` names; still allowed on every core. | Scheduling only. | `SwitchThreadIdealCores` = false (`config_kernel.inl:41`) | `MarathonRecomp/cpu/guest_thread.cpp`, `MarathonRecomp/cpu/guest_thread.h` | U R7 | A/B; perf5 ships `switch-ab-ideal-cores.toml`; no result reported |
| Host thread cores | The render thread prefers core 1; the pipeline compilers and the profiler report writer prefer core 2 (affinity stays every core of the process). | Scheduling only. | `SwitchHostThreadCores` = true (`config_renderer.inl:18`) | `video.cpp:12931-12936, 13640-13645, 1731-1733`; `perf/renderer_switch.cpp:36-45` | U R8 | on; perf2-5 |
| Render-queue spin | The render thread's queue spins ~1,000 times before sleeping instead of moodycamel's 10,000 (it runs at 0x2C, not time-sliced). | The same commands in the same order. | always (`MAX_SEMA_SPINS = 1000`) | `video.cpp:2713-2718` | U R1-3 | perf2-5 |
| Frame limiter sleep | The frame limiter sleeps to the frame's deadline (`svcSleepThread`) instead of yield-spinning its last 2 ms. | The same deadline, never left before it. | `SwitchFrameLimiterSleep` = true (`config_renderer.inl:23`) | `video.cpp:5872-5880` | New | on; perf2-5 |
| Audio through `audout` | The game's audio goes to `audout`, fed from the pump thread (0x2B), instead of SDL's audio thread (0x3B, which can starve and stop for good); surround keeps SDL. | The same frames, converted F32 to S16 exactly as SDL's NEON converter does, instruction for instruction. | `SwitchAudioOut` = true (`config_audio.inl:7`) | `MarathonRecomp/apu/driver/sdl2_driver.cpp:30-140, 219, 475`; `MarathonRecomp/apu/audio_switch.h` | U R6 | on; perf2-5 |
| Audio thread core | The audio pump and the XMA decoder threads are pinned to one core, off the game's main core 0. | Scheduling only; the XMA ring offsets are published with release/acquire. | `SwitchAudioThreadCores` = true, `SwitchAudioThreadCore` = 2 (`config_audio.inl:12-13`) | `perf/audio_switch.cpp:24-69`; `MarathonRecomp/apu/xma_decoder.cpp` | New | on; perf3-5 |
| XMA event wait | XMA decoder threads sleep until a pass can make progress (woken by resume, submit, flush, destroy) instead of waking every 2 ms. | A pass that would change nothing is not run; a wake generation covers the races; the timeout still bounds anything outside the protocol. | `SwitchXmaEventWait` = true (`config_audio.inl:17`) | `MarathonRecomp/apu/xma_decoder.cpp:517-600`; `MarathonRecomp/apu/xma_decoder.h` | New | on; perf3-5 |
| Vibration dedupe | `hidSendVibrationValues` (an IPC per `XamInputSetState`) is skipped when the same values go to the same devices with the same controllers; resent every 250 ms and after any controller change. | The hid service keeps playing the last value it was sent. | `SwitchVibrationDedupe` = true (`config_audio.inl:21`) | `MarathonRecomp/hid/driver/switch_hid.cpp:22-75` | U R13 (there only repeated stops) | on; perf2-5 |
| Fewer file queries | The size of a read-only handle comes from `fstat` of its descriptor (one IPC) instead of `std::filesystem::file_size(path)` (about five); `GetFileAttributes` does one `status` instead of two. | The game cannot delete or rename files, so the handle's path names its file; anything else takes the path. | `SwitchFewerFileQueries` = true (`config_kernel.inl:10`) | `MarathonRecomp/kernel/io/file_system.cpp:15-40, 192-230` | New (cf. U R11 "file sizes from the open handle") | on; perf2-5 |
| Guest heap rounding | o1heap's fragment size by `std::bit_ceil` (one instruction instead of a doubling loop), computed outside the heap lock. | The same value for every size. | always | `MarathonRecomp/kernel/heap.cpp:32-38, 106-108, 139-141` | New | perf2-5 |
| No logging work when logging is off | `LOG*` macros test `IsEnabled()` before formatting; the "Loading file" line no longer takes the commit mutex (`IsRangeCommitted`) when nothing is logged. | No argument of a `LOG*` call has side effects. | follows `SwitchLog` (= true in this tree, `config_diagnostics.inl:8`) | `MarathonRecomp/os/logger.h:9-17`; `MarathonRecomp/app.cpp:139-142` | New | perf2-5 |
| Event spin | A wait without timeout on an event that is not set watches it (reads only) for ~2 µs before sleeping. | The same wait and wake conditions; the spin reads a hint written with the event's state under its mutex and still takes the event through the usual path. | `SwitchEventSpin` = true (`config_kernel.inl:33`) | `imports.cpp:59-68, 113, 134` | New (cf. U R14 spins) | on; perf6-7 (perf5: 2.1 ms a frame of job waits) |
| Engine threads off core 0 | The game's engine threads started at `sub_825866A8` (its job workers and the other per-frame threads the game thread waits for) prefer cores 1 and 2 in turn instead of core 0, where the game thread runs; still allowed on every core. | Scheduling only. | `SwitchJobWorkersOffMainCore` = true (`config_kernel.inl:38`) | `MarathonRecomp/cpu/guest_thread.cpp:33, 139-143` | New | on since perf7 (A/B in perf6) |
| Sound thread off core 0 | The game's sound thread (`sub_8255B848`, the CRI mixer, ~20 % of a core) prefers core 2 with the host audio threads. | Scheduling only. | `SwitchSoundThreadOffMainCore` = false (`config_kernel.inl`) | `MarathonRecomp/cpu/guest_thread.cpp:38-39, 144-145`; `perf/kernel_switch.cpp` | New | on in perf7, off since perf8: perf7 had 23 audio gaps (guest threads all run at the time-sliced 0x3B, so on core 2 it shared its time with whichever engine thread landed there); A/B: `switch-ab-sound-thread-core2.toml` |

### 2.3 Native function replacements

Each hook runs the recompiled code when its key is off or when it cannot reproduce the result with certainty
(`perf/native_hooks.h`).

| Optimisation | What it does | Why it is exact | Key / option and current default | Files | Origin | Status |
|---|---|---|---|---|---|---|
| Native RTTI | `type_info::operator==` (`sub_826DE850`, 1,124 call sites) as `strcmp` of the decorated names, with a lock-free 1,024-entry memo of in-image pairs; `__RTtypeid` (`sub_826DEE58`) from the complete object locator. | The guest loop is `strcmp(...) == 0`; in-image names never change; the throwing cases run the game's code. | `SwitchNativeRtti` = true (`config_native.inl:7`) | `perf/native_rtti.cpp:31-93` | U R8 (memo U R9, one table U R10) | on; perf2-5 |
| Native `dynamic_cast` | `__RTDynamicCast` (`sub_826DF418`, 1,157 call sites) for single-inheritance hierarchies, with a 2,048-entry memo per (hierarchy, source, target). | The guest's arithmetic; multiple/virtual inheritance, throwing casts and RTTI outside the image run the game's code; identical on the PC for 5,106 classes (567,480 casts). | `SwitchNativeDynamicCast` = true (`config_native.inl:11`); verify `SwitchVerifyNativeDynamicCast` = false (`:13`) | `perf/native_rtti.cpp:95-299` | New | on; perf2-5 |
| Native shader-constant setters | The D3D device's float constant setters (`sub_82546818` vertex, `sub_82546928` pixel) as one copy of r6 × 16 bytes, an OR into the dirty word and the guest's spill of r7. | Byte-identical when the ranges do not overlap; overlaps, the two stack slots and wrapping ranges run the game's code. | `SwitchNativeShaderConstants` = true (`config_native.inl:15`) | `perf/native_shader_constants.cpp` | U R8 | on; perf2-5 |
| Native CRT strings | `_stricmp` (`sub_826E6BB0`, 491 sites), `strncpy` (`sub_826DFCE0`, 109), `strchr` (`sub_826E49D0`, 39) and the CRT's second memcpy (`sub_826DFAA0`, 36) 8 bytes at a time instead of one byte per volatile access. | The same bytes and r3; 8-byte reads only within the page the guest reads; overlapping or wrapping ranges and a `c` outside 0-255 run the game's code. | `SwitchNativeCrtStrings` = true (`config_native.inl:17`) | `perf/native_crt.cpp:8-160, 295-439` | New | on; perf2-5 |
| Native inflate | The archive loader's one-shot zlib inflate (`sub_825BE758`) by the host's zlib into a buffer of its own, copied to the destination when the stream ends. | A valid stream's output is unique; any other case runs the game's decoder on untouched memory; identical on the PC for 21,064 entries of 93 archives (3.4 GB). | `SwitchNativeInflate` = true (`config_native.inl:20`); verify `SwitchVerifyNativeInflate` = false (`:22`) | `perf/native_inflate.cpp`; `MarathonRecomp/CMakeLists.txt` (links `z`) | New (Marathon's counterpart of U R9 native LZX) | on; perf2-5 |
| UI modifier cache | The aspect-ratio modifier lookups (per scene, cast node and cast drawn) are cached per address, tagged with the generation of the loaded-path map; the first thread gets a table outside TLS (perf8: 4,096 entries in 2,048 sets of two, most recent first; 512 direct-mapped before), others 64 in TLS. | Any load or free of a CSD project bumps the generation; entries point into a constant map. | `SwitchModifierCache` = true (`config_native.inl:24`) | `MarathonRecomp/patches/aspect_ratio_patches.cpp` (`FindCsdModifierCached`) | U R8 (owner table U R10, two-way 4,096 U R15) | on; perf2-8 (larger in perf8) |
| Native vector math | The 4x4 matrix product `sub_82168C48` (413 call sites) and its twin `sub_82272648` (16), and the box-corner projection `sub_825A1490` (1), with the vector registers as locals and no stack copies. | The same simde operations in the same order; each `vmaddfp`/`vnmsubfp` is one expression in both, contracted alike; an unaligned r1 or overlapping inputs run the game's code. | `SwitchNativeVectorMath` = true (`config_native.inl:27`); verify `SwitchVerifyNativeVectorMath` = false (`:30`) | `perf/native_vector_math.cpp` | New | on; perf5 (~15 % of the game thread in the perf4 profile, `docs/SWITCH-PERFORMANCE.md:158`) |
| Box projection: scalar reciprocals | The box projection's reciprocal chain on one lane (scalar `fdiv`, `fmaf` in the guest's operand order, `fneg`), splatted. | Bit-identical on 200,000 PC cases with the ARM build's fusion. | `SwitchNativeVectorMath` = true (`config_native.inl:27`) | `perf/native_vector_math.cpp:341-433` | New | on; perf6-7 (perf5 profile: 4.2 %) |
| Box projection: independent corners | The matrix rows and the flush mode set once, the eight corner transforms as independent chains, the bounds folded in the guest's order, the final registers rebuilt as the recompiled code leaves them. | Bit-identical on 200,000 PC cases, registers and stores included. | as above | `perf/native_vector_math.cpp:341-433` | New | on; perf7 (perf6 profile: 3.5 % of the game thread) |

### 2.4 Renderer CPU side

| Optimisation | What it does | Why it is exact | Key / option and current default | Files | Origin | Status |
|---|---|---|---|---|---|---|
| Batched render commands | The D3D thread's commands go to the render thread in batches, flushed at draws, texture unlocks, ImGui, the Present sequence and a change of D3D thread. The D3D thread follows the device's `AcquireThreadOwnership`/`ReleaseThreadOwnership` (the main thread, or the loading thread during loading screens). | The same commands, content and order: each carries its data or is flushed first; each change of D3D thread is a full queue barrier. | `SwitchBatchRenderCommands` = true (`config_renderer.inl:45`) | `video.cpp:2276-2290, 2727-2740, 15150-15175` | U R1-3; ownership hand-over New | on; perf2-5 |
| Several draws per hand-over | A batch goes out at half its capacity instead of after every draw. | As above. | `SwitchBatchSeveralDraws` = true (`config_renderer.inl:47`) | `video.cpp:11004-11010` | U R7 | on; perf2-5 |
| Larger batches | 256 commands, handed over at 128 (was 128 and 64). | As above. | `SwitchLargerCommandBatches` = true (`config_renderer.inl:49`) | `video.cpp:2813-2817` | U R8 | on; perf2-5 |
| Queue token | The D3D thread's batches use a moodycamel producer of their own instead of a per-enqueue lookup of the implicit producer. | The ownership hooks keep a thread's direct commands and batches in order. | `SwitchRenderQueueToken` = true (`config_renderer.inl:51`) | `video.cpp:2806-2811` | U R8 | on; perf2-5 |
| Zero-copy batches | A batch is handed over as its buffer, run in place and returned to a pool; copied as before when none is free. | The same commands in the same order. | `SwitchZeroCopyBatches` = true (`config_renderer.inl:53`) | `video.cpp:2741-2750` | U R9 | on; perf2-5 |
| Idle render-thread batches | While the render thread sleeps waiting for work, batches go out only when nearly full (512, at 448): each hand-over wakes it with a system call. | As above. | `SwitchIdleRenderThreadBatches` = true (`config_renderer.inl:56`) | `video.cpp:2813-2817` | U R10 | on; perf2-5 |
| D3D-thread test by register | "Is this the D3D thread?" compares the thread's TLS region (`TPIDRRO_EL0`) instead of calling `pthread_self` or reading a `thread_local`. | Every live thread has its own TLS region. | always | `video.cpp:2276-2290` | U R9-R10 | perf2-5 |
| Redundant render and sampler states | The D3D thread does not send a render or sampler state the render thread already has; a state from another thread or a change of D3D thread clears the memory. | The render thread applies these as plain assignments. | `SwitchSkipRedundantRenderStates` = true, `SwitchSkipRedundantSamplerStates` = true (`config_renderer.inl:59-60`) | `video.cpp:2964-2975` | U R8 | on; perf2-5 |
| Present on the render thread | Present waits only until the frame is recorded; the render thread submits, presents, waits for the reused frame slot and acquires the next image while the game thread goes on. | Only the game's own presents; the D3D thread waits for it before locking a buffer or texture. | `SwitchPresentOnRenderThread` = true (`config_renderer.inl:64`) | `video.cpp:2305-2310` | U R10 | on; perf3-5 |
| Sparse constant copies | The game thread copies only the runs of changed constant registers (up to four) instead of the whole span. | The render thread's array already holds the unchanged registers. | `SwitchSparseConstantCopies` = true (`config_renderer.inl:9`) | `video.cpp:11020-11030` | U R7 | on; perf2-5 |
| Constants in host byte order | Constants are byte-swapped once when set (swap, compare and store in one pass), and uploads are plain copies. | The same bytes reach the GPU. | always | `video.cpp:11357-11365, 11664, 11738` | U R1-3, U R7 | perf2-5 |
| Trimmed constant uploads | Uploads copy only the registers the draw's shaders can read (XenosRecomp counts them; whole block with relative addressing), rounded to 256 bytes, reserving the whole block. | Nothing past them is read. | `SwitchTrimConstantUploads` = true (`config_renderer.inl:86`) | `video.cpp:990-1000, 10450-10460, 11655-11660`; `MarathonRecompLib/shader/shader_cache.h`; XenosRecomp patch:1222 | U R6 | on; perf2-5 |
| Skipped pixel constants | Draws without a fragment stage upload no pixel constants (they stay dirty for the next draw). | Such a draw reads none. | `SwitchSkipUnusedPixelConstants` = true (`config_renderer.inl:88`) | `video.cpp:11648-11650` | U R5 | on; perf2-5 |
| Copies keep vertex constants | With the uniform-buffer path, the port's resolve copies no longer force a vertex-constant re-upload. | The block uploaded last is still bound and intact; its pointer is pushed again before a draw that reads pointers. | `SwitchCopyKeepsVertexConstants` = true (`config_renderer.inl:90`) | `video.cpp:7740-7750` | New | on; perf2-5 |
| Lazy pointer pushes | With the uniform-buffer path, the three constant pointers are pushed only before draws whose shaders still read them (decided from their SPIR-V). | Shaders that read them get them as before. | follows `SwitchConstantsUBO` | `video.cpp:3501-3530` | U R1-3 | perf2-5 |
| Pipeline lookup cache | The render thread keeps its last pipeline lookups, keyed by the raw state (and the gatherable slots). | The lookup reads nothing else; a new pipeline starts a new generation. | `SwitchPipelineLookupCache` = true (`config_renderer.inl:20`) | `video.cpp:10684-10700` | U R8 | on; perf2-5 |
| Sampler cache | Recent sampler state words keep their converted description and descriptor. | The description is a pure function of those bits; descriptors are never removed. | `SwitchSamplerCache` = true (`config_renderer.inl:21`) | `video.cpp:11239-11250` | U R8 | on; perf2-5 |
| Persistent pipeline cache | `cache/pipelines.bin`, keyed by the linked driver's SHA-256 and the shader cache's SHA-256, CRC32C-checked, dropped past 96 MiB, saved by a 0x3B thread 30 presents after loading-screen updates stop. | The driver hands back only binaries it compiled itself from the same shaders and state. | `SwitchPipelineCache` = true (`config_renderer.inl:70`); `SwitchPipelineCacheSaveDuringPlay` = false (`:72`) | `perf/renderer_switch.cpp:78-106`; `video.cpp:5635-5715`; plume patch:41-46, 362-385; `MarathonRecomp/patches/loading_patches.h`; `MarathonRecomp/CMakeLists.txt` (driver and shader IDs) | U R1-3 (save at loading end U R5, driver key U R10); shader key, checksum, size cap New | on; perf2-5 |
| Pipeline compiler threads restored | Background threads (core 2 with host thread cores) build pipeline variants nothing waits for; the render thread draws with the state's own pipeline until they arrive. | The variants give the same image. | always | `video.cpp:13560-13650` | Unleashed's compiler threads (background variants since U R4), which Marathon had commented out | perf2-5 |
| NEON byte-swapped copies | Vertex/index buffer unlocks and the render thread's swapped uploads copy 64 bytes per step with NEON (`vrev`) instead of 4 bytes per step re-reading size and source. | The same elements with their bytes reversed. | always | `video.cpp:905-933, 1024, 4901` | New | perf5 (`UnlockBuffer` 5-7 % of the game thread, `docs/SWITCH-PERFORMANCE.md:157`) |
| Present without the record wait | Present does not wait for the render thread to record the frame; the wait moves to the start of the next Present. Command memory is double-buffered, brightness and viewport are taken with the command, a resizing frame still waits. perf7: `g_needsResize` is cleared once Present has recomputed the viewport; nothing in Marathon cleared it before, so in perf6 every frame took the record wait. | The render thread gets the same commands with the same contents in the same order. The flag's readers on Switch recompute the same viewport until the swap chain or a video option changes, and those set it again (`GameWindow::Update`'s use is a no-op on Switch). | `SwitchPresentWithoutRecordWait` = true (`config_renderer.inl:77`) | `video.cpp:5901-5995` | U R11 | on; perf6 (did not run), perf7 (perf6: 1.37 ms a frame of the game thread) |
| Deferred buffer unlocks | A vertex/index buffer unlock on the D3D thread copies the bytes into the frame's command memory; the render thread writes them byte-swapped into the GPU buffer when it reaches the command. | The same bytes, at the point the D3D thread's write waited for (`WaitForPresentTail`). | `SwitchDeferredBufferUnlocks` = true (`config_renderer.inl:81`) | `video.cpp:5089` | New | on; perf6-7 (perf5 profile: ~2.7 % of the game thread) |

### 2.5 GPU (renderer, shader translator, driver and plume)

| Optimisation | What it does | Why it is exact | Key / option and current default | Files | Origin | Status |
|---|---|---|---|---|---|---|
| Constants in uniform buffers | Translated shaders read constants from dynamic uniform buffers (set 5; set 4 is the conditional survey buffer), served by NVK from hardware constant banks, instead of 64-bit pointer loads. | Both paths read the same bytes of the same upload allocation; a spec constant compiles the other out. | `SwitchConstantsUBO` = true (`config_renderer.inl:84`) | `video.cpp:281-300`; XenosRecomp patch:332-337; plume patch:72-110 | U R1-3 (set 4 there) | on; perf2-5 |
| Hand-written shaders on the UBO path | `csd_vs`, `csd_no_tex_vs` and `blend_color_alpha_ps` read their constants from set 5 when the pipeline has the UBO bit. | The same registers. | follows `SwitchConstantsUBO` | `MarathonRecomp/gpu/shader/hlsl/csd_vs.hlsl`, `csd_no_tex_vs.hlsl`, `blend_color_alpha_ps.hlsl`; `tools/generate-switch-app-shaders.sh` | U R4 | perf2-5 |
| Compact texture heap | 16,384 texture descriptors (64 KB) instead of 32,768, so NVK reads them from a constant bank. | The same indices hold the same descriptors while fewer than 16,384 are in use; beyond that a texture would get the null descriptor (logged once). | `SwitchCompactTextureHeap` = true (`config_renderer.inl:13`) | `video.cpp:652-705, 4289` | U R1-3 | on; perf3-5 |
| Depth-only draws without pixel shader | Draws without a colour target whose pixel shader can affect neither depth nor stencil are built without a fragment stage; never the survey. | The SPIR-V analysis proves no depth/stencil/sample-mask/memory write and no kill the pipeline keeps; no alpha to coverage. | `SwitchDepthOnlyWithoutPixelShader` = true (`config_renderer.inl:75`) | `video.cpp:10415-10448`; `perf/spirv_analysis_switch.cpp` | U R1-3 | on; perf2-5 |
| Trimmed vertex outputs | Vertex shaders write 0 to the output components the pixel shader never reads (all of them without a fragment stage), via 72 spec-constant bits. | Nothing reads them. | `SwitchTrimVertexOutputs` = true (`config_renderer.inl:81`) | `video.cpp:10558-10565`; XenosRecomp patch:350-357, 1507-1520 | U R1-3, U R4 | on; perf2-5 |
| Driver varying linking | The driver drops unread output components when it links a pipeline (`NVK_LINK_VARYINGS`, the driver's default). | Nothing reads them. | `SwitchLinkVaryings` = true (`config_renderer.inl:33`) | `perf/renderer_switch.cpp:50-53` | U R4 | on; perf2-5 |
| Trimmed pixel outputs | Pixel shaders write 0 to the `oC0` channels that are neither written nor read by blending, the alpha test or alpha to coverage. | Nothing can observe them. | `SwitchTrimPixelOutputs` = true (`config_renderer.inl:78`) | `video.cpp:10583-10590`; XenosRecomp patch:1525 | U R4 | on; perf2-5 |
| Alpha-test early-out | The arithmetic between the last write of `oC0.w` and the alpha test runs only for pixels the test keeps. | The same comparison as the clip (a NaN alpha still runs it); kept pixels run the same code. | `SwitchAlphaTestEarlyOut` = true (`config_renderer.inl:164`) | XenosRecomp patch:2136-2160 | U R6 | on; perf2-5 |
| Alpha-test sinking | Everything before the early-out that only feeds the kept pixels' colour (arithmetic, shadow gathers) moves into it (per-pixel variant). | An independent symbolic checker proves each variant equivalent when the cache is generated, and the generation fails otherwise; off while a verify mode is on. | `SwitchAlphaTestSink` = true (`config_renderer.inl:167`) | XenosRecomp patch:2255-2560 (sinking), 5553- (`sink_check.cpp`); `perf/renderer_switch.cpp:174` | U R8-R10; the checker New | on; perf2-5 |
| Transparent pixels skipped | A blended pixel that would leave an 8-bit target unchanged is discarded before the blend. | UNORM targets only (source clamped, texels finite); never with depth/stencil writes, alpha to coverage or a survey; NaN never discarded. | `SwitchSkipTransparentPixels` = true (`config_renderer.inl:171`) | `video.cpp:10219-10235`; XenosRecomp patch:2072-2077 | U R8 (+ stencil and survey conditions New) | on; perf2-5 |
| Texture sizes from constants | Shadow-map 2D-array sizes (and 2D sizes) come from the shared constants instead of a `GetDimensions()` query per fetch. | The same integers, converted to float the same way; the renderer writes them wherever a slot changes. | `SwitchTextureSizeConstants` = true (`config_renderer.inl:147`); verify `SwitchVerifyTextureSizes` = false (`:149`) | `video.cpp:301-310, 758-790`; XenosRecomp patch (shader_common.h) | U R1-3 | on; perf3-5 |
| Shadow gathers | The cascaded shadow maps' 2x2 point-fetch filter read with one gather where that returns the same texels; a specialised pipeline drops the runtime check. | Not exact on the X1 (see section 3). | `SwitchShadowGather` = **false** (`config_renderer.inl:156`); `SwitchShadowGatherSpecialization` = true (`:161`, inactive without gathers); verify `SwitchVerifyShadowGather` = false (`:158`) | `video.cpp:758-790, 10764-10775`; XenosRecomp patch:1876-1881 | U R4, U R7 | off since perf5 (on in perf3-4) |
| Vertex swap specialization | Vertex shaders get the declaration's half-swap masks as a specialization constant instead of a shared-constant read per attribute per vertex. | The declaration is part of the pipeline key, so every draw of the pipeline had those masks. | `SwitchVertexSwapSpecialization` = true (`config_renderer.inl:174`) | `video.cpp:10616-10625`; XenosRecomp patch (spec constant 4) | New | on; perf2-5 |
| Clip distance specialization | Pipelines drawn with the clip plane off get vertex shaders without the clip distance output (a SPIR-V edit makes its store conditional). | With the plane off every vertex wrote +0.0, which clips nothing; draws with the plane on use a pipeline without the bit. | `SwitchClipDistanceSpecialization` = true (`config_renderer.inl:177`) | `video.cpp:10390-10403`; XenosRecomp patch:5882- (`spirv_patch.cpp`) | New | on; perf3-5 |
| Indexed constants from memory (A/B) | a0-indexed constant arrays (bone palettes) read through the pointer (L1) instead of the constant bank. | The same bytes. | `SwitchIndexedConstantsFromMemory` = false (`config_renderer.inl:184`) | `video.cpp:10405-10408` | U R4 | A/B; not in a test build |
| `max(a, a)` as `a` | The translator emits the operand for Xenos's "move" instead of `max`, which NAK turned into a multiply. | `max(a, a) == a` exactly, NaN and signed zero included. | always (translator) | XenosRecomp patch:1106-1115 | U R1-3 | perf2-5 |
| Predicate blocks | Consecutive instructions under the same predicate share one `if (p0)` block. | Each instruction still runs under the same `p0`. | always (translator) | XenosRecomp patch:3854-3860 | U R1-3 | perf2-5 |
| 32-bit swap mask | The texcoord swap test uses a 32-bit mask instead of a 64-bit shift. | The same result (4-bit fields below 16). | always (translator) | XenosRecomp patch:775-776 | U R1-3 | perf2-5 |
| Survey plain store | The lens-flare occlusion survey stores 1 instead of an atomic add per pixel. | The counter is only compared with zero. | `SwitchSurveyPlainStore` = true (`config_renderer.inl:26`) | `MarathonRecomp/gpu/shader/hlsl/conditional_survey_store_ps.hlsl`; `video.cpp:4569-4575` | New | on; perf2-5 |
| Survey slots | Each survey gets a fresh slot and generation instead of a counter zeroed by a copy-engine transfer and barriers (a render pass split) before each survey. | "Not the generation" means what "0" meant; slots are reused only once nothing can read them. | `SwitchSurveySlots` = true (`config_renderer.inl:181`) | `video.cpp:2099-2125, 4375, 4583`; `MarathonRecomp/gpu/shader/hlsl/conditional_survey_slots_ps.hlsl` | New | on; perf3-5 |
| Gamma push constants | The gamma pass's 24 bytes of constants as push constants instead of four pointer loads. | The same values and layout. | `SwitchGammaPushConstants` = true (`config_renderer.inl:28`) | `video.cpp:6801-6806`; `MarathonRecomp/gpu/shader/hlsl/gamma_correction_push_ps.hlsl`, `gamma_correction_ps.hlsl` | New | on; perf2-5 |
| Per-query timestamp reset | The two frame timestamp queries are reset one at a time on the 3D engine instead of a copy-engine fill. | Each query is still reset before it is written. | `SwitchQueryResetPerQuery` = true (`config_renderer.inl:30`) | `video.cpp:3909-3912` | New | on; perf2-5 |
| Single copy triangle | Full-screen copies and the gamma pass draw one triangle instead of 1.5. | Each pixel computes its value from its own position, without blending. | `SwitchSingleCopyTriangle` = true (`config_renderer.inl:16`) | `video.cpp:619-622` | U R8 | on; perf2-5 |
| Lazy resolves | A depth resolve is not copied for draws that only test it, nor at clears that leave it; the copy is "owed" and made before anything could see the difference. | The surface does not change before the copy is made. | `SwitchLazyResolves` = true (`config_renderer.inl:112`) | `video.cpp:7779-7790, 9744` | U R5; owed copies New | on; perf2-5 |
| Hand-over at clears | A surface resolved into one texture and then cleared completely gives the texture its image (swap) instead of being copied. | Images created alike; the clear overwrites the swapped image entirely (depth: depth and stencil). | `SwitchResolveHandOver` = true (`config_renderer.inl:115`) | `video.cpp:8381-8395, 7100, 1258` | U R5 | on; perf2-5 |
| Resolves kept pending over Present | Colour resolves still pending at the end of a frame stay pending, so the next clear can hand the image over. | The texture keeps reading an unchanged surface; any change makes the copy first. | `SwitchKeepResolvesPending` = true (`config_renderer.inl:118`) | `video.cpp:7932-7940` | U R5 | on; perf2-5 |
| Hand-over at draws | A surface resolved into one texture and then drawn into gives the texture its image; stencil marks and a fix-up copy restore the pixels the draw does not write. | Every pixel ends with what the copy followed by the draw gave. | `SwitchCoverageHandOver` = true (`config_renderer.inl:121`) | `video.cpp:8521-8535, 10522-10526`; plume patch:96, 156, 565 (S8 stencil, dynamic reference) | U R6, U R7 | on; perf2-5 |
| Exact coverage | No marks and no fix-up when the draw provably covers every pixel but a thin edge (full-screen passes); edge pixels are copied first. | Rasterisation of an axis-aligned rectangle computed as the GPU does, with a 1/64-pixel margin. | `SwitchExactCoverage` = true (`config_renderer.inl:123`) | `video.cpp:8833-8845, 9468`; XenosRecomp patch:1609 | U R8 | on; perf2-5 |
| Overwritten clears skipped | A colour clear followed by a draw proven to replace every pixel is not made (only its edges). | Any other command makes it first, exactly as before. | `SwitchSkipOverwrittenClears` = true (`config_renderer.inl:125`) | `video.cpp:9150-9156, 9780` | U R8 | on; perf2-5 |
| No-op draws skipped | Draws that can write no colour, depth or stencil and are not surveys are not sent. | They leave every image and buffer as it was. | `SwitchSkipNoOpDraws` = true (`config_renderer.inl:106`) | `video.cpp:9264-9272` | U R8 (+ stencil and survey conditions New) | on; perf2-5 |
| Restore draws skipped | Draws that copy a surface's own pending resolve back into it (EDRAM restores) are skipped, with the copy they would force. | Each pixel reads its own texel through the same format; no blending or depth write. Colour only. | `SwitchSkipRestoreDraws` = true (`config_renderer.inl:128`) | `video.cpp:9294-9307` | U R9 | on; perf3-5 |
| Eager sample transitions | A colour target left with pending resolves moves to the sampling layout with the change of target, not in its own barrier batch mid-pass. | Only the barrier moves. | `SwitchEagerSampleTransitions` = true (`config_renderer.inl:109`) | `video.cpp:11528-11532` | U R7 | on; perf2-5 |
| Precise barriers | Barrier access masks follow the layouts instead of "all memory" both sides; the waits stay. From a survey to the end of the next frame, the full masks stay. | The stage masks, and the waits for idle, are unchanged. | `SwitchPreciseBarriers` = true (`config_renderer.inl:100`) | `video.cpp:2331-2345`; plume patch:48-52 | U R1-3 (survey exception New) | on; perf3-5 |
| ZCULL | Depth targets are made without `TRANSFER_DST`, so NVK gives them a hierarchical-Z plane (not the D32 shadow arrays). | Hierarchical Z culls only what the depth test rejects. | `SwitchZcull` = true (`config_renderer.inl:104`) | plume patch:519-530, 608; `perf/renderer_switch.cpp:190`; `video.cpp:7041-7050` | U R1-3 (a key in Marathon) | on; perf3-5 |
| D32 shadow-map arrays | Cascaded shadow-map array textures as `D32_FLOAT` instead of `D32_FLOAT_S8_UINT`: half the bytes copied and fetched. | They receive only depth copies and are sampled through depth-aspect views; their stencil is never used. | `SwitchDepthArrayTexturesD32` = true (`config_renderer.inl:131`) | `video.cpp:7041-7050` | New | on; perf2-5 |
| Stable framebuffers | Draws that turn colour writes or the depth/stencil tests off keep the bound framebuffer instead of ending the render pass. | Nothing reads or writes the kept attachment. | `SwitchStableFramebuffers` = true (`config_renderer.inl:134`) | `video.cpp:9679-9690, 7562` | New | on; perf3-5 |
| Read-only depth sampling | Draws that only test a depth buffer while sampling its pending resolve attach it read-only and sample it instead of copying it. | The views return what the copy would hold; the tests read the same values. | `SwitchReadOnlyDepthSampling` = true (`config_renderer.inl:137`) | `video.cpp:9593-9610`; plume patch:608 | New | on; perf3-5 |
| Uniform stencil clears | A depth-only clear of a surface whose stencil holds one known value also clears the stencil to that value (one full clear). | The same texels. | `SwitchUniformStencilClears` = true (`config_renderer.inl:140`) | `video.cpp:9734-9737` | New | on; perf3-5 |
| plume: redundant binds filtered | Identical pipeline, index/vertex buffer, viewport, scissor and depth-bias binds are skipped. | Vulkan state persists until the command buffer is reset. | always | plume patch:629-850, 1148-1160 | U R1-3 | perf2-5 |
| plume: persistent mapping | Host-visible buffers stay mapped, so `map()` is a pointer read. | The same memory. | always | plume patch:482-490 | U R1-3 | perf2-5 |
| plume: no `thread_local` vectors | Per-command-list scratch storage instead of `thread_local` vectors in hot paths. | One thread records a command list at a time. | always | plume patch:1168-1172 | U R1-3 | perf2-5 |
| ZCULL "greater" | ZCULL in the "greater" direction (`NVK_ZCULL=greater`): the main 1280x720 pass draws with reverse Z (greater-or-equal in every draw group of the perf5 draw profile), for which the driver's "less" direction culls nothing; the shadow passes lose their culling instead. | Hierarchical Z skips only work whose depth test would fail. | `SwitchZcullGreater` = true (`config_renderer.inl:38`) | `perf/renderer_switch.cpp:57` | U (its default) | on; perf6-7 |
| NAK operand reuse | The shader compiler marks Maxwell operand-reuse slots (`NAK_DEBUG=reuse`). | The same instructions and results, fewer register-file reads. | `SwitchOperandReuse` = true (`config_renderer.inl:42`) | `perf/renderer_switch.cpp:61` | U R14 | on; perf6-7 |
| D32 2D depth textures | The game's 2D depth textures (the depth resolves of the main depth) as `D32_FLOAT` instead of `D32_FLOAT_S8_UINT` (without anti-aliasing): half the bytes copied at Present and before draws, and fetched. | The same float depth; the game never samples their stencil. Depth hand-overs and read-only depth sampling need equal formats and do not apply to them (both ran 0 times a frame). | `SwitchDepthTexturesD32` = true (`config_renderer.inl:157`) | `video.cpp:4633, 7294` | New | on; perf6-7 (perf5 GPU-limited: 3.1 ms of depth copies) |
| Precise survey barriers | After a lens-flare survey draw only the next barrier batch keeps the conservative masks (with an explicit survey-buffer barrier), instead of every batch to the end of the next frame. | That batch makes the survey's writes visible to every later command. | `SwitchPreciseSurveyBarriers` = true (`config_renderer.inl:122`) | `video.cpp:2427, 2487` | New | on; perf6-7 (perf5: 31 conservative batches of 30 a frame) |
| Cascade adoption | The shadow cascades are drawn straight into the layers of a second image made like the cascaded shadow map, which becomes the map's image (a swap, as a resolve hand-over). perf8: the swap waits for the first draw whose shaders sample the map (the translator's sampler masks), not its bind; a cascade that keeps drawing with depth writes after its resolve continues in the layer its next resolve went to the frame before (one transfer copy within the cascade image, GENERAL layout), so the held layer keeps its content; layers known to equal the map's slice (filled in, or the map's previous image) are not filled in again; the copies between the two images are transfer copies. A cascade that needs more (a colour target, MSAA, stencil tests or writes) goes back to the shadow surface with its content; 8 unexpected resolves turn it off for the session. | The same draws write the same depth values into the layer the copy would have filled; the map's other slices hold what it held before the swap; the surface gets its content (and known stencil value) back before anything else reads or draws into it. | `SwitchCascadeAdoption` = true (`config_renderer.inl:163`; needs `SwitchLazyResolves`) | `video.cpp` (`CascadeAdopt`, `CascadeContinue`, `CascadeAdoptForDraw` and the hooks: resolve, clear, framebuffer, draw, Present, destruction) | New | perf7 (lost 0.9 ms: adopted at the bind before the third pass, four copies instead of three), perf8 v2 (expected: one layer copy a frame) |
| Shader sampler masks | The translator records in every shader cache entry the texture slots its shader can fetch from (its declared samplers); the renderer tells a texture that is only bound from one a draw samples. | Every fetch names a declared sampler (an undeclared one would not compile), so the mask covers every slot read. | always (translator; `ShaderCacheEntry::textureSlotsRead`) | XenosRecomp patch (`textureSlotsRead`); `MarathonRecompLib/shader/shader_cache.h`; `video.cpp` (`DrawTextureSlotsRead`) | U R15 | perf8 |

### 2.6 Memory

| Optimisation | What it does | Why it is exact | Key / option and current default | Files | Origin | Status |
|---|---|---|---|---|---|---|
| Wrap guard after the 4 GB window | 64 KB reserved and never committed after the guest window, so a wide D-form access whose 32-bit sum would wrap faults there. | The fault handler emulates it at the wrapped address (`WrapGuestByte`). Enabler for wide D-form. | always | `MarathonRecomp/kernel/memory.h:19-23`; `MarathonRecomp/kernel/memory.cpp:256-259`; `MarathonRecomp/os/switch/exception_switch.cpp:131-155` | U R11 (guard); wrapped emulation New | perf2-5 (used from perf4) |
| Fault emulation for every access form | The handler that gives uncommitted guest pages PC's committed-zero behaviour decodes every load/store form GCC emits (register, immediate, pair, SIMD&FP, LD1-4/ST1-4, lanes, LD1R-LD4R, DC ZVA), byte by byte. | A merged access straddling pages reads/writes its committed part as the separate accesses would. Enabler for plain memory, wide D-form, LTO and inline copies. | always | `MarathonRecomp/os/switch/exception_switch.cpp:1-30` | New | perf2-5 |
| `-ffixed-x16` | x16 is kept out of register allocation in the recompiled code, the app and the LTO link, because the fault handler resumes through x16. | Register allocation only. | `SWITCH_FIXED_X16=1` | `MarathonRecompLib/CMakeLists.txt:58-62`; `MarathonRecomp/CMakeLists.txt:516-522`; `CMakeLists.txt:86-88` | New | on; perf2-5 |
| Profiler tables allocated on demand | The CPU profiler's sample tables are allocated only when the sampler starts (static ones took 1.1 MB of the newlib heap that backs guest memory commits). | No behaviour change. | always | `perf/cpu_profiler_switch.cpp:16-20` | New | perf2-5 |
| 2 MB-aligned heap commits | Guest heap commits end on absolute 16 MB boundaries of the guest window, and a commit that starts off a 2 MB boundary is split there, so later commits take 2 MB-aligned backing the kernel can map with 2 MB blocks (4 KB-aligned fallback). | The same guest addresses committed, zero-filled, at the same allocations; only their grouping into commits changes. | always | `MarathonRecomp/kernel/heap.cpp:40-73`; `MarathonRecomp/kernel/memory.cpp:92-110, 263` | New | perf6-7 (TLB misses on first loads of heap objects) |

### 2.7 Diagnostics and profilers

None of these changes what is computed or drawn; their output goes to `sdmc:/switch/MarathonRecomp/stderr.log` (which
each of them opens by itself), apart from crash reports.

| Optimisation | What it does | Why it is exact | Key / option and current default | Files | Origin | Status |
|---|---|---|---|---|---|---|
| Logs | `stderr.log` (line-buffered; first lines: build and every key) and `MarathonRecomp.log`; off, neither is opened and lines are not formatted. | Writes files only. | `SwitchLog` = true (`config_diagnostics.inl:8`) | `perf/diagnostics_switch.cpp`; `MarathonRecomp/os/switch/logger_switch.cpp`; `MarathonRecomp/os/logger.h` | U final | on (test state); perf3-5 |
| CPU profiler | Samples registered threads every 2 ms; every 30 s the hottest 16-byte code lines per thread and the call chains of system calls ("wait" lines). | Pauses and resumes threads; writes files. | `SwitchCpuProfiler` = true (`config_diagnostics.inl:11`) | `perf/cpu_profiler_switch.cpp`; `MarathonRecomp/os/switch_cpu_profiler.h`; `tools/switch-cpu-profile.py` | U R6 (+ U R10 wait attribution) | on; perf4-5 |
| CPU slow frames | Every game-thread sample, running or waiting, with callers, in frames whose work took at least the threshold; per-core split, wait stacks. | As above. | `SwitchSlowFrameProfileMs` = 25 (`config_diagnostics.inl:14`) | `perf/cpu_profiler_switch.cpp:49-120, 792-800` | U R12-R14 | on; perf4-5 |
| Stall watchdog | `[stall]` dumps of every thread after that many seconds without a frame; `[hitch]` summaries of frames over 100 ms. | As above. | `SwitchStallWatchSeconds` = 1.0 (`config_diagnostics.inl:18`) | `perf/stall_watch_switch.cpp`; `MarathonRecomp/os/switch_stall_watch.h` | U R9 | on; perf3-5 |
| Overlay FPS | Publishes FPS and render resolution in SaltyNX's NX-FPS block for Status Monitor. | Shared memory only; needs SaltyNX. | `SwitchOverlayFps` = true (`config_diagnostics.inl:22`) | `perf/overlay_switch.cpp`; `MarathonRecomp/os/switch_overlay.h` | U R1-3 | on; perf3-5 |
| Handheld GPU profile | Requests apm configuration 0x92220008 (GPU 460.8 MHz, memory 1331.2 MHz) in handheld mode (since perf9 whenever the console is in it, also after undocking). | Clocks only; trades battery and heat. | `SwitchHandheldGpuBoost` = true (`config_diagnostics.inl:25`) | `perf/perf_switch.cpp` | U R1-3 | on; perf3-5 |
| GPU pass profiler | Timestamps at every framebuffer change and run of resolve copies; every 300 frames `[gpu passes]`: time per pass, per-frame counters of the renderer's work and savings, the game thread's frame breakdown, per-thread CPU use, audio gaps. | Timestamps cost GPU time only. | `SwitchGpuPassProfiler` = true (`config_renderer.inl:190`) | `video.cpp:1294-1330` | U R1-3 (+ U R5, U R9 additions) | on; perf4-5 |
| GPU slow frames | Passes (and draw groups) by the time they add in frames at or over the threshold; the three slowest frames whole. Turns the pass profiler on. | As above. | `SwitchGpuSlowFrameMs` = 25 (`config_renderer.inl:194`) | `video.cpp:1481-1486`; `tools/switch-gpu-profile.py` | U R13 | on; perf4-5 |
| GPU draw profiler | A timestamp after every draw; the most expensive draw groups (shader pair and state) of the most expensive passes. | As above (costs more GPU time). | `SwitchGpuDrawProfiler` = false (`config_renderer.inl:197`) | `video.cpp:1310-1320, 6110` | U R1-3 | off (the benchmark preset turns it on) |
| Frame log | One frame of render-thread commands, once a minute, five times. | Text only. | `SwitchFrameLog` = false (`config_renderer.inl:200`) | `video.cpp:1944-1950` | U R9 | off |
| Profiler window | The built-in profiler window from the start (no F1 key on the console). | An overlay the tester asks for. | `SwitchShowProfiler` = false (`config_renderer.inl:203`) | `video.cpp:5053-5056` | U R1-3 | off |
| Resolve statistics | Per-frame averages of resolve copies, hand-overs, owed/skipped copies, clears, barrier batches and framebuffer changes every 300 frames. | Counting only. | `SwitchResolveStats` = false (`config_renderer.inl:96`) | `video.cpp:1190-1200` | New key (U R5 counted these in its pass report) | off; on in the verify presets |
| Crash reports | A fatal CPU exception or a lost GPU appends a report to `crash.log` whatever `SwitchLog` says (driver messages through a debug-utils messenger). | Only on a crash. | always | `perf/crash_switch.cpp`; `MarathonRecomp/os/switch_crash.h`; plume patch:56-60 | U final | perf2-5 |
| Report writer thread | Profiler reports are formatted on the render thread and written by a 0x3B thread in one write. | Avoids SD-card stalls of the render thread. | always | `video.cpp:1710-1740` | U R4 (fix U R9) | perf2-5 |
| GPU timer correction | GPU times scaled by ~1.627 ns per tick (NVK reports 1 ns). | A displayed number only. | always | `video.cpp:1323-1327, 5834` | U R1-3 | perf2-5 |
| Texture descriptor high-water mark | "Texture descriptors: N in use at most so far" every 1,024 new descriptors. | Logging only. | always | `video.cpp:699-703` | U R1-3 | perf2-5 |
| Driver environment | `"NAME=value;..."` set before the Vulkan instance is created (e.g. `NVK_SHADER_STATS=1`, `NVK_ZCULL`). | Testing knob. | `SwitchMesaEnvironment` = `""` (`config_renderer.inl:37`) | `perf/renderer_switch.cpp:55-76` | U R1-3 | empty |
| Refusal counters | The resolve statistics count, per reason and frame, why colour resolves were not kept over Present and why read-only depth sampling was refused. | Counting only. | with `SwitchResolveStats` | `video.cpp:1245-1301, 6271-6278` | New | perf6-7 |
| Cascade counters | A `cascades:` line in the resolve statistics: resolves held, images adopted, fill-in copies and those not needed (perf8), held layers copied, surfaces given their content back, cascades continued into the next layer (perf8). | Counting only. | with `SwitchCascadeAdoption` | `video.cpp` (`ReportResolveStats`) | New | perf7-8 |

The verification keys (`SwitchVerifyTextureSizes`, `SwitchVerifyShadowGather`, `SwitchVerifyNativeDynamicCast`,
`SwitchVerifyNativeInflate`, `SwitchVerifyNativeVectorMath`, all `false`) are described in [section 4](#4-verification-tools-and-checks).

### 2.8 Build and tooling

| Optimisation | What it does | Why it is exact | Key / option and current default | Files | Origin | Status |
|---|---|---|---|---|---|---|
| Submodule patch stamps | Each submodule is reset and patched only when its patches or tree changed; hand edits are backed up to `.git/switch-patch-backups/` first. | Unchanged trees keep their timestamps. | always (`SWITCH_SKIP_PATCHES=0`) | `tools/build-switch.sh:309-388` | U R4 (backups New) | build-only |
| `ppc/` regenerated only when needed | `ppc/` is regenerated only when XenonRecomp, its patches, `Marathon.toml`, `switch_table.toml`, `default.xex`, the mode or the codegen pass (tool and options) change; the pass refuses to run twice. | Same inputs, same text. | always | `tools/build-switch.sh:408-448`; `tools/switch-codegen-pass.py:293-303` | U R4 | build-only |
| Direct-call pass writes only changed files | Undoes its previous run in memory and writes only files whose text changes. | Idempotent. | always | `tools/switch-direct-calls.py:197-252` | U R1-3 | build-only |
| Shader cache stamps | The game shader cache is regenerated only when the translator, `shader_common.h` or the `.arc` files change, and an identical cache keeps its timestamp; app shader headers likewise. | Same inputs, same blobs. | always | `tools/build-switch.sh:473-558` | U R1-3 (shader cache stamp); app-shader stamp New | build-only |
| FFmpeg staged only when changed | `make install` output is copied only for files whose content changed. | Avoids relinking (a whole LTO link). | always | `tools/build-switch-ffmpeg.sh` | New | build-only |
| App-only rebuild | Skips patches, host tools, PPC/shader generation and FFmpeg; the direct-call pass still runs. | Reuses generated outputs (checked present). | `SWITCH_APP_ONLY=0` | `tools/build-switch.sh:286-306` | New | build-only |
| Build ID header | Build ID, options, driver SHA-256 and shader-cache SHA-256 in a generated header, rewritten only when its content changes. | Names the build in logs; keys the pipeline cache. | `SWITCH_BUILD_ID` (default: date, time, options) | `MarathonRecomp/CMakeLists.txt` (switch_build_id.h block); `tools/build-switch.sh:576-592` | U R8 (build ID), U R10 (driver ID); shader ID New | build-only |
| PGO profile ID | A hash of the `.gcda` files on every compile line, so a new profile rebuilds every object. | Build correctness. | with `SWITCH_PGO=use` | `MarathonRecomp/CMakeLists.txt` (PGO block) | New | build-only |
| `ppc/` file count from the folder | The configure step takes as many `ppc_recomp.N.cpp` as XenonRecomp wrote. | Build correctness. | always (pregenerated PPC) | `MarathonRecompLib/CMakeLists.txt:115-127` | New | build-only |
| Link launcher with `DEVKITPRO` | Every link runs with the configured `DEVKITPRO`, so `ninja` links from any shell. | Build correctness. | always | `toolchains/switch-devkitA64.cmake:56-62` | New | build-only |
| Classic code generation (A/B) | `ppc/` generated by XenonRecomp's previous code generation (volatile memory, no loop barriers, no mode tracking), for A/B NROs. | The reference. | `SWITCH_CLASSIC_CODEGEN=0` | `tools/build-switch.sh:106-115`; XenonRecomp patch:9-18 | U R8 | build-only |
| Single-file compile check | Compiles single sources with the build's exact flags into a scratch folder, without ninja. | Tooling. | manual | `tools/switch-compile-check.py` | New | tooling |
| Switch app-shader variants | `MARATHON_RECOMP_SWITCH_APP_SHADERS` and the three Switch-only shaders (survey store, survey slots, gamma push constants). | See their rows. | always (pregenerated app shaders) | `tools/generate-switch-app-shaders.sh:94-102` | New | perf2-5 |
| Code generation lists in the stamp | `ppc/` is regenerated when `call_locals.txt` or `fp_no_contract.txt` changes (their hashes are part of the code generation stamp), and the options line names the perf7 options. | Build correctness. | always | `tools/build-switch.sh` (`codegen_id`, options summary) | New | perf7 |
| PGO training build | `SWITCH_PGO=generate` (no LTO) builds the same code instrumented; it dumps the `.gcda` files to `sdmc:/switch/MarathonRecomp/pgo/` every 3 minutes (accumulating), for `SWITCH_PGO=use` from the same build folder. | Build only; the "use" build needs `tools/switch-fp-check.py` against the previous one (profile-guided inlining can move fused multiply-adds). | `SWITCH_PGO=` | `perf/pgo_switch.cpp`; `MarathonRecomp/CMakeLists.txt` (PGO block) | U R9-10 | first Marathon training build: perf8-pgo-generate |

### 2.9 Correctness changes made alongside (not optimisations)

These came with the work and are needed by it, but make nothing faster (the first one costs time):

- **Guest fences.** `sync` → `PPC_SYNC()` (`dmb ish`), `lwsync`/`eieio` → `PPC_LWSYNC()`: libnx lets every thread run on
  any core, so the base port's no-ops were only safe on one core (XenonRecomp patch:281-285, 467-470, 561-564, 966-976;
  21 sites in the current `ppc/`). From Unleashed's XenonRecomp patch.
- **`GuestMemmove`** for the memcpy/memmove hooks and the native hooks' guest copies: newlib's `memcpy` uses x16, which a
  resumed fault clobbers (`perf/native_crt.cpp:167-293`; `MarathonRecomp/misc_impl.cpp:35-75`).
- **XMA output offsets** published with release/acquire (`MarathonRecomp/apu/xma_decoder.cpp:37-48`).
- **Dispatcher lost wake-up**: the generation is advanced under the dispatcher mutex (`imports.cpp:74-78`).
- **Clip plane as a render command**, ordered with the draws instead of written into the render thread's constants
  from the game thread (`video.cpp:12798-12805`).
- **Indirect-call traps** without `lr`/`ctr` when they are locals (`MarathonRecomp/kernel/memory.cpp:190-223`).
- **lsfg-vk `get_mpa()`** returns the driver's `vk_icdGetInstanceProcAddr` (needed by the LTO link;
  `thirdparty/lsfg-vk/lsfg-vk-common/src/vulkan/vulkan.cpp`).
- **Explicit fused multiply-add text**: XenonRecomp writes the guest's fused operations as `PPC_FMA`/`PPC_FMS`/
  `simde_mm_vmaddfp`/`simde_mm_vnmsubfp`, which are the same plain expressions unless `PPC_EXPLICIT_FMA` is defined
  (XenonRecomp patch:332-414, 597-617, 1373-1409). It only matters for `SWITCH_EXACT_FMA` (section 3).

### 2.10 Counts

| Area | Rows |
|---|---|
| 2.1 CPU code generation | 30 |
| 2.2 CPU runtime | 24 |
| 2.3 Native function replacements | 9 |
| 2.4 Renderer CPU side | 22 |
| 2.5 GPU | 47 |
| 2.6 Memory | 5 |
| 2.7 Diagnostics and profilers | 19 |
| 2.8 Build and tooling | 15 |
| **Total** | **171** (144 up to perf5, 13 from perf6, 11 from perf7, 3 new rows from perf8 and 6 rows changed; plus 8 correctness changes in 2.9) |

## 3. Tried and turned off, rejected or not ported

| Item | What happened | Evidence |
|---|---|---|
| `SwitchShadowGather` | Turned **off** for perf5: the perf4 `SwitchVerifyShadowGather` session (2026-10-01) showed scattered magenta over Soleanna's shadowed ground. On the X1 a gather picks another texel than the point fetches at some coordinates on texel boundaries (hardware coordinate snapping, which `tfetch2DArrayGatherExact` does not model). The GPU is not the limit at the benchmark spot. | `config_renderer.inl:150-156`; `dist/switch/MarathonRecomp-perf5/README-TEST.txt:30-34` |
| `lfs`, `lfsx`, `frsp` in `SWITCH_FEWER_MODE_SWITCHES` | Excluded, unlike Unleashed: their `double(float)` tells GCC the value is a float, which lets it narrow single-precision products and sums and fuse them; with Unleashed's `PPCFloatToDoubleAnyMode` helper, `tools/switch-fp-check.py` found 7 functions whose fused multiply-adds changed. They keep the FPU mode; perf5 has the same fused multiply-adds as perf4. | XenonRecomp patch:421-460, 921-931; `docs/SWITCH-PERFORMANCE.md:148`; perf5 README-TEST.txt:18-21 |
| Unleashed's level 2 of fewer mode switches | Not ported: XenonRecomp reads only `XENON_RECOMP_FEWER_MODE_SWITCHES=1`. | XenonRecomp patch:25-31 |
| `SWITCH_EXACT_FMA` | Opt-in (off): exactly the guest's fused operations fused (`-ffp-contract=off` plus explicit fma), as on the Xbox 360, which changes the last bit of some float results compared with the base port. It defaulted to on until made opt-in; not exact with LTO; ignored with classic code generation or `ppc/` text without explicit fma. | `docs/BUILDING-SWITCH.md:147-177`; `CMakeLists.txt:90-104, 135-186`; `tools/build-switch.sh:105-109, 570-575` |
| Unleashed's four round-10 renderer changes | Not ported (dead copies, carried clears, skipped depth clears, early depth transitions): long play with them lost the GPU in Unleashed. | `docs/SWITCH-PERFORMANCE.md:189-190` |
| Quad-uniform alpha-test sinking | Not ported (slower in Unleashed); spec bit 25 reserved. | `docs/SWITCH-PERFORMANCE.md:193`; XenosRecomp patch:312, 2255-2256 |
| Skinning specialization, reverse-Z pass skip | Not applicable: Sonic '06 has no vertex booleans and no reverse Z; spec bits 14/15 reserved. | `docs/SWITCH-PERFORMANCE.md:192`; XenosRecomp patch:307 |
| Native LZX | Not applicable: Marathon's archives use zlib; replaced by native inflate. | `docs/SWITCH-PERFORMANCE.md:193`; `perf/native_inflate.cpp:18-24` |
| Unleashed's PGO profile and hot list | Not applicable to Marathon's code; Marathon's hot list comes from its own perf4 profile; 1.0.3 uses Marathon's own PGO profile (perf9 training build). | `docs/SWITCH-PERFORMANCE.md:194`; `tools/switch-cpu-profile.py` docstring |
| Display defaults (resolution scales, window sizes) | Not ported as performance changes; since perf9 the window size per console mode is a user setting (`resolution.txt`, docked 1080p and handheld 720p by default), taken at start because Marathon's game makes its render targets once (Unleashed remakes its swap chain live). | `MarathonRecomp/ui/game_window.cpp`; `docs/SWITCH-PERFORMANCE.md` (perf9) |
| Depth restore draws | Not skipped (only colour restores are): the port's depth textures read through swizzled views. | `video.cpp:9307` |
| Library calls for the other memcpy/memmove/memset | Not done (Unleashed round 14 calls the C library directly): only sizes GCC copies inline, since a library call would use x16. | `tools/switch-codegen-pass.py:25-32` |
| Callee-saved stores dropped (`skipCalleeSaves`) | Inactive: needs `non_volatile_as_local`, which `Marathon.toml` keeps off; 0 dropped stores in the current `ppc/`. | XenonRecomp patch:676-684; `MarathonRecompLib/config/Marathon.toml:13` |
| Streaming vertex/index buffers (Unleashed round 4) | Not applicable: Marathon's buffer unlocks write the GPU buffer on the calling thread and are not render commands. | `video.cpp:2735-2736` |
| `SwitchPipelineCacheSaveDuringPlay` | Off: a save briefly holds the driver's cache lock, which pipeline creation on the render thread also takes. | `config_renderer.inl:71-72`; `video.cpp:5635-5640` |
| `SwitchThreadIdealCores`, `SwitchIndexedConstantsFromMemory` | Off, A/B only. | `config_kernel.inl:39-41`; `config_renderer.inl:182-184` |
| Unleashed's `SwitchZcullGreater` until perf6 | Not ported up to perf5 on the belief that Marathon does not use reverse Z; the perf5 draw profile showed the main pass draws greater-or-equal, and perf6 turned it on (section 2.5). | `config_renderer.inl:34-38` |
| Unleashed's `SwitchStrongCriticalSectionCas`, `SwitchSemaphoreWakeOne` (round 14) | Not ported as such: Marathon's sleeping locks retry at once when the weak CAS failed on a free section, and `SwitchSemaphoreWakeCount` wakes N waiters for N units. | `imports.cpp:1016-1020, 259-290` |
| Other Unleashed items with no Marathon counterpart | No reason recorded in code or docs: per-resource lock waits (U R13), gamma `pow` skip (U R13), NVK draw-path fast paths (off in Unleashed since its round 7), render-thread priority key (U R10), UI modifier hash index (U R14), and Unleashed's game-specific natives (message dispatch, light field, map find, CRI mixer kernels, resource waits, visibility test; rounds 11-13). | absent from `MarathonRecomp/`, `tools/`, `patches/` |
| Cascade adoption, owed depth copies aliased or carried, register locals across calls (perf6) | Checked and deferred in perf6; cascade adoption and call locals were done in perf7. | `docs/SWITCH-PERFORMANCE.md` (perf6, perf7) |
| `lfs` denormal guard, PGO (perf6) | Rejected until their effect on fused multiply-adds is established. | `docs/SWITCH-PERFORMANCE.md` (perf6) |
| Owed depth copies aliased (perf7) | Not done: three copies of ~0.1 ms each at 230 MHz; aliasing the texture to a surface that later draws write needs a copy-on-write path for little gain. | perf6 GPU-limited profile |
| Gamma pass as a lookup table (perf7) | Not done: the pass is bound by texture fetches, and a lookup adds one per channel. | `gamma_correction_ps.hlsl` |
| `SampleLevel(0)` for single-level textures (perf7) | Not done: Maxwell fetches with an explicit level at the same rate, so no gain. | — |
| Predicted exact gather (perf7) | Not done: the X1's gather snaps coordinates differently from point fetches (perf4's magenta); a prediction of when they agree needs an exhaustive test on the console. | section 3, `SwitchShadowGather` |
| Stencil-less main depth (perf7) | Not done: pipelines built for the stencil format would need variants compiled without a hitch. | — |
| Clears as load ops, ZCULL direction per pass (perf7) | Not done: both need NVK changes. | — |
| Faster snapshot copy for deferred unlocks (perf7) | Not done: the copy is bound by memory bandwidth, and reading the guest buffer later instead could see bytes the game changed after the unlock. | `video.cpp:5089` |
| Call locals reloading only ABI-volatile registers (perf7) | Tried and removed: more instructions in the hot functions, and it relied on the game's code following the PowerPC ABI. | — |
| Call locals in every function (perf8) | Measured and not done: compiled both ways, 18,211 of 22,964 functions did more loads and stores with locals (12.9 % more instructions in all); only those with fewer are listed now. | perf8 compile comparison (`call_locals.txt` header) |
| Integer locals in every floating-point function (perf8) | Not done: the large ones spilled heavily (one hot Havok function went from 10 to 259 stack accesses); only those with fewer loads and stores are listed. | perf8 compile comparison |
| Native ADX decoder (UnleashedRecomp-NX round 15) (perf8) | Not applicable as it is: the hottest function of Marathon's sound thread (`sub_8290CC88`) is a CRI filter step with an inlined helper, not the ADX decoder; it is exact without contraction (69 fused = 69) but its locals form does more loads and stores, so it stays as it is. | perf8 analysis |
| Scalar reciprocal square root of dot products (U R15) (perf8) | Not done: 11 `vrsqrtefp` sites in Marathon (816 in Unleashed). | `raw ppc` count |
| Copying only the locked range of a vertex buffer (perf8) | Not done: three of the four lock sites lock the whole buffer, and a range copy would miss writes the game makes outside the lock. | lock call sites (`sub_8253B5D0`, `sub_8253B6F0`) |
| Gamma `pow` skipped at gamma 1 (perf8) | Not done: exact for the 8-bit intermediary, but the default brightness gives gamma 1/0.85, so it would only help a changed setting. | `video.cpp` (gamma constants) |
| Owed copies decided by the sampled slots (U R15 `SwitchSampledSlotResolves`) (perf8) | Not done: no copy of the GPU-limited frame was triggered by a bind ("0.0 other"). | perf7 `[gpu passes]` |
| Copies decided at submit (U R15 `SwitchSubmitTimeCopies`) (perf8) | Not done yet: which copies are dead needs the frame log (`switch-frame-log.toml`). | — |
| The "BurnoutBlurFilter" passes (perf8) | Looked at: the shader is a one-fetch copy, so the three passes (2 ms at 230 MHz) are the game's own full-screen copy, downsample and composite draws; no exact change found without the frame log. | translator dump of 62573AB4C9EC44EE |

## 4. Verification tools and checks

**Checks every build runs (`tools/build-switch.sh`).**

| Check | What it guards | Where |
|---|---|---|
| Patches apply | A patch that no longer applies stops the build with git's reason. | `tools/build-switch.sh:357-373` |
| Codegen stamp and pass stamp | `ppc/` text matches the requested options; the pass refuses to run on text it already changed with other options. | `tools/build-switch.sh:412-448`; `tools/switch-codegen-pass.py:293-303` |
| Exact-FMA text check | `SWITCH_EXACT_FMA=1` is dropped (with a message) unless `ppc/` has explicit fma text; the CMake configure step checks the same. | `tools/build-switch.sh:570-575`; `CMakeLists.txt:137-180` |
| Direct-call hook check | `switch-direct-calls.py verify-elf`: every strong `sub_X` in the linked ELF is a hook and none was called directly; fails if it finds no hook at all. | `tools/build-switch.sh:624-630`; `tools/switch-direct-calls.py:255-293` |
| Alpha-test sinking proof | `sink_check.cpp` symbolically executes the sunk and the previous code of every pixel shader; a variant it proves wrong fails the shader cache generation ("no shader cache written"). | XenosRecomp patch:186-220, 5553- |
| PGO sanity | `SWITCH_PGO=generate` with LTO and `use` without `.gcda` files are refused. | `tools/build-switch.sh:102-104, 245-247`; `MarathonRecomp/CMakeLists.txt` |

**Developer tools (run by hand).**

| Tool | What it checks |
|---|---|
| `tools/switch-check-fault-emulation.py` | Disassembles the recompiled functions (objects or a linked ELF; `--all-functions` for LTO builds) and lists every load/store form the fault emulation does not decode, atomics separately, and every x16 use; exit 1 on a finding. Not run by `tools/build-switch.sh`. |
| `tools/switch-fp-check.py old.elf new.elf` | Per floating-point game function, the counts of fused (`fmla`, `fmadd`...) and unfused (`fmul`, `fadd`...) instructions; lists the functions that differ. The perf5 check is where the 7 functions of the `lfs` exclusion came from. |
| `tools/switch-cpu-profile.py` | Names the CPU profiler's, stall watchdog's and `crash.log`'s offsets with the debug ELF; `--hot-list` writes `hot_functions.txt`. |
| `tools/switch-gpu-profile.py` | Tables of `[gpu passes]`, `[gpu draws]` and `[gpu slow frames]`, compared between logs, with `NVK_SHADER_STATS` data. |
| `tools/switch-compile-check.py` | Compiles single sources with the build's flags. |

Checks recorded in code comments as done on the PC: native inflate against all 21,064 compressed entries of the 93
archives (`perf/native_inflate.cpp:34-36`); native `dynamic_cast` for 567,480 casts over 5,106 classes
(`perf/native_rtti.cpp:107-109`); `vperm` exhaustively against simde (XenonRecomp patch:1362); native vector math on
200,000 random cases (stated in `docs/SWITCH-PERFORMANCE.md:158` and perf5 README-TEST.txt:15-16, not in the code).

**On the console: verification keys** (all `false` by default; each slows the game):

| Key | Check | Reports |
|---|---|---|
| `SwitchVerifyTextureSizes` (`config_renderer.inl:149`) | Keeps the size queries and draws magenta where a size from the constants differs | magenta pixels |
| `SwitchVerifyShadowGather` (`config_renderer.inl:158`) | Turns gathers on, does both, magenta where a gather differs | magenta pixels |
| `SwitchVerifyNativeDynamicCast` (`config_native.inl:13`) | Runs the game's cast too and compares | `[rtti] MISMATCH` lines |
| `SwitchVerifyNativeInflate` (`config_native.inl:22`) | Runs the game's decoder too and compares | `[inflate]` lines ("all identical") |
| `SwitchVerifyNativeVectorMath` (`config_native.inl:30`) | Runs the recompiled bodies too and compares memory and registers | `[native vector] MISMATCH` lines |
| `SwitchResolveStats` (`config_renderer.inl:96`) | Counts resolves, hand-overs, clears, barriers | per-frame averages |

Alpha-test sinking is switched off while a verify mode is on (`perf/renderer_switch.cpp:174`).

**Presets (perf7, `dist/switch/MarathonRecomp-perf7/config-presets/`).** As in perf5, plus three A/B presets that keep
the draw profiler on (the benchmark setting): `switch-ab-perf7-gpu-off.toml` (`SwitchCascadeAdoption` off),
`switch-ab-perf7-cpu-off.toml` (the record wait back, the engine and sound threads on core 0) and
`switch-ab-all-gpu-off.toml` (every renderer key of rounds 1-7 that changes the GPU's work off); `switch-safe.toml` also
turns the perf7 keys off. perf5's presets: `switch-verify.toml`: `SwitchVerifyTextureSizes`,
`SwitchVerifyNativeDynamicCast`, `SwitchVerifyNativeInflate`, `SwitchVerifyNativeVectorMath`, `SwitchResolveStats`,
`SwitchGpuPassProfiler` (no gather check: gathers are off). `switch-verify-cpu.toml`: `SwitchVerifyNativeVectorMath`.
`switch-safe.toml`: turns 24 risky runtime keys off. `switch-no-profiler.toml`: profilers off for a clean FPS figure.
`switch-draw-profiler.toml`: `SwitchGpuDrawProfiler` (the benchmark preset). `switch-ab-ideal-cores.toml`:
`SwitchThreadIdealCores`.

**Results so far.** perf4's verify session (`dist/switch/MarathonRecomp-perf4-profiler/verifier.log`, with
`SwitchVerifyTextureSizes`, `SwitchVerifyShadowGather`, `SwitchVerifyNativeDynamicCast`, `SwitchVerifyNativeInflate` and
`SwitchResolveStats`): no `MISMATCH` line; the last `[inflate]` line reads "2112 decompressions, 2112 native and compared
with the game's decoder ... all identical"; the magenta seen came from the shadow gathers. No log of perf4 or perf5
contains a "Texture descriptors: N in use at most" line, so fewer than 1,024 descriptors were ever allocated in those
sessions (the line is printed every 1,024, `video.cpp:699-703`). No `switch-verify-cpu.toml` session has been reported.

## 5. Discrepancies found

The main session checked these against the code on 2026-10-02 and fixed items 1, 2, 4-21 (docs, comments, the
`SWITCH_HOT_FUNCTIONS` normalisation, Python for the codegen pass, the build options line, the CMake shader list, the
XenonRecomp comment order; the tested code generation is now the build default). Item 3 (stale "Off ..." wording in
some key comments) is partly fixed.
Items 22 and 23 were found in the perf7 round and fixed; items 24-26 in the perf8 round.


1. **Off-by-default list vs code.** `docs/SWITCH-PERFORMANCE.md:95-112` lists as off by default, and
   `dist/switch/MarathonRecomp-perf2/config-presets/switch-safe.toml` sets to false, keys that now default to `true`:
   `SwitchTextureSizeConstants` (`config_renderer.inl:147`), `SwitchCompactTextureHeap` (`:13`),
   `SwitchPresentOnRenderThread` (`:64`), `SwitchPreciseBarriers` (`:100`), `SwitchZcull` (`:104`),
   `SwitchSkipRestoreDraws` (`:128`), `SwitchStableFramebuffers` (`:134`), `SwitchReadOnlyDepthSampling` (`:137`),
   `SwitchUniformStencilClears` (`:140`), `SwitchClipDistanceSpecialization` (`:177`), `SwitchSurveySlots` (`:181`),
   `SwitchSleepingGuestLocks` (`config_kernel.inl:16`), `SwitchGuestSpinBeforeSleep` (`:33`), `SwitchFastEvents`
   (`:36`), `SwitchSemaphoreWakeCount` (`:38`), `SwitchRelaxedAtomics` (`config_codegen.inl:9`),
   `SwitchAudioThreadCores` (`config_audio.inl:12`), `SwitchXmaEventWait` (`:17`). Only `SwitchShadowGather`,
   `SwitchThreadIdealCores` and `SwitchIndexedConstantsFromMemory` are off. The cause is the "all on" test state
   (`MarathonRecomp/user/config_def.h:116-117`).
2. **Diagnostics defaults.** `docs/SWITCH-PERFORMANCE.md:167` says "Off in a release build", but `SwitchLog`,
   `SwitchCpuProfiler`, `SwitchOverlayFps`, `SwitchHandheldGpuBoost` (`config_diagnostics.inl:8, 11, 22, 25`),
   `SwitchStallWatchSeconds` = 1.0 (`:18`), `SwitchSlowFrameProfileMs` = 25 (`:14`), `SwitchGpuPassProfiler` = true and
   `SwitchGpuSlowFrameMs` = 25 (`config_renderer.inl:190, 194`) are on, and nothing in the code gives a release build
   other defaults. (1.0.3: logging, the profilers, the slow-frame reports and the stall watchdog now default to off;
   the overlay and the handheld GPU profile stay on and no longer open `stderr.log` by themselves.)
3. **Key comments contradict their defaults.** "Off ..." comments above keys that default to `true`:
   `config_renderer.inl:11-12, 62-63, 98-99, 101-103, 126-127, 133, 136, 139, 145-146, 176, 179-180`;
   `config_kernel.inl:11-12, 17-20`; `config_codegen.inl:7-8`; `config_audio.inl:10-11, 16`;
   `config_diagnostics.inl:7, 16-17, 20-21, 23-24`; also `video.cpp:656` (compact heap "stays off"), `video.cpp:1314`
   (pass and draw profilers "both are off by default"; the pass profiler is on) and `perf/perf_switch.cpp:3`
   (handheld profile "off by default"). `config_def.h:116-117` acknowledges this.
4. **`SwitchSkipRestoreDraws` comment.** `config_renderer.inl:126-127` says "Off, as in Unleashed", but Unleashed's
   default is `true` (`UnleashedRecomp-NX/UnleashedRecomp/user/config_def.h:272`), as is Marathon's (`:128`).
5. **Audio globals' initialisers.** `perf/audio_switch.cpp:11` says the globals are "Initialised to the keys' defaults",
   but `g_switchAudioThreadCores` and `g_switchXmaEventWait` start `false` (`:13, :15`) while the keys default to `true`
   (`config_audio.inl:12, 17`). Harmless (overwritten before use), but the comment is wrong.
6. **Presets.** `docs/SWITCH-PERFORMANCE.md:22-29` says three presets ship in `dist/switch/config-presets/` and that
   `switch-safe.toml` is the built-in defaults. That folder does not exist; the presets are per build
   (`dist/switch/MarathonRecomp-perfN/config-presets/`), `switch-aggressive.toml` exists only for perf2, and since perf3
   `switch-safe.toml` turns risky keys off (24 keys in perf5) instead of listing the defaults.
7. **How the section is saved.** `docs/SWITCH-PERFORMANCE.md:21-22` says the game writes the whole `[Switch]` section
   when it saves; `MarathonRecomp/user/config.cpp:885-890` writes only keys that differ from their default.
8. **"Not measured".** `docs/SWITCH-PERFORMANCE.md:9` says nothing has been measured on a console; perf3, perf4 and
   perf5 have been (section 1.3), and perf4 had a verify session.
9. **"All four of the last ones".** `docs/SWITCH-PERFORMANCE.md:151` says perf4 had "all four of the last ones" of the
   code generation table on. Since perf5 inserted `SWITCH_FEWER_MODE_SWITCHES` (`:148`), the last four rows are
   `SWITCH_PLAIN_GUEST_MEMORY`, `SWITCH_NARROW_BARRIER`, `SWITCH_FEWER_MODE_SWITCHES` and `SWITCH_WIDE_DFORM`; perf4 had
   `SWITCH_REGISTER_LOCALS`, plain memory, narrow barrier and wide D-form (perf4 README-TEST.txt:7-11), not fewer mode
   switches.
10. **Attribution of the code generation table.** `docs/SWITCH-PERFORMANCE.md:132` ("Unleashed rounds 11-14") and perf4
    README-TEST.txt:6 include `SWITCH_REGISTER_LOCALS` (Unleashed gets these through XenonRecomp options in
    `UnleashedRecompLib/config/SWA.toml:8-15`, not a round 11-14 change) and `SWITCH_PLAIN_GUEST_MEMORY` (Unleashed round 8).
11. **Render-queue spin key.** `docs/SWITCH-PERFORMANCE.md:72` puts the ~1,000-spin render queue under
    `SwitchHostThreadCores`; the spin is a constant (`MAX_SEMA_SPINS = 1000`, `video.cpp:2718`) with no key.
12. **Direct-call count.** `docs/SWITCH-PERFORMANCE.md:120` says 152,048 direct calls; the current `ppc/` has 151,618.
    The difference, 430, is exactly the call sites of the three functions perf5 hooked for native vector math
    (`sub_82168C48` 413, `sub_82272648` 16, `sub_825A1490` 1), so the figure predates perf5.
13. **"The build's ELF scan".** `docs/SWITCH-PERFORMANCE.md:125` says the build's ELF scan finds no access the fault
    emulation cannot handle; `tools/switch-check-fault-emulation.py` is not run by `tools/build-switch.sh` (the only
    ELF check there is `switch-direct-calls.py verify-elf`, `:624-630`).
14. **Build options in BUILDING-SWITCH.md.** `docs/BUILDING-SWITCH.md:131-134` says every option except
    `SWITCH_FIXED_X16` and `SWITCH_DIRECT_CALLS` is off and the default build compiles the same code as before; but
    `SWITCH_HOT_FUNCTIONS` (its own `:118`), `SWITCH_LEAF_LOCALS`, `SWITCH_CONST_VMX_TABLES`, `SWITCH_INLINE_FP_COMPARE`,
    `SWITCH_INLINE_MEMCPY` and `SWITCH_FEWER_MODE_SWITCHES` default to 1 (`tools/build-switch.sh:84-88, 458`) and change
    the generated code. `:129-131` says the options map to CMake options of the same name; only those passed at
    `tools/build-switch.sh:609-620` do (not `SWITCH_APP_ONLY`, `SWITCH_SKIP_PATCHES`, `SWITCH_DIRECT_CALLS`,
    `SWITCH_HOT_FUNCTIONS` or any code generation option). Its option table (`:113-127`) lists none of the code
    generation options (they are only in `tools/build-switch.sh:45-56` and `docs/SWITCH-PERFORMANCE.md:138-149`).
15. **`SWITCH_HOT_FUNCTIONS=off`.** `docs/BUILDING-SWITCH.md:111` says `on`/`off` work for every variable, but
    `SWITCH_HOT_FUNCTIONS` is not normalised (`tools/build-switch.sh:458`, outside the `bool_option` loop at `:84-90`) and
    `tools/switch-direct-calls.py:318` treats anything but `"0"` as on: `SWITCH_HOT_FUNCTIONS=off` keeps the hot marking.
16. **Python for the codegen pass.** `docs/BUILDING-SWITCH.md:39-41` says Python is needed for the direct-call pass; the
    code generation pass needs it too, but `tools/build-switch.sh` looks for Python only when the direct-call pass runs
    (`:299-303`). With `SWITCH_DIRECT_CALLS=0`, no `PYTHON` set and no direct-call manifest, regenerating `ppc/` runs
    `env ... "" tools/switch-codegen-pass.py` (`:445`) and fails.
17. **Stale hot-list note.** `tools/switch-direct-calls.py:36-37` says the hot list "is empty until Marathon has a CPU
    profile"; `MarathonRecompLib/config/hot_functions.txt` holds 600 entries.
18. **Tested builds are not the default build, and logs cannot show it.** `tools/build-switch.sh:86-88` defaults
    `SWITCH_REGISTER_LOCALS`, `SWITCH_PLAIN_GUEST_MEMORY`, `SWITCH_NARROW_BARRIER` and `SWITCH_WIDE_DFORM` to 0, but perf4
    and perf5 were built with all four on (section 1.4). The `options:` line of `stderr.log` lists only CMake options
    (`MarathonRecomp/CMakeLists.txt`, build options loop), and perf4/perf5 used fixed build IDs without the
    `-reglocals`/`-narrow`/`-wide` suffixes (`tools/build-switch.sh:587-590`), so neither log records those options.
19. **App-shader comment.** `tools/generate-switch-app-shaders.sh:98` says the Switch-only shaders are "not in
    MarathonRecomp/CMakeLists.txt", but `MarathonRecomp/CMakeLists.txt:719-720` compiles `conditional_survey_store_ps`
    and `gamma_correction_push_ps` (not `conditional_survey_slots_ps`, so a non-pregenerated build would lack
    `SwitchSurveySlots`).
20. **Misplaced comment.** XenonRecomp patch:20-24 describes the register-locals option above the
    `XENON_RECOMP_FEWER_MODE_SWITCHES` block; the register-locals code is at patch:33-42.
21. **Minor wording.** `docs/SWITCH-PERFORMANCE.md:142` describes `SWITCH_CONST_VMX_TABLES` as `lvlx`/`lvrx`-style
    shuffles; the pass rewrites any shuffle indexed by a `VectorMaskL/R` row, from `lvx`/`stvx`/`lvlx`/`lvrx`
    (`tools/build-switch.sh:49`, XenonRecomp patch:1341-1344). `tools/switch-codegen-pass.py:3` calls itself a port of
    Unleashed's round 11-12 pass, but it includes `SWITCH_INLINE_MEMCPY` (Unleashed round 14).
22. **perf6's Present without the record wait never ran.** Its condition requires `!g_needsResize`, and nothing in
    Marathon cleared the flag once the first swap chain creation set it (Unleashed clears it in its render director
    hook), so every perf6 frame took the record wait (1.34-1.39 ms a frame in the perf6 CPU-limited log). Fixed in perf7:
    Present clears it after recomputing the viewport (`video.cpp:5979-5991`).
23. **ZCULL direction.** Section 3 said Marathon does not use reverse Z and that ZCULL keeps the driver's "less"
    direction; the perf5 draw profile shows the main pass drawing greater-or-equal, and perf6 turned `SwitchZcullGreater`
    on. The row now says so.
24. **perf7's call locals were chosen by profile rank, not by effect.** Compiled with and without them, 679 of the 1,028
    functions did more instructions and most did more loads and stores (the stores before each call and the reloads
    after it), and 107 of the 145 no-contract functions did more loads and stores than their context form. perf8 picks
    each function from its compiled code.
25. **perf7's cascade adoption could draw into a held layer.** A depth-writing draw on the shadow surface after its
    cascade's resolve went into the cascade image's layer that already held the map's slice; in the GPU-limited spot
    the map's bind just before (an adoption) hid it. perf8 continues such a cascade in the next layer or takes the
    surface's content back first.
26. **perf7's thread placement caused audio gaps.** 23 in the perf7 session, none in perf6's: the sound thread on core 2
    shared its time slices with the engine threads there. perf8 puts it back on core 0 by default.
