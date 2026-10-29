# release.ps1 - the release exe "THPS 2X Recomp.exe": written to private\deliver\ only, never committed (a
# release is uploaded by hand after review).
#
#   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Version <name> [-Variant <toolkit variant>]
#
# Needs a finished build of this repo (tools\build.ps1 [-Variant ...]) - it reads that build's build.ninja (the
# compile flags, include folders, link line) and ninja's header list of the port's sources, so the player's machine
# builds exactly what this one does. The payload (a zip appended to the launcher exe, read back by the toolkit's
# src/launcher): our prebuilt libraries + the headers the port's sources include, the port's sources (src/, the icon
# resource), the lifter (the toolkit's tools/ packages + templates/runtime, the port's seeds + hooks), the embeddable
# Python 3.12 + capstone, librashader.dll, licences, and the descriptors lift.txt / build.txt / toolchain.txt.
# Never in it: game data, generated C, the lifted game code. Staging in a NEW folder below private\release\.
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Console status output only.')]
param(
    [Parameter(Mandatory)] [string]$Version,
    [string]$Variant
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")   # $Repo, $Private, $ToolsRoot, $Shared
$Name = Split-Path -Leaf $Repo
$Sfx = if ($Variant) { "_$Variant" } else { "" }
$Toolkit = Join-Path $ToolsRoot "xboxrecomp$Sfx"
$Build = Join-Path $Private "build\$Name$(if ($Variant) { "-$Variant" })"
$Cache = Join-Path $Shared "deps\release_cache"
$PyZip = Join-Path $Cache "python-3.12.10-embed-amd64.zip"
$PyZipSha = "4ACBED6DD1C744B0376E3B1CF57CE906F9DC9E95E68824584C8099A63025A3C3"
$Capstone = Join-Path $env:LOCALAPPDATA "Programs\Python\Python312\Lib\site-packages\capstone"   # winget's Python 3.12 (setup.ps1)
$Deliver = Join-Path $Private "deliver"
$Toolchain = @{
    url    = "https://github.com/mstorsjo/llvm-mingw/releases/download/20260908/llvm-mingw-20260908-ucrt-x86_64.zip"
    sha256 = "1bcf74d06b724aeecaa6412ca85f5b26fb1da770e7cdcefa9263c9c5c3ad34b6"
    size   = "190677197"
    folder = "llvm-mingw-20260908-ucrt-x86_64"
}
if ($Version.Length -gt 31 -or $Version -notmatch '^[A-Za-z0-9._-]+$') { throw "-Version: up to 31 characters A-Z a-z 0-9 . _ -" }
. (Join-Path $PSScriptRoot "lock.ps1")
. (Join-Path $PSScriptRoot "run_dir.ps1")
function Norm([string]$p) { return ([IO.Path]::GetFullPath(($p -replace '/', '\'))).TrimEnd('\') }
function Fwd([string]$p) { return ($p -replace '\\', '/') }
$utf8 = New-Object Text.UTF8Encoding($false)

$lock = Enter-GlobalLock -Class shared -Heavy -Who "A (THPS 2X)" -What "release: payload + launcher exe $Version"
try {
    $ninja = Join-Path $Build "build.ninja"
    if (-not (Test-Path $ninja)) { throw "no build in $Build - run tools\build.ps1$(if ($Variant) { " -Variant $Variant" }) first" }
    $launcher = Join-Path $Build "launcher\THPS 2X Recomp.exe"
    if (-not (Test-Path -LiteralPath $launcher)) { throw "no launcher exe in $Build\launcher (toolkit without src/launcher?)" }
    if ((Get-FileHash $PyZip).Hash -ne $PyZipSha) { throw "$PyZip is not the pinned python-3.12.10-embed-amd64.zip" }
    $stamp = Get-RunStamp
    $Stage = New-RunDir (Join-Path $Private "release\${stamp}_$Version")
    $P = Join-Path $Stage "payload"
    New-Item -ItemType Directory $P | Out-Null
    $lines = Get-Content $ninja

    # -- the compile flags of the port's own sources (main.c's object) and its link line -----------------------
    $i = [array]::IndexOf($lines, ($lines | Where-Object { $_ -like "build CMakeFiles/thps2x_recomp.dir/src/main.c.obj:*" } | Select-Object -First 1))
    $vars = @{}
    for ($k = $i + 1; $k -lt $lines.Count -and $lines[$k] -match '^  (\w+) = (.*)$'; $k++) { $vars[$Matches[1]] = $Matches[2] }
    $ln = $lines | Where-Object { $_ -match '^build .*THPS2X\.exe: C_EXECUTABLE_LINKER__thps2x_recomp' } | Select-Object -First 1
    $j = [array]::IndexOf($lines, $ln)
    $lvars = @{}
    for ($k = $j + 1; $k -lt $lines.Count -and $lines[$k] -match '^  (\w+) = (.*)$'; $k++) { $lvars[$Matches[1]] = $Matches[2] }
    $objs = (($ln -split ': C_EXECUTABLE_LINKER__thps2x_recomp_Release ', 2)[1] -split ' \| ')[0] -split ' '
    $portObjs = @($objs | Where-Object { $_ -like "CMakeFiles/thps2x_recomp.dir/src/*.obj" })
    if (-not $portObjs) { throw "no port objects on the link line" }

    # include roots -> where they go in the payload ({TOOLS} = the player's tools\)
    $roots = [ordered]@{}
    $roots[(Norm (Join-Path $Repo "src"))] = "src"
    $roots[(Norm $Toolkit)] = "sdk/toolkit"
    $roots[(Norm (Join-Path $Shared "deps\install\include"))] = "sdk/include"
    $roots[(Norm $Build)] = "sdk/build"
    function Map-Path([string]$p) {
        $n = Norm $p
        foreach ($r in $roots.Keys) {
            if ($n -eq $r) { return $roots[$r] }
            if ($n.StartsWith("$r\", [StringComparison]::OrdinalIgnoreCase)) { return "$($roots[$r])/$(Fwd $n.Substring($r.Length + 1))" }
        }
        return $null
    }
    $cc = New-Object System.Collections.Generic.List[string]
    foreach ($f in ($vars["FLAGS"] -split ' ')) { if ($f -and $f -ne "-g" -and $f -ne "-gcodeview") { $cc.Add($f) } }
    foreach ($d in ($vars["DEFINES"] -split ' ')) { if ($d) { $cc.Add($d) } }
    $inc = $vars["INCLUDES"] -split ' '
    for ($k = 0; $k -lt $inc.Count; $k++) {
        $a = $inc[$k]
        if (-not $a) { continue }
        if ($a -eq "-isystem") { $k++; $a = "-I" + $inc[$k] }
        if ($a -notlike "-I*") { continue }
        $path = $a.Substring(2)
        if ((Norm $path) -eq (Norm (Join-Path $Private "thps2x$Sfx\gen")) -or $path -match '[\\/]thps2x[^\\/]*[\\/]gen$') { $cc.Add("-I{GEN}"); continue }
        if ($path -like "*VulkanSDK*") { continue }                         # the port's sources include no Vulkan header
        $m = Map-Path $path
        if (-not $m) { throw "include folder $path is not under a known root" }
        $cc.Add("-I{TOOLS}/$m")
    }

    # -- the headers the port's sources include (ninja's list), our sources, the generated port_icon.h -----------
    $ErrorActionPreference = "Continue"
    $deps = & ninja -C $Build -t deps @portObjs 2>$null
    $ErrorActionPreference = "Stop"
    $hdrs = New-Object System.Collections.Generic.HashSet[string]
    foreach ($d in $deps) {
        $t = $d.Trim()
        if (-not $t -or $t -like "CMakeFiles/*" -or $t -match '\.c$') { continue }
        if ($t -match 'llvm-mingw' -or $t -match '[\\/]gen[\\/]') { continue }   # the compiler's own / regenerated
        [void]$hdrs.Add((Norm $t))
    }
    $nh = 0
    foreach ($h in $hdrs) {
        $m = Map-Path $h
        if (-not $m) { throw "header $h is not under a known root" }
        $dst = Join-Path $P ($m -replace '/', '\')
        New-Item -ItemType Directory (Split-Path $dst) -Force | Out-Null
        Copy-Item -LiteralPath $h $dst
        $nh++
    }
    $srcLines = @()
    foreach ($o in $portObjs) {
        $rel = ($o -replace '^CMakeFiles/thps2x_recomp\.dir/', '') -replace '\.obj$', ''
        $dst = Join-Path $P ($rel -replace '/', '\')
        New-Item -ItemType Directory (Split-Path $dst) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $Repo $rel) $dst
        $srcLines += "src`t{TOOLS}/$rel"
    }
    New-Item -ItemType Directory "$P\src\assets" -Force | Out-Null
    Copy-Item "$Repo\assets\thps2x.rc", "$Repo\assets\icon.ico" "$P\src\assets"
    New-Item -ItemType Directory "$P\sdk\build" -Force | Out-Null
    Copy-Item "$Build\port_icon.h" "$P\sdk\build\port_icon.h"
    Write-Host "headers: $nh; port sources: $($portObjs.Count)"

    # -- libraries (the link line, in order) --------------------------------------------------------------------
    New-Item -ItemType Directory "$P\sdk\lib" | Out-Null
    $link = New-Object System.Collections.Generic.List[string]
    foreach ($f in ($lvars["LINK_FLAGS"] -split ' ')) { if ($f -and $f -notlike "-Wl,--pdb=*") { $link.Add($f) } }
    # the player's THPS2X.exe is a GUI program: no console window, also when started directly (main() stays the
    # entry; the log is in data\)
    $link.Add("-Wl,--subsystem,windows"); $link.Add("-Wl,--entry=mainCRTStartup")
    $copied = @{}
    foreach ($a in ($lvars["LINK_LIBRARIES"] -split '\s+')) {
        if (-not $a) { continue }
        if ($a -like "-*") { $link.Add($a); continue }
        $src = if ([IO.Path]::IsPathRooted($a)) { $a } else { Join-Path $Build $a }
        $leaf = Split-Path $src -Leaf
        if (-not $copied.ContainsKey($leaf)) {
            Copy-Item -LiteralPath $src "$P\sdk\lib\$leaf"
            $copied[$leaf] = 1
        }
        $link.Add("{TOOLS}/sdk/lib/$leaf")
    }
    Write-Host "libraries: $($copied.Count) ($([math]::Round((Get-ChildItem "$P\sdk\lib" | Measure-Object Length -Sum).Sum / 1MB, 1)) MB)"

    # -- the lifter ----------------------------------------------------------------------------------------------
    $Lf = "$P\lifter"
    New-Item -ItemType Directory "$Lf\tools", "$Lf\templates", "$Lf\port\seeds" | Out-Null
    Copy-Item "$Toolkit\tools\__init__.py" "$Lf\tools\__init__.py"
    foreach ($m in "xbe_parser", "disasm", "func_id", "abi_analysis", "recomp") {
        $ErrorActionPreference = "Continue"
        robocopy "$Toolkit\tools\$m" "$Lf\tools\$m" /E /XD __pycache__ tests /XF "test_*.py" /NFL /NDL /NJH /NJS /NP | Out-Null
        $rc = $LASTEXITCODE
        $ErrorActionPreference = "Stop"
        if ($rc -ge 8) { throw "copy of tools\$m failed" }
    }
    Copy-Item "$Toolkit\templates\runtime" "$Lf\templates\runtime" -Recurse
    Copy-Item "$Repo\seeds\*_seeds.json" "$Lf\port\seeds"
    Copy-Item "$Repo\config\hooks.toml" "$Lf\port\hooks.toml"
    $seedArgs = (Get-ChildItem "$Lf\port\seeds" -Filter "*_seeds.json" | Sort-Object Name | ForEach-Object { "--seed-functions`t{TOOLS}/lifter/port/seeds/$($_.Name)" }) -join "`t"
    $lift = @(
        "# the lift (the same steps as tools\lift.ps1): name, then python's arguments; {XBE} {PIPE} {GEN} {TOOLS}",
        "mkdir`t{PIPE}/disasm`t{PIPE}/func_id`t{PIPE}/abi`t{PIPE}/recomp`t{GEN}",
        "xbe_parser`t-m`ttools.xbe_parser`t{XBE}`t--json`t{PIPE}/xbe.json",
        "disasm`t-m`ttools.disasm`t{XBE}`t-o`t{PIPE}/disasm`t--analysis-json`t{PIPE}/xbe.json`t--force`t$seedArgs",
        "func_id`t-m`ttools.func_id`t{XBE}`t--functions`t{PIPE}/disasm/functions.json`t--strings`t{PIPE}/disasm/strings.json`t--xrefs`t{PIPE}/disasm/xrefs.json`t-o`t{PIPE}/func_id",
        "abi_analysis`t-m`ttools.abi_analysis`t{XBE}`t--disasm-dir`t{PIPE}/disasm`t--func-id-dir`t{PIPE}/func_id`t--output-dir`t{PIPE}/abi",
        "recomp`t-m`ttools.recomp`t{XBE}`t--all`t--split`t1000`t--game-name`tTHPS2X`t--gen-dir`t{GEN}`t--exclude-manual`t{TOOLS}/src/recomp_manual.c`t--weak-functions`t--hooks`t{TOOLS}/lifter/port/hooks.toml`t--disasm-dir`t{PIPE}/disasm`t--func-id-dir`t{PIPE}/func_id`t--abi-dir`t{PIPE}/abi`t-o`t{PIPE}/recomp"
    )
    [IO.File]::WriteAllText("$P\lift.txt", (($lift -join "`r`n") + "`r`n"), $utf8)

    # -- Python (embeddable) + capstone ----------------------------------------------------------------------------
    Expand-Archive -LiteralPath $PyZip -DestinationPath "$P\python"
    New-Item -ItemType Directory "$P\python\Lib\site-packages" | Out-Null
    Copy-Item $Capstone "$P\python\Lib\site-packages\capstone" -Recurse
    Get-ChildItem "$P\python\Lib\site-packages\capstone" -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force
    $pth = Get-ChildItem "$P\python" -Filter "python*._pth" | Select-Object -First 1
    [IO.File]::WriteAllLines($pth.FullName, @("python312.zip", ".", "Lib\site-packages", "..\lifter"))

    # -- librashader, licences, descriptors -------------------------------------------------------------------
    New-Item -ItemType Directory "$P\dll", "$P\licenses" | Out-Null
    Copy-Item (Join-Path $Shared "deps\install\bin\librashader.dll") "$P\dll\librashader.dll"
    # every licence text of what the release carries or the launcher links / fetches (maintainer, 2026-10-27: all
    # of them before a public release); a missing source file stops the release
    $Mingw = Join-Path (Split-Path (Split-Path (Get-Command clang.exe).Source)) "x86_64-w64-mingw32\share\mingw32"
    $lic = [ordered]@{
        "LICENSE-THPS2X-recomp-GPL-3.0.txt"        = "$Repo\LICENSE"
        "LICENSE-xboxrecomp-toolkit-MIT.txt"       = "$Toolkit\LICENSE"
        "NOTICE-toolkit.txt"                       = "$Toolkit\NOTICE"
        "LICENSE-xemu-parts-LGPL-2.1.txt"          = "$Toolkit\LICENSES\LGPL-2.1.txt"
        "LICENSE-pl_mpeg-MIT.txt"                  = "$Toolkit\src\bootvideo\third_party\pl_mpeg\LICENSE"
        "LICENSE-miniz-MIT.txt"                    = "$Toolkit\src\shaderpack\third_party\miniz\LICENSE"
        "LICENSE-SMAA-MIT.txt"                     = "$Toolkit\src\nv2a_vk\third_party\smaa\LICENSE.txt"
        "LICENSE-SDL3-zlib.txt"                    = (Join-Path $Shared "deps\src\SDL3\LICENSE.txt")
        "LICENSE-glslang.txt"                      = (Join-Path $Shared "deps\src\glslang\LICENSE.txt")
        "LICENSE-librashader-MPL-2.0.txt"          = (Join-Path $Shared "deps\src\librashader\LICENSE.md")
        "LICENSE-Vulkan-SDK.txt"                   = (Get-ChildItem "C:\VulkanSDK\*\Licenses\LICENSE.txt" | Select-Object -Last 1).FullName
        "LICENSE-capstone-BSD.txt"                 = (Join-Path (Split-Path $Capstone) "capstone-5.0.9.dist-info\LICENSE.TXT")
        "LICENSE-Python.txt"                       = "$P\python\LICENSE.txt"
        "LICENSE-llvm-mingw-Apache-2.0-LLVM.txt"   = (Join-Path (Split-Path (Split-Path (Get-Command clang.exe).Source)) "LICENSE.TXT")
        "COPYING-MinGW-w64-runtime.txt"            = "$Mingw\COPYING.MinGW-w64-runtime.txt"
        "COPYING-winpthreads.txt"                  = "$Mingw\COPYING.winpthreads.txt"
    }
    foreach ($k in $lic.Keys) {
        if (-not $lic[$k] -or -not (Test-Path -LiteralPath $lic[$k])) { throw "licence text missing for ${k}: $($lic[$k])" }
        Copy-Item -LiteralPath $lic[$k] "$P\licenses\$k"
    }
    [IO.File]::WriteAllText("$P\licenses\README.txt", ("THPS 2X Recomp $Version - licences of everything in this release.`r`n`r`n" +
        "THPS 2X Recomp (the port's own code): GPL-3.0-or-later. Built on the xboxrecomp toolkit (MIT; its third-party parts`r`n" +
        "in NOTICE-toolkit.txt: the xemu parts LGPL-2.1-or-later, pl_mpeg / miniz / SMAA / dr_libs / stb_vorbis / burnout3-sw`r`n" +
        "MIT or public domain). Linked in: SDL3 (zlib), glslang (BSD / MIT / Apache-2.0), the Vulkan loader's import library`r`n" +
        "(Apache-2.0), the llvm-mingw runtime (libc++ etc.: Apache-2.0 with LLVM exception; MinGW-w64 runtime, winpthreads).`r`n" +
        "Shipped next to the game: librashader.dll (MPL-2.0; source: https://github.com/SnowflakePowered/librashader,`r`n" +
        "tag librashader-v0.12.1, built Vulkan-only), Python 3.12.10 embeddable (PSF licence) with capstone 5.0.9 (BSD) for`r`n" +
        "the translator. Fetched at the first start: llvm-mingw 20260908 (https://github.com/mstorsjo/llvm-mingw).`r`n`r`n" +
        "Source of this release: the public THPS 2X Recomp and xboxrecomp repositories, tag v$Version.`r`n"), $utf8)
    $build = @("# compile + link (from $Name's build.ninja): cc = the common arguments, src / gen = the units, rc, link, copy") +
             @("cc`t" + ($cc -join "`t")) + $srcLines + @("gen`t*.c", "rc`t{TOOLS}/src/assets/thps2x.rc`t/I`t{TOOLS}/src/assets",
             ("link`t" + ($link -join "`t")), "copy`t{TOOLS}/dll/librashader.dll")
    [IO.File]::WriteAllText("$P\build.txt", (($build -join "`r`n") + "`r`n"), $utf8)
    $tcl = @("url`t$($Toolchain.url)", "sha256`t$($Toolchain.sha256)", "size`t$($Toolchain.size)", "folder`t$($Toolchain.folder)")
    [IO.File]::WriteAllText("$P\toolchain.txt", (($tcl -join "`r`n") + "`r`n"), $utf8)
    [IO.File]::WriteAllText("$P\README.txt", ("THPS 2X Recomp $Version - files the launcher unpacked (tools\).`r`nAI disclosure:  AI assistance yes (Claude Code by Anthropic).`r`n" +
        "Nothing here is game code or game data: the game is translated from your own disc image on your machine.`r`n" +
        "Licences: licenses\ (the toolkit's NOTICE lists every third-party part).`r`n"), $utf8)

    # -- zip the payload (deflate; the launcher's reader: no ZIP64, entries up to 64 MB) ----------------------
    $files = @(Get-ChildItem $P -Recurse -File)
    $big = $files | Where-Object { $_.Length -ge 64MB }
    if ($big) { throw "payload entries over 64 MB: $($big.FullName -join ', ')" }
    if ($files.Count -ge 65535) { throw "payload has $($files.Count) files (no ZIP64)" }
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zip = Join-Path $Stage "payload.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($P, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
    Write-Host "payload: $($files.Count) files, $([math]::Round(($files | Measure-Object Length -Sum).Sum / 1MB, 1)) MB -> $([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB zipped"

    # -- launcher exe + payload + trailer ------------------------------------------------------------------------
    if (-not (Test-Path $Deliver)) { New-Item -ItemType Directory $Deliver | Out-Null }
    $out = Join-Path $Deliver "THPS 2X Recomp.exe"
    $exeBytes = [IO.File]::ReadAllBytes($launcher)
    $zipBytes = [IO.File]::ReadAllBytes($zip)
    $trailer = New-Object byte[] 48
    [Text.Encoding]::ASCII.GetBytes("XRPAYLD1").CopyTo($trailer, 0)
    [BitConverter]::GetBytes([uint64]$exeBytes.Length).CopyTo($trailer, 8)
    [Text.Encoding]::ASCII.GetBytes($Version).CopyTo($trailer, 16)
    $fs = [IO.File]::Create($out)
    $fs.Write($exeBytes, 0, $exeBytes.Length); $fs.Write($zipBytes, 0, $zipBytes.Length); $fs.Write($trailer, 0, 48)
    $fs.Close()
    Write-Host "release exe: $out ($([math]::Round((Get-Item -LiteralPath $out).Length / 1MB, 1)) MB, version $Version)"
    [IO.File]::WriteAllText("$Stage\release.txt", "version $Version`r`nexe $out`r`nsha256 $((Get-FileHash -LiteralPath $out).Hash)`r`n", $utf8)
} finally {
    Exit-GlobalLock $lock
}
