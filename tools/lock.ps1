# lock.ps1 - the build/test lock of every project on this machine, two classes (maintainer, 2026-10-16).
#
#   . "$PSScriptRoot\lock.ps1"
#   $l = Enter-GlobalLock -Class shared    -Who "A (THPS 2X)" -What "build thps2x_recomp"
#   $l = Enter-GlobalLock -Class exclusive -Who "A (THPS 2X)" -What "level sweep with frame times"
#   try { ... } finally { Exit-GlobalLock $l }
#
#   powershell -File tools\lock.ps1 status      holders and waiters: agent, class, purpose, since, session, pid
#
# Classes:
#   shared     builds, relifts, normal test runs (menu tour, storage, screenshot checks). At most 2 holders at
#              once, so with <= 2 compiler processes per build at most 4 across all agents.
#   exclusive  frame-time measurements, benchmarks, baselines, soak tests, emulator oracle comparisons
#              (xemu, Cxbx-Reloaded): alone on the machine.
# Writer priority, bounded (maintainer, 2026-10-25): while an exclusive request waits, no new shared holder gets
# in - but an exclusive waiter gives up after 15 min (-TimeoutMin, or RC_LOCK_EXCLUSIVE_WAIT_MIN), withdraws its
# place in the queue so shared requests flow again, and writes a note to coordination.md. Then it retries after a
# backoff of 5, 10 and 20 min (-BackoffMin, or RC_LOCK_BACKOFF_MIN="5,10,20"), each attempt noted in
# coordination.md; after the last retry fails it stops with an error - no endless retries. During a backoff it
# holds nothing and blocks nobody. Shared requests wait up to 240 min (they block nobody while they wait).
#
# Protocol version and single source (maintainer, 2026-10-25): this file lives in the toolkit (tools\lock.ps1);
# repos get it only through the toolkit's tools\copy_lock.ps1. The machine's lock carries the protocol version in
# lock\protocol (written by copy_lock.ps1, which switches over only while nobody holds or waits for the lock);
# every record carries it too. A lock.ps1 whose protocol differs from the machine's is refused with a clear
# message. Protocol 1 = every version before this one (no lock\protocol file); this file is protocol 2.
#
# Self-deadlock guard (maintainer, 2026-10-25): an exclusive request fails AT ONCE with a clear error when the
# same session already holds the lock (shared or exclusive) in another process - such a request could only wait
# on itself (2026-10-25: an agent's own background shared lock kept its exclusive series waiting 4 h). It does not
# release the other process's lock for it: stop that holder (Exit-GlobalLock in it, or end the process), then
# retry. Session = RC_LOCK_SESSION if set, else the Claude Code session (CLAUDE_CODE_SESSION_ID), else this
# process alone. Nested steps see RC_GLOBAL_LOCK (the outer class) in the environment and do not take it again.
#
# Waiting is an OS wait, no polling (maintainer, 2026-10-24): named kernel mutexes Global\rc_lock_gate and
# Global\rc_lock_slot0/1 mirror the files below; a waiter sleeps in WaitOne/WaitAny and wakes when the holder
# releases them or dies (abandoned mutex = acquired). The files are still taken on top, so a holder running an
# older lock.ps1 (files only) is respected; only then does a waiter fall back to retrying every 5 s (it says so).
#
# Files (C:\rc\private\), all held as open handles, so a holder that dies releases them with its process:
#   build.lock   the gate: an exclusive holder keeps it from the moment it starts waiting until it is done; a
#                shared requester only passes it (open, take a slot, close). An older lock.ps1 that holds
#                build.lock for everything therefore counts as exclusive.
#   lock\slot0, lock\slot1   one per shared holder; exclusive takes both.
#   lock\who\<pid>.json      one per holder or waiter: who (agent), session, pid + start time, class, what
#                            (purpose), state (waiting/holding/backoff), since, protocol. Records of processes
#                            that are gone are ignored and deleted by the next reader.
#   lock\protocol            the machine's lock protocol version (copy_lock.ps1).
#   lock\lock.log            every take-over of a stale coordination line, every exclusive give-up, every switch-over.
# While held, one line "who | class | what | since (pid, started)" stands in coordination.md in the repos' parent folder;
# lines of processes that are gone are removed (and logged) whenever anybody takes the lock.
# Tests use their own copy of everything: RC_LOCK_ROOT (files), RC_LOCK_COORDINATION, RC_LOCK_NS (mutex names);
# tools\test_lock.ps1.
$script:GlobalLockRoot = if ($env:RC_LOCK_ROOT) { $env:RC_LOCK_ROOT } else { "C:\rc\private" }
$script:GlobalLockDir = Join-Path $script:GlobalLockRoot "lock"
$script:GlobalLockGate = Join-Path $script:GlobalLockRoot "build.lock"
$script:GlobalLockWho = Join-Path $script:GlobalLockDir "who"
$script:GlobalLockNs = if ($env:RC_LOCK_NS) { $env:RC_LOCK_NS } else { "rc_lock" }
$script:CoordinationPath = if ($env:RC_LOCK_COORDINATION) { $env:RC_LOCK_COORDINATION } else {
    "C:\rc\coordination.md" }                        # = <parent folder>\coordination.md (junction)
