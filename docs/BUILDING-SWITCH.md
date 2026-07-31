# Building for Nintendo Switch

The Switch port cross-compiles with **devkitA64** and links a static **NVK**
(Mesa) Vulkan driver. Once set up, a full build — including future updates — is
a single command:

```bash
NVK_ROOT=/c/path/to/mesa-nvk  tools/build-switch.sh
```

It produces `dist/switch/MarathonRecomp.nro`.

---

## 1. Prerequisites (Windows, MSYS2)

- **devkitPro** with the **Switch** workload (provides `devkitA64`, `libnx`,
  `nacptool`, `elf2nro`). Set `DEVKITPRO` if it isn't `/opt/devkitpro`.
- **MSYS2 CLANGARM64** toolchain — the native ARM64 host compiler + build tools:
  ```bash
  pacman -S mingw-w64-clang-aarch64-clang mingw-w64-clang-aarch64-cmake mingw-w64-clang-aarch64-ninja
  ```
- **MSYS2 CLANG64** (x86_64 clang):
  ```bash
  pacman -S mingw-w64-clang-x86_64-clang
  ```
  Only used to build the shader tool against the **x64 DXC**. The bundled arm64
  DXC emits invalid SPIR-V for some shaders, so the x64 DXC is used under
  emulation instead (the app-shader step already does this).

## 2. Inputs you supply (never committed)

These are `.gitignore`d and must be provided locally:

| Input | Location | Notes |
|---|---|---|
| `default.xex` | `MarathonRecompLib/private/` | from your game dump (root) |
| `shader.arc`, `shader_lt.arc` | `MarathonRecompLib/private/` | from `xenon/archives` |
| **NVK Vulkan driver** | `NVK_ROOT` env var | closed build; **not shipped**. Accepts the relocatable Switch SDK (`lib/libvulkan.a`) or a legacy Mesa build tree (`builddir-switch/src/nouveau/vulkan/libvulkan.a`). |
| `Lossless.dll` (optional) | `sdmc:/switch/MarathonRecomp/lsfg/` | Required at runtime only when LSFG-VK frame generation is enabled; not bundled. |

## 3. Build

```bash
NVK_ROOT=/c/path/to/mesa-nvk  tools/build-switch.sh
```

The script runs, idempotently:

1. **Apply submodule patches** (`patches/*.patch`) — Switch fixes for volk
   (`dlfcn`), XenonRecomp (`ppc_context.h` simde, `xbox.h`), plume, imgui, etc.
2. **Host tools (arm64)** — XenonRecomp, file_to_c, u8extract, x_decompress.
3. **XenosRecomp (x64)** — built for the x64 DXC (see prerequisites).
4. **Recompile PPC** from `default.xex` → `MarathonRecompLib/ppc/`.
5. **Generate shaders** — game shader cache + app SPIR-V headers.
6. **Cross-build FFmpeg** (`tools/build-switch-ffmpeg.sh`) — minimal
   avcodec+avutil with the XMAFRAMES decoder → `thirdparty/ffmpeg-core/switch/`.
7. **Configure + cross-compile** the app (devkitA64 + NVK).
8. **Package** — `nacptool` + strip + `elf2nro` → `dist/switch/MarathonRecomp.nro`.

Re-running is safe; already-applied patches and existing FFmpeg/PPC/shader
outputs are reused. Delete `build/` (and the generated `MarathonRecompLib/ppc/`
+ `shader_cache.cpp`) to force a clean regeneration.

## 4. Common tasks

- **Bump the app version:** edit `MarathonRecomp/res/version.txt`
  (`VERSION_MAJOR/MINOR/REVISION`); the NRO picks it up on the next package.
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
- **LSFG-VK frame generation** is disabled by default. Enable it in Video
  options after placing `Lossless.dll` in `sdmc:/switch/MarathonRecomp/lsfg/`,
  then restart the game. Its pipeline cache is written under
  `sdmc:/switch/MarathonRecomp/cache/`.
