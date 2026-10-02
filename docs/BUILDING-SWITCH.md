# Building for Nintendo Switch

The Switch port cross-compiles with **devkitA64** and links a static **NVK**
(Mesa) Vulkan driver. Once set up, a full build — including future updates — is
a single command:

```bash
NVK_ROOT=/c/path/to/mesa-nvk  tools/build-switch.sh
```

It produces `dist/switch/MarathonRecomp.nro`.

---

## 1. Prerequisites (Windows, MSYS2 or Git Bash)

- **devkitPro** with the **Switch** workload (provides `devkitA64`, `libnx`,
  `nacptool`, `elf2nro`). `DEVKITPRO` defaults to `/opt/devkitpro`, else
  `C:/devkitPro` (Git Bash exports `DEVKITPRO=/opt/devkitpro` even where that
  folder does not exist; the scripts then use `C:/devkitPro`).
- **Host tools** (XenonRecomp, u8extract, file_to_c) are built natively with
  MSYS2 clang, cmake and ninja:
  - on an **arm64** PC, MSYS2 **CLANGARM64**:
    ```bash
    pacman -S mingw-w64-clang-aarch64-clang mingw-w64-clang-aarch64-cmake mingw-w64-clang-aarch64-ninja
    ```
  - on an **x86_64** PC, MSYS2 **CLANG64** (the same packages, `mingw-w64-clang-x86_64-*`).

  The script looks in devkitPro's own MSYS2 (`C:/devkitPro/msys2`) first, then
  in `C:/msys64`. Set `CLANGARM64=/c/.../bin` (host tools, cmake, ninja) and
  `CLANG64=/c/.../bin` to choose other folders.
- **MSYS2 CLANG64** (x86_64 clang):
  ```bash
  pacman -S mingw-w64-clang-x86_64-clang
  ```
  Used to build the shader tool against the **x64 DXC**. The bundled arm64
  DXC emits invalid SPIR-V for some shaders, so the x64 DXC is used under
  emulation instead (the app-shader step already does this).
- **Python 3** for the direct-calls pass (`SWITCH_DIRECT_CALLS=1`, the
  default): Windows Python (`python`/`py`) or MSYS2's `python3` both work. Set
  `PYTHON=` to choose one.

## 2. Inputs you supply (never committed)

These are `.gitignore`d and must be provided locally:

| Input | Location | Notes |
|---|---|---|
| `default.xex` | `MarathonRecompLib/private/` | from your game dump (root) |
| `shader.arc`, `shader_lt.arc` | `MarathonRecompLib/private/` | from `xenon/archives` |
| **NVK Vulkan driver** | `NVK_ROOT` env var | closed build; **not shipped**. Accepts the relocatable Switch SDK (`lib/libvulkan.a`), a mesa-switch tree built with `build-unified.sh` (`mesa-unified-install/opt/devkitpro/portlibs/switch/lib/libvulkan.a`) or a legacy Mesa build tree (`builddir-switch/src/nouveau/vulkan/libvulkan.a`). |
| `Lossless.dll` (optional) | `sdmc:/switch/MarathonRecomp/lsfg/` | Required at runtime only when LSFG-VK frame generation is enabled; not bundled. |

## 3. Build

```bash
NVK_ROOT=/c/path/to/mesa-nvk  tools/build-switch.sh
```

The script runs, idempotently:

1. **Apply submodule patches** (`patches/*.patch`) — Switch fixes for volk
   (`dlfcn`), XenonRecomp (`ppc_context.h` simde, `xbox.h`), plume, imgui, etc.
   Each submodule is reset (`git reset --hard`, never recursive, and
   `git clean -fd`) and gets its patches in order, but only when the patches
   changed or the tree no longer holds what they produced
   (`build/patch-stamps/`). An unchanged tree keeps its timestamps, so nothing
   is rebuilt. Changes found in a tree before a reset (hand edits, new files,
   whatever `status.showUntrackedFiles` says) are saved first, as
   `changes.diff` plus a copy of each changed or new file, to
   `.git/switch-patch-backups/<submodule>-<date>/` of this repository, where
   deleting `build/` does not remove them. Nested submodules are reset only by
   their own entry below, after their own backup.
   Patches per submodule, in order (`?` = applied when present):
   - `thirdparty/plume`: `plume.patch`, `?plume-switch-perf.patch`
   - `thirdparty/plume/contrib/volk`: `volk.patch`
   - `thirdparty/imgui`, `thirdparty/implot`, `thirdparty/concurrentqueue`: their patch
   - `tools/XenonRecomp`: `XenonRecomp.patch`, `?XenonRecomp-switch-perf.patch`
   - `tools/XenonRecomp/thirdparty/tomlplusplus`: `tomlplusplus-msys-host.patch`
   - `tools/XenosRecomp`: `XenosRecomp.patch`, `XenosRecomp-mingw-dxc.patch`, `?XenosRecomp-switch-perf.patch`

   A patch that does not apply stops the build with git's reason. Changes to
   these submodules therefore have to be written back into their patch files;
   `SWITCH_SKIP_PATCHES=1` builds hand-edited trees as they are.
