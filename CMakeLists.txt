cmake_minimum_required(VERSION 3.20)

# THPS 2X - static recompilation.
# After sp00nznet/xboxrecomp templates/new-game/CMakeLists.txt and
# refs/defjam_recomp/CMakeLists.txt. Build only via tools/build.ps1.

project(thps2x_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

# Toolkit: option-c (THPS 2X 2.0, maintainer 2026-11-18); tools/build.ps1 passes the pinned worktree
# (-Variant <name> -> tools/xboxrecomp_<name>). Up to 1.x: branch vulkan-backend (docs/toolkit.md).
set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xboxrecomp" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# Generated C lives outside the repo (never committed): tools/lift.ps1 output.
set(RECOMP_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../private/thps2x/gen" CACHE PATH
    "Directory holding the generated C (recomp_*.c, recomp_types.h, ...)")

# Record every indirect-branch target at runtime (icall_targets.dump) for the
# seeds feedback loop. option-c: a toolkit option for development builds only,
# never a release (hot-path store; a bare RECOMP_ICALL_FEEDBACK definition draws
# a warning). 1.x shipped with it on - note for the 1.x vs 2.0 comparison.
option(THPS_ICALL_FEEDBACK "Record indirect-call targets (development builds only)" OFF)
set(XBOXRECOMP_ICALL_FEEDBACK ${THPS_ICALL_FEEDBACK} CACHE BOOL "" FORCE)

# Vulkan renderer (toolkit nv2a_vk): glslang + SDL3 from tools/deps.ps1
# (CMAKE_PREFIX_PATH). option-c has no D3D11 / CPU-executor path any more:
# THPS_VULKAN stays ON (kept as a variable for the parts below), no XBOXRECOMP_D3D11.
set(THPS_VULKAN ON)
set(XBOXRECOMP_VULKAN ON CACHE BOOL "" FORCE)

# LLE fallbacks (M3.5 step 2, A10): the MCPX APU and the OHCI/USB model are
# not in the default build -- DirectSound and XInput run at API level
# (toolkit src/xdk). THPS_LLE=ON builds them for comparison runs
# (RECOMP_DSOUND_HLE=0 + RECOMP_AC97_READY=1, RECOMP_XINPUT_HLE=0).
option(THPS_LLE "Build the LLE fallbacks (MCPX APU, USB/OHCI) for tests" OFF)
set(XBOXRECOMP_LLE_APU ${THPS_LLE} CACHE BOOL "" FORCE)
set(XBOXRECOMP_LLE_USB ${THPS_LLE} CACHE BOOL "" FORCE)

# CodeView for the toolkit libraries too (only the .pdb grows; same code): the
# crash handler and the sampling profiler (RECOMP_SAMPLE) name their static
# functions instead of the nearest exported one.
if(MINGW)
    add_compile_options(-g -gcodeview)
endif()
add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

# CONFIGURE_DEPENDS: a re-lift that adds/removes chunks is picked up.
file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${RECOMP_GEN_DIR}/*.c")
if(NOT RECOMP_GEN_SOURCES)
    message(FATAL_ERROR "No generated C in ${RECOMP_GEN_DIR} - run tools/lift.ps1 first")
endif()

add_executable(${PROJECT_NAME}
    src/main.c
    src/recomp_manual.c
    src/input.c
    src/settings.c
    src/soundtracks.c
    src/vk_check.c
    ${RECOMP_GEN_SOURCES}
)
# The host layer (src/platform/host.h): Windows, or Linux (2.0, AppImage; model Panzer Dragoon Orta Recompiled).
if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE src/platform/host_win32.c)
else()
    target_sources(${PROJECT_NAME} PRIVATE src/platform/host_linux.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS} pthread)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME THPS2X)
# The port's icon (assets/README.md): the exe's icon in Explorer as a resource; the window / taskbar icon as the
# BMP's bytes in a generated header (main.c -> xbox_set_window_icon).
if(WIN32)
    enable_language(RC)
    target_sources(${PROJECT_NAME} PRIVATE assets/thps2x.rc)
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_64.bmp" THPS_ICON_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," THPS_ICON_HEX "${THPS_ICON_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/port_icon.h"
     "/* generated from assets/icon_64.bmp (CMakeLists.txt) */\nstatic const unsigned char g_port_icon_bmp[] = {${THPS_ICON_HEX}};\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_64.bmp")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# M3.5 step 4a: call counts of the D3D8 functions the game calls (strong
# overrides of the lifter's weak sub_X; RECOMP_D3D_CENSUS=<seconds>). Off by
# default: diagnostics stay out of the normal build.
option(THPS_D3D_CENSUS "Count D3D8 calls (src/d3d_census.c)" OFF)
if(THPS_D3D_CENSUS)
    target_sources(${PROJECT_NAME} PRIVATE src/d3d_census.c)
endif()
# Only the game goes into the portable install layout, not the toolkit's tests.
if(THPS_OUTPUT_DIR)
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${THPS_OUTPUT_DIR}")
    # (no librashader copy any more: RetroArch shaders removed from THPS 2.0,
    # maintainer 2026-11-19; still in option-c)
endif()

# The toolkit libraries the game uses, named one by one instead of the
# umbrella target `xboxrecomp`: that one also pulls in xbox_d3d8 (D3D8 ->
# D3D11 for titles without their own D3D) and xbox_nv2a (an older D3D11
# PGRAPH path). THPS 2X's D3D is lifted code driving the NV2A pushbuffer, so
# with nv2a_vk neither is needed -- Vulkan only (CLAUDE.md).
target_link_libraries(${PROJECT_NAME} PRIVATE
    xbox_kernel xbox_input xbox_xdk xbox_video)
if(THPS_LLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE xbox_usb xbox_apu)
    target_compile_definitions(${PROJECT_NAME} PRIVATE THPS_LLE=1)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME THPS2X_lle)
