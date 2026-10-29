# shot_compare.ps1 - compare a run's screenshots (PNG, tools\dump_png.ps1) with a local reference set.
#
#   powershell -ExecutionPolicy Bypass -File tools\shot_compare.ps1 -Run <shot folder> [-Ref <folder>] [-Threshold 0.5]
#   powershell -ExecutionPolicy Bypass -File tools\shot_compare.ps1 -Run <shot folder> -Update      # new reference
#   powershell -ExecutionPolicy Bypass -File tools\shot_compare.ps1 -Run <2nd run, same build> -LearnMask
#   powershell -ExecutionPolicy Bypass -File tools\shot_compare.ps1 -Run x -MaskRect "07_select_level:62,158,312,186"
#
# Per image: the share of compared pixels where a channel differs by more than 8. Above -Threshold (percent) the
# image is reported as CHANGED, a missing one as MISSING; exit code 1 then.
# Masks: menus animate from run to run (the scrolling background grid follows the frames since the menu opened,
# which shift by a frame or two with input timing; the select-player turntable), up to ~9 % of the pixels. Those
# pixels are learned and left out instead of loosening the tolerance: -LearnMask compares the reference with
# another run of the same build and adds every pixel that differs (grown by 2 px) to <name>.mask next to the
# reference image (one byte per pixel, 1 = ignore); more runs can be learned on top. -Update starts a new
# reference and drops the masks. With masks a broken frame (wrong surface, missing layer) still shows as tens of
# percent; without a mask the tolerance applies to every pixel.
# The reference lives in private\ (game imagery, never committed): default private\reference\menu_tour.
#
# Mask rule (maintainer, 2026-10-17; THPS 2X only, CLAUDE.md "Resources"): at most 25 % of a screenshot
# masked (learned + rectangles together); above that an image is not compared but reported MASK>CAP. Masks are
# frozen: -LearnMask, -MaskRect and -Update change nothing without -MaintainerOk, and print the masked share
# before/after. Images listed in <Ref>\manual.txt (one name per line, '#' comments) are left out of the automatic
# compare and reported as "manual visual check".
param(
    [Parameter(Mandatory = $true)][string]$Run,
    [string]$Ref = "",                    # default: private\reference\menu_tour
    [double]$Threshold = 0.5,
    [switch]$Update,
    [switch]$LearnMask,
    # Fixed ignore rectangles for parts that animate by design (a level preview, a turning globe):
    # "07_select_level:62,158,312,186;08_multiplayer:326,138,208,204" -- OR'ed into <name>.mask.
    [string]$MaskRect,
    [switch]$MaintainerOk,    # required for any change to the reference or its masks (frozen)
    # with -Update: replace only these reference images ("11_options,12_options_cursor"), keeping their masks
    # and every other image -- for a screen that changed on purpose (a menu change)
    [string]$Only,
    [double]$MaskCap = 25.0
)
$ErrorActionPreference = "Stop"
if (-not $Ref) { . (Join-Path $PSScriptRoot "paths.ps1"); $Ref = Join-Path $Private "reference\menu_tour" }
Add-Type -AssemblyName System.Drawing

if (($Update -or $LearnMask -or $MaskRect) -and -not $MaintainerOk) {
    "masks and reference are frozen (mask rule): -Update / -LearnMask / -MaskRect need -MaintainerOk"
    exit 2
}
# The reference folder is a local git repo (no remote: game imagery never leaves the machine); every approved
# change is committed there, so it can be rolled back.
function Save-RefHistory([string]$what) {
    & git -C $Ref rev-parse --is-inside-work-tree 2>$null | Out-Null       # the repo is private\reference, a level up
    if ($LASTEXITCODE -eq 0) {
        & git -C $Ref add -A 2>$null | Out-Null
        & git -C $Ref commit -q -m "$what (shot_compare, -MaintainerOk)" 2>$null | Out-Null
        "reference history: committed ($what)"
    }
}
function Get-MaskedPct([string]$maskPath) {
    if (-not (Test-Path $maskPath)) { return 0.0 }
    $m = [System.IO.File]::ReadAllBytes($maskPath); $c = 0; foreach ($v in $m) { $c += $v }
    return 100.0 * $c / $m.Length
}
$manual = @()
$manualFile = Join-Path $Ref "manual.txt"
if (Test-Path $manualFile) {
    $manual = Get-Content $manualFile | ForEach-Object { ($_ -replace "#.*", "").Trim() } | Where-Object { $_ }
}