2. **Host tools (native)** — XenonRecomp, file_to_c, u8extract.
3. **XenosRecomp (x64)** — built for the x64 DXC (see prerequisites).
4. **Recompile PPC** from `default.xex` → `MarathonRecompLib/ppc/`, only when
   XenonRecomp (its tree and patches), `Marathon.toml`, `switch_table.toml`,
   `default.xex` or the code generation mode changed
   (`ppc/ppc_codegen_stamp.txt` lists them). Then the **direct-calls pass**
   (`tools/switch-direct-calls.py apply`), on every build.
5. **Generate shaders** — the game shader cache, only when XenosRecomp (its
   tree, patches and submodules), `shader_common.h` or the game's `.arc` files
   changed (`shader_cache.cpp.translator`); the app SPIR-V headers, only when
   their sources, includes, the script or DXC changed
   (`build/switch-stamps/app-shaders.stamp`).
6. **Cross-build FFmpeg** (`tools/build-switch-ffmpeg.sh`) — minimal
   avcodec+avutil with the XMAFRAMES decoder → `thirdparty/ffmpeg-core/switch/`
   (files are copied there only when their content changed).
7. **Configure + cross-compile** the app (devkitA64 + NVK). With direct calls
   on, the linked ELF is then checked: no hooked function may be called
   directly (`switch-direct-calls.py verify-elf`).
8. **Package** — `nacptool` + strip + `elf2nro` → `dist/switch/MarathonRecomp.nro`.

Re-running is safe; unchanged patches, PPC sources, shaders and FFmpeg are
reused. To force a step, delete its stamp (or `build/`, the generated
`MarathonRecompLib/ppc/` and `shader_cache.cpp` for a clean regeneration).

### Build options

Environment variables of `tools/build-switch.sh` (`0`/`1`; `on`/`off` work too):

| Variable | Default | Effect |
|---|---|---|
| `SWITCH_APP_ONLY` | 0 | rebuild only the app from the outputs of an earlier full build (skips steps 1-6; the direct-calls pass still runs) |
| `SWITCH_SKIP_PATCHES` | 0 | leave the submodule trees as they are (no reset, no patching) |
| `SWITCH_DIRECT_CALLS` | 1 | calls between recompiled functions made direct so GCC can inline them; `0` undoes the rewrite |
| `SWITCH_HOT_FUNCTIONS` | 1 | passed to the direct-calls pass (`0`: no `.text.hot` marking; always `0` in classic builds) |
| `SWITCH_LTO`, `SWITCH_LTO_JOBS` | 0, 2 | link-time optimisation of the whole app (`-flto`, balanced partitions); needs a lot of build RAM and time |
| `SWITCH_PGO`, `SWITCH_PGO_DIR` | empty, `pgo/` | `generate` for an instrumented build (no LTO), `use` to build with its profile |
| `SWITCH_O2` | 0 | all the code with `-O2` instead of `-O3` |
| `SWITCH_RECOMP_O2` | 0 | the recompiled code only with `-O2` |
| `SWITCH_IPA_PTA` | 0 | `-fipa-pta` at compile and LTO link |
| `SWITCH_CLASSIC_CODEGEN` | 0 | `ppc/` generated by XenonRecomp's classic code generation (`XENON_RECOMP_CLASSIC=1`, volatile guest memory), for A/B builds |
| `SWITCH_FIXED_X16` | 1 | `-ffixed-x16` for the recompiled code, the app and the LTO link (see below) |
| `SWITCH_EXACT_FMA` | 0 | Xbox 360-exact fused multiply-add rounding; changes the last bit of some float results compared with builds without it (see below) |
| `SWITCH_BUILD_ID` | date, time and options | names the build: `MARATHON_RECOMP_SWITCH_BUILD_ID` in the generated `switch_build_id.h` |
| `SWITCH_REGISTER_LOCALS` | 1 | CR, CTR, XER and the reservation as locals of each recompiled function, no LR/MSR copies (XenonRecomp) |
| `SWITCH_PLAIN_GUEST_MEMORY` | 1 | guest memory accesses of the recompiled code not `volatile`; loop barriers keep wait loops re-reading memory |
| `SWITCH_NARROW_BARRIER` | 1 | with plain memory: the loop barriers cover guest memory only |
| `SWITCH_WIDE_DFORM` | 1 | register + displacement guest accesses as 64-bit addresses (guard after the 4 GB window, emulated wrap) |
| `SWITCH_FEWER_MODE_SWITCHES` | 1 | XenonRecomp keeps a known FPCR flush mode for instructions whose result does not depend on it |
| `SWITCH_LEAF_LOCALS`, `SWITCH_CONST_VMX_TABLES`, `SWITCH_INLINE_FP_COMPARE`, `SWITCH_INLINE_MEMCPY` | 1 | `tools/switch-codegen-pass.py` options (see `docs/SWITCH-PERFORMANCE.md`) |
| `SWITCH_HOT_FUNCTIONS` | 1 | `MarathonRecompLib/config/hot_functions.txt` functions as `PPC_HOT_FUNC_IMPL` (`.text.hot`) |