endif()
if(THPS_VULKAN)
    # (xbox_shaderpack no longer linked into the game: RetroArch shaders
    # removed from THPS 2.0, maintainer 2026-11-19; the launcher still gets it
    # through xbox_launcher)
    target_link_libraries(${PROJECT_NAME} PRIVATE xbox_nv2a_vk xbox_bootvideo)
    target_compile_definitions(${PROJECT_NAME} PRIVATE THPS_VULKAN=1)
    # The first-start installer (toolkit src/install, M5 item 5) once the toolkit has it.
    if(TARGET xbox_install_ui)
        target_link_libraries(${PROJECT_NAME} PRIVATE xbox_install_ui)
        target_compile_definitions(${PROJECT_NAME} PRIVATE THPS_INSTALLER=1)
    endif()
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${RECOMP_GEN_DIR}
    ${XBOXRECOMP_DIR}/include
)

# System libraries the toolkit's Windows runtime still needs. No D3D: the
# renderer is Vulkan (M2 stage 5 removed d3d11/dxgi/dxguid).
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        $<IF:$<BOOL:${MINGW}>,xinput1_4,xinput>
        winmm
        dbghelp   # crash handler: host RIP -> generated sub_XXXXXXXX
        shell32   # CommandLineToArgvW: the command line as UTF-16
    )
else()
    # Linux: dladdr names the generated function in a crash report when the symbols are exported
    target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)
endif()

if(MINGW)
    # CodeView + .pdb (llvm-mingw/lld) so dbghelp in the crash handler can
    # name the generated function; DWARF is invisible to dbghelp.
    target_compile_options(${PROJECT_NAME} PRIVATE -g -gcodeview)
    # -static: libc++ (glslang) and winpthread linked in, so the portable
    # folder needs no extra DLLs (docs/toolkit.md, toolchain decision).
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--pdb= -Wl,--large-address-aware -static)
endif()
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE /LARGEADDRESSAWARE
        $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj $<$<CONFIG:Release>:/Zi>)
endif()

# The release launcher thps2x_launcher.exe (until v1.2.8 "THPS 2X Recomp.exe"; docs/release_plan.md): the toolkit's
# launcher with THPS 2X's names and icon - no game code. Built into the build folder (not the layout);
# tools/release.ps1 appends the payload to it.
if(TARGET xbox_launcher)
    # From 2.0 (maintainer 2026-11-23: every port's launcher in the designs of option-p's launcher; model Panzer
    # Dragoon Orta Recompiled 4a99345): Windows and Linux link the toolkit's port launcher xbox_launcher_port
    # (launcher_port.h: Windows 95's look on Windows, Windows 2000's on Linux; it brings its own xr_setup_ui_* and the
    # launcher's Vulkan check). THPS 2X's own setup_win32.c / setup_ui_sdl.c are gone; src/vk_check.c stays the
    # game's own Vulkan check (THPS2X.exe).
    add_executable(thps2x_launcher launcher/main.c)
    target_include_directories(thps2x_launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(thps2x_launcher PRIVATE xbox_launcher_port)
    # Release layout (maintainer 2026-11-21): only thps2x_launcher on top, the game data\thps2x.exe (launcher/main.c
    # game_exe; the dev build's own THPS2X.exe in the install layout keeps its name). Linux: the AppImage's internal
    # usr/bin/thps2x-recomp stays (the user-data folder follows the slug).
    if(WIN32)
        target_sources(thps2x_launcher PRIVATE assets/thps2x.rc)   # the exe's icon = resource 1, the window loads it
        set_target_properties(thps2x_launcher PROPERTIES WIN32_EXECUTABLE ON)
        set_target_properties(thps2x_launcher PROPERTIES OUTPUT_NAME "thps2x_launcher")
    else()
        set_target_properties(thps2x_launcher PROPERTIES OUTPUT_NAME "thps2x-recomp")
    endif()
    set_target_properties(thps2x_launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/launcher")
    target_include_directories(thps2x_launcher PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(thps2x_launcher PRIVATE xbox_launcher)
    if(MINGW)
        target_link_options(thps2x_launcher PRIVATE -static)
    endif()
endif()