$script:ExclusiveWaitMin = if ($env:RC_LOCK_EXCLUSIVE_WAIT_MIN) { [double]$env:RC_LOCK_EXCLUSIVE_WAIT_MIN } else { 15 }
$script:SharedWaitMin = 240
$script:BackoffMin = if ($env:RC_LOCK_BACKOFF_MIN) { @($env:RC_LOCK_BACKOFF_MIN -split ',' | ForEach-Object {
    [double]::Parse($_.Trim(), [Globalization.CultureInfo]::InvariantCulture) }) } else { @(5, 10, 20) }
$script:LockProtocol = 2
$script:GlobalLockProtocolFile = Join-Path $script:GlobalLockDir "protocol"

# The machine's lock protocol: lock\protocol, or 1 (every lock.ps1 before protocol versions) when it is missing.
function Get-MachineLockProtocol {
    try { return [int]((Get-Content -Raw -ErrorAction Stop -LiteralPath $script:GlobalLockProtocolFile).Trim()) } catch { return 1 }
}

function Assert-LockProtocol {
    $m = Get-MachineLockProtocol
    if ($m -ne $script:LockProtocol) {
        throw ("lock: refused - this lock.ps1 speaks protocol $($script:LockProtocol), the machine's lock is protocol $m " +
               "($($script:GlobalLockProtocolFile)$(if ($m -eq 1) { ' missing = 1' })). Get the toolkit's lock.ps1 with " +
               "<toolkit>\tools\copy_lock.ps1 -Repo <this repo>; it switches the machine over only while nobody holds or " +
               "waits for the lock.")
    }
}

function Open-LockFile([string]$path) {
    try { return [System.IO.File]::Open($path, 'OpenOrCreate', 'ReadWrite', 'None') } catch { return $null }
}

function Get-LockTime([datetime]$t, [string]$fmt = "yyyy-MM-dd HH:mm:ss") {
    $t.ToString($fmt, [Globalization.CultureInfo]::InvariantCulture)
}

function Write-LockLog([string]$msg) {
    try { Add-Content -Encoding utf8 (Join-Path $script:GlobalLockDir "lock.log") "$(Get-LockTime (Get-Date)) $msg" } catch { }
}

function Get-LockSession {
    if ($env:RC_LOCK_SESSION) { return $env:RC_LOCK_SESSION }
    if ($env:CLAUDE_CODE_SESSION_ID) { return "claude:$($env:CLAUDE_CODE_SESSION_ID)" }
    "pid:$PID"
}

# coordination.md is also read and edited by the agents and the maintainer: a write (or read) while another
# process has it open fails with "in use by another process" - retry for up to ~6 s instead of failing the run.
function Invoke-CoordinationIO([scriptblock]$op) {
    for ($i = 1; ; $i++) {
        try { return (& $op) } catch {
            if ($i -ge 20) { throw }
            Start-Sleep -Milliseconds 300
        }
    }
}

