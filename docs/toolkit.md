# Toolkit plan (M0)

Status: **proposal – the maintainer decides.** Findings from reading the repos on 2026-10-13.

## What exists

| Toolkit | State | GPU path | Notes |
|---|---|---|---|
| **sp00nznet/xboxrecomp** `main` (v0.13.1, 2026-10-07) | upstream, newest lifter fixes (incl. NoRain211's recomp/disasm fixes) | **none** – NV2A pushbuffer executed on the CPU (Burnout 3: 2–4 fps) | has `tools/xiso`, `tools/xbe_parser`, kernel audit tools – use them, don't rewrite |
| **comoria08/xboxrecomp** `defjam-bringup` (2026-10-10) | branched from upstream in Sept (merge-base 3706cef) + 159 Def Jam bring-up points | **D3D11 built into the executor** (`kernel/nv2a_pb_exec.c` ~6.3k lines, `nv2a_d3d11.c`, combiners → HLSL, DLSS) | Windows only; MSVC or llvm-mingw; proven on Def Jam |
| **burnout3-sw** vendored toolkit (via the NFSU2/Carbon Switch fork, based on an older upstream) | Burnout 3 playable on Linux/Switch | **Back-end interface** `kernel/nv2a_backend.h` (`draw_raw`); `nv2a_gl` (OpenGL) and **`nv2a_vk` (Vulkan 1.3, ~4.2k lines)** share the GLSL generators `nv2a_gl/gl_vsh.c` (vertex programs) and `gl_psh.c` (register combiners); glslang at run time; SDL2; pthreads | Vulkan only built for Linux/Switch; its Windows host still uses D3D11. Older lifter than upstream |
| TGP482/XboxXBE-Recomp | separate design (HLE D3D8 → D3D12) | – | reference only (Vulkan-only rule) |

The two GPU paths are **independent implementations**, not one code base: comoria's D3D11 code lives inside its
executor, burnout3-sw puts a back-end interface between executor and renderer. A clean "merge" is not possible;
fixes have to be ported by hand.

## Licensing (checked)

- Toolkit: MIT. `src/apu/dsp/` (MCPX DSP, from Hatari/xemu): **GPL-2.0-or-later**. APU core, `nv2a_regs.h` and a few
  xemu-derived files: **LGPL-2.1-or-later**. No GPL-2.0-only code found (`NOTICE`, `LICENSES/`, grep over sources).
- burnout3-sw: MIT (vendored toolkit MIT plus the same third-party files).
- ⇒ Everything is compatible with this repo's **GPL-3.0-or-later**. A binary that links `src/apu/dsp/` is a GPL
  combined work – irrelevant here, since no binaries are distributed.

## Audio decoders for custom soundtracks (licence check, 2026-10-24 – proposal, the maintainer decides)

Why: custom soundtracks (CLAUDE.md → Custom soundtracks) play the user's MP3, FLAC, WAV, Ogg Vorbis and AAC files,
decoded platform-neutrally (no Media Foundation, no OS codecs – so the toolkit's `src/audio/wma_decoder.c`, which
uses Media Foundation, is not used for this). The game's own WMA music stays on its lifted WMA decoder. Licences as
stated by each project; to be re-read in the exact file version when it is vendored (`NOTICE`, `LICENSES/`).

| Format | Candidate | Licence | Fits MIT toolkit / GPL-3.0-or-later port | Notes |
|---|---|---|---|---|
| MP3 | dr_mp3 (mackron/dr_libs) | public domain (Unlicense) or MIT-0, choice | yes / yes | single header, no allocations beyond the decoder |
| FLAC | dr_flac (dr_libs) | public domain or MIT-0 | yes / yes | |
| WAV | dr_wav (dr_libs) | public domain or MIT-0 | yes / yes | PCM, IEEE float, ADPCM |
| Ogg Vorbis | stb_vorbis (nothings/stb) | MIT or public domain | yes / yes | |
| `.m4a` container | minimp4 (lieff) **or** the boot video's own small MP4 demuxer | CC0 | yes / yes | only the audio track's sample table is needed |
| AAC | faad2 | GPL-2.0-or-later | toolkit module becomes GPL (like `src/apu/dsp/`) / yes | mature, LC + HE-AAC |
| AAC | PacketVideo/OpenCORE AAC decoder (Android `aacdec`, old) | Apache-2.0 | yes / yes (Apache-2.0 is GPL-3 compatible) | unmaintained, LC + HE-AAC v1 |
| AAC | Fraunhofer FDK AAC | FDK licence | **no** (GPL-incompatible terms) | rejected |
| AAC | Helix AAC | RPSL / RCSL | **no** | rejected |
| AAC | FFmpeg native decoder | LGPL-2.1-or-later | yes / yes, but a large library for one format | rejected (size) |

**Closed (maintainer, 2026-10-27): no AAC decoder.** `.m4a` / `.aac` stay skipped with a log line - the user
converts them to MP3 or Ogg; the boot animation uses MPEG-1 + MP2 through pl_mpeg instead (next section).
Reference design (not code): `refs/doaxvb-re` docs/custom-soundtracks.md – GPL-3.0, so its code may go into this
repo but not into the MIT toolkit; the toolkit part is written new.

## pl_mpeg (boot animation, maintainer 2026-10-27)

**Why:** the alternative boot animation (CLAUDE.md, `media/boot.mpg`) needs a video and an audio decoder. H.264 +
AAC (the first plan) needed Vulkan Video decode (missing on many Android and Intel drivers), an MP4/MKV demuxer and an
AAC decoder whose licences were all awkward (table above). MPEG-1 video + MP2 audio in an MPEG program stream needs
one small decoder that runs everywhere on the CPU; for a short intro the CPU cost is small, and the player converts
the clip once with ffmpeg (the README has the command). **What:** pl_mpeg (phoboslab/pl_mpeg, commit `c871f2b`,
2025-12-30): one header, `pl_mpeg.h` - MPEG-1 video decoder, MP2 audio decoder, MPEG-PS demuxer - vendored
unchanged in the toolkit's `src/bootvideo/third_party/pl_mpeg/` (its implementation compiled in `pl_mpeg_impl.c`).
No new runtime library: C only; file reading, audio output and the pads through SDL3, the frames through the
renderer (`nv2a_vk_boot_frame`). **Licence:** MIT (`SPDX-License-Identifier: MIT` in the header; the upstream repo
has no separate licence file, so `third_party/pl_mpeg/LICENSE` holds the standard MIT text with the author's name,
Dominic Szablewski) - fits the MIT toolkit and this GPL-3.0-or-later port; credited in the toolkit `NOTICE` and
`docs/sources.md`.

## SMAA (RECOMP OPTIONS → ANTIALIASING, maintainer 2026-10-26)

**Why:** SMAA smooths edges better than the existing FXAA pass at a fraction of the cost of a higher render scale
(THPS 2X, RTX 3050 Ti, 16:9 at 4.5x: GPU 3.8 ms against 3.1 ms with FXAA, measured 2026-10-26), and it works on every surface, where MSAA only reaches the presented one. **What:** the reference
implementation, iryoku/smaa commit `71c806a` – `SMAA.hlsl` (one shader source with a GLSL 4 mode) and the
precomputed area / search textures `AreaTex.h` / `SearchTex.h` – vendored unchanged in the toolkit's
`src/nv2a_vk/third_party/smaa/` with its `LICENSE.txt`. **No new library:** three header/shader files compiled by
the glslang the renderer already has; CMake turns `SMAA.hlsl` into a header at configure time. **Licence:** MIT,
(c) 2013 Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez – fits the MIT toolkit
and this GPL-3.0-or-later port; credited in the toolkit `NOTICE` and `docs/sources.md`. Size: the area texture
header is 1.1 MB of source (160x560 RG8 = 179 KB in the binary).

## librashader (shader module, M6; maintainer 2026-10-24 / decisions 2026-10-27)

**Why:** RetroArch slang presets (`.slangp`: CRT, pixel-art scaling, handheld looks) are a large, maintained
library of picture filters players know; librashader is the one runtime for them outside RetroArch with a C API and
a Vulkan back end, so the port stays C and needs no own slang compiler (spec: `docs/shader_module.md`). **What:**
only the C header `librashader.h` (SnowflakePowered/librashader tag `librashader-v0.12.0`, commit `87e8a97`, C ABI
2 / API 5; identical in v0.12.1) is vendored, unchanged, in the toolkit's `src/nv2a_vk/third_party/librashader/`.
**The library is built from source (maintainer, 2026-10-27, variant 2; every port):** `tools\deps.ps1` builds
librashader `librashader-v0.12.1` (commit `07ac790`) with the **Vulkan runtime only** (`--no-default-features
--features runtime-vulkan`: no D3D9 / D3D11 / D3D12 / OpenGL / Metal) for `x86_64-pc-windows-gnullvm`, its C/C++
parts and the link with our llvm-mingw clang, libc++ / libunwind linked in statically - the result imports only
Windows' own DLLs. Why: the official Windows build imports `D3DX9_43.dll` (its D3D9 runtime), which Windows 11 does
not ship, so it does not load on a stock system (found in phase 2, 2026-10-27); a Vulkan-only build
is also what Android needs later. **Rust** is a build-time-only dependency, pinned (1.99.1), installed
project-local into `private\deps\rust` by `deps.ps1 -InstallRust` (no PATH or profile change). The DLL goes next to
the exe at build time (CMake copies `private\deps\install\bin\librashader.dll`), never committed. A build without
Rust has no `librashader.dll`: the shader module is unavailable, the SHADERS menu greyed with its hint, the game as
before. **Loader** (decision 1): our own, `SDL_LoadObject` with the absolute path
`<exe folder>/librashader.*` (`src/nv2a_vk/shader_pass.c`); not `librashader_ld.h` (it includes `windows.h` and
calls a bare `LoadLibraryW`, which follows the DLL search order). The ABI is checked before any other symbol;
missing file or another ABI = the module stays off. **Cache:** librashader's own shader cache is always disabled
(`disable_cache`) - it writes to `%LOCALAPPDATA%\librashader`, which breaks the portable rule; every preset load
compiles. **Licences:** the header is MIT (c) 2022 chyyran - fits the MIT toolkit and this GPL-3.0-or-later port;
credited in the toolkit `NOTICE` and `docs/sources.md`. The library is **MPL-2.0** (or GPLv3), built unchanged
from its upstream source at the pinned commit; it stays a separate DLL (MPL-2.0 is file-level copyleft: our code
that only loads it is not affected). Like everything else of the port it is built locally by whoever builds the
port and not distributed by us; a distributed build (a release, the Android APK later) must carry librashader's
licence text and say where its source is (MPL-2.0 §3.2) - noted for the release notes. **Inflate for the zip
packs (phase 3, decision 2):** miniz's `tinfl` (richgel999/miniz `3.1.2`, commit `77d0dce`, MIT, (c) Rich Geldreich
/ RAD Game Tools / Valve) vendored unchanged in the toolkit's `src/shaderpack/third_party/miniz/` (only
`miniz_tinfl.c` compiled; the zip / deflate / zlib parts switched off); the central-directory reader is our own
(`zip_read.c`).

