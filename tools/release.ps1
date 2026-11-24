# release.ps1 - the release exe thps2x_launcher.exe (a local file in private\deliver\ only, never committed).
#
#   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Version <name> [-Slim] [-Variant v200]
#
# Since 2.0 the toolkit's tools\release\port_release.ps1 does the work (it grew out of this script's 1.x version via
# Panzer Dragoon Orta Recompiled) with THPS's names from tools\release.psd1 (lift steps: tools\lift.txt). This wrapper
# adds THPS's own check: the build is tools\build.ps1 -ForRelease (no F9 debug menu, option-c docs/porting_guide.md,
# release steps). -Variant: the toolkit worktree xboxrecomp_<name> whose release script runs (2.0: v200).
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Console status output only.')]
param(
    [Parameter(Mandatory)] [string]$Version,
    [switch]$Slim,
    [string]$Variant = "v200"
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")   # $Repo, $Private, $ToolsRoot
$Toolkit = Join-Path $ToolsRoot "xboxrecomp$(if ($Variant) { "_$Variant" })"
$C = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot "release.psd1")
$Build = if ($C.Build) { if ([IO.Path]::IsPathRooted($C.Build)) { $C.Build } else { Join-Path $Repo $C.Build } }
         else { Join-Path $Private ("build\" + (Split-Path -Leaf $Repo)) }
$cache = Get-Content (Join-Path $Build "CMakeCache.txt") -ErrorAction SilentlyContinue
if (-not ($cache -match '^XR_DEBUG_PANEL:BOOL=OFF$')) {
    throw "build in $Build has the F9 debug menu - run tools\build.ps1 -ForRelease$(if ($Variant) { " -Variant $Variant" })"
}
$a = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $Toolkit "tools\release\port_release.ps1"),
       "-Config", (Join-Path $PSScriptRoot "release.psd1"), "-Version", $Version)
if ($Slim) { $a += "-Slim" }
Push-Location $Repo                       # release.psd1's relative paths
try { & powershell @a; $rc = $LASTEXITCODE } finally { Pop-Location }
exit $rc
