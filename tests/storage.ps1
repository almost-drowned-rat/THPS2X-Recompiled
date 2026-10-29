# storage.ps1 - save/data layout checks with the real game (M3, docs/status.md).
#
#   powershell -ExecutionPolicy Bypass -File tests\storage.ps1 [-Out <new dir>]
#
# Runs THPS2X.exe from the install layout with a fresh, empty saves\ and data\ (--save-dir / --data-dir under
# a new private\tests\storage_<stamp>, deleted again when every check passed), driven by tests\input\*.txt:
#   1. save_new           a new save is written to saves\UDATA\41560001\<save id>\
#   2. save_new again     a second save, with RECOMP_SAVE_CRASH_TEST="CAREER B": the run dies right before the
#                         save file is committed -> the first save is byte-identical, the second not committed
#   3. load_game_after_crash  the next start drops the unfinished copy; the game reports that the most
#                         recent (unfinished) save cannot be loaded -- as on a console -- and the first save loads
#   4. load_game          after deleting data\ completely: the save still loads, data\ is rebuilt
#   5. save_overwrite x5  Options -> Load/Save -> overwrite keeping the name: the game refuses ("already exists",
#                         its menu logic, see step 5) and the save stays byte-identical
# Overwriting files in place is covered by the toolkit test tests/storage_layout.
# Screenshots (shot=...) and logs go to -Out, a folder that must not exist yet (tools\run_dir.ps1: a run never
# clears or overwrites an earlier run's evidence). Exit code 0 = every check passed.
# Holds the global lock (tools\lock.ps1) for all runs of the test.
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '', Justification = 'Console test report.')]
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingPositionalParameters', '', Justification = 'Local helper Run, called like a table.')]
param(
    [string]$Out = "",                                                  # default: evidence\m3\storage\<stamp>
    [string]$Exe = ""                                                   # default: private\install\...\THPS2X.exe; -Variant builds: their layout
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $Repo "tools\paths.ps1")    # $Private
. (Join-Path $Repo "tools\run_dir.ps1")
$stamp = Get-RunStamp
if (-not $Exe) { $Exe = Join-Path $Private "install\THPS 2X Recomp\THPS2X.exe" }
if (-not $Out) { $Out = Join-Path $Private "evidence\m3\storage\$stamp" }
$Out = New-RunDir $Out
$Work = New-RunDir (Join-Path $Private "tests\storage_$stamp")
$Saves = Join-Path $Work "saves"
$Data = Join-Path $Work "data"
$SaveRoot = Join-Path $Saves "UDATA\41560001"
$failed = 0
. (Join-Path $Repo "tools\lock.ps1")
$lock = Enter-GlobalLock -Class shared -Heavy -Who "A (THPS 2X)" -What "game test tests\storage.ps1"
trap { Exit-GlobalLock $lock; break }

function Check([bool]$ok, [string]$what) {
    if ($ok) { Write-Host "  PASS  $what" } else { Write-Host "  FAIL  $what" -ForegroundColor Red; $script:failed++ }
}

