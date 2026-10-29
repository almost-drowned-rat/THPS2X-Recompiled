# regress.ps1 - the full regression before every merge (maintainer, 2026-10-28; CLAUDE.md "Resources": after any
# menu change, and for every merge from now on).
#
#   powershell -ExecutionPolicy Bypass -File tools\regress.ps1 [-Exe <THPS2X.exe>] [-Out <new dir>]
#
# Three parts, one summary line each (PASS / FAIL + the evidence path), written to <Out>\summary.txt too:
#   1. menu tour   tests\input\menu_tour.txt, screenshots -> tools\dump_png.ps1 -> tools\shot_compare.ps1 against the
#                  frozen reference (private\reference\menu_tour, masks + manual.txt); the shots on the manual list
#                  are named in the line - they are checked by eye.
#   2. split       tests\input\split_handicap.txt (2 players, scripted player 2): every one of its shots written.
#   3. storage     tests\storage.ps1 (its own checks: new save, interrupted save, load, data\ deleted, overwrite).
# Parts 1 and 2 also fail on problem lines in the game log (null pointer, untranslated instruction, unresolved or
# non-code call targets, fatal errors, crashes).
# Every run: a fresh folder (tools\run_dir.ps1 New-RunDir, default private\evidence\regress\<stamp>; an existing
# folder is refused), throw-away saves\ and data\ per part, RECOMP_NO_DIALOGS=1, THPS_DISPLAY_MODE=windowed, the
# shared global lock (tools\lock.ps1) for the whole run - storage.ps1 runs under it. Exit code 0 = all three passed.
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Console test report.')]
param(
    [string]$Exe = "",                                                  # default: private\install\THPS 2X Recomp\THPS2X.exe; a -Variant build: its layout's exe
    [string]$Out = ""                                                   # default: private\evidence\regress\<stamp>
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")   # $Repo, $Private
. (Join-Path $Repo "tools\run_dir.ps1")
. (Join-Path $Repo "tools\lock.ps1")
if (-not $Exe) { $Exe = Join-Path $Private "install\THPS 2X Recomp\THPS2X.exe" }
if (-not $Out) { $Out = Join-Path $Private "evidence\regress\$(Get-RunStamp)" }
$Out = New-RunDir $Out
$Problems = "\[NULLPTR\]|\[UNIMPL\]|FATAL|Failed to resolve|is not code|access violation|unhandled exception"
# Known untranslated instructions, no-ops on a PC, in every build since M3 (v0.9.1.1 has the same three): D3D 3947's
# port I/O (`in al, dx` 0x00013A42, `out dx, al` 0x0002040E) and its cache flush (`wbinvd` 0x0001DE63). Not
# counted; any other address is a problem line.
$KnownUnimpl = "\[UNIMPL\].*at 0x(00013A42|0002040E|0001DE63)\b"
$summary = New-Object System.Collections.Generic.List[string]
$failed = 0

function Line([bool]$ok, [string]$part, [string]$detail, [string]$path) {
    $s = "{0,-10} {1}  {2}  ({3})" -f $part, $(if ($ok) { "PASS" } else { "FAIL" }), $detail, $path
    Write-Host $s -ForegroundColor $(if ($ok) { "Green" } else { "Red" })
    $script:summary.Add($s)
    if (-not $ok) { $script:failed++ }
}

# One game run in its own saves\ + data\, shots into $Shots; returns the exit code and the problem lines.
function GameRun([string]$name, [string]$script, [int]$seconds, [string]$Shots) {
    $work = New-RunDir (Join-Path $Out "work_$name")
    New-Item -ItemType Directory -Path (Join-Path $work "saves"), (Join-Path $work "data") | Out-Null
    $env:RECOMP_AC97_READY = "1"
    $env:RECOMP_NO_DIALOGS = "1"        # automated: errors to the log, never a message box
    $env:THPS_DISPLAY_MODE = "windowed" # automated: a window, never the whole screen
    $env:RECOMP_SHOT_PREFIX = "$Shots\"
    $p = Start-Process -FilePath $Exe -PassThru -Wait -ArgumentList @(
        "--max-seconds", "$seconds", "--save-dir", "`"$work\saves`"", "--data-dir", "`"$work\data`"",
        "--input-script", "`"$(Join-Path $Repo "tests\input\$script")`"")
    $log = Join-Path $Out "$name.log"
    Copy-Item (Join-Path $work "data\thps2x.log") $log -ErrorAction SilentlyContinue
    $bad = if (Test-Path $log) {
        @(Select-String -Path $log -Pattern $Problems | Where-Object { $_.Line -notmatch $KnownUnimpl })
    } else { @("no log") }
    return @{ exit = $p.ExitCode; bad = $bad }
}

Write-Host "regress: $Exe -> $Out"
$lock = Enter-GlobalLock -Class shared -Heavy -Who "A (THPS 2X)" -What "full regression tools\regress.ps1 (untimed)"
try {
    # 1. menu tour + compare
    $tour = New-RunDir (Join-Path $Out "menu_tour")
    $r = GameRun "menu_tour" "menu_tour.txt" 135 $tour
    & (Join-Path $Repo "tools\dump_png.ps1") $tour | Out-Null
    $cmp = @(& (Join-Path $Repo "tools\shot_compare.ps1") -Run $tour 2>&1)
    $cmpExit = $LASTEXITCODE
    $cmp | Set-Content (Join-Path $Out "menu_tour_compare.txt")
    $manual = @($cmp | Where-Object { "$_" -match "manual visual check \(not compared\)" } |
        ForEach-Object { ("$_" -split "\s+")[0] -replace "\.png$", "" })
    $total = @($cmp | Where-Object { "$_" -match "images outside" }) | Select-Object -Last 1
    Line ($cmpExit -eq 0 -and $r.exit -eq 0 -and $r.bad.Count -eq 0) "menu tour" `
        ("$total; exit $($r.exit), problem lines $($r.bad.Count); manual check: " +
         $(if ($manual.Count) { $manual -join ", " } else { "none" })) $tour

    # 2. split screen
    $split = New-RunDir (Join-Path $Out "split")
    $want = @(Select-String -Path (Join-Path $Repo "tests\input\split_handicap.txt") -Pattern "^\d+:shot=").Count
    $r = GameRun "split" "split_handicap.txt" 130 $split
    $got = @(Get-ChildItem $split -Filter *.bmp).Count
    Line ($got -eq $want -and $r.exit -eq 0 -and $r.bad.Count -eq 0) "split" `
        "$got of $want shots, exit $($r.exit), problem lines $($r.bad.Count)" $split

    # 3. storage (its own run folder below ours; it sees RC_GLOBAL_LOCK and does not take the lock again)
    $stOut = Join-Path $Out "storage"
    $st = @(& (Join-Path $Repo "tests\storage.ps1") -Exe $Exe -Out $stOut *>&1)
    $stExit = $LASTEXITCODE
    $st | Set-Content (Join-Path $Out "storage.txt")
    $fails = @($st | Where-Object { "$_" -match "^\s*FAIL" }).Count
    Line ($stExit -eq 0 -and $fails -eq 0) "storage" "$(@($st | Where-Object { "$_" -match '^\s*PASS' }).Count) checks passed, $fails failed" $stOut
} finally {
    Exit-GlobalLock $lock
}
$summary | Set-Content (Join-Path $Out "summary.txt")
Write-Host $(if ($failed) { "regress: $failed part(s) FAILED" } else { "regress: all parts passed" })
exit $(if ($failed) { 1 } else { 0 })