The compiler options (`SWITCH_LTO`, `SWITCH_PGO`, `SWITCH_O2`, `SWITCH_RECOMP_O2`, `SWITCH_IPA_PTA`,
`SWITCH_CLASSIC_CODEGEN`, `SWITCH_FIXED_X16`, `SWITCH_EXACT_FMA`, `SWITCH_PLAIN_GUEST_MEMORY`) map to CMake cache
options of the same names with a `MARATHON_RECOMP_` prefix, declared in the root `CMakeLists.txt`. The code generation
options change how `ppc/` is generated (it is regenerated when they change) and have no CMake counterpart; the
first line of `stderr.log` lists the ones `ppc/` was written with. The defaults are the code generation of the
console-tested perf4/perf5 builds; `SWITCH_CLASSIC_CODEGEN=1` gives the code generation of before this work.

Always on for the recompiled code (`MarathonRecompLib`):
`-fno-math-errno -fno-trapping-math` (the guest never sees errno or the FP
exception flags; rounding is still honoured with `-frounding-math`) and
`-fomit-frame-pointer` (the app keeps its frame pointers for crash reports).

**x16.** `os/switch/exception_switch.cpp` resumes an emulated guest load or
store by branching through x16 without restoring it, so no value may live in
x16 there. `-ffixed-x16` keeps it out of register allocation in the recompiled
code and the app, and at the LTO link (lto-wrapper takes `-ffixed-*` from the
link line).

**Exact FMA (opt-in, `SWITCH_EXACT_FMA=1`).** Off, the recompiled code is
built with GCC's default floating-point contraction, as in the base port: GCC
fuses a multiply and an add into one fused multiply-add (one rounding) where
it sees fit, which covers most of the guest's own fused instructions (fmadd,
vmaddfp...) but also some separate guest multiplies and adds, and which ones
depends on the surrounding code and on inlining. `SWITCH_EXACT_FMA=1` fuses
exactly the guest's fused instructions (`PPC_EXPLICIT_FMA`, recompiled code
with `-ffp-contract=off`), one rounding per guest instruction as on the
Xbox 360. This changes the last bit of some float results (vector and
matrix math) compared with builds without it, so it is not the default.
- It needs `ppc/` text with the guest's fused operations as explicit fma
  (`PPC_FMA`, `simde_mm_vmaddfp`...), which only XenonRecomp with
  `XenonRecomp-switch-perf.patch` writes, and not in classic mode. With
  other text, `-ffp-contract=off` would round every guest fused operation
  twice, so the script and the CMake configure step check the text and turn
  the option off with a message (always off with `SWITCH_CLASSIC_CODEGEN=1`).
  The build options string then has no `exact_fma`, and the default build ID
  no `-exact-fma`.
- Until it was made opt-in it defaulted to on: a build folder configured by
  hand before then keeps `MARATHON_RECOMP_SWITCH_EXACT_FMA=ON` in its CMake
  cache (`tools/build-switch.sh` always passes the option).