## Options

**A – upstream + Vulkan back end from burnout3-sw (recommended)**
- Base: `sp00nznet/xboxrecomp` main (newest lifter, important for an early-XDK title).
- Port onto it: `nv2a_backend.h` + the executor hooks, `nv2a_gl/gl_vsh.c`, `gl_psh.c`, `nv2a_vk/`, platform bits.
- Make `nv2a_vk` build on Windows: SDL2 (later SDL3), pthreads (llvm-mingw ships winpthreads; with clang-cl use a thin
  Win32 shim), glslang/SPIRV-Tools from the Vulkan SDK.
- Port proven fixes from defjam-bringup where they apply (texture formats, surface handling, kernel/APU fixes).
- Validate with Burnout 3 (ask the maintainer for the image), then Def Jam, then THPS 2X.
- Pro: newest lifter, Vulkan from the start, clean renderer interface. Con: most integration work; upstream and
  burnout3-sw executors have diverged.

**B – burnout3-sw toolkit as is, ported to Windows**
- Fastest to a Vulkan image on Windows (renderer already wired to its executor).
- Con: older lifter – upstream lifter fixes must be cherry-picked later; risk of lifter bugs on THPS 2X.

**C – comoria08 defjam-bringup (D3D11) for bring-up, Vulkan later**
- Fastest to a first THPS 2X image on Windows, proven toolchain.
- Con: breaks the Vulkan-only rule; renderer work done on D3D11 is thrown away later. Fallback only.

