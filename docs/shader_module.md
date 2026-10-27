# Shader module – RetroArch slang shader support (M6, planned)

Spec by the maintainer, recorded 2026-10-24. **Done (2026-10-27):** phases 1-5 below are built, tested and in
the release; each phase was reported before the next.
**Phase 1 done (2026-10-27):** [shader_module_phase1.md](shader_module_phase1.md) – plan, risks, effort, and five
decisions for the maintainer before phase 2.
**Decisions (maintainer, 2026-10-27, all as recommended):** (1) own loader via `SDL_LoadObject` with a fixed path
next to the exe – no `librashader_ld.h` search, no `windows.h` outside `platform/` (this replaces "`librashader_ld.h`"
under Integration); (2) zip: tinfl (MIT) + own small reader, licence in `docs/toolkit.md` and the toolkit NOTICE;
(3) bezel / border presets hidden from the browser; (4) FXAA skipped while a shader is active; (5) ARMSX2 cloned
read-only into `refs/` when phase 4 starts. librashader's cache stays disabled (it writes to `%LOCALAPPDATA%`).
Measure the load / compile time of a heavy preset; a short "loading shader" hint only if it is noticeable.
**Phase 2 built (2026-10-27, branches `shader-module` / `shader`, not merged):** toolkit `src/nv2a_vk/shader_pass.c`
(own loader, worker-thread build with the deferred create, the pass in place of FXAA / the blit, the game rectangle as
viewport), THPS `shader_preset` in `saves\settings.ini` (path relative to the layout folder or absolute) and
`THPS_SHADER_PRESET`; toolkit `RECOMP_SHADER_PRESET` wins. Load time: crt-easymode (1 pass) 141–201 ms,
crt-guest-advanced (12 passes, 148 parameters) 238–387 ms on the worker, < 1.1 ms on the render thread, no extra
hitch – **no "loading shader" hint needed** (the picture stays unshaded until the chain is ready; the SHADERS menu of
phase 4 can show LOADING from `nv2a_vk_shader_status`). **Found:** the official Windows `librashader.dll` (v0.12.0,
also the win7 build; 0.12.1 has no binaries) imports **`D3DX9_43.dll`** – not part of Windows 11; without it the DLL
does not load (logged as such, shaders off). **Decided 2026-10-27 (maintainer, variant 2, every port):**
librashader is built from source, Vulkan runtime only, by `tools\deps.ps1` (Rust build-time only; reason and
licence in `docs/toolkit.md`) - this replaces "user-supplied DLL" under Integration; no D3DX9 hint anywhere.
**Phase 3 built (2026-10-27, toolkit `shader-pack`, THPS `shader-pack`):** toolkit `src/shaderpack/` - each zip in
`media\shaders\` extracted once to `data\shaders\<zip name>\` (own central-directory reader, miniz `tinfl` 3.1.2 MIT
vendored for deflate, CRC-32 checked, stored + deflate only, no ZIP64; zip-slip guard; the single root folder
`slang-shaders-master/` stripped; `<pack>.partial\` + `.pack_files` list, renamed when complete, marker
`.pack_source` = zip size + mtime written last; an unchanged zip is never touched again, a changed one replaces
exactly the listed files - no recursive delete), folders in `media\shaders\` used in place, the preset index
`data\shaders\index.txt` (pass count through `#reference`, `bezel` / `border` folders left out) rebuilt only when a
pack changed. THPS `main.c` starts it on a low-priority thread at start-up (`shader_pack_start`); the menu (phase 4)
reads `shader_pack_index`. Unit test `tests/shaderpack`: 71 checks, a crafted malicious zip among them. Stock pack:
5214 files / 68.1 MB extracted and 1123 presets indexed (2660 minus 1494 bezel and 43 border) in 6.9–7.9 s on the
background thread at the first start, no extra hitch; later starts read the index back in 1 ms.
**Phase 4 built (2026-10-27, toolkit `shader-menu`, THPS `shader-menu`) - NOT merged: a menu / visual change, the
maintainer's check first.** RECOMP OPTIONS -> **SHADERS** (its last line, "SHADERS: OFF" / "SHADERS: CRT EASYMODE",
so the existing input scripts keep their lines): SHADER ON / OFF (left / right or A), PRESET (the browser), PARAMETERS
("PARAMETERS: 17  1 CHANGED"), SAVE AS PRESET, RESET ALL PARAMETERS (confirmation RESET / CANCEL, on CANCEL).
Without librashader or without a pack: the hint line ("NEEDS LIBRASHADER IN THE GAME FOLDER" / "NEEDS A SHADER PACK IN
MEDIA SHADERS") and the four lines greyed. Title "SHADERS  LOADING" while a preset builds (the screen follows by
itself). Browser: the packs (+ MY PRESETS), NONE at the top, ".. UP" inside, folders first ("CRT ..."), presets with
their pass count ("CRT EASYMODE  1 PASS", "BROKEN" for an unresolved reference), 10 a page, "CRT  3 OF 11", NEXT
PAGE and left / right for pages; it opens on the folder of the preset in use. Parameters: 10 a page from the preset
in place (toolkit `nv2a_vk_shader_params`; the label is the parameter's description in the panel font), left /
right one step (no step given: 1/100 of the range, at most 10,000 steps, on the step grid), A = back to the
preset's value, min == max = a caption; live. Saved in `saves\settings.ini`: `shader_on`, `shader_preset`
(relative to the layout folder when inside it), `shader_params` (`name=value;...`, only the changed ones, dropped
when another preset is picked). SAVE AS PRESET writes `saves\shaders\My Presets\MY PRESET <n>.slangp` (`#reference`
relative + the changed values; toolkit `shader_pack_save_preset`) and uses it at once. Test input:
`tests/input/opt_shaders.txt`.
**Approved by the maintainer 2026-10-27 after two fixes, then merged:** the "SAVED AS <name>" line goes back to SAVE
AS PRESET on any change (preset, parameter, reset, shader on / off) and when SHADERS is left; the reset confirmation
reads "RESET <n> CHANGE / CHANGES TO PRESET VALUES" (the panel font has no parentheses, so no "CHANGE(S)"); with a
shader in place (LOADING or ON) and FXAA chosen, RECOMP OPTIONS shows "ANTIALIASING: FXAA OFF WITH SHADER" (the
chain replaces FXAA in `vk_flip`). No shoulder-button big steps (maintainer: not needed); a "MODIFIED ONLY" view is
not planned.
The maintainer's own test on the normal install passed (2026-10-27).

**Phase 5 done (2026-10-27).** Measurement (exclusive, one block, a test layout copy, Hangar `aa_hangar.txt` 60 s
window, 4:3, window 960x720, FXAA chosen = skipped while a shader is on, pack copied into each fresh data dir
beforehand, GPU clock logged).
Each run has one long frame right before the closing screenshot (the BMP write: 90 ms at 1x, 0.65 s at 3x, 1.4 s
at 4.5x - the measurement, not the shader; left out below). Frame ms p50 / p99, GPU ms avg (p99):

| preset (passes) | 1x | 3x | 4.5x |
|---|---|---|---|
| off (FXAA) | 16.63 / 17.31, GPU 0.41 (0.60) | 16.63 / 17.27, GPU 1.19 (1.66) | 16.62 / 17.29, GPU 2.32 (3.25) |
| sharp-bilinear (1) | 16.62 / 17.32, GPU 0.39 (0.55) | 16.63 / 17.31, GPU 1.08 (1.45) | 16.62 / 17.30, GPU 2.12 (2.91) |
| crt-easymode (1) | 16.63 / 17.31, GPU 0.45 (0.64) | 16.64 / 17.27, GPU 1.24 (1.62) | 16.63 / 17.29, GPU 2.15 (2.93) |
| crt-guest-advanced (12) | 16.62 / 17.29, GPU 0.98 (1.15) | 16.62 / 17.30, GPU 3.11 (3.61) | 16.63 / 17.27, GPU 5.74 (6.61) |
| crt-yah base-medium (13, `#reference` chain) | 16.63 / 17.35, GPU 1.14 (1.30) | 16.62 / 17.32, GPU 6.50 (6.80) | 16.62 / 17.35, **GPU 15.86 (16.78), 8 frames ~31 ms** |

60 fps everywhere except crt-yah at 4.5x, where the chain's GPU time (~16 ms, its first passes run at the source
size, 2880x2160) fills the frame - a real load, not an outlier, so not repeated. Off equals the baseline (p99 17.31
here, 17.31 / 17.32 ms in the phase 2+3 check). The shader cost grows with the render scale because the chain reads
the rendered image at its own size; the window size stays 960x720. Build times: sharp-bilinear 162 ms,
crt-easymode 114, crt-guest-advanced 265, crt-yah base-medium 660 ms (on the worker thread; 0.5-1 ms on the render
thread). The stock pack has no Retro Crisis preset; crt-yah base-medium (a two-step `#reference` chain from the
stock pack) stands in for it - a real Retro Crisis pack is measured once the maintainer drops one into
`media\shaders`.
Functional (`20261027_145802_phase5_functional`, `20261027_150848_phase5_functional2`): no `librashader.dll`
- the game runs, log "no librashader.dll next to the exe -- shaders off", SHADERS shows "NEEDS LIBRASHADER IN THE
GAME FOLDER"; no pack - "NEEDS A SHADER PACK IN MEDIA SHADERS"; a broken preset (missing pass file) - log names the
file, "the picture stays unshaded", the game presents normally (RECOMP OPTIONS still says "SHADERS: BROKEN PASS"
without marking the failure - small open point); saved MY PRESET 1 after a restart (same saves and data) - built
again at start (132 ms), visible from the first logo; 16:9 + crt-easymode - menus 4:3 pillarboxed, gameplay 16:9
through the chain (captures with PrintWindow, the game window only); re-extract after `data\` was deleted -
phase 3 (`20261027_080440_phase3`). Found: a saved preset's `#reference` points into `data\shaders`; after
`data\` is deleted the pack is re-extracted in the background, but the preset is tried at start - expected (from
the code and the first restart attempt, whose data folder was gone: "loading the preset failed ... not used"), not
run as its own case: it stays off for that one start and works from the next one. **Closed 2026-10-27
(maintainer: implement):** when the extraction at start ends, a preset that failed meanwhile is applied once more
(`sh_retry_tick` in `thps_options_tick`; off-then-on, since the toolkit takes the same path again as no new
request) - run `20261027_162223_shader_retry/g2_redo`: "loading the preset failed", 5214 files extracted in 6.7 s,
"the preset that failed at start once more", MY PRESET 1 built in 109 ms. **Also closed:** a preset that failed
reads "SHADERS: <name>  FAILED" in RECOMP OPTIONS (`f_failed/05_recomp_options`). Retro Crisis: not installed, so
no case (maintainer).

## Goal

Optional RetroArch slang shaders (`.slangp` presets) on the final game image. A toolkit-generic presenter pass in
the Vulkan back end; THPS 2X only wires config + menu. Reusable for every later port.

## Integration (maintainer decision)

- **librashader** via its C API with `librashader_ld.h` (runtime loading). No Rust in our build, no own slang
  runtime, our code stays C.
- The user supplies `librashader.dll`/`.so` (official librashader releases) next to the exe. We never ship or
  commit it (MPL-2.0 binary). Missing DLL → the loader falls back to no-op; the shader submenu is greyed out with a
  hint.
- Vulkan path with dynamic rendering (Vulkan 1.3 baseline).
- Reference for logic (preset index, parameter model, menu structure, simple-preset saving): **ARMSX2** (GPL-3.0) –
  `GPSDeviceVK.cpp` librashader sections, ShaderIndex / ShaderParams / ShaderParamsEditor. Port the logic only;
  credit ARMSX2 in the README and `docs/sources.md`.
- RetroArch only as the format reference (`.slangp`, `#reference` simple presets).
- Reason + licence note (librashader MPL-2.0 / GPLv3, user-supplied binary) in the toolkit's `docs/toolkit.md`
  **before** adding it.

