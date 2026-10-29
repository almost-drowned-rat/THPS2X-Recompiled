# build.ps1 - the only way to build THPS 2X (CLAUDE.md, Resources).
#
#   powershell -ExecutionPolicy Bypass -File tools\build.ps1 [-Clean] [-Jobs 2] [-Lle]
#
# -Lle: the test build with the LLE fallbacks (MCPX APU, USB/OHCI; THPS_LLE=ON) as
#   THPS2X_lle.exe in the same layout, from its own build directory (<name>-lle).
#
# - one build at a time, across all projects: the global lock (tools\lock.ps1, tools\private\build.lock;
#   waits while another build, relift or game test run holds it; a line in coordination.md meanwhile)
# - max. 2 compiler processes, incremental; -Clean only after lifter changes
# - one build directory per worktree, outside the repo:
#     private\build\<worktree folder name>
# - output goes into the portable install layout, never into the repo:
#     private\install\THPS 2X Recomp\THPS2X.exe
# -Variant <name>: a toolkit branch under test in its own worktree (CLAUDE.md, Agents): toolkit
#   tools\xboxrecomp_<name>, generated C private\thps2x_<name>, build dir private\build\thps2x_recomp-<name>,
#   layout private\install_<name>\THPS 2X Recomp (its game\ folder may be a junction to the normal one).
#   The normal toolkit, generated C and install layout are left alone.
# Paths: tools\paths.ps1 (space-free; llvm-mingw clang + Ninja use them directly).
# glslang/SDL3 for the Vulkan renderer: built with the same compiler by tools\deps.ps1 (docs/toolkit.md).

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Interactive script; console status output only.')]
param(
    [switch]$Clean,
    [int]$Jobs = 2,
    [string]$Layout,
    [switch]$Lle,
    [switch]$Census,         # THPS_D3D_CENSUS: count D3D8 calls (src/d3d_census.c)
    [string]$Variant
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")           # $Repo, $Private, $ToolsRoot, $Shared
$Name   = Split-Path -Leaf $Repo
$Src    = $Repo
$Sfx    = if ($Variant) { "_$Variant" } else { "" }
$Build  = Join-Path $Private "build\$Name"
if ($Variant) { $Build = "$Build-$Variant" }
if ($Lle) { $Build = "$Build-lle" }
if (-not $Layout) { $Layout = Join-Path $Private "install$Sfx\THPS 2X Recomp" }
$Toolkit = Join-Path $ToolsRoot "xboxrecomp$Sfx"
$GenDir = Join-Path $Private "thps2x$Sfx\gen"
if (-not (Test-Path $Toolkit)) { throw "toolkit $Toolkit missing" }
# A branch that does not touch the lifter (renderer, kernel) needs no relift: without its own generated C the
# variant builds from the normal one.
if ($Variant -and -not (Test-Path $GenDir)) {
    $GenDir = Join-Path $Private "thps2x\gen"
    Write-Host "variant $Variant has no generated C of its own - using $GenDir"
}
if ($Jobs -gt 2) { $Jobs = 2 }

$clang = (Get-Command clang -ErrorAction SilentlyContinue).Source
if (-not $clang) { throw "clang (llvm-mingw) not on PATH - run tools\setup.ps1" }
$bin = Split-Path -Parent $clang
$Deps = Join-Path $Shared "deps\install"           # glslang + SDL3 (tools\deps.ps1)
if (-not (Test-Path (Join-Path $Deps "lib\libglslang.a"))) { throw "glslang/SDL3 missing - run tools\deps.ps1" }

New-Item -ItemType Directory -Force -Path $Build, $Layout | Out-Null
. (Join-Path $PSScriptRoot "lock.ps1")
$lock = Enter-GlobalLock -Class shared -Heavy:([bool]$Clean) -Who "A (THPS 2X)" -What "build $Name$Sfx$(if ($Lle) { ' -Lle' })$(if ($Clean) { ' -Clean' })"
try {
    if ($Clean) {
        Get-ChildItem $Build -Exclude "build.lock" | Remove-Item -Recurse -Force
    }
    function Cm($p) { return ($p -replace '\\', '/') }
    # Configure every time: cheap with a cache, and picks up changed settings.
    & cmake -S $Src -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_C_COMPILER=$(Cm $clang)" `
        "-DCMAKE_CXX_COMPILER=$(Cm (Join-Path $bin 'clang++.exe'))" `
        "-DCMAKE_PREFIX_PATH=$(Cm $Deps)" `
        "-DCMAKE_RC_COMPILER=$(Cm (Join-Path $bin 'llvm-rc.exe'))" `
        "-DTHPS_OUTPUT_DIR=$(Cm $Layout)" `
        "-DXBOXRECOMP_DIR=$(Cm $Toolkit)" `
        "-DRECOMP_GEN_DIR=$(Cm $GenDir)" `
        "-DTHPS_LLE=$(if ($Lle) { 'ON' } else { 'OFF' })" `
        "-DTHPS_D3D_CENSUS=$(if ($Census) { 'ON' } else { 'OFF' })" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    $t = [Diagnostics.Stopwatch]::StartNew()
    & cmake --build $Build --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
    Write-Host ("Built in {0:N0} s: {1}" -f $t.Elapsed.TotalSeconds, (Join-Path $Layout $(if ($Lle) { "THPS2X_lle.exe" } else { "THPS2X.exe" })))
} finally {
    Exit-GlobalLock $lock
}