# Drop coordination lines whose process is gone (a holder killed before its finally ran).
function Clear-StaleCoordination {
    if (-not (Test-Path $script:CoordinationPath)) { return }
    $lines = Invoke-CoordinationIO { Get-Content -Encoding utf8 -ErrorAction Stop $script:CoordinationPath }
    $keep = foreach ($ln in $lines) {
        if ($ln -match '\(pid (\d+)') {
            $holderPid = [int]$Matches[1]
            if (-not (Get-Process -Id $holderPid -ErrorAction SilentlyContinue)) {
                Write-LockLog "stale coordination line removed (holder pid $holderPid gone): $ln"
                continue
            }
        }
        $ln
    }
    if (@($keep).Count -ne @($lines).Count) {
        Invoke-CoordinationIO { Set-Content -Encoding utf8 -ErrorAction Stop $script:CoordinationPath $keep }
    }
}

# --- holder/waiter records (lock\who\<pid>.json) ---
function Get-ProcessStarted([int]$id) {
    $p = Get-Process -Id $id -ErrorAction SilentlyContinue
    if ($p) { try { return Get-LockTime $p.StartTime } catch { } }
    $null
}

function Write-LockRecord([string]$State, [string]$Class, [string]$Who, [string]$What, [datetime]$Since) {
    New-Item -ItemType Directory -Force $script:GlobalLockWho | Out-Null
    $r = [ordered]@{ who = $Who; session = (Get-LockSession); pid = $PID; started = (Get-ProcessStarted $PID);
                     class = $Class; what = $What; state = $State; since = (Get-LockTime $Since "s");
                     protocol = $script:LockProtocol }
    try { Set-Content -Encoding utf8 (Join-Path $script:GlobalLockWho "$PID.json") ($r | ConvertTo-Json -Compress) } catch { }
}

function Remove-LockRecord {
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $script:GlobalLockWho "$PID.json")
}

# Live records only: a record whose process is gone (or whose pid now belongs to another process) is deleted.
function Get-LockRecords {
    if (-not (Test-Path $script:GlobalLockWho)) { return @() }
    $out = @()
    foreach ($f in Get-ChildItem $script:GlobalLockWho -Filter *.json -ErrorAction SilentlyContinue) {
        try { $r = Get-Content -Raw -Encoding utf8 -ErrorAction Stop $f.FullName | ConvertFrom-Json } catch { continue }
        if ($r.started -and (Get-ProcessStarted ([int]$r.pid)) -eq $r.started) { $out += $r; continue }
        Remove-Item -Force -ErrorAction SilentlyContinue $f.FullName
    }
    $out
}

# Holders and waiters: the records, plus coordination lines of holders that have none (an older lock.ps1).
function Get-GlobalLockStatus {
    $recs = @(Get-LockRecords)
    $now = Get-Date
    $out = @(foreach ($r in $recs) {
        $since = if ($r.since -is [datetime]) { $r.since } else {
            [datetime]::ParseExact($r.since, "s", [Globalization.CultureInfo]::InvariantCulture) }
        [pscustomobject]@{ State = $r.state; Class = $r.class; Who = $r.who; What = $r.what; Since = $since;
                           Minutes = [math]::Round(($now - $since).TotalMinutes, 1); Session = $r.session;
                           Pid = [int]$r.pid; Protocol = $(if ($r.protocol) { [int]$r.protocol } else { 1 }); Source = "record" }
    })
    if (Test-Path $script:CoordinationPath) {
        $lines = Invoke-CoordinationIO { Get-Content -Encoding utf8 -ErrorAction Stop $script:CoordinationPath }
        foreach ($ln in $lines) {
            if ($ln -notmatch '^\| (.+?) \| (shared|exclusive) \| (.+) \| since (\S+ \S+) \(pid (\d+), started') { continue }
            $p = [int]$Matches[5]
            if (@($recs | Where-Object { [int]$_.pid -eq $p }).Count -or -not (Get-Process -Id $p -ErrorAction SilentlyContinue)) { continue }
            $since = [datetime]::ParseExact($Matches[4], "yyyy-MM-dd HH:mm", [Globalization.CultureInfo]::InvariantCulture)
            $out += [pscustomobject]@{ State = "holding"; Class = $Matches[2]; Who = $Matches[1]; What = $Matches[3];
                                       Since = $since; Minutes = [math]::Round(($now - $since).TotalMinutes, 1);
                                       Session = "?"; Pid = $p; Protocol = 1; Source = "coordination (older lock.ps1, no record)" }
        }
    }
    @($out | Sort-Object State, Since)
}