## Shader packs and storage (portable folder rules)

- No download, no network access at all.
- The user places `shaders_slang.zip` (or an extracted folder, plus optional extra packs such as Retro Crisis) in
  `media/shaders/` (read only).
- On start the port extracts new/changed zips once to `data/shaders/<pack-id>/` (a cache; rebuilt from `media/` if
  `data/` is deleted). Keep the directory structure, zip-slip guard, strip a single common root folder. Extra packs
  that need the base pack: refuse with a clear message if it is missing.
- Preset index (with the pass count per preset) as a cache in `data/`; rebuilt when packs change.
- User presets ("My Presets"), the chosen preset and parameter overrides in `saves/shaders/` – never in `data/`.
- Missing pack → shader submenu greyed out with a hint.

## Rendering rules

- Applied after the internal resolution factor, before the swapchain. Does not change the resolution factor.
- Covers only the game image area: 4:3 content (menus, loading screens, movies) only in its 4:3 area, 16:9 gameplay
  the full area. Never on the pillarbox bars – window and fullscreen.
- Handles a resolution-factor change, the 4:3 ↔ 16:9 switch, resize/fullscreen and swapchain recreation.
- **Default OFF**; when off: zero extra passes, zero per-frame cost.
- Broken preset / compile failure → log, run without shader, never crash.

