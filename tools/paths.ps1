# paths.ps1 - the folder layout, derived from where this repo lives (dot-source it: . "$PSScriptRoot\paths.ps1").
#
#   <root>\projects\thps2x\thps2x_recomp[_<worktree>]   this repo and its worktrees     ($Repo, $Proj)
#   <root>\projects\thps2x\private\                     THPS data: layouts, build dirs, generated C, evidence ($Private)
#   <root>\tools\xboxrecomp[_<name>]                    the toolkit and its worktrees   ($ToolsRoot)
#   <root>\tools\private\                               shared: deps, lock               ($Shared)
#   <root>\roms\xbox\                                   the maintainer's disc images     ($Roms)
#
# The paths must not contain
# spaces: llvm-mingw clang + Ninja + CMake are used on them directly.
$Repo      = Split-Path -Parent $PSScriptRoot
$Proj      = Split-Path -Parent $Repo
$Root      = Split-Path -Parent (Split-Path -Parent $Proj)
$Private   = Join-Path $Proj "private"
$ToolsRoot = Join-Path $Root "tools"
$Shared    = Join-Path $ToolsRoot "private"
$Roms      = Join-Path $Root "roms"
foreach ($p in @($Repo, $Private, $ToolsRoot)) {
    if ($p -match ' ') { throw "path with a space: $p (the build needs space-free paths, see tools\paths.ps1)" }
}