function Format-GlobalLockStatus {
    $st = @(Get-GlobalLockStatus)
    "lock status $(Get-LockTime (Get-Date)) ($($script:CoordinationPath)), machine protocol $(Get-MachineLockProtocol), this lock.ps1 protocol $($script:LockProtocol):"
    if (-not $st.Count) { "  free - no holders, no waiters"; return }
    foreach ($s in $st) {
        "  {0,-7} {1,-9} {2} | {3} | since {4} ({5} min) | session {6}, pid {7}{8}" -f $s.State, $s.Class, $s.Who,
            $s.What, (Get-LockTime $s.Since "yyyy-MM-dd HH:mm:ss"),
            $s.Minutes.ToString("0.0", [Globalization.CultureInfo]::InvariantCulture), $s.Session, $s.Pid,
            $(if ($s.Source -ne "record") { " [$($s.Source)]" } else { "" })
    }
}

# For a script about to take the exclusive lock (g3_series.ps1): problems that should stop it before it starts -
# the calling session holds or waits for the lock in another process, or a waiter is older than the limit.
function Test-GlobalLockPreflight([double]$WaiterLimitMin = $script:ExclusiveWaitMin) {
    $bad = @()
    $me = Get-LockSession
    foreach ($s in Get-GlobalLockStatus) {
        if ($s.Pid -eq $PID) { continue }
        if ($s.Session -eq $me) {
            $bad += "this session ($me) already $($s.State) the $($s.Class) lock: pid $($s.Pid), '$($s.What)' since $(Get-LockTime $s.Since)"
        } elseif ($s.State -eq "waiting" -and $s.Minutes -gt $WaiterLimitMin) {
            $bad += "$($s.Who) waits for the $($s.Class) lock for $($s.Minutes) min (> $WaiterLimitMin): pid $($s.Pid), '$($s.What)'"
        }
    }
    $bad
}

# Kernel mutex wait with a deadline; an abandoned mutex (its holder died) is acquired all the same.
function Wait-LockMutex([System.Threading.Mutex]$m, [datetime]$deadline) {
    $ms = [int][math]::Max(0, [math]::Min([int]::MaxValue, ($deadline - (Get-Date)).TotalMilliseconds))
    try { return $m.WaitOne($ms) } catch [System.Threading.AbandonedMutexException] { return $true }
}

function Wait-AnyLockMutex([System.Threading.Mutex[]]$ms, [datetime]$deadline) {
    $t = [int][math]::Max(0, [math]::Min([int]::MaxValue, ($deadline - (Get-Date)).TotalMilliseconds))
    try { return [System.Threading.WaitHandle]::WaitAny($ms, $t) }
    catch [System.Threading.AbandonedMutexException] { return $_.Exception.MutexIndex }
}

function Write-CoordinationNote([string]$note) {
    Write-LockLog $note
    try { Invoke-CoordinationIO { Add-Content -Encoding utf8 -ErrorAction Stop $script:CoordinationPath @("", $note) } } catch { }
}

function Get-LockOthers {
    $o = @(Get-GlobalLockStatus | Where-Object { $_.Pid -ne $PID } |
        ForEach-Object { "$($_.Who) $($_.State) $($_.Class) '$($_.What)'" }) -join "; "
    if ($o) { $o } else { "none recorded" }
}

