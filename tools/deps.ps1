# deps.ps1 - build the renderer's third-party libraries from source (M2).
#
#   powershell -ExecutionPolicy Bypass -File tools\deps.ps1 [-Jobs 2] [-Force]
#
# glslang (GLSL -> SPIR-V at run time, nv2a_vk) and SDL3 (window, Vulkan
# surface, later input/audio) are built with the same llvm-mingw clang as the
# game, as static libraries, and installed to private\deps\install. The Vulkan
# SDK's prebuilt glslang is MSVC C++ (imports MSVCP140) and cannot be linked
# with llvm-mingw; see docs/toolkit.md "Toolchain decision".
#
# Sources: shallow clones at fixed tags into private\deps\src (never
# committed). Same lock and job limit as build.ps1. Rerun after changing a tag;
# -Force rebuilds everything.
#
# No librashader / Rust step any more (RetroArch shaders removed from THPS
# 2.0, maintainer 2026-11-19; still in option-c), and so no -InstallRust.

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Interactive script; console status output only.')]
param(
    [int]$Jobs = 2,
    [switch]$Force
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")            # $Shared = <root>\tools\private (deps are shared)
$Root    = Join-Path $Shared "deps"
$SrcRoot = Join-Path $Root "src"
$BldRoot = Join-Path $Root "build"
$Prefix  = Join-Path $Root "install"
if ($Jobs -gt 2) { $Jobs = 2 }

# Pinned versions. glslang matches the installed Vulkan SDK (1.4.363 ships 16.6.0).
$Deps = @(
    @{ Name = "glslang"; Url = "https://github.com/KhronosGroup/glslang.git"; Tag = "16.6.0"
       Args = @("-DENABLE_OPT=OFF", "-DENABLE_HLSL=OFF", "-DGLSLANG_TESTS=OFF",
                "-DENABLE_GLSLANG_BINARIES=OFF", "-DENABLE_SPVREMAPPER=OFF",
                "-DGLSLANG_ENABLE_INSTALL=ON", "-DBUILD_SHARED_LIBS=OFF") },
    @{ Name = "SDL3"; Url = "https://github.com/libsdl-org/SDL.git"; Tag = "release-3.4.18"
       Args = @("-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TEST_LIBRARY=OFF",
                "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF") }
)

$clang = (Get-Command clang -ErrorAction SilentlyContinue).Source
if (-not $clang) { throw "clang (llvm-mingw) not on PATH - run tools\setup.ps1" }
$bin = Split-Path -Parent $clang
function Cm($p) { return ($p -replace '\\', '/') }

New-Item -ItemType Directory -Force -Path $SrcRoot, $BldRoot, $Prefix | Out-Null
$lockPath = Join-Path $Root "deps.lock"
try {
    $lock = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None')
} catch {
    throw "Another deps build is running (lock $lockPath)"
}
try {
    foreach ($d in $Deps) {
        $src = Join-Path $SrcRoot $d.Name
        $bld = Join-Path $BldRoot $d.Name
        $stamp = Join-Path $Prefix ("{0}.stamp" -f $d.Name)
        if (-not $Force -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $d.Tag)) {
            Write-Host ("{0} {1}: up to date" -f $d.Name, $d.Tag)
            continue
        }
        if (Test-Path $src) {
            $have = (& git -C $src describe --tags --exact-match 2>$null)
            if ($have -ne $d.Tag) { Remove-Item -Recurse -Force $src }
        }
        if (-not (Test-Path $src)) {
            & git clone --quiet --depth 1 --branch $d.Tag $d.Url $src
            if ($LASTEXITCODE -ne 0) { throw "clone of $($d.Name) $($d.Tag) failed" }
        }
        if (Test-Path $bld) { Remove-Item -Recurse -Force $bld }
        $t = [Diagnostics.Stopwatch]::StartNew()
        & cmake -S $src -B $bld -G Ninja -DCMAKE_BUILD_TYPE=Release `
            "-DCMAKE_C_COMPILER=$(Cm $clang)" `
            "-DCMAKE_CXX_COMPILER=$(Cm (Join-Path $bin 'clang++.exe'))" `
            "-DCMAKE_RC_COMPILER=$(Cm (Join-Path $bin 'llvm-rc.exe'))" `
            "-DCMAKE_INSTALL_PREFIX=$(Cm $Prefix)" @($d.Args) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "configure of $($d.Name) failed" }
        & cmake --build $bld --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw "build of $($d.Name) failed" }
        & cmake --install $bld | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "install of $($d.Name) failed" }
        Set-Content -Path $stamp -Value $d.Tag -Encoding ascii
        Write-Host ("{0} {1}: built in {2:N0} s" -f $d.Name, $d.Tag, $t.Elapsed.TotalSeconds)
    }

    Write-Host "Installed to $Prefix"
} finally {
    $lock.Close()
}
