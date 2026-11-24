# release_zip.ps1 - the player's release zip from the exe tools\release.ps1 built.
#
#   powershell -File tools\release_zip.ps1 -Version <v> -Notes <release_notes.md> [-Folder <name>] [-Variant v200]
#
# Since 2.0 the toolkit's tools\release\port_release_zip.ps1 does the work (it grew out of this script's 1.x version)
# with THPS's names from tools\release.psd1: a new folder private\deliver\v<v>\ with zip\ (thps2x_launcher.exe,
# README.txt from tools\release_readme.txt, LICENSES\, tools\bundle\ with the pinned llvm-mingw zip),
# THPS2X-Recompiled-v<v>-win64.zip, SHA256SUMS.txt, release_notes.md. The compiler ships inside the zip; the first
# start needs no internet.
param(
    [Parameter(Mandatory)] [string]$Version,
    [Parameter(Mandatory)] [string]$Notes,
    [string]$Folder,
    [string]$Variant = "v200"
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")   # $Repo, $ToolsRoot
$Toolkit = Join-Path $ToolsRoot "xboxrecomp$(if ($Variant) { "_$Variant" })"
$a = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $Toolkit "tools\release\port_release_zip.ps1"),
       "-Config", (Join-Path $PSScriptRoot "release.psd1"), "-Version", $Version, "-Notes", (Resolve-Path -LiteralPath $Notes).Path)
if ($Folder) { $a += @("-Folder", $Folder) }
Push-Location $Repo                       # release.psd1's relative paths
try { & powershell @a; $rc = $LASTEXITCODE } finally { Pop-Location }
exit $rc