function Enter-GlobalLock {
    param([Parameter(Mandatory = $true)][string]$Who, [Parameter(Mandatory = $true)][string]$What,
          [ValidateSet("shared", "exclusive")][string]$Class = "exclusive",
          [double]$TimeoutMin = -1,                              # < 0: 15 min exclusive, 240 min shared
          [double[]]$BackoffMin = $null)                         # exclusive retries; $null: 5, 10, 20 min
    if ($env:RC_GLOBAL_LOCK) {                                   # an outer step holds it
        if ($Class -eq "exclusive" -and $env:RC_GLOBAL_LOCK -ne "exclusive") {
            Write-Host "lock: exclusive step nested in a shared one ($What) - runs under the outer shared lock"
        }
        return $null
    }
    Assert-LockProtocol
    if ($TimeoutMin -lt 0) { $TimeoutMin = if ($Class -eq "exclusive") { $script:ExclusiveWaitMin } else { $script:SharedWaitMin } }
    if ($Class -ne "exclusive") { return Enter-GlobalLockOnce $Who $What $Class $TimeoutMin }
    if ($null -eq $BackoffMin) { $BackoffMin = $script:BackoffMin }
    $n = $BackoffMin.Count + 1                                   # first attempt + one retry per backoff
    $tag = "exclusive request of $Who ('$What', session $(Get-LockSession), pid $PID)"
    for ($a = 1; ; $a++) {
        try {
            $l = Enter-GlobalLockOnce $Who $What $Class $TimeoutMin
            if ($a -gt 1) { Write-CoordinationNote "Note (lock.ps1, $(Get-LockTime (Get-Date) 'yyyy-MM-dd HH:mm')): $tag got the lock on attempt $a/$n." }
            return $l
        } catch [System.TimeoutException] {
            $stamp = Get-LockTime (Get-Date) 'yyyy-MM-dd HH:mm'
            if ($a -ge $n) {
                Write-CoordinationNote ("Note (lock.ps1, $stamp): $tag attempt $a/$n gave up after $TimeoutMin min and left the " +
                    "queue - no further retries, stopped; report to the maintainer. Holders/waiters then: $(Get-LockOthers).")
                throw "global lock (exclusive) not free in $n attempts of $TimeoutMin min (backoff $($BackoffMin -join '/') min) - stopped, no further retries (notes in coordination.md)"
            }
            $b = $BackoffMin[$a - 1]
            $until = (Get-Date).AddMinutes($b)
            Write-CoordinationNote ("Note (lock.ps1, $stamp): $tag attempt $a/$n gave up after $TimeoutMin min and left the " +
                "queue, shared requests flow again; retry in $b min (about $(Get-LockTime $until 'HH:mm')). Holders/waiters then: $(Get-LockOthers).")
            Write-Host "lock: exclusive attempt $a/$n gave up after $TimeoutMin min; retry in $b min"
            Write-LockRecord "backoff" $Class $Who $What (Get-Date)
            Start-Sleep -Milliseconds ([int]($b * 60000))       # holds nothing meanwhile
            Remove-LockRecord
        }
    }
}