if ($Update -and $Only) {
    $names = $Only -split ',' | ForEach-Object { $_.Trim() -replace '\.png$', '' } | Where-Object { $_ }
    foreach ($n in $names) {
        $src = Join-Path $Run "$n.png"
        if (-not (Test-Path $src)) { "missing in the run: $n.png - nothing changed"; exit 2 }
    }
    foreach ($n in $names) {
        Copy-Item (Join-Path $Run "$n.png") (Join-Path $Ref "$n.png") -Force
        "{0,-32} reference replaced, mask kept ({1:N1} % masked)" -f "$n.png", (Get-MaskedPct (Join-Path $Ref "$n.mask"))
    }
    Save-RefHistory "replaced $($names -join ', ') from $Run"
    exit 0
}
if ($Update) {
    New-Item -ItemType Directory -Force $Ref | Out-Null
    Get-ChildItem $Ref -Filter *.png | Remove-Item -Force
    Get-ChildItem $Ref -Filter *.mask -ErrorAction SilentlyContinue | Remove-Item -Force
    Copy-Item (Join-Path $Run "*.png") $Ref
    "reference updated: $((Get-ChildItem $Ref -Filter *.png).Count) images in $Ref (masks dropped)"
    Save-RefHistory "new reference from $Run"
    exit 0
}

if ($MaskRect) {
    foreach ($spec in ($MaskRect -split ";")) {
        if (-not $spec) { continue }
        $name, $r = $spec -split ":", 2
        $x0, $y0, $rw, $rh = ($r -split ",") | ForEach-Object { [int]$_ }
        $png = Join-Path $Ref "$name.png"
        $img = [System.Drawing.Bitmap]::FromFile($png); $w = $img.Width; $h = $img.Height; $img.Dispose()
        $maskPath = Join-Path $Ref "$name.mask"
        $mask = if (Test-Path $maskPath) { [System.IO.File]::ReadAllBytes($maskPath) } else { New-Object byte[] ($w * $h) }
        for ($y = [Math]::Max(0, $y0); $y -lt [Math]::Min($h, $y0 + $rh); $y++) {
            for ($x = [Math]::Max(0, $x0); $x -lt [Math]::Min($w, $x0 + $rw); $x++) { $mask[$y * $w + $x] = 1 }
        }
        [System.IO.File]::WriteAllBytes($maskPath, $mask)
        $masked = 0; foreach ($v in $mask) { $masked += $v }
        "{0,-32} rect {1},{2} {3}x{4} masked, {5:N1} % of the image ignored" -f $name, $x0, $y0, $rw, $rh, (100.0 * $masked / ($w * $h))
    }
    Save-RefHistory "mask rectangles $MaskRect"
    exit 0
}

function Get-Pixels([string]$path) {
    $img = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $r = New-Object System.Drawing.Rectangle 0, 0, $img.Width, $img.Height
        $d = $img.LockBits($r, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $n = $d.Stride * $img.Height
        $b = New-Object byte[] $n
        [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $b, 0, $n)
        $img.UnlockBits($d)
        return @{ W = $img.Width; H = $img.Height; B = $b }
    } finally { $img.Dispose() }
}

