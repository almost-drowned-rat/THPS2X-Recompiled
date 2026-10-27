# lift.ps1 - THPS 2X: disc image -> generated C (the toolkit's lifting pipeline).
#
#   powershell -ExecutionPolicy Bypass -File tools\lift.ps1 [-Iso <path>] [-Layout <dir>]
#
# Steps (same order as refs\defjam_recomp\installeur\installer.ps1):
#   1. game files: extract the disc into <Layout>\game\ once (read-only access to the ISO)
#   2. xbe_parser -> disasm (with seeds\*.json) -> func_id -> abi_analysis -> recomp
# Everything generated goes to private\ (never committed):
#   private\thps2x\pipeline\  intermediate JSON
#   private\thps2x\gen\       generated C + headers (CMake: RECOMP_GEN_DIR)
# A full rebuild is needed after every lift (CLAUDE.md: only after lifter changes).
# Holds the global lock C:\rc\private\build.lock (tools\lock.ps1) for the whole lift.
# -Variant <name>: a toolkit branch under test in its own worktree (CLAUDE.md, Agents): toolkit
#   projects\xboxrecomp_<name>, generated C private\thps2x_<name>, build dir private\build\thps2x_recomp-<name>,
#   layout private\install_<name>\THPS 2X Recomp (its game\ folder may be a junction to the normal one).
#   The normal toolkit, generated C and install layout are left alone.

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Interactive script; console status output only.')]
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingBrokenHashAlgorithms', '', Justification = 'SHA1 identifies the XBE version (docs/xbe.md), not a security use.')]
param(
    [string]$Iso = (Join-Path $PSScriptRoot "..\..\..\roms\xbox\Tony Hawk's Pro Skater 2x (USA).iso"),   # <parent>\roms\xbox
    [string]$Layout,
    [string]$Variant
)
$ErrorActionPreference = "Stop"
$Repo     = Split-Path -Parent $PSScriptRoot
$Projects = Split-Path -Parent $Repo
$Sfx      = if ($Variant) { "_$Variant" } else { "" }
$Toolkit  = Join-Path $Projects "xboxrecomp$Sfx"
$Private  = Join-Path $Projects "private\thps2x$Sfx"
if (-not $Layout) { $Layout = Join-Path $Projects "private\install$Sfx\THPS 2X Recomp" }
if (-not (Test-Path $Toolkit)) { throw "toolkit $Toolkit missing" }
$Game     = Join-Path $Layout "game"
$Xbe      = Join-Path $Game "default.xbe"
$Pipe     = Join-Path $Private "pipeline"
$Gen      = Join-Path $Private "gen"
$XbeSha1  = "469818E015AED2730641C0BDCBD2557D8A6B8BB4"   # THPS 2X (USA), docs/xbe.md

function Run([string]$what, [string[]]$a) {
    Write-Host "== $what"
    & py -3 @a
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" }
}

. (Join-Path $PSScriptRoot "lock.ps1")
$lock = Enter-GlobalLock -Class shared -Who "A (THPS 2X)" -What "relift thps2x_recomp$Sfx (tools\lift.ps1)"
Push-Location $Toolkit
try {
    if (-not (Test-Path -LiteralPath $Xbe)) {
        if (-not (Test-Path -LiteralPath $Iso)) { throw "Disc image not found: $Iso" }
        New-Item -ItemType Directory -Force -Path $Game | Out-Null
        Run "extract disc -> $Game" @("-m", "tools.xiso", "unpack", $Iso, "-o", $Game)
    }
    $h = (Get-FileHash -Algorithm SHA1 -LiteralPath $Xbe).Hash
    if ($h -ne $XbeSha1) { Write-Warning "unknown default.xbe version (SHA1 $h) - continuing" }

    $dis = Join-Path $Pipe "disasm"; $fid = Join-Path $Pipe "func_id"
    $abi = Join-Path $Pipe "abi";    $rec = Join-Path $Pipe "recomp"
    foreach ($d in @($dis, $fid, $abi, $rec, $Gen)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }
    $analysis = Join-Path $Pipe "xbe.json"

    $seeds = @()
    foreach ($f in Get-ChildItem (Join-Path $Repo "seeds") -Filter "*_seeds.json") {
        $seeds += @("--seed-functions", $f.FullName)
    }

    Run "xbe_parser" @("-m", "tools.xbe_parser", $Xbe, "--json", $analysis)
    Run "disasm" (@("-m", "tools.disasm", $Xbe, "-o", $dis, "--analysis-json", $analysis, "--force") + $seeds)
    Run "func_id" @("-m", "tools.func_id", $Xbe, "--functions", (Join-Path $dis "functions.json"),
        "--strings", (Join-Path $dis "strings.json"), "--xrefs", (Join-Path $dis "xrefs.json"), "-o", $fid)
    Run "abi_analysis" @("-m", "tools.abi_analysis", $Xbe, "--disasm-dir", $dis, "--func-id-dir", $fid, "--output-dir", $abi)

    Get-ChildItem $Gen -File -ErrorAction SilentlyContinue | Remove-Item
    Run "recomp" @("-m", "tools.recomp", $Xbe, "--all", "--split", "1000", "--game-name", "THPS2X",
        "--gen-dir", $Gen, "--exclude-manual", (Join-Path $Repo "src\recomp_manual.c"),
        # M3.5 step 3 (A12): every function overridable without a re-lift
        # (weak sub_X -> sub_X_gen), per-game instruction hooks.
        "--weak-functions", "--hooks", (Join-Path $Repo "config\hooks.toml"),
        "--disasm-dir", $dis, "--func-id-dir", $fid, "--abi-dir", $abi, "-o", $rec)
} finally { Pop-Location; Exit-GlobalLock $lock }

$n = (Get-ChildItem $Gen -Filter "*.c").Count
Write-Host "Done: $n C files in $Gen"