function Run([string]$name, [string]$script, [int]$seconds, [hashtable]$envs = @{}) {
    $env:RECOMP_AC97_READY = "1"
    $env:RECOMP_NO_DIALOGS = "1"        # automated: errors to the log, never a message box
    $env:THPS_DISPLAY_MODE = "windowed" # automated: a window, never the whole screen (default is borderless)
    $env:RECOMP_SHOT_PREFIX = Join-Path $Out "$($name)_"
    Remove-Item Env:RECOMP_SAVE_CRASH_TEST -ErrorAction SilentlyContinue
    foreach ($k in $envs.Keys) { Set-Item "Env:$k" $envs[$k] }
    $p = Start-Process -FilePath $Exe -PassThru -Wait -ArgumentList @(
        "--max-seconds", "$seconds", "--save-dir", "`"$Saves`"", "--data-dir", "`"$Data`"",
        "--input-script", "`"$(Join-Path $Repo "tests\input\$script")`"")
    Copy-Item (Join-Path $Data "thps2x.log") (Join-Path $Out "$name.log") -ErrorAction SilentlyContinue
    Write-Host "[$name] exit $($p.ExitCode)"
    return $p.ExitCode
}

function SaveHashes {
    $h = @{}
    if (Test-Path $SaveRoot) {
        Get-ChildItem $SaveRoot -Recurse -File | Where-Object { $_.DirectoryName -ne $SaveRoot } |
            ForEach-Object { $h[$_.FullName.Substring($SaveRoot.Length)] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash }
    }
    return $h
}

# Every file of $a is still there with the same content ($b may hold more, e.g. a new save's folder).
function SameHashes($a, $b) {
    if ($a.Count -eq 0) { return $false }
    foreach ($k in $a.Keys) { if ($b[$k] -ne $a[$k]) { return $false } }
    return $true
}

New-Item -ItemType Directory -Path $Saves, $Data | Out-Null

Write-Host "1. new save"
Run "1_save_new" "save_new.txt" 75 | Out-Null
$h1 = SaveHashes
Check ($h1.Count -ge 2) "save files written under saves\UDATA\41560001 ($($h1.Count) files)"
Check (Test-Path (Join-Path $Out "1_save_new_save_new_done.bmp")) "screenshot after saving"
Check (Test-Path (Join-Path $Saves "storage_layout.txt")) "saves\storage_layout.txt written"

Write-Host "2. second save, killed before it is committed"
$code = Run "2_save_crash" "save_new.txt" 75 @{ RECOMP_SAVE_CRASH_TEST = "CAREER B" }
$log2 = Get-Content (Join-Path $Out "2_save_crash.log") -Raw
Check ($code -eq 3 -and $log2 -match "RECOMP_SAVE_CRASH_TEST: terminating") "run ended inside the save write (exit $code)"
Check (SameHashes $h1 (SaveHashes)) "the existing save is byte-identical to before"
Check (-not (Get-ChildItem $SaveRoot -Recurse -File | Where-Object { $_.Name -like "*CAREER B*" })) "the interrupted save file was not committed"
Check ((Get-ChildItem (Join-Path $Saves ".pending") -File -Force).Count -ge 1) "unfinished copy left in saves\.pending"

Write-Host "3. restart, load"
Run "3_load_after_crash" "load_game_after_crash.txt" 75 | Out-Null
$log3 = Get-Content (Join-Path $Out "3_load_after_crash.log") -Raw
Check ($log3 -match "dropped unfinished save write") "unfinished copy dropped at start"
Check ((Get-ChildItem (Join-Path $Saves ".pending") -File -Force).Count -eq 0) "saves\.pending empty"
Check (Test-Path (Join-Path $Out "3_load_after_crash_load_done.bmp")) "screenshot after loading"

Write-Host "4. delete data\, load"
$h3 = SaveHashes
Remove-RunItem $Data
Run "4_load_without_data" "load_game_after_crash.txt" 75 | Out-Null
Check (SameHashes $h3 (SaveHashes)) "saves untouched by deleting data\"
Check (Test-Path (Join-Path $Out "4_load_without_data_load_done.bmp")) "screenshot after loading"
Check ((Get-ChildItem (Join-Path $Data "cache\41560001") -File -ErrorAction SilentlyContinue).Count -gt 0) "data\cache\41560001 rebuilt"

Write-Host "5. overwrite the save from Options -> Load/Save, keeping its name (5 times)"
# The game builds this save screen in "new save" mode (0x0010CB8D); "overwrite" mode only comes from the
# in-game path (previous menu state 0x16, 0x0010CB45). Keeping the name therefore ends in the game's own
# "a file named CAREER A already exists" -- the save must stay untouched every time.
# fresh saves\ with one save: step 2 left an unfinished second save folder behind on purpose
Remove-RunItem $Saves; Remove-RunItem $Data
New-Item -ItemType Directory -Path $Saves, $Data | Out-Null
Run "5_save_new" "save_new.txt" 75 | Out-Null
$h4 = SaveHashes
Check ($h4.Count -ge 2 -and (Get-ChildItem $SaveRoot -Directory).Count -eq 1) "one fresh save to overwrite"
for ($i = 1; $i -le 5; $i++) {
    $code = Run "5_overwrite_$i" "save_overwrite.txt" 85 @{ RECOMP_SAVE_TRACE = "1" }
    $log5 = Get-Content (Join-Path $Out "5_overwrite_$i.log") -Raw
    Check ($code -eq 0 -and $log5 -match 'name-exists "CAREER A" .* -> 1 ret=0x00110759') "run $($i): the game reports the name as taken (exit $code)"
    Check (SameHashes $h4 (SaveHashes)) "run $($i): the save is byte-identical"
    Check ((Get-ChildItem $SaveRoot -Directory).Count -eq 1) "run $($i): still exactly one save folder"
}
Remove-Item Env:RECOMP_SAVE_TRACE -ErrorAction SilentlyContinue

Write-Host ""
Exit-GlobalLock $lock
if ($failed) { Write-Host "$failed check(s) FAILED (work folder kept: $Work)" -ForegroundColor Red; exit 1 }
Remove-RunItem $Work
Write-Host "all checks passed"
exit 0