- With `SWITCH_LTO=1` it is not exact everywhere: an app hook that calls its
  guest body directly (`__imp__sub_X(ctx, base)`) can get that body, and the
  guest functions it calls, inlined by LTO; that inlined code is then built
  with the app's default contraction, which can fuse separate guest
  multiplies and adds again (the reverse also happens: app code inlined into
  recompiled code is not contracted). The LTO build reviewed when this was
  written had no such extra fusion, but any change of the guest code, the
  hooks or a PGO profile can add one. Compare the fused instructions
  (`fmadd`, `fmla`...) of the hook functions with the guest's fused
  operations (`objdump`) before relying on it in an LTO build.

**LTO.** Note that the libraries under `tools/` that the app links (fmt, zstd,
XenonUtils, disasm) are LTO objects in every build (`tools/CMakeLists.txt`
turns IPO on), so every link runs `lto-wrapper` for them. `SWITCH_LTO=1`
extends that to the whole app. It needed lsfg-vk's `get_mpa()` to return the
driver's `vk_icdGetInstanceProcAddr` (in this binary `vkGetInstanceProcAddr`
is volk's function pointer variable).

**PGO.** Two builds from the same build folder, without regenerating code or
editing sources in between:

1. `SWITCH_PGO=generate tools/build-switch.sh` (LTO off). Play for as long as
   possible. The game writes `.gcda` files to `sdmc:/switch/MarathonRecomp/pgo/`
   (`os/switch/perf/pgo_switch.cpp`).
2. Copy those files into `pgo/` in the repository (or `SWITCH_PGO_DIR`), then
   `SWITCH_PGO=use SWITCH_LTO=1 tools/build-switch.sh`.

A profile only applies to the same generated code compiled at the same `-O`
level: collect it again after `ppc/` is regenerated, after the direct-calls
hook list changes, and with the same `SWITCH_O2`/`SWITCH_RECOMP_O2` as the
build that uses it. Functions whose control flow no longer matches are built
without profile (`-Wcoverage-mismatch` warnings).

A refreshed profile (`.gcda` files changed, added or removed in `pgo/`)
rebuilds every object: the configure step hashes the files into a define on
every compile line (`MARATHON_RECOMP_SWITCH_PGO_PROFILE_ID`), and the build
options string names it (`pgo_profile=...`). GCC's dependency files do not
list the profile, so without that only the files including
`switch_build_id.h` would be compiled again.

**Build ID and driver ID.** `build/switch-app/generated/switch_build_id.h`
defines `MARATHON_RECOMP_SWITCH_BUILD_ID`, `MARATHON_RECOMP_SWITCH_BUILD_OPTIONS`
(the options this build was configured with) and
`MARATHON_RECOMP_SWITCH_DRIVER_ID` (the SHA-256 of the linked `libvulkan.a`,
which keys the persistent pipeline cache). The header is rewritten only when
its content changes; include it from a single source file, not from
`stdafx.h`, because the default build ID changes with every build. Set a fixed
`SWITCH_BUILD_ID` to rebuild without relinking.

## 4. Common tasks

- **Bump the app version:** edit `MarathonRecomp/res/version.txt`
  (`VERSION_MAJOR/MINOR/REVISION`); the NRO picks it up on the next package.
- **Rebuild only the app** after editing app sources: `SWITCH_APP_ONLY=1 tools/build-switch.sh`.
- **Re-package only** (after a manual app rebuild): `tools/package-switch-nro.sh`.
- **Bump FFmpeg:** set `FFMPEG_TAG` (default `n7.1`) and delete
  `build/ffmpeg-switch`, then rerun. Keep it aligned with the public headers in
  `thirdparty/ffmpeg-core/include`.

## 5. Notes

- The **NVK driver is closed and never committed** (`**/libvulkan.a` is
  ignored). Everyone building supplies their own via `NVK_ROOT`.
- Generated artifacts (PPC sources, shader cache, app SPIR-V, FFmpeg `.a`) are
  `.gitignore`d — they're reproduced by the scripts, not stored in git.
- An unstripped `build/switch-app/MarathonRecomp/MarathonRecomp.debug.elf` is
  kept for symbolicating crash addresses.
- libnx's `switch.specs` finds its linker script through the `DEVKITPRO`
  environment variable at link time. The toolchain file runs every link with
  the configured `DEVKITPRO`, so `ninja -C build/switch-app` also links from a
  shell whose `DEVKITPRO` is missing or in another form.
- **LSFG-VK frame generation** is disabled by default. Enable it in Video
  options after placing `Lossless.dll` in `sdmc:/switch/MarathonRecomp/lsfg/`,
  then restart the game. Its pipeline cache is written under
  `sdmc:/switch/MarathonRecomp/cache/`.