# One attempt; a timeout throws System.TimeoutException after leaving the queue.
function Enter-GlobalLockOnce([string]$Who, [string]$What, [string]$Class, [double]$TimeoutMin) {
    New-Item -ItemType Directory -Force $script:GlobalLockDir | Out-Null
    if ($Class -eq "exclusive") {                                # self-deadlock guard: never wait on our own lock
        $me = Get-LockSession
        $own = @(Get-LockRecords | Where-Object { $_.session -eq $me -and [int]$_.pid -ne $PID -and $_.state -eq "holding" })
        if ($own.Count) {
            $o = $own[0]
            throw ("lock: exclusive request refused (self-deadlock) - this session ($me) already holds the $($o.class) " +
                   "lock in pid $($o.pid) ('$($o.what)', since $($o.since)); the request would wait on itself. " +
                   "Release that lock first (stop that process or let it call Exit-GlobalLock), then retry.")
        }
    }
    $slots = @((Join-Path $script:GlobalLockDir "slot0"), (Join-Path $script:GlobalLockDir "slot1"))
    $waitStart = Get-Date
    $deadline = $waitStart.AddMinutes($TimeoutMin)
    $ns = $script:GlobalLockNs
    $mGate = New-Object System.Threading.Mutex($false, "Global\${ns}_gate")
    $mSlots = [System.Threading.Mutex[]]@((New-Object System.Threading.Mutex($false, "Global\${ns}_slot0")),
                                          (New-Object System.Threading.Mutex($false, "Global\${ns}_slot1")))
    $owned = @()                                                 # mutexes this call owns
    $fail = {
        foreach ($m in $owned) { try { $m.ReleaseMutex() } catch { } }   # withdraws an exclusive place in the queue
        Remove-LockRecord
        throw [System.TimeoutException]::new("global lock ($Class) still not free after $TimeoutMin min (see coordination.md)")
    }
    Write-LockRecord "waiting" $Class $Who $What $waitStart
    if (-not (Wait-LockMutex $mGate (Get-Date))) {               # not free right now: say so, then sleep in the OS
        Write-Host "waiting for the global lock ($Class, up to $TimeoutMin min; see $($script:CoordinationPath))"
        if (-not (Wait-LockMutex $mGate $deadline)) { & $fail }
    }
    $owned += $mGate
    $mine = @()                                                  # slot indices this call owns
    if ($Class -eq "exclusive") {
        foreach ($i in 0, 1) {                                   # the gate keeps new shared holders out meanwhile
            if (-not (Wait-LockMutex $mSlots[$i] $deadline)) { & $fail }
            $owned += $mSlots[$i]; $mine += $i
        }
    } else {
        $i = Wait-AnyLockMutex $mSlots $deadline
        if ($i -eq [System.Threading.WaitHandle]::WaitTimeout) { & $fail }
        $owned += $mSlots[$i]; $mine += $i
        $mGate.ReleaseMutex(); $owned = @($owned | Where-Object { $_ -ne $mGate })
    }
    # The files on top (an older lock.ps1 knows only them): normally free at once; a holder with the old version
    # is waited for by retrying.
    $gate = $null; $held = @(); $said = $false
    while ($true) {
        if ($Class -eq "exclusive") {
            if (-not $gate) { $gate = Open-LockFile $script:GlobalLockGate }
            if ($gate) {
                foreach ($sl in $slots) { if (-not ($held | Where-Object { $_.Name -eq $sl })) { $h = Open-LockFile $sl; if ($h) { $held += $h } } }
                if ($held.Count -eq $slots.Count) { break }
            }
        } else {
            $g = Open-LockFile $script:GlobalLockGate                           # passes only if nobody exclusive waits/holds
            if ($g) {
                $h = Open-LockFile $slots[$mine[0]]; if ($h) { $held = @($h) }
                $g.Close()
                if ($held.Count) { break }
            }
        }
        if ((Get-Date) -gt $deadline) {
            foreach ($h in $held) { $h.Close() }; if ($gate) { $gate.Close() }
            & $fail
        }
        if (-not $said) { Write-Host "lock: a holder with an older lock.ps1 (files only) - retrying every 5 s"; $said = $true }
        Start-Sleep -Seconds 5
    }
    Clear-StaleCoordination
    $now = Get-Date
    Write-LockRecord "holding" $Class $Who $What $now
    $line = "| $Who | $Class | $What | since $(Get-LockTime $now 'yyyy-MM-dd HH:mm') (pid $PID, started $(Get-ProcessStarted $PID)) |"
    if (-not (Test-Path $script:CoordinationPath)) {
        Set-Content -Encoding utf8 $script:CoordinationPath @(
            "# Coordination", "",
            "Who holds the build/test lock right now (tools\lock.ps1: shared = builds, relifts, normal test runs, max 2;",
            "exclusive = measurements, benchmarks, soak, oracle runs, alone). Written when the lock is taken, removed when",
            "it is released; lines of processes that are gone are removed by the next taker (lock\lock.log).", "",
            "| who | class | what | since |", "|---|---|---|---|")
    }
    Invoke-CoordinationIO { Add-Content -Encoding utf8 -ErrorAction Stop $script:CoordinationPath $line }
    $env:RC_GLOBAL_LOCK = $Class
    return @{ Gate = $gate; Slots = $held; Line = $line; Mutexes = $owned }
}

function Exit-GlobalLock($l) {
    if ($null -eq $l) { return }
    try {
        Invoke-CoordinationIO {
            $keep = Get-Content -Encoding utf8 -ErrorAction Stop $script:CoordinationPath | Where-Object { $_ -ne $l.Line }
            Set-Content -Encoding utf8 -ErrorAction Stop $script:CoordinationPath $keep
        }
    } catch { Write-Host "coordination.md: could not remove '$($l.Line)'" }
    $env:RC_GLOBAL_LOCK = $null
    Remove-LockRecord
    foreach ($h in $l.Slots) { $h.Close() }
    if ($l.Gate) { $l.Gate.Close() }
    foreach ($m in $l.Mutexes) { try { $m.ReleaseMutex() } catch { }; $m.Dispose() }
}

# powershell -File lock.ps1 status   (dot-sourcing passes no arguments, so this never runs then)
if ($args.Count -and $args[0] -eq "status") { Format-GlobalLockStatus; exit 0 }
