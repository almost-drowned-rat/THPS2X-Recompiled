# m0_analyze.ps1 - read-only analysis of the THPS 2X disc image for M0.
#
#   powershell -ExecutionPolicy Bypass -File tools\m0_analyze.ps1
#
# Uses the upstream toolkit's own readers (refs\xboxrecomp-upstream\tools\xiso, \xbe_parser).
# Writes generated data to private\thps2x\ (never committed) and a short summary to docs\xbe-raw.txt.
# The disc image is only read.

[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Interactive script; console status output only.')]
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingBrokenHashAlgorithms', '', Justification = 'SHA1 identifies the XBE version (docs/toolkit.md), not a security use.')]
param(
    [string]$Iso                          # default: <root>\roms\xbox\Tony Hawk's Pro Skater 2x (USA).iso
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")   # $Repo, $Private, $ToolsRoot, $Roms
if (-not $Iso) { $Iso = Join-Path $Roms "xbox\Tony Hawk's Pro Skater 2x (USA).iso" }
$Toolkit  = Join-Path $ToolsRoot "refs\xboxrecomp-upstream"
$Out      = Join-Path $Private "thps2x"
New-Item -ItemType Directory -Force -Path $Out | Out-Null

if (-not (Test-Path -LiteralPath $Iso)) { throw "Disc image not found: $Iso" }
if (-not (Test-Path (Join-Path $Toolkit "tools\xiso"))) { throw "Run tools\setup.ps1 first (missing $Toolkit)" }

Push-Location $Toolkit
try {
    # File list of the whole disc (movie/audio formats, sizes).
    & py -3 -m tools.xiso ls $Iso | Out-File -Encoding utf8 (Join-Path $Out "files.txt")
    if ($LASTEXITCODE -ne 0) { throw "xiso ls failed" }

    # default.xbe only - no full extraction needed for the analysis.
    & py -3 -m tools.xiso get $Iso default.xbe -o (Join-Path $Out "default.xbe")
    if ($LASTEXITCODE -ne 0) { throw "xiso get default.xbe failed" }

    # Header, certificate (title ID, region), sections, XDK library versions, kernel imports.
    & py -3 -m tools.xbe_parser (Join-Path $Out "default.xbe") --json (Join-Path $Out "xbe.json") |
        Out-File -Encoding utf8 (Join-Path $Out "xbe_info.txt")
    if ($LASTEXITCODE -ne 0) { throw "xbe_parser failed" }
} finally { Pop-Location }

$sha1 = (Get-FileHash -Algorithm SHA1 (Join-Path $Out "default.xbe")).Hash
$ext = Get-Content (Join-Path $Out "files.txt") |
    Where-Object { $_ -match '^\s+\d+\s+(.+)$' -and $_ -notmatch 'total$' } |
    ForEach-Object { if ($_ -match '\.([A-Za-z0-9]+)\s*$') { $Matches[1].ToLower() } else { "(none)" } } |
    Group-Object | Sort-Object Count -Descending | Select-Object Count, Name

$summary = @()
$summary += "default.xbe SHA1: $sha1"
$summary += ""
$summary += "File extensions on the disc (count):"
$summary += ($ext | Format-Table -AutoSize | Out-String).TrimEnd()
$summary += ""
$summary += (Get-Content (Join-Path $Out "xbe_info.txt"))
$summary | Out-File -Encoding utf8 (Join-Path $Repo "docs\xbe-raw.txt")

Write-Host "default.xbe SHA1: $sha1"
Write-Host "Written: docs\xbe-raw.txt (summary, no game data) and private\thps2x\ (xbe, file list, json)"
