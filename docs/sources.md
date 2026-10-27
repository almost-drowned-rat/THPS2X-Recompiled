# Sources and findings

Borrowed code (repo, commit, file → where it went) and findings from references.

## Borrowed code

| From | Commit | File | Used in | Licence |
|---|---|---|---|---|
| sp00nznet/xboxrecomp | 193e299 | `templates/new-game/src/main.c` | `src/main.c` (boot sequence), `src/platform/host_win32.c` (VEH crash handler, guest stack scan) | MIT |
| sp00nznet/xboxrecomp | 193e299 | `templates/new-game/src/recomp_manual.c` | `src/recomp_manual.c` (ICALL logging, `recomp_unimpl`) | MIT |
| sp00nznet/xboxrecomp | 193e299 | `templates/new-game/CMakeLists.txt` | `CMakeLists.txt` | MIT |
| defjam_recomp | acfb314 | `installeur/installer.ps1` (pipeline order, seeds) | `tools/lift.ps1` | see repo |
| takedawn-xboxrecomp (sp00nz) | 16a4a44 | `src/install/xdvdfs.c`, `xdvdfs.h` (XDVDFS image reader: descriptor at the known partition offsets, directory walk, path protection) | toolkit `src/install/` (unchanged, attribution header) → **Installer**: the first-start install flow `game_setup.c` is ours (maintainer, 2026-10-24) | MIT |
| doa3-recomp | – | `doa3/src/game/xiso_extract.c` (extract into a temp folder, move into place only after the check) | reference only, no code taken (no licence file) | unclear |
| xboxrecomp `tools.xiso` | – | XDVDFS format | reference only (dev tooling keeps using it) | MIT |
| antoxa2584x/burnout3-sw | 0bf84b5 | `xboxrecomp/src/kernel/nv2a_backend.h`, `nv2a_pb_exec.c`, `nv2a_pb_scan.c` | toolkit fork `src/kernel/` (executor replaced; `dma_resolve` adapted to upstream's contiguous arena) | MIT |
| antoxa2584x/burnout3-sw | 0bf84b5 | `xboxrecomp/src/kernel/xbox_memory_layout.c` (NV2A poll thread only: flag thread, interrupt/vblank hold, resync, tick-count thread) | toolkit fork `src/kernel/xbox_memory_layout.c` | MIT |
| antoxa2584x/burnout3-sw | 0bf84b5 | `xboxrecomp/src/kernel/xbox_devbus.c` (IRQ lines), `kernel_bridge.c` (`kernel_service_irqs`, `xbox_log_quiet`, `xbox_vblank_report`), `kernel_xbox.c` (`xbox_video_widescreen`, default changed to 4:3) | toolkit fork `src/kernel/` | MIT |
| antoxa2584x/burnout3-sw | 0bf84b5 | `xboxrecomp/src/nv2a_vk/nv2a_vk.c`, `nv2a_gl/gl_vsh.c`, `gl_psh.c` | toolkit fork `src/nv2a_vk/`, `src/nv2a_gl/` (SDL2 → SDL3, pthread → SDL threads off the Switch) | MIT |
| antoxa2584x/burnout3-sw | 0bf84b5 | `src/recomp_manual.c` (`sub_00351770` BlockOnTime: register the D3D semaphore with the executor) | the Burnout 3 test bench (a separate, unpublished project; Burnout 3 only) | MIT |
| antoxa2584x/burnout3-sw | 0bf84b5 | `src/recomp_manual.c` (BlockOnTime semaphore wrapper, as pattern) | `src/recomp_manual.c` `sub_00019FE0` (THPS 2X D3D 3947 fence wait, semaphore at `*(dev+0x3F0)`) | MIT |
| sp00nznet/burnout3 | 79b578d | whole game project (`CMakeLists.txt`, `src/game/main.c`, `recomp_manual.c`, `config/seed_functions.json`) | now a separate, unpublished project; nothing of it is in this repo | MIT |
| TGP482/XboxXBE-Recomp | 65e959d | `runtime/xdk/xapi.cpp` (design: device-type record kept current from the host, handle per open port, packet number from the state, SetState completing at once) | toolkit fork `src/xdk/xdk_xinput.c` (rewritten in C, no poller thread); bound in `src/recomp_manual.c` | MIT |
| TGP482/XboxXBE-Recomp | 65e959d | `runtime/xdk/dsound.cpp` (design: buffers playing from title memory, streams as packet queues completed on the title's side, one host mixer) | toolkit fork `src/xdk/xdk_dsound.c` (rewritten in C, SDL3); bound in `src/recomp_manual.c` | MIT |
| iryoku/smaa (Jimenez, Echevarria, Masia, Navarro, Gutierrez) | 71c806a | `SMAA.hlsl`, `Textures/AreaTex.h`, `Textures/SearchTex.h`, `LICENSE.txt` (vendored unchanged) | toolkit `vulkan-backend` `src/nv2a_vk/third_party/smaa/` (GLSL 4 mode, preset HIGH, three passes before the present; NOTICE; merged `48ddeda`) → RECOMP OPTIONS → ANTIALIASING ▸ SMAA | MIT |
| SnowflakePowered/librashader (chyyran) | tag `librashader-v0.12.0`, 87e8a97 | `include/librashader.h` (vendored unchanged; the C ABI 2 / API 5 types and `PFN_` typedefs) | toolkit branch `shader-module` `src/nv2a_vk/third_party/librashader/` → `src/nv2a_vk/shader_pass.c` (own SDL loader, not `librashader_ld.h`) | MIT (header) |
| phoboslab/pl_mpeg (Dominic Szablewski) | c871f2b (2025-12-30) | `pl_mpeg.h` (vendored unchanged: MPEG-1 video, MP2 audio, MPEG-PS demuxer) | toolkit branch `boot-video` `src/bootvideo/third_party/pl_mpeg/` → `src/bootvideo/boot_video.c` (the boot animation, `media/boot.mpg`) | MIT |
| SnowflakePowered/librashader (the library) | tag `librashader-v0.12.1`, 07ac790 | the whole workspace, unchanged; `librashader-capi` built with the Vulkan runtime only | `tools\deps.ps1` → `private\deps\install\bin\librashader.dll` → next to THPS2X.exe (CMake), never committed | MPL-2.0 (or GPLv3); a separate DLL |
| xemu-project/xemu | 36334d9 | `hw/xbox/nv2a/pgraph/glsl/vsh-ff.c` (fixed-function light model; REFLECTION_MAP texgen + texture matrix), `glsl/vsh.c` (specular enable / D1), `pgraph/pgraph.c` `reconstruct_specular_power` (curve tables) | toolkit `vulkan-backend` `src/nv2a_gl/gl_vsh_ff.c` (own file, NOTICE; merged `c840f90`, texgen `ccba5f0`) | LGPL-2.1-or-later |

## Findings

### GPU interrupt / vblank of early D3D (XDK 39xx) – 2026-10-13

- **doa3-recomp** (XDK 3925): no GPU interrupt is delivered. The D3D vblank handler is emulated by hand in
  `recomp_manual.c` around the per-frame `D3D_SetFence(2)` from Present: bump the vblank counter
  (`device+0x820`), signal the vblank KEVENT (`device+0x1E8`), and call the callback registered with
  `D3DDevice_SetVerticalBlankCallback` (`device+0x1E4`) with the counter as cdecl argument. Also pulses the vblank
  event at wait loops (`kernel_bridge.c`, deterministic fiber key). Lesson: wrappers must preserve esi/edi/ebx
  (Present keeps the device pointer in esi).
- **xboxrecomp upstream** (193e299): `KeConnectInterrupt` records the ISR per vector and reports success;
  `kernel_vblank_tick()` (`src/kernel/kernel_bridge.c`, enabled with `RECOMP_VBLANK=1`) sets `PCRTC_INTR_0` /
  `PMC_INTR_0` and calls the connected ISR – a generic version of the DOA3 approach. Try this first for THPS 2X;
  the DOA3 device offsets apply to 3925 and must be verified for 3947.

### burnout3-sw toolkit vs. upstream (M2 stage 2) – 2026-10-13

- burnout3-sw's vendored toolkit branched from upstream around `fc84381` (2026-09-27, v0.12.0 merge batch); found by
  minimising the diff of 9 kernel files over upstream history. Upstream then reworked its CPU executor (combiners,
  vsh interpreter, threaded rasteriser, 09-28..09-30); burnout3-sw built a GPU back-end path instead. The two
  executors cannot be merged – the fork now runs burnout3-sw's.
- Not ported yet (burnout3-sw kernel, unrelated to the renderer): runtime data block above the image
  (`xbox_RtDataPlace`, fixes kernel data exports inside Burnout 3's .bss – its saves), contiguous arena above the image +
  `XBOX_PHYS_LOWVA_TAG`, extended-VMA reservation, heap lock, MMIO routing (`--mmio-sections`), real-time vblank
  timing in the timer thread, threads/waits/DPC rework in `kernel_bridge.c`. Take them when Burnout 3 or THPS 2X needs one.

### Burnout 3 on the fork with nv2a_vk (M2 stage 3) – 2026-10-13

- Game project: `refs/burnout3` (made for upstream main = the fork's base), not burnout3-sw's (that one needs its own
  kernel and lifter `--mmio-sections`). Extra per title: the `sub_00351770` semaphore wrapper; upstream's
  `xbox_Nv2aMirrorFence` must be **off** with this executor (reports submissions consumed before they ran). Without the
  wrapper the title hangs in BlockOnTime after 5 frames.
- clang folds MMIO loads into ALU instructions (`and edx,[mmio]`, `test [mmio],imm32`) where MSVC loads first – the VEH
  decoder had to learn these forms (fork `45851ea`). THPS 2X will hit the same with any VEH-trapped device.
- Seen, not fixed: vblank runs at ~40 Hz instead of 60 (fork timer thread: 10 ms wait + 16 ms spacing); `--seconds` in
  refs/burnout3's `main.c` does not end the run (killed by the harness); present mode 0 (IMMEDIATE) = no frame cap,
  160–200 fps in loading screens. burnout3-sw's vblank timing and its D3D vblank/PGRAPH handler wrappers (flip
  retirement, trap lock at DISPATCH) are not ported yet.

### THPS 2X on nv2a_vk (M2 stage 4) – 2026-10-14

- Same fence pattern as Burnout 3: D3D 3947's `sub_00019FE0` waits on the GPU time at `*(g_pDevice+0x3F0)` (`g_pDevice`
  at 0x00028030). Registering that address with `nv2a_pb_set_semaphore_target` was enough – the executor's semaphore
  releases drive the wait; upstream's fence mirror is off with the executor. The swap-counter mirror stays for now.
- No MMIO decoder problems seen in the first 90 s (USB/APU not enabled in THPS 2X yet).
- The Bink movies are textured quads (2 draws per frame, one 640x480 texture upload per movie frame); the colour
  surface is 1280x480 (horizontally doubled – likely the title's horizontal anti-aliasing).
- Movie frames show noise and wrong blocks along edges. Not yet known whether the lifted Bink decoder (MMX/SSE paths)
  or the texture upload timing is to blame – next thing to check (compare with Cxbx-Reloaded).

### Bink movie artifacts: `movsx ebp, bp` lifted as a zero extension (M2 stage 5) – 2026-10-14

- Symptom: noise and wrong blocks along every edge in all four Bink movies; frames bit-identical from run to run
  (so a decode error, not an upload race).
- `RECOMP_CPUID_EDX` (new, fork `1d2c68a`): hiding MMX+SSE (`0x0003FBFF`) leaves Bink without blitters (it has no
  plain-x86 ones – NULL pointers as in M1, black frames); hiding only SSE (`0x0183FBFF`) gives bit-identical frames →
  SSE is not involved. Bink's only MMX code are the ten YUV→RGB blitters 0x001D4E70–0x001D6FE0.
- Conformance `--xbe-func` (fork `76c32c1`): the ten blitters against the original code with real buffers – 0
  mismatches. The saturating MMX ops (`paddsw`, `psubusw`, `pmulhw`, …) are lifted correctly.
- **State replay** (fork `76c32c1`): debug wrappers in `recomp_manual.c` snapshot guest state before/after the n-th call
  (`xbox_StateSnapshot`), `py -3 -m tools.conformance.replay VA before.bin after.bin` runs the original machine code in
  Unicorn and lists the bytes the lifted code got wrong. Bisecting the call tree movie loop `sub_001B05E0` →
  `sub_001BFA00` (per frame) → `sub_001CCB30` (video) → `sub_001CAD70` (plane decoder) → leaves: only
  **`sub_001D87B0`** (8×8 integer IDCT) was wrong. Its lifted C had `ebp = LO16(ebp)` for `movsx ebp, bp`:
  `_lift_movsx` lacked bp/sp in its 16-bit register list. Fix in the lifter (fork `7d8d0e2`) + tests; after a re-lift
  the decoder replays with 0 wrong bytes. (Bink call graph: `sub_001C1220` = BinkOpen, `sub_001BF3F0` = copy-to-surface
  dispatch, `sub_001C37A0` = the blit core.)
- Replay artifact to ignore: a few bytes below the final esp (dead stack) differ because the lifted code keeps ebp
  partly in C locals, so the snapshot's ebp is stale.
- D3D11 out of the THPS 2X build: toolkit option `XBOXRECOMP_D3D11=OFF` (fork `885f5bf`), explicit toolkit libraries
  in `CMakeLists.txt`, no `xbox/xboxrecomp.h` umbrella include. The exe imports no d3d11/dxgi/D3DCompiler/MF DLL.

### Gamepads over the emulated USB host (M3 stage 1) – 2026-10-14

- Approach from burnout3-sw (`docs/boot.md`, "Past the title screen" #11–#16; antoxa2584x/burnout3-sw, MIT): the
  title's own XPP driver enumerates the pad on the toolkit's OHCI model (`src/usb/ohci.c`, `usb_gamepad.c`),
  `RECOMP_USB=1`, register faults routed by the host VEH (`xbox_OhciOwnsAddress`/`xbox_OhciHandleMmio`). DOA3
  (`refs/doa3-recomp`, XDK 3925) overrides `XGetDevices`/`XInputGetState` instead – not needed for 3947.
- First run: enumeration stopped after SET_CONFIGURATION (4 setups, no interrupt ED). Same as Burnout 3 #13: the XID
  class driver's AddDevice **0x00219068** (and RemoveDevice 0x00218A16) are only reachable through the XPP
  class-driver table at 0x002070D8/0x002070F0 (entries `{class, init 0x00216CE3, add, remove, …}` for classes 0x03 and
  0x58) → `seeds/misc_seeds.json`. Next stop 0x00218783 (control-pipe handler in the endpoint table at 0x00207178,
  sibling 0x002187E8), plus the class-0x08 (memory unit) entry 0x00217A84. After that: interrupt ED open,
  ~100 reports/s, Start skips the movies.
- Same pattern in game code: menus hung on `[ICALL] Failed to resolve VA 0x001121C0` (C++ vtable in `.rdata`).
  `tools/scan_table_seeds.py` scans all data sections for pointers to 16-byte-aligned function starts behind padding
  that are not lifted → `seeds/table_seeds.json` (66 seeds, 7470 → 7540 functions; one, 0x00144FB0, is dropped by
  the lifter). Bink's sound callback 0x001DCB27 (stored with `mov [0x002B0074], imm`) seeded by hand.
- Keyboard: toolkit `src/input` now keys on HID usage IDs (= SDL scancodes) with a settable keymap
  (`xbox_InputSetKeymap`); `nv2a_vk` forwards its SDL key events (`nv2a_vk_set_key_handler`) and window close
  (`nv2a_vk_set_quit_handler`). Layout and script syntax: `docs/input.md`.
- Still open: one wild URB completion pointer during enumeration (`[ICALL] target 0x00000002 / 0x810A9000 … returning
  to 0x002192A3`, XPP's generic completion `sub_00219292` calls `urb+8`) – skipped once, the pad works.

### Sub-menus usable, menu graphics (M3 stage 2) – 2026-10-14

- **Saves:** `\Device\Harddisk0\Partition1\UDATA` / `TDATA` landed in `game/` (Partition1 rule). Fork: own rules
  before it – UDATA → `saves\UDATA\…`, TDATA → `data\TDATA\…`. Not the Career-menu cause: the title only probes
  `UDATA\41560001\` (and writes `this.sux`) at boot; nothing touches storage when the menu opens.
- **Pad port:** XPP 3911 maps HC0 root ports 0/1/2/3 to controller ports 2/3/0/1 (measured: `XGetDeviceChanges`
  insertion mask with `RECOMP_USB_PORT`); the toolkit default put pad 1 on port 2. `main.c`: `RECOMP_USB_NDP=4`,
  `RECOMP_USB_PORT=2` → port 0; pad 2 lands on port 1.
- **Career menu deaf (the real cause):** the OHCI model resolved every physical address below the contiguous
  allocator's high-water mark into the contiguous window. XPP's report buffer is pool memory (VA = physical
  0x01C0F52A); once the menus had allocated > 28 MB of contiguous memory, reports went into the window, the driver
  kept its stale buffer (A still held) and the menu saw no new input. Found by: `RECOMP_PAD_TRACE` (game's
  `XInputGetState` result frozen), peeks of the XInput handle (packet number moving, buffer stale), byte trace of the
  report write, and `THPS_WATCH_WRITE` (hardware watchpoint: no other writer). Fork: `MmGetPhysicalAddress` records
  per 4 KB page whether it handed the number out for RAM or for the window (`xbox_PhysOwner`), `bus_resolve` uses
  that first. Other bus masters (APU, executor `dma_resolve`) still use their own rules – same ambiguity possible.
- **Select Player → A hung:** D3D 3947's fence wait `sub_00019FE0` sleeps for far targets on a KEVENT at dev+0x2500
  that D3D builds itself and its PGRAPH handler sets on a NOP(0x310) trap. The bridge cannot map a self-built event
  (waits fail with STATUS_UNSUCCESSFUL, the `while (KeWait…)` spins forever). `RECOMP_TITLE_KEVENTS=1` maps it but
  then D3D init blocks on its vblank event (burnout3-sw's D3D vblank/PGRAPH handler wrappers are not ported).
  `recomp_manual.c`: the fence wrapper always takes D3D's own spin path (insert fence if target == current time,
  else kick off, spin on the GPU semaphore). Long waits only while `RECOMP_GL_DUMP` reads frames back.
- **Select Player → B did nothing:** `[ICALL] Failed to resolve VA 0x00144FB0` – the vtable seed the disassembler
  dropped as mid-instruction (an inline jump table before it decodes as code). Seeded with `"observed": true`
  (the disassembler's rule for addresses a run reached).
- **Grey veil, invisible menu text:** the front end draws with D3D fixed function and fog on (LINEAR, gen PLANAR,
  plane (0,0,1,0), colour 0xFF888080). The Vulkan path gave fixed-function vertices fog distance 0 and the fog unit
  a "raw" factor of 0 → every pixel mixed fully to the fog colour through D3D's final combiner
  (`fog.a·(spare0+spec) + (1−fog.a)·fog.rgb`). Fork: fog off → factor 1 for every transform mode (xemu
  `vsh.c`: `oFog = 1.0`, behaviour only); fog on in fixed function → distance per `SET_FOG_GEN_MODE` in the vertex
  shader (spec alpha, fog plane against the model-view position – folded into one object-space vector on the CPU,
  radial approximated as planar – or the fog coordinate), then the fog mode. Found with `RECOMP_FRAME_TRACE`
  (new trace lines: stage-0 texture format, colour combiner, fog registers).
- New tools: pad script `shot=<name>` (toolkit `g_xbox_screenshot_hook`, nv2a_vk saves the next frame;
  `RECOMP_SHOT_PREFIX`), `THPS_WATCH_WRITE` (host layer, DR0–DR3), `THPS_LOG_RAW`, `RECOMP_HEAP_WATCH`,
  `RECOMP_USB_REPORT_TRACE` (fork). `tests/input/menu_tour.txt`.
