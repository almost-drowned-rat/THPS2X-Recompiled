# Shader module – phase 1 analysis (2026-10-27, no code)

Phase 1 of [shader_module.md](shader_module.md): where the pass goes, how 4:3 / 16:9 reach it, how the menu can host
it, how librashader is loaded – plan, risks, effort. **Report for the maintainer; nothing is built.** Sources: the
librashader repository (README, `include/librashader.h`, `include/librashader_ld.h`, `librashader-cache`), docs.rs
(`runtime::vk::FilterChain`), toolkit `vulkan-backend` `3bc8af6` (`src/nv2a_vk/nv2a_vk.c` `vk_flip`), THPS
`src/recomp_manual.c`, and a read-only survey of the test pack `media/shaders/shaders_slang.zip`.

## 1. librashader as it is today

- **Version:** 0.12.1 (latest release), C **ABI 2**, **API 5** (`LIBRASHADER_CURRENT_ABI` / `_VERSION`); unchanged
  since 0.11.x. ABI 2 since 0.5.0. Binaries: `librashader.dll` (Windows x64 on GitHub Releases), `.so` (OBS), `.dylib`.
- **Licences:** the implementation is **MPL-2.0 or GPLv3** (user's choice). The C headers `librashader.h` and
  `librashader_ld.h` are **MIT** (© 2022 chyyran) – they can be vendored into the MIT toolkit (`third_party/`,
  NOTICE) like SMAA; the DLL stays user-supplied, never shipped or committed (spec).
- **Vulkan API (all we need):**
  - `libra_preset_create_with_options(path, ctx, &opt, &preset)` (the plain `libra_preset_create` is deprecated),
    `libra_preset_get_runtime_params` → `libra_preset_param_list_t` of `{name, description, initial, minimum,
    maximum, step}` (free with `libra_preset_free_runtime_params`), `libra_preset_set_param` / `_get_param`.
  - `libra_vk_filter_chain_create(preset, libra_device_vk_t{physical_device, instance, device, queue, entry =
    vkGetInstanceProcAddr}, &filter_chain_vk_opt_t{version, frames_in_flight, force_no_mipmaps,
    use_dynamic_rendering, disable_cache}, &chain)`, or `_create_deferred(..., VkCommandBuffer, ...)` (records the
    set-up into our command buffer; it must have executed before the first frame).
  - `libra_vk_filter_chain_frame(chain, cb, frame_count, libra_image_vk_t in{VkImage, format, w, h},
    libra_image_vk_t out, &libra_viewport_t{x, y, width, height}, mvp = NULL, &frame_vk_opt_t{clear_history,
    frame_direction, rotation, aspect_ratio, frames_per_second, frametime_delta, ...})` – records into **our**
    command buffer; no own submit.
  - `libra_vk_filter_chain_set_param` / `_get_param` (live parameter changes), `_set_active_pass_count`,
    `libra_vk_filter_chain_free`.
  - Layouts: input in `SHADER_READ_ONLY_OPTIMAL`, output in `COLOR_ATTACHMENT_OPTIMAL`, output left there (no
    final barrier – we transition it). Creating a chain on another thread is safe; drawing needs external sync.
- **Loader `librashader_ld.h`:** `librashader_load_instance()` → `libra_instance_t` (function table +
  `instance_loaded`); define `LIBRA_RUNTIME_VULKAN`. ABI mismatch or missing DLL → every entry is a no-op stub, so
  "missing DLL → module off" comes for free. **But:** it includes `<windows.h>` and calls
  `LoadLibraryW(L"librashader.dll")` with no path and no override macro.

## 2. Findings that change the plan

1. **librashader's own cache writes outside the folder.** `librashader-cache` puts `librashader.db.1` into the
   platform cache dir (`platform_dirs` → on Windows `%LOCALAPPDATA%\librashader`), no env var, no path option. That
   breaks the portable rule ("nothing written outside it"). → **Always `disable_cache = true`.** Cost: every preset
   load compiles its slang → SPIR-V → pipelines again (no disk cache) – see risk R1.
2. **`librashader_ld.h` as shipped conflicts with two of our rules:** `windows.h` outside `platform/`
   (portability), and a bare `LoadLibraryW("librashader.dll")` that follows the DLL search order (the exe folder
   first, but then system folders and `PATH` – a RetroArch or other `librashader.dll` elsewhere could be picked up).
   **Proposal:** use the MIT `librashader.h` for the types and `PFN_` typedefs and fill the same function table
   ourselves with `SDL_LoadObject("<exe dir>/librashader.dll")` + `SDL_LoadFunction` (SDL3 is already a dependency,
   portable, absolute path); same no-op fallback, same ABI check (`libra_instance_abi_version` first). ~120 lines,
   no `windows.h`. Alternative: vendor `librashader_ld.h` unchanged and compile it in `platform/` only – keeps the
   search-order risk. **Maintainer decides** (the spec names `librashader_ld.h`).
3. **The pack has no inflate in the toolkit.** `shaders_slang.zip`: 5748 entries (5214 files, 71.4 MB unpacked,
   54.6 MB packed), methods stored (693) + deflate (5055), no zip64, one root folder `slang-shaders-master/` (strip
   it, spec), longest path 156 chars, 9 levels. Extraction needs a raw-deflate decoder: **new dependency → reason in
   `docs/toolkit.md` first.** Candidates: `tinfl` from miniz (MIT/Unlicense, one file, inflate only), `sinfl.h`
   (public domain / zlib, ~600 lines), or our own inflate (~400 lines + tests). Recommendation: tinfl + our own
   ~200-line central-directory reader (stored + deflate only, zip-slip guard, no zip64) in the toolkit.
   Option without any decoder: accept only an already extracted folder in `media/shaders/` (the spec allows "or an
   extracted folder") and leave zip support for later.
4. **Presets that only `#reference` another:** 2660 `.slangp`, 695 of them with `#reference` (and no `shaders =`
   line). The index must follow the reference chain (relative paths, recursion guard) to show a pass count; pass
   counts: min 1, median 11, p90 22, **max 51**.
5. **Bezel / border presets** (1494 under `bezel/`, 43 under `border/`) draw a frame *around* the picture and expect
   the whole output as their viewport. The spec says "never on the pillarbox bars". With the viewport = the game
   area they still work but the bezel is drawn inside the game picture (the content shrinks). **Maintainer
   decides:** hide `bezel/` and `border/` in the browser (simplest, matches the rule), or let those categories use the
   whole window as an exception.

## 3. Our side – where the pass goes (toolkit `nv2a_vk.c` `vk_flip`, `3bc8af6`)

Today per flip: `ms_flip(s)` (MSAA), `pres = smaa_run(s)` (SMAA copy or the surface itself), acquire, then the
letterbox rectangle `dx, dy, dw, dh` from the surface's logical size and `xbox_present_wide()` /
`xbox_video_widescreen() && stretch_43()` (4:3 frames pillarboxed, 16:9 gameplay full), then either
`present_fxaa(...)` (a draw straight into the swapchain image inside that rectangle) or clear-to-black + `vkCmdBlitImage`
of `pres` into the rectangle, then `hud_draw` (FPS counter) on top, transition to `PRESENT_SRC`, submit, present.

**The shader pass replaces the FXAA draw / the blit, nothing else:**
1. clear the swapchain image to black (as today – the bars stay black, never shaded);
2. barrier: `pres->image` GENERAL → SHADER_READ_ONLY, swapchain image → COLOR_ATTACHMENT;
3. `libra_vk_filter_chain_frame(chain, s_cb, s_frame, in = {pres->image, its format, pres->pw, pres->ph}, out =
   {swapchain image, s_sc_format, extent}, viewport = {dx, dy, dw, dh}, NULL, &opt)`; `opt.aspect_ratio` =
   dw / dh, `frames_per_second` = the frame rate setting, `clear_history = 1` on the frame where the rectangle
   changes (4:3 ↔ 16:9, resize) so feedback/history passes do not smear across the switch;
4. barrier back (`pres` → GENERAL; swapchain COLOR_ATTACHMENT → TRANSFER_DST for the HUD blit → PRESENT_SRC).

So the rules of the spec fall out of the existing code: after the render scale (input = the scaled surface), before
the swapchain; only the game area (the viewport is the letterbox rectangle; 4:3 content in its 4:3 area, 16:9
gameplay the full area); window / fullscreen / resize / swapchain re-creation need nothing extra (the output image is
passed per frame, the chain does not own it); the FPS counter stays unshaded on top. **Off = no chain, no branch
taken beyond one `if` – zero passes, zero cost.**

Device side: the device is Vulkan 1.3 with dynamic rendering as a core feature → `use_dynamic_rendering = true`
(librashader asks for "VK_KHR_dynamic_rendering enabled at device creation"; check in phase 2 that the 1.3 core
feature is enough or enable the extension name too). `frames_in_flight` = our `RECOMP_VK_FRAMES` count. The chain
and the preset live on the render thread; a new preset is built on a worker thread (allowed by librashader) and
swapped in at a flip; the old chain is freed after its frame's fence (the existing garbage list).

**Interaction with ANTIALIASING:** MSAA and SMAA work on the surface before `pres` – they can stay. FXAA is itself a
present pass into the swapchain – with a shader on it would be a second present pass. Proposal: **shader on → FXAA
skipped** (CRT/pixel shaders want the raw pixels; SMAA/MSAA stay selectable). Maintainer decides.

## 4. Menu (THPS `recomp_manual.c` options model)

- Panels have **12 line slots** (`OptList.line[12]`), lines 1–4 can be greyed (the presence words); built lists
  exist (`opt_build`, value lines, sub-lists `OPT_SUB`), and paging exists already in BUTTON MAPPING ("NEXT PAGE",
  "BUTTON MAPPING 2 OF 3", `BIND_PAGE` 8 per page). A value change re-enters the screen through the A latch.
- **SHADERS ▸** as a new RECOMP OPTIONS line → a list: SHADER (ON / OFF), PRESET ▸ (the browser), PARAMETERS ▸,
  SAVE AS PRESET, RESET ALL. The browser: one page = up to 10 entries + ".." / NEXT PAGE; folders first, presets with
  their pass count ("CRT EASYMODE  3 PASSES"), "NONE" on top. Parameters: 8 per page with the value in the line
  ("SCANLINE STRENGTH: 0.45"), left/right = step, the shoulder buttons = 10 steps (the base handlers already route
  left/right; the triggers need a hook like the BUTTON MAPPING one), A on a parameter = reset it; min == max →
  a caption line (not selectable); step 0 → (max-min)/100, cap 10,000 steps; the title shows "N CHANGED".
- **Font limits:** the panel font has capitals, digits, the dot, the colon and the space only (no hyphen,
  underscore, lowercase, slash). Preset and parameter names are upper-cased and other characters become spaces
  (`soundtracks.c` does the same for album names); parameter `description` texts (often long, mixed case) are
  shown the same way, cut to the line width. Names that collide after mapping get a number.
- Up to ~900 parameters → up to ~113 pages of 8: usable but slow; a "MODIFIED ONLY" toggle and jumping by page
  with the triggers keep it practical. The list is built from `libra_preset_get_runtime_params` (never hardcoded).
- Pause menu: like BUTTON MAPPING, front-end Options only (the pause menu has no Options entry).

## 5. Storage (spec rules, checked against the layout)

`media/shaders/` read only → `data/shaders/<pack-id>/` extracted once (pack id = zip name + size + mtime; a marker
file written last, so a cut extraction is redone); the preset index (`data/shaders/index.txt`: path, pass count,
the `#reference` resolved) rebuilt when a pack changes; `saves/shaders/My Presets/` for simple presets
(`#reference` + changed params, RetroArch format), the chosen preset + overrides in `saves\settings.ini`
(`shader_preset`, overrides as `shader_param.<name>`). Disk: ~72 MB in `data/` for the stock pack.

## 6. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Preset build time with the cache off: big presets (20-51 passes) compile many slang files through glslang + pipelines – likely hundreds of ms to seconds, at every start with a saved preset and on every preset change | build on a worker thread, keep showing the unshaded picture until it is ready, log the build time; measure in phase 5; a VkPipelineCache of ours cannot be handed to librashader |
| R2 | GPU cost of heavy presets at high render scales (crt-guest-advanced has ~10+ passes at output size) on the RTX 3050 Ti | the pass count shown as a cost hint (spec); phase 5 measures 1x / 3x / 4.5x; no automatic limit (the player decides, as with VRAM) |
| R3 | The DLL from someone else's folder (bare `LoadLibraryW`) or another ABI | own loader with an absolute path (finding 2); ABI check before any other symbol |
| R4 | librashader internals: its own allocator (gpu-allocator) and descriptor pools beside ours; validation messages inside the DLL | phase 2 runs the validation layer once (as for SMAA/MSAA); errors are librashader's, logged, the pass turned off |
| R5 | A broken preset or a compile failure | `libra_error_t` → `libra_error_write` into the log, chain stays NULL, picture unshaded, the setting kept (the player can pick another) |
| R6 | 4:3 ↔ 16:9 every few seconds (menus vs gameplay) re-sizes the chain's intermediate targets | measured in phase 5 (one switch should be a few ms at most); `clear_history` on the switch |
| R7 | Zip support needs a new dependency | finding 3: tinfl + own reader, or folder-only first |
| R8 | Font: names lose `-`, `_`, `/` and case | upper-casing as for album names; collisions numbered |

## 7. Effort (agent sessions, rough)

| Phase | Content | Effort |
|---|---|---|
| 2 | toolkit: own loader (or ld.h), `nv2a_vk_set_shader_preset(path)` / `_param`, the pass in `vk_flip`, worker-thread build, FXAA rule, `RECOMP_SHADER_PRESET` / THPS `THPS_SHADER_PRESET`; tests: missing DLL, broken preset, 4:3 ↔ 16:9, resize, validation layer | 1–2 |
| 3 | pack extraction (inflate decision), index with `#reference` resolution, re-extract after `data/` deleted | 1 |
| 4 | SHADERS menu: browser with pages, parameters with pages, save simple preset, load on start; full regression | 2 |
| 5 | measurement (exclusive): off = baseline, 4 presets at 1x / 3x / 4.5x; the test list of the spec | 1 |

## 8. Decisions needed before phase 2

1. Loader: own SDL-based loader with an absolute path (recommended) or `librashader_ld.h` unchanged in `platform/`.
2. Zip: tinfl (MIT) + own reader (recommended), another decoder, or extracted folders only for now.
3. Bezel / border presets: hidden (recommended, keeps "never on the bars") or whole-window exception.
4. FXAA with a shader on: skipped (recommended) or kept as a pre-pass.
5. ARMSX2 (GPL-3.0) as the logic reference: not in `refs/` yet – clone it read-only into `refs/` when phase 4 starts?
