# setup.ps1 - M0 setup for the THPS 2X recomp on Windows.
#
#   powershell -ExecutionPolicy Bypass -File tools\setup.ps1
#
# Safe to run again: installed tools and existing clones are skipped.
# Installs build tools with winget, clones the toolkit candidates and the read-only
# references, and creates the short path C:\rc -> the repos' parent folder for tools
# that cannot cope with spaces. Never touches the disc images in roms\xbox.

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Interactive script; console status output only.')]
param()
$ErrorActionPreference = "Stop"
$Projects = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # ...\projects
$Repo     = Split-Path -Parent $PSScriptRoot                         # ...\projects\thps2x_recomp
$Refs     = Join-Path $Projects "refs"
$Private  = Join-Path $Projects "private"
$Log      = Join-Path $Private "setup.log"
New-Item -ItemType Directory -Force -Path $Refs, $Private | Out-Null
Start-Transcript -Path $Log -Append | Out-Null

$results = New-Object System.Collections.ArrayList
function Note($what, $state) { [void]$results.Add([pscustomobject]@{ Step = $what; Result = $state }) }

# --- 1. Tools via winget ------------------------------------------------------
$packages = @(
    @{ Id = "Python.Python.3.12";               Args = @() },
    @{ Id = "Kitware.CMake";                    Args = @() },
    @{ Id = "Ninja-build.Ninja";                Args = @() },
    @{ Id = "MartinStorsjo.LLVM-MinGW.UCRT";    Args = @() },
    @{ Id = "KhronosGroup.VulkanSDK";           Args = @() },
    @{ Id = "Microsoft.VisualStudio.2022.BuildTools"; Args = @("--override",
        "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended --passive --wait") }
)
foreach ($p in $packages) {
    $installed = winget list --id $p.Id -e --accept-source-agreements 2>$null | Select-String -SimpleMatch $p.Id
    if ($installed) { Note $p.Id "already installed"; continue }
    Write-Host "Installing $($p.Id) ..."
    $wingetArgs = @("install", "--id", $p.Id, "-e", "--accept-package-agreements", "--accept-source-agreements") + $p.Args
    & winget @wingetArgs
    if ($LASTEXITCODE -eq 0) { Note $p.Id "installed" } else { Note $p.Id "FAILED (exit $LASTEXITCODE)" }
}

# Refresh PATH for this session so freshly installed tools are found.
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")

$py = Get-Command py -ErrorAction SilentlyContinue
if ($py) {
    # Same set as the toolkit's tools/macos/setup.sh: capstone for the lifter,
    # pytest/pefile/numpy for its test suite (py -3 -m pytest tools/); unicorn
    # for tools.conformance.replay (original code vs. lifted, docs/sources.md).
    & py -3 -m pip install --upgrade --quiet capstone pytest pefile numpy unicorn
    if ($LASTEXITCODE -eq 0) { Note "pip capstone pytest pefile numpy unicorn" "installed" } else { Note "pip capstone pytest pefile numpy unicorn" "FAILED" }
} else { Note "pip packages" "skipped: py launcher not found yet - open a new terminal and run setup again" }

# --- 2. Short path without spaces --------------------------------------------
$Short = "C:\rc"
if (Test-Path $Short) {
    Note "junction C:\rc" "exists"
} else {
    try {
        New-Item -ItemType Junction -Path $Short -Target $Projects | Out-Null
        Note "junction C:\rc" "created -> $Projects"
    } catch { Note "junction C:\rc" "FAILED: $($_.Exception.Message)" }
}