## Menu

Own submenu **SHADERS** in RECOMP OPTIONS (extends the menu model for every port – update the toolkit's porting
guide + `templates/game` when built). Usable with a controller alone.

- Shader: ON / OFF.
- Preset browser: folder navigation (`crt/`, `handheld/`, …), pass count shown as a cost hint, "None".
- Parameters submenu generated at run time from librashader (never hardcoded):
  - 0 to ~900 parameters → paged / virtualized list
  - min == max → caption, not a control
  - step 0 or invalid → 100 steps; cap at 10,000 steps
  - D-pad adjusts, shoulder buttons take large steps, per-parameter reset, "Reset all" with confirmation
  - changes apply live; the number of modified parameters is shown
- "Save as preset": a RetroArch simple preset (`#reference` relative path + changed params) in
  `saves/shaders/My Presets/`, selectable like any other preset.
- The last preset + overrides are loaded on start.

## Phases (each reported before the next)

1. **Analysis, no code:** the final-frame point before present; how 4:3 content vs 16:9 gameplay reach the
   presenter; how the game's options menu can host a multi-level submenu with controller navigation;
   `librashader_ld.h` integration points. Report plan, risks, effort.
2. Presenter pass + config (env override `THPS_SHADER_PRESET` for tests).
3. Pack extraction + index.
4. Menu + parameters + save preset.
5. **Measurement** (exclusive lock, the project's benchmark conditions): frame time off/on
   at 1×, 3×, 4.5×; off must equal the baseline. Presets: sharp-bilinear, crt-easymode, crt-guest-advanced (many
   params), one Retro Crisis preset (`#reference` chain). Tests: missing DLL, missing pack, broken preset,
   re-extract after `data/` was deleted, saved preset after a restart, 4:3 ↔ 16:9 switch.

## Wrap-up (when built)

- Small focused commits; a "Shader module" section in the toolkit docs for reuse.
- Built on `vulkan-backend` for THPS, then added to the toolkit's lessons list for later ports.
- README + release notes: AI-assistance note, licence notes (librashader user-supplied, shaders user-supplied),
  ARMSX2 credit.
- No personal references; repos stay private; no PRs/pushes to foreign repos.

## Notes for phase 1 (from work so far, 2026-10-24)

- Test pack present: `shaders_slang.zip` (libretro/slang-shaders master, 2026-10-24, 54.6 MB) in the normal
  layout's `media/shaders/`. Builds and installers never touch `media/` (CLAUDE.md, Portable Install Layout);
  extraction goes to `data/shaders/` only.

- The Options screen has a working multi-list model (`s_opt` in `src/recomp_manual.c`, panel rewrite, A/B handlers)
  and now left/right on a line (vtable 0x0022A2B0 +0x38/+0x3C overrides; a step re-enters the screen through the A
  latch). Limits found: 12 line slots per panel; a value change rebuilds the screen. A ~900-entry parameter list
  needs paging on top of that.
- The present path: `nv2a_vk.c` flip/present blit (scaled surfaces, 4:3 pillarbox via `xbox_present_wide`); the
  render scale (1x–5x) is per surface – the shader pass sits after it.