## Fork policy (maintainer decision, 2026-10-13)

- The toolkit is the maintainer's fork of `sp00nznet/xboxrecomp` (branch `vulkan-backend`; published as
  `almost-drowned-rat/xboxrecomp-vulkan`). General fixes (lifter, kernel, renderer) are committed there directly –
  **no upstream PRs, not even prepared**.
- Changes from `sp00nznet/xboxrecomp` are merged into the fork **only on the maintainer's request**.

## Independent of the choice

- **M1 needs no renderer** (entry point + log). Lifting and kernel bring-up can start on upstream right away,
  while the renderer is integrated in parallel.
- **Toolchain:** decided in M2 stage 2, see "Toolchain decision" below.
- **Toolkit tests:** `py -3 -m pytest tools/` in `projects\xboxrecomp`. Needs `capstone pytest pefile numpy`
  (installed by `tools\setup.ps1`, same set as the toolkit's `tools/macos/setup.sh`); without them the suite
  shows import errors that are not code bugs.
- Folder paths without spaces (`tools\paths.ps1`; since the SSD move 2026-10-29 no short-path junction).

## Toolchain decision (M2 stage 2, 2026-10-13)

**llvm-mingw clang everywhere; glslang and SDL3 built from source with it** (`tools\deps.ps1`, static, installed to
`private\deps\install`). Not clang-cl with the Vulkan SDK's glslang.

Why:
- The SDK's glslang is MSVC C++ (`glslang.lib`/`glslang.dll` import `MSVCP140`/`VCRUNTIME140`); llvm-mingw cannot link
  it. Using it would mean switching the whole project – 7470 lifted functions, crash handler with CodeView/PDB via lld,
  `build.ps1` – to clang-cl + MSVC headers/libs. `clang-cl` is not even installed (llvm-mingw has none).
- `nv2a_vk` uses only glslang's **C interface and no optimizer** → glslang builds with `ENABLE_OPT=OFF`, no
  SPIRV-Tools: **40 s** at 2 jobs. SDL3 (static): **94 s**. One-time cost, rerun only when a tag changes.
- **Portable install:** the exe links glslang, SDL3, libc++ and winpthread statically; its only non-system import is
  `vulkan-1.dll` (GPU driver). No VC++ redistributable needed. Checked on `tests/nv2a_vk_window`.
- **Later ports:** the same source build of glslang/SDL3 works for Linux and Android (NDK = clang + libc++); an SDK
  binary would be a Windows-only special case.
- Versions pinned in `tools\deps.ps1`: glslang **16.6.0** (= Vulkan SDK 1.4.363), SDL **release-3.4.18**. The SDK is
  still used for the Vulkan headers and `vulkan-1.lib` (import library, C ABI, lld reads it) and the validation layer.

## Renderer port state (M2 stage 2)

- Fork `vulkan-backend`: `70928a6` executor + back-end interface from burnout3-sw (replaces upstream's CPU executor),
  `6248012` `nv2a_vk` + shader generators on SDL3, option `XBOXRECOMP_VULKAN` (off by default).
- Only the NV2A parts of burnout3-sw's kernel were taken; the rest is listed in `docs/sources.md` (findings).
- `tests/nv2a_vk_window` (fork): the back end alone – window, clear, one vertex-coloured triangle via `draw_raw`, flip.
  Build: `cmake -S <root>/tools/xboxrecomp/tests/nv2a_vk_window -B <root>/tools/private/build/nv2a_vk_window
  -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
  -DCMAKE_PREFIX_PATH=<root>/tools/private/deps/install`.

## Open (filled in by `tools\m0_analyze.ps1` on the device)

- THPS 2X `default.xbe`: SHA1, XDK build (GameList says 3947), library versions, kernel imports, sections.
- Movie and audio formats on the disc (file extensions).
- Original frame rate / frame-rate coupling: needs a run in Cxbx-Reloaded or analysis of the main loop.

## Design principles (maintainer decision A10, 2026-10-15)

Priority over the 2026-10-14 directions below where they conflict. Apply to every decision, in this order of priority: **1. resource efficiency** (CPU, RAM, battery, disk),
**2. portability** (Windows, Linux, Android as equal targets), **3. compatibility** across games and XDK builds,
**4. development speed**. Consequences:

- Headless Linux build in CI from now on; Android test build after THPS 2X M4. Every HLE/renderer decision is
  checked for Android (tile GPUs, ARM64) from the start.
- LLE paths (pushbuffer executor, USB/OHCI, MCPX APU) are compile-time optional; a release build drops them once
  the HLE fully covers its game.
- Shaders: SPIR-V generated and cached on disk; glslang only on a cache miss, or removed entirely.
- No settings overlay: settings live in the game's own options menu (plus env-var overrides).
- Dependencies limited to C, SDL3 and Vulkan; a new library needs a written reason.
- No busy-waiting: frame cap, pause and loading screens sleep; V-Sync on by default.
- No duplicate texture copies in host RAM.
- Budgets (RAM, CPU ms/frame, binary size) once the frame-time counter has data; CI warns when a change exceeds them.

### Low-end target (A11, 2026-10-15)

"Runs on a potato" – meaning weak **Vulkan 1.3** hardware. **Vulkan 1.3 is the fixed baseline** (maintainer,
2026-10-16): dynamic rendering and extended dynamic state are required (what `nv2a_vk` uses; vertex-input dynamic
state is optional since 2026-10-29 - without it the vertex formats are baked into the pipelines); devices with only Vulkan 1.1/1.2 drivers are not supported, and there is no 1.1 fallback (the earlier
nv2a_vk downgrade plan is dropped). Further: DXT → ETC2/ASTC transcode cached on disk (RGBA fallback), hard cache
caps and a RAM budget, a "low" profile (native resolution, 30 fps with decoupled logic, no MSAA), background shader
pre-warm, reached-code-only builds, game thread + render thread. Reference low-end set: Radeon Vega 7 iGPU (Ryzen 5
5600H, the dev laptop) and Intel HD Graphics 520 + Mesa ANV (Vulkan 1.3) – measured on the RTX
3050 Ti only for now (no hardware changes, the iGPU stays unused); the HD 520 is a paper target (design guidance,
not a test requirement). The Samsung Galaxy A12 was dropped (Vulkan 1.1 only). Details: toolkit
`docs/porting_guide.md`.

### From XboxXBE-Recomp (A12, 2026-10-15; TGP482, MIT – attribution kept)

- **THPS 2X, M3.5/M4:** overridable recompiled functions (weak symbols) + per-game instruction hooks; Bink fix above
  30 fps; host CRT math.
- **Next port:** per-game TOML config (functions, switch tables, data ranges), lifter coverage (gap discovery, every
  jump-table form, C++ exceptions, x87 precision, LTCG libraries), incremental partitioned codegen, renderer
  extension API, xbe2pe Ghidra export, frame-capture key, XBE image switching.
- **Decision point after M3.5 + the SCDA benchmark:** "our toolkit + adopted features" vs. "fork XboxXBE-Recomp +
  Vulkan back end behind `gpu/backend.h` + SDL3 audio + more XDK builds". A short comparison (effort, efficiency,
  portability, XDK coverage) is prepared for it.

## Direction (maintainer decisions, 2026-10-14)

1. **THPS 2X stays on the current toolkit** (fork `vulkan-backend`). Target for M4/M5: game logic decoupled from
   the frame rate, uncapped / 120+ fps without speed-up (reference: `refs/SCDA-XBERecomp/src/game/fpsfixes.cpp`).
2. **Hybrid toolkit.** An XDK library HLE layer: signature detection per XDK build, DirectSound, XInput, D3D8 →
   `nv2a_vk`. The LLE paths (pushbuffer executor, OHCI/USB, MCPX APU/DSP) remain only as the fallback for unknown XDK
   builds or scenes the HLE does not render correctly yet. Reference design: `refs/XboxXBE-Recomp` (MIT) –
   `runtime/xdk/` for the API layer, its function discovery for lifter gap analysis.
3. **THPS 2X is the first D3D8 HLE target** (after M4): signatures and structure layouts for XDK 3947 (D3D8 3947,
   DSOUND 3925, rest 3911), so the layer covers early XDK builds as well. DirectSound and XInput move to API level
   in M3 stage 3 already (SDL3 audio, SDL3 gamepads; USB/OHCI as fallback).
4. **CPU efficiency** (after M4): one mapped guest block (base + address), MMIO checks only where the lifter proves
   a hardware access, guest registers as C locals, `-O2` + LTO. Before that: a **frame-time counter** (game logic /
   pushbuffer or HLE translation / GPU per frame, CSV in `data\`) so every change is measured before and after.
5. **References:** `refs/black-pc` (Spark198rus, GPL-2.0-or-later, read only) for in-game settings menus (M6),
   frame-rate decoupling, widescreen/FOV and performance notes; relevant for Burnout 3 too (same developer). Not a
   toolkit base (loader + D3D9).
6. **Porting playbook in the toolkit fork:** `docs/porting_guide.md` (workflow M0–M6 as checklists with commands),
   `docs/lessons_learned.md` (symptom → cause → fix → how to spot it early), `templates/game/` (starter repo), and
   "Improvements for the next port". Updated at every milestone; THPS docs link there.
7. **Efficiency / portability plan** (after THPS 2X M4): render thread; asynchronous pipeline creation plus an
   on-disk `VkPipelineCache`; extended dynamic state; texture cache with dirty-page tracking instead of hashing;
   swizzle and format conversion in compute shaders; ring buffers for vertex data; PGO after M5. Android:
   tile-friendly render passes, ARM64/NEON.
8. **Process:** the toolkit consumable as an `sdk/` submodule (game repos hold game code only); a growing XDK
   signature database; an early headless Linux build in CI; a regression suite (input scripts + screenshot
   compare) for THPS 2X, Burnout 3 and Def Jam on every toolkit change.