# --- 3. Clones ----------------------------------------------------------------
# Toolkits and ports: full history without blobs (git log searches work, small download).
# Large emulators: shallow.
$clones = @(
    @{ Url = "https://github.com/sp00nznet/xboxrecomp.git";      Dir = "xboxrecomp-upstream"; Branch = "";               Mode = "history" },
    @{ Url = "https://github.com/comoria08/xboxrecomp.git";      Dir = "defjam-bringup";      Branch = "defjam-bringup"; Mode = "history" },
    @{ Url = "https://github.com/antoxa2584x/burnout3-sw.git";   Dir = "burnout3-sw";         Branch = "";               Mode = "history" },
    @{ Url = "https://github.com/comoria08/defjam_recomp.git";   Dir = "defjam_recomp";       Branch = "";               Mode = "history" },
    @{ Url = "https://github.com/NoRain211/doaxvb-re.git";       Dir = "doaxvb-re";           Branch = "";               Mode = "history" },
    @{ Url = "https://github.com/parkerallan/doa3-recomp.git";   Dir = "doa3-recomp";         Branch = "";               Mode = "history" },
    @{ Url = "https://github.com/sp00nznet/burnout3.git";        Dir = "burnout3";            Branch = "";               Mode = "history" },
    @{ Url = "https://github.com/TGP482/XboxXBE-Recomp.git";     Dir = "XboxXBE-Recomp";      Branch = "";               Mode = "history" },
    @{ Url = "https://github.com/TGP482/SCDA-XBERecomp.git";     Dir = "SCDA-XBERecomp";      Branch = "";               Mode = "history" },
    @{ Url = "https://github.com/arcanite24/pinyon-shift.git";   Dir = "pinyon-shift";        Branch = "";               Mode = "shallow" },
    @{ Url = "https://github.com/FrankHUN88453/CoD3Recomp.git";  Dir = "CoD3Recomp";          Branch = "";               Mode = "shallow" },
    @{ Url = "https://github.com/jeltaqq/Xbox-Original-GameList.git"; Dir = "Xbox-Original-GameList"; Branch = "";      Mode = "shallow" },
    @{ Url = "https://github.com/Cxbx-Reloaded/Cxbx-Reloaded.git"; Dir = "Cxbx-Reloaded";     Branch = "";               Mode = "shallow" },
    @{ Url = "https://github.com/xemu-project/xemu.git";         Dir = "xemu";                Branch = "";               Mode = "shallow" }
)
foreach ($c in $clones) {
    $dest = Join-Path $Refs $c.Dir
    if (Test-Path (Join-Path $dest ".git")) { Note "clone $($c.Dir)" "exists"; continue }
    $gitArgs = @("clone", "-q")
    if ($c.Mode -eq "shallow") { $gitArgs += @("--depth", "1") } else { $gitArgs += @("--filter=blob:none") }
    if ($c.Branch) { $gitArgs += @("-b", $c.Branch) }
    $gitArgs += @($c.Url, $dest)
    & git @gitArgs
    if ($LASTEXITCODE -eq 0) { Note "clone $($c.Dir)" "ok" } else { Note "clone $($c.Dir)" "FAILED" }
}

# --- 4. Def Jam toolchain test layout (defjam_recomp + its toolkit side by side) ---
$DjRoot = Join-Path $Private "defjam_test"
New-Item -ItemType Directory -Force -Path $DjRoot | Out-Null
foreach ($pair in @(@("https://github.com/comoria08/defjam_recomp.git", "defjam_recomp", ""),
                    @("https://github.com/comoria08/xboxrecomp.git", "xboxrecomp", "defjam-bringup"))) {
    $dest = Join-Path $DjRoot $pair[1]
    if (Test-Path (Join-Path $dest ".git")) { Note "defjam_test\$($pair[1])" "exists"; continue }
    $gitArgs = @("clone", "-q", "--depth", "1")
    if ($pair[2]) { $gitArgs += @("-b", $pair[2]) }
    & git @($gitArgs + @($pair[0], $dest))
    if ($LASTEXITCODE -eq 0) { Note "defjam_test\$($pair[1])" "ok" } else { Note "defjam_test\$($pair[1])" "FAILED" }
}

# --- 5. Git repo for this project -----------------------------------------------
if (-not (Test-Path (Join-Path $Repo ".git"))) {
    & git -C $Repo init -q -b main
    Note "git init thps2x_recomp" "done (add a GitHub remote for backups)"
} else { Note "git init thps2x_recomp" "exists" }

Stop-Transcript | Out-Null
Write-Host ""
$results | Format-Table -AutoSize
Write-Host "Log: $Log"
Write-Host ""
Write-Host "Next (Def Jam toolchain test, interactive):"
Write-Host "  C:\rc\private\defjam_test\defjam_recomp\installer.bat"
Write-Host "  -> pick your Def Jam - Fight for NY (USA) disc image"
Write-Host "Then: powershell -ExecutionPolicy Bypass -File tools\m0_analyze.ps1"