# 1 per pixel that differs by more than 8 in a channel.
function Get-Diff($x, $y, [int]$count) {
    $d = New-Object byte[] $count
    for ($i = 0; $i -lt $count; $i++) {
        $o = $i * 4
        if ([Math]::Abs($x[$o] - $y[$o]) -gt 8 -or [Math]::Abs($x[$o + 1] - $y[$o + 1]) -gt 8 -or
            [Math]::Abs($x[$o + 2] - $y[$o + 2]) -gt 8) { $d[$i] = 1 }
    }
    return , $d
}

$bad = 0
$manualSeen = 0
foreach ($f in Get-ChildItem $Ref -Filter *.png | Sort-Object Name) {
    if ($manual -contains $f.BaseName) {
        if (-not $LearnMask) { "{0,-32} manual visual check (not compared)" -f $f.Name }
        $manualSeen++
        continue
    }
    $p = Join-Path $Run $f.Name
    if (-not (Test-Path $p)) { "{0,-32} MISSING" -f $f.Name; $bad++; continue }
    $a = Get-Pixels $f.FullName; $b = Get-Pixels $p
    if ($a.W -ne $b.W -or $a.H -ne $b.H) { "{0,-32} SIZE {1}x{2} vs {3}x{4}" -f $f.Name, $a.W, $a.H, $b.W, $b.H; $bad++; continue }
    $n = $a.W * $a.H
    $diff = Get-Diff $a.B $b.B $n
    $maskPath = Join-Path $Ref ($f.BaseName + ".mask")
    $mask = if (Test-Path $maskPath) { [System.IO.File]::ReadAllBytes($maskPath) } else { New-Object byte[] $n }
    $before = Get-MaskedPct $maskPath
    if (-not $LearnMask -and $before -gt $MaskCap) {
        "{0,-32} MASK>CAP  {1:N1} % masked (cap {2} %): shoot at a deterministic point or list it in manual.txt" -f $f.Name, $before, $MaskCap
        $bad++
        continue
    }
    if ($LearnMask) {
        $w = $a.W; $h = $a.H; $added = 0
        for ($i = 0; $i -lt $n; $i++) {
            if (-not $diff[$i]) { continue }
            $cx = $i % $w; $cy = [Math]::Floor($i / $w)
            for ($dy = -2; $dy -le 2; $dy++) {
                $yy = $cy + $dy; if ($yy -lt 0 -or $yy -ge $h) { continue }
                for ($dx = -2; $dx -le 2; $dx++) {
                    $xx = $cx + $dx; if ($xx -lt 0 -or $xx -ge $w) { continue }
                    $k = $yy * $w + $xx
                    if (-not $mask[$k]) { $mask[$k] = 1; $added++ }
                }
            }
        }
        [System.IO.File]::WriteAllBytes($maskPath, $mask)
        $masked = 0; foreach ($v in $mask) { $masked += $v }
        "{0,-32} mask +{1} px, {2:N1} % -> {3:N1} % of the image ignored" -f $f.Name, $added, $before, (100.0 * $masked / $n)
        continue
    }
    $changed = 0; $compared = 0
    for ($i = 0; $i -lt $n; $i++) {
        if ($mask[$i]) { continue }
        $compared++
        if ($diff[$i]) { $changed++ }
    }
    $pct = if ($compared) { 100.0 * $changed / $compared } else { 0 }
    $tag = if ($pct -gt $Threshold) { $bad++; "CHANGED" } else { "ok" }
    "{0,-32} {1,6:N2} %  {2}  ({3:N1} % masked)" -f $f.Name, $pct, $tag, (100.0 * ($n - $compared) / $n)
}
if ($LearnMask) { "masks learned from $Run"; Save-RefHistory "masks learned from $Run"; exit 0 }
"{0} of {1} images outside {2} %{3}" -f $bad, ((Get-ChildItem $Ref -Filter *.png).Count - $manualSeen), $Threshold, $(if ($manualSeen) { " ($manualSeen manual visual check)" } else { "" })
exit $(if ($bad) { 1 } else { 0 })
