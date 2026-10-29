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
# librashader (the shader module, docs/toolkit.md; maintainer 2026-10-27:
# self-built, Vulkan runtime only, for every port): built with Rust - a
# build-time-only dependency, installed project-local into private\deps\rust
# (RUSTUP_HOME / CARGO_HOME there, PATH untouched) by -InstallRust, pinned
# below. Target x86_64-pc-windows-gnullvm, so its C/C++ parts (glslang,
# SPIRV-Cross) and the link use the same llvm-mingw clang. Result:
# private\deps\install\bin\librashader.dll, which build.ps1 copies next to
# THPS2X.exe. No Rust: the step is skipped with a message and the shader
# module is unavailable (the game runs as before).

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Interactive script; console status output only.')]
param(
    [int]$Jobs = 2,
    [switch]$Force,
    [switch]$InstallRust
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

    # librashader (see the header). Stamp = "<tag> rust <version>". The clone is
    # checked by its commit ($RsCommit): the tag is a tag of a tag, which
    # `git describe --exact-match` does not resolve. Native tools here (rustup,
    # git, cargo) write progress to stderr - Windows PowerShell turns that into
    # errors under "Stop", so this block runs with "Continue" and checks exit codes.
    $ErrorActionPreference = "Continue"
    $RustVer  = "1.99.1"
    $RustHome = Join-Path $Root "rust"
    $RsTag    = "librashader-v0.12.1"
    $RsCommit = "07ac790cdb9826f37cf8e1daf5011f03e3c4daa7"
    $RsUrl    = "https://github.com/SnowflakePowered/librashader.git"
    $RsTarget = "x86_64-pc-windows-gnullvm"
    $cargo    = Join-Path $RustHome "cargo\bin\cargo.exe"
    $env:RUSTUP_HOME = Join-Path $RustHome "rustup"
    $env:CARGO_HOME  = Join-Path $RustHome "cargo"
    if ($InstallRust -and -not (Test-Path $cargo)) {
        $dl = Join-Path $RustHome "dl"
        New-Item -ItemType Directory -Force -Path $dl | Out-Null
        $init = Join-Path $dl "rustup-init.exe"
        if (-not (Test-Path $init)) {
            Invoke-WebRequest "https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-gnu/rustup-init.exe" -OutFile $init
        }
        & $init -y --no-modify-path --profile minimal --default-host $RsTarget --default-toolchain $RustVer | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Rust $RustVer install failed" }
    }
    $rsStamp = Join-Path $Prefix "librashader.stamp"
    $rsWant  = "$RsTag rust $RustVer"
    if (-not (Test-Path $cargo)) {
        Write-Host "librashader: skipped - no Rust toolchain (tools\deps.ps1 -InstallRust); the shader module stays unavailable"
    } elseif (-not $Force -and (Test-Path $rsStamp) -and ((Get-Content $rsStamp -Raw).Trim() -eq $rsWant)) {
        Write-Host "librashader $RsTag`: up to date"
    } else {
        & (Join-Path $RustHome "cargo\bin\rustup.exe") toolchain install $RustVer --profile minimal 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Rust $RustVer toolchain install failed" }
        $src = Join-Path $SrcRoot "librashader"
        if (Test-Path $src) {
            $have = (& git -C $src rev-parse HEAD 2>$null)
            if ($have -ne $RsCommit) { Remove-Item -Recurse -Force $src }
        }
        if (-not (Test-Path $src)) {
            & git clone --quiet --depth 1 --branch $RsTag $RsUrl $src 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "clone of librashader $RsTag failed" }
            if ((& git -C $src rev-parse HEAD) -ne $RsCommit) { throw "librashader $RsTag is not commit $RsCommit" }
        }
        $bld = Join-Path $BldRoot "librashader"
        $t = [Diagnostics.Stopwatch]::StartNew()
        $env:PATH = (Join-Path $RustHome "cargo\bin") + ";$env:PATH"
        $env:CC_x86_64_pc_windows_gnullvm = "clang"
        $env:CXX_x86_64_pc_windows_gnullvm = "clang++"
        $env:AR_x86_64_pc_windows_gnullvm = "llvm-ar"
        $env:CARGO_TARGET_X86_64_PC_WINDOWS_GNULLVM_LINKER = "clang"
        # libc++ / libunwind linked in: by default the DLL imports llvm-mingw's
        # libc++.dll and libunwind.dll, which players do not have. crt-static
        # makes Rust's std take libunwind.a; CXXSTDLIB makes the cc crate (the
        # C++ of glslang / SPIRV-Cross) ask for libc++.a instead of -lc++ (a
        # plain "-static" link flag does not help: rustc puts -Bdynamic in front
        # of every dylib it names).
        $mingwLib = Join-Path (Split-Path -Parent $bin) "x86_64-w64-mingw32\lib"
        $env:CXXSTDLIB_x86_64_pc_windows_gnullvm = "static=c++"
        $env:CARGO_TARGET_X86_64_PC_WINDOWS_GNULLVM_RUSTFLAGS = "-C target-feature=+crt-static -L native=$mingwLib"
        $env:CARGO_BUILD_JOBS = "$Jobs"
        Push-Location $src
        # the C API with the Vulkan runtime only: no D3D9 (D3DX9_43.dll), D3D11/12, OpenGL, Metal
        & cargo "+$RustVer" build -p librashader-capi --profile optimized --no-default-features `
            --features runtime-vulkan --target $RsTarget --target-dir $bld 2>&1 |
            Where-Object { "$_" -match '^(error|warning: unused)' } | ForEach-Object { Write-Host "$_" }
        $rc = $LASTEXITCODE
        Pop-Location
        if ($rc -ne 0) { throw "build of librashader failed (cargo exit $rc)" }
        New-Item -ItemType Directory -Force -Path (Join-Path $Prefix "bin") | Out-Null
        Copy-Item (Join-Path $bld "$RsTarget\optimized\librashader_capi.dll") (Join-Path $Prefix "bin\librashader.dll") -Force
        Set-Content -Path $rsStamp -Value $rsWant -Encoding ascii
        Write-Host ("librashader {0} (Vulkan only, Rust {1}): built in {2:N0} s" -f $RsTag, $RustVer, $t.Elapsed.TotalSeconds)
    }
    $ErrorActionPreference = "Stop"
    Write-Host "Installed to $Prefix"
} finally {
    $lock.Close()
}
