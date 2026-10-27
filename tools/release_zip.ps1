# release_zip.ps1 - the player's release zip from the exe tools\release.ps1 built.
#
#   powershell -File tools\release_zip.ps1 -Version <v> -Notes <release_notes.md>
#
# Into a new folder C:\rc\private\deliver\v<v>\ (refused if it exists): zip\ (the exe, README.txt from
# tools\release_readme.txt, LICENSES\ from the release's payload, tools\download\ with the pinned llvm-mingw zip),
# THPS2X-Recompiled-v<v>-win64.zip, SHA256SUMS.txt, release_notes.md. The compiler ships inside the zip
# (maintainer, 2026-10-27, v0.9.1): the first start needs no internet - the launcher finds the zip in
# tools\download\ with the pinned size and SHA-256 and only unpacks it (it still downloads when the file is gone).
# The llvm-mingw zip is the unchanged upstream release (Apache-2.0 with LLVM exception, licence in LICENSES\),
# cached once in private\deps\release_cache.
param(
[Parameter(Mandatory)] [string]$Version,
[Parameter(Mandatory)] [string]$Notes,
[string]$Folder                                  # the folder name below private\deliver (default v<Version>)
)
$ErrorActionPreference = "Stop"
$Short = "C:\rc"
$Deliver = Join-Path $Short "private\deliver"
$Cache = Join-Path $Short "private\deps\release_cache"
$Exe = Join-Path $Deliver "THPS 2X Recomp.exe"
$utf8 = New-Object Text.UTF8Encoding($false)
. (Join-Path $PSScriptRoot "lock.ps1")

if (-not (Test-Path -LiteralPath $Notes)) { throw "no release notes at $Notes" }
$D = Join-Path $Deliver $(if ($Folder) { $Folder } else { "v$Version" })
if (Test-Path -LiteralPath $D) { throw "$D exists - a release folder is written once" }

# the exe must be this version's (the payload trailer: magic, offset, 32-byte version)
$fs = [IO.File]::OpenRead($Exe)
try {
    $t = New-Object byte[] 48
    [void]$fs.Seek(-48, [IO.SeekOrigin]::End)
    [void]$fs.Read($t, 0, 48)
} finally { $fs.Close() }
if ([Text.Encoding]::ASCII.GetString($t, 0, 8) -ne "XRPAYLD1") { throw "$Exe has no payload trailer" }
$exeVer = [Text.Encoding]::ASCII.GetString($t, 16, 32).TrimEnd([char]0)
if ($exeVer -ne $Version) { throw "$Exe is version '$exeVer', not '$Version' - run tools\release.ps1 -Version $Version" }

# the release's payload stage (newest of this version): licences + the pinned toolchain
$stage = Get-ChildItem (Join-Path $Short "private\release") -Directory | Where-Object { $_.Name -like "*_$Version" } |
    Sort-Object Name | Select-Object -Last 1
if (-not $stage) { throw "no release stage for $Version in private\release" }
$P = Join-Path $stage.FullName "payload"
$tc = @{}
foreach ($l in Get-Content (Join-Path $P "toolchain.txt")) { $kv = $l -split "`t", 2; if ($kv.Count -eq 2) { $tc[$kv[0]] = $kv[1] } }
$tcName = Split-Path -Leaf $tc["url"]
$tcZip = Join-Path $Cache $tcName

$lock = Enter-GlobalLock -Class shared -Who "A (THPS 2X)" -What "release zip $Version"
try {
    if (-not (Test-Path -LiteralPath $tcZip)) {
        Write-Host "fetching $($tc['url']) into the release cache"
        $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -UseBasicParsing -Uri $tc["url"] -OutFile $tcZip
    }
    if ((Get-Item -LiteralPath $tcZip).Length -ne [int64]$tc["size"]) { throw "$tcZip has the wrong size" }
    if ((Get-FileHash -LiteralPath $tcZip).Hash -ne $tc["sha256"].ToUpperInvariant()) { throw "$tcZip has the wrong SHA-256" }

    $Z = Join-Path $D "zip"
    New-Item -ItemType Directory (Join-Path $Z "LICENSES") | Out-Null
    New-Item -ItemType Directory (Join-Path $Z "tools\download") | Out-Null
    Copy-Item -LiteralPath $Exe -Destination $Z
    Copy-Item (Join-Path $P "licenses\*") (Join-Path $Z "LICENSES")
    Copy-Item -LiteralPath $tcZip -Destination (Join-Path $Z "tools\download")
    $readme = [IO.File]::ReadAllText((Join-Path $PSScriptRoot "release_readme.txt")).Replace("{VERSION}", $Version)
    [IO.File]::WriteAllText((Join-Path $Z "README.txt"), ($readme -replace "`r?`n", "`r`n"), $utf8)
    Copy-Item -LiteralPath $Notes -Destination (Join-Path $D "release_notes.md")

    # the zip: '/' separators (unpacks the same on Linux); the compiler zip stored as it is (already compressed)
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zipPath = Join-Path $D "THPS2X-Recompiled-v$Version-win64.zip"
    $zs = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew)
    $za = New-Object IO.Compression.ZipArchive($zs, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($f in Get-ChildItem -LiteralPath $Z -Recurse -File) {
            $rel = $f.FullName.Substring($Z.Length + 1).Replace('\', '/')
            $lvl = if ($f.Extension -eq ".zip") { [IO.Compression.CompressionLevel]::NoCompression } else { [IO.Compression.CompressionLevel]::Optimal }
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $f.FullName, $rel, $lvl)
        }
    } finally { $za.Dispose(); $zs.Close() }

    $sums = foreach ($f in @($zipPath, (Join-Path $Z "THPS 2X Recomp.exe"), (Join-Path $Z "tools\download\$tcName"))) {
        $i = Get-Item -LiteralPath $f
        "{0}  {1}  {2} bytes" -f (Get-FileHash -LiteralPath $f).Hash, $i.Name, $i.Length
    }
    [IO.File]::WriteAllText((Join-Path $D "SHA256SUMS.txt"), (($sums -join "`r`n") + "`r`n"), $utf8)
    $sums | ForEach-Object { Write-Host $_ }
    Write-Host "release folder: $D"
} finally {
    Exit-GlobalLock $lock
}
