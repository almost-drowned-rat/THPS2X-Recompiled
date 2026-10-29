# lock.ps1 - the build/test lock of every project on this machine, two classes (maintainer, 2026-10-16).
#
#   . "$PSScriptRoot\lock.ps1"
#   $l = Enter-GlobalLock -Class shared    -Who "A (THPS 2X)" -What "build thps2x_recomp"
#   $l = Enter-GlobalLock -Class exclusive -Who "A (THPS 2X)" -What "level sweep with frame times"
#   $l = Enter-GlobalLock -Class shared -Heavy -Who "A (THPS 2X)" -What "full build thps2x_recomp"
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
# Heavy jobs (maintainer, 2026-10-29, memory rule): relifts, full builds and GPU-heavy runs (game, timing, emulator)
# take the lock with -Heavy; every exclusive request is heavy by itself. At most ONE heavy holder across all agents
# (named mutex Global\rc_lock_heavy), and a heavy request gets the lock only while at least 12 GB of physical RAM
# are available (Windows "available" = free + standby, as Task Manager shows it) at the moment it is taken; the
# value is logged in lock\lock.log and on the coordination.md line. Order, so nothing can deadlock: the heavy mutex
# first, then the RAM check, then the gate and slots; if the RAM fell below 12 GB while the class lock was waited
# for, the class lock is given back (the heavy mutex kept) and the RAM is waited for again. A heavy request polls
# the RAM (every 5 s) only while it already holds the heavy mutex - so never during another heavy job or an
# exclusive series; before that it sleeps in the OS. The RAM wait counts against the same timeout. A heavy step
# nested in an outer shared lock that was not taken with -Heavy is refused (take the outer lock with -Heavy); a
# heavy request from a session that already holds the heavy slot in another process is refused at once.
#
# Protocol version and single source (maintainer, 2026-10-25): this file lives in the toolkit (tools\lock.ps1);
# repos get it only through the toolkit's tools\copy_lock.ps1. The machine's lock carries the protocol version in
# lock\protocol (written by copy_lock.ps1, which switches over only while nobody holds or waits for the lock);
# every record carries it too. A lock.ps1 whose protocol differs from the machine's is refused with a clear
# message. Protocol 1 = every version before this one (no lock\protocol file); protocol 2 = 2026-10-25 (records,
# OS wait); this file is protocol 3 (heavy jobs + RAM rule, 2026-10-29).
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
# Files (<root>\tools\private\, default below), all held as open handles, so a holder that dies releases them with its process:
#   build.lock   the gate: an exclusive holder keeps it from the moment it starts waiting until it is done; a
#                shared requester only passes it (open, take a slot, close). An older lock.ps1 that holds
#                build.lock for everything therefore counts as exclusive.
#   lock\slot0, lock\slot1   one per shared holder; exclusive takes both.
#   lock\who\<pid>.json      one per holder or waiter: who (agent), session, pid + start time, class, what
#                            (purpose), state (waiting/holding/backoff), since, protocol, heavy, note (what a
#                            heavy waiter waits for / the RAM at the take). Records of processes
#                            that are gone are ignored and deleted by the next reader.
#   lock\protocol            the machine's lock protocol version (copy_lock.ps1).
#   lock\lock.log            every take-over of a stale coordination line, every exclusive give-up, every switch-over.
# While held, one line "who | class | what | since (pid, started)" stands in <root>\tools\coordination.md;
# lines of processes that are gone are removed (and logged) whenever anybody takes the lock.
# Tests use their own copy of everything: RC_LOCK_ROOT (files), RC_LOCK_COORDINATION, RC_LOCK_NS (mutex names);
# tools\test_lock.ps1. Only with RC_LOCK_ROOT set (a private test lock) does lock.ps1 read a faked available RAM
# from RC_LOCK_TEST_FREE_MB_FILE and the poll interval from RC_LOCK_TEST_RAM_POLL_MS; the real lock has no knob
# for the 12 GB.
# The defaults follow the folder layout of tools\paths.ps1: <root>\tools\private (lock) and <root>\tools\coordination.md,
# with <root> derived from where this file lives (<root>\projects\<game>\<repo>\tools).
$script:LockLayoutRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$script:GlobalLockRoot = if ($env:RC_LOCK_ROOT) { $env:RC_LOCK_ROOT } else { Join-Path $script:LockLayoutRoot "tools\private" }
$script:GlobalLockDir = Join-Path $script:GlobalLockRoot "lock"
$script:GlobalLockGate = Join-Path $script:GlobalLockRoot "build.lock"
$script:GlobalLockWho = Join-Path $script:GlobalLockDir "who"
$script:GlobalLockNs = if ($env:RC_LOCK_NS) { $env:RC_LOCK_NS } else { "rc_lock" }
$script:CoordinationPath = if ($env:RC_LOCK_COORDINATION) { $env:RC_LOCK_COORDINATION } else {
    Join-Path $script:LockLayoutRoot "tools\coordination.md" }
$script:ExclusiveWaitMin = if ($env:RC_LOCK_EXCLUSIVE_WAIT_MIN) { [double]$env:RC_LOCK_EXCLUSIVE_WAIT_MIN } else { 15 }
$script:SharedWaitMin = 240
$script:BackoffMin = if ($env:RC_LOCK_BACKOFF_MIN) { @($env:RC_LOCK_BACKOFF_MIN -split ',' | ForEach-Object {
    [double]::Parse($_.Trim(), [Globalization.CultureInfo]::InvariantCulture) }) } else { @(5, 10, 20) }
$script:LockProtocol = 3
$script:GlobalLockProtocolFile = Join-Path $script:GlobalLockDir "protocol"
$script:HeavyFreeGB = 12
$script:RamPollMs = if ($env:RC_LOCK_ROOT -and $env:RC_LOCK_TEST_RAM_POLL_MS) { [int]$env:RC_LOCK_TEST_RAM_POLL_MS } else { 5000 }
Add-Type -AssemblyName Microsoft.VisualBasic

# Available physical RAM in GB (GlobalMemoryStatusEx: free + standby). A private test lock may fake it.
function Get-LockFreeRamGB {
    if ($env:RC_LOCK_ROOT -and $env:RC_LOCK_TEST_FREE_MB_FILE -and (Test-Path -LiteralPath $env:RC_LOCK_TEST_FREE_MB_FILE)) {
        return [double]::Parse((Get-Content -Raw -LiteralPath $env:RC_LOCK_TEST_FREE_MB_FILE).Trim(),
                               [Globalization.CultureInfo]::InvariantCulture) / 1024
    }
    (New-Object Microsoft.VisualBasic.Devices.ComputerInfo).AvailablePhysicalMemory / 1GB
}

function Format-LockGB([double]$gb) { $gb.ToString("0.0", [Globalization.CultureInfo]::InvariantCulture) }

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

function Write-LockRecord([string]$State, [string]$Class, [string]$Who, [string]$What, [datetime]$Since,
                          [bool]$Heavy = $false, [string]$Note = "") {
    New-Item -ItemType Directory -Force $script:GlobalLockWho | Out-Null
    $r = [ordered]@{ who = $Who; session = (Get-LockSession); pid = $PID; started = (Get-ProcessStarted $PID);
                     class = $Class; what = $What; state = $State; since = (Get-LockTime $Since "s");
                     protocol = $script:LockProtocol; heavy = $Heavy; note = $Note }
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
                           Pid = [int]$r.pid; Protocol = $(if ($r.protocol) { [int]$r.protocol } else { 1 }); Source = "record";
                           Heavy = [bool]$r.heavy; Note = [string]$r.note }
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
                                       Session = "?"; Pid = $p; Protocol = 1; Source = "coordination (older lock.ps1, no record)";
                                       Heavy = $false; Note = "" }
        }
    }
    @($out | Sort-Object State, Since)
}

function Format-GlobalLockStatus {
    $st = @(Get-GlobalLockStatus)
    "lock status $(Get-LockTime (Get-Date)) ($($script:CoordinationPath)), machine protocol $(Get-MachineLockProtocol), this lock.ps1 protocol $($script:LockProtocol):"
    if (-not $st.Count) { "  free - no holders, no waiters"; return }
    foreach ($s in $st) {
        "  {0,-7} {1,-15} {2} | {3} | since {4} ({5} min) | session {6}, pid {7}{8}{9}" -f $s.State,
            "$($s.Class)$(if ($s.Heavy) { ' heavy' })", $s.Who,
            $s.What, (Get-LockTime $s.Since "yyyy-MM-dd HH:mm:ss"),
            $s.Minutes.ToString("0.0", [Globalization.CultureInfo]::InvariantCulture), $s.Session, $s.Pid,
            $(if ($s.Note) { " | $($s.Note)" } else { "" }),
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
          [double[]]$BackoffMin = $null,                         # exclusive retries; $null: 5, 10, 20 min
          [switch]$Heavy)                                        # relift, full build, game/timing/emulator run
    $isHeavy = $Heavy -or $Class -eq "exclusive"
    if ($env:RC_GLOBAL_LOCK) {                                   # an outer step holds it
        if ($isHeavy -and $env:RC_GLOBAL_LOCK -ne "exclusive" -and $env:RC_GLOBAL_LOCK_HEAVY -ne "1") {
            throw ("lock: heavy step '$What' nested in a shared lock that was not taken with -Heavy - refused (memory " +
                   "rule: one heavy job at a time, 12 GB available RAM). Take the outer lock with -Heavy " +
                   "(run_locked.ps1 -Heavy), then the step runs under it.")
        }
        if ($Class -eq "exclusive" -and $env:RC_GLOBAL_LOCK -ne "exclusive") {
            Write-Host "lock: exclusive step nested in a shared one ($What) - runs under the outer shared heavy lock"
        }
        return $null
    }
    Assert-LockProtocol
    if ($TimeoutMin -lt 0) { $TimeoutMin = if ($Class -eq "exclusive") { $script:ExclusiveWaitMin } else { $script:SharedWaitMin } }
    if ($Class -ne "exclusive") { return Enter-GlobalLockOnce $Who $What $Class $TimeoutMin $isHeavy }
    if ($null -eq $BackoffMin) { $BackoffMin = $script:BackoffMin }
    $n = $BackoffMin.Count + 1                                   # first attempt + one retry per backoff
    $tag = "exclusive request of $Who ('$What', session $(Get-LockSession), pid $PID)"
    for ($a = 1; ; $a++) {
        try {
            $l = Enter-GlobalLockOnce $Who $What $Class $TimeoutMin $true
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
            Write-LockRecord "backoff" $Class $Who $What (Get-Date) $true
            Start-Sleep -Milliseconds ([int]($b * 60000))       # holds nothing meanwhile
            Remove-LockRecord
        }
    }
}

# The gate and slots for one class, mutexes and files; $null on the deadline (everything taken given back).
function Enter-LockClass([string]$Class, [datetime]$deadline) {
    $slots = @((Join-Path $script:GlobalLockDir "slot0"), (Join-Path $script:GlobalLockDir "slot1"))
    $ns = $script:GlobalLockNs
    $mGate = New-Object System.Threading.Mutex($false, "Global\${ns}_gate")
    $mSlots = [System.Threading.Mutex[]]@((New-Object System.Threading.Mutex($false, "Global\${ns}_slot0")),
                                          (New-Object System.Threading.Mutex($false, "Global\${ns}_slot1")))
    $owned = @()                                                 # mutexes this call owns
    $undo = { foreach ($m in $owned) { try { $m.ReleaseMutex() } catch { } } }   # withdraws an exclusive place in the queue
    if (-not (Wait-LockMutex $mGate (Get-Date))) {               # not free right now: say so, then sleep in the OS
        Write-Host "waiting for the global lock ($Class; see $($script:CoordinationPath))"
        if (-not (Wait-LockMutex $mGate $deadline)) { & $undo; return $null }
    }
    $owned += $mGate
    $mine = @()                                                  # slot indices this call owns
    if ($Class -eq "exclusive") {
        foreach ($i in 0, 1) {                                   # the gate keeps new shared holders out meanwhile
            if (-not (Wait-LockMutex $mSlots[$i] $deadline)) { & $undo; return $null }
            $owned += $mSlots[$i]; $mine += $i
        }
    } else {
        $i = Wait-AnyLockMutex $mSlots $deadline
        if ($i -eq [System.Threading.WaitHandle]::WaitTimeout) { & $undo; return $null }
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
            & $undo; return $null
        }
        if (-not $said) { Write-Host "lock: a holder with an older lock.ps1 (files only) - retrying every 5 s"; $said = $true }
        Start-Sleep -Seconds 5
    }
    @{ Gate = $gate; Slots = $held; Mutexes = $owned }
}

function Exit-LockClass($c) {
    foreach ($h in $c.Slots) { $h.Close() }
    if ($c.Gate) { $c.Gate.Close() }
    foreach ($m in $c.Mutexes) { try { $m.ReleaseMutex() } catch { }; $m.Dispose() }
}

# A heavy request with the heavy mutex: available RAM >= 12 GB, polled until the deadline; $null on the deadline.
function Wait-LockFreeRam([datetime]$deadline, [scriptblock]$onWait) {
    $said = $false
    while ($true) {
        $f = Get-LockFreeRamGB
        if ($f -ge $script:HeavyFreeGB) { return $f }
        if ((Get-Date) -gt $deadline) { return $null }
        if (-not $said) { & $onWait $f; $said = $true }
        Start-Sleep -Milliseconds $script:RamPollMs
    }
}

# One attempt; a timeout throws System.TimeoutException after leaving the queue.
function Enter-GlobalLockOnce([string]$Who, [string]$What, [string]$Class, [double]$TimeoutMin, [bool]$Heavy = $false) {
    New-Item -ItemType Directory -Force $script:GlobalLockDir | Out-Null
    $me = Get-LockSession
    if ($Class -eq "exclusive") {                                # self-deadlock guard: never wait on our own lock
        $own = @(Get-LockRecords | Where-Object { $_.session -eq $me -and [int]$_.pid -ne $PID -and $_.state -eq "holding" })
        if ($own.Count) {
            $o = $own[0]
            throw ("lock: exclusive request refused (self-deadlock) - this session ($me) already holds the $($o.class) " +
                   "lock in pid $($o.pid) ('$($o.what)', since $($o.since)); the request would wait on itself. " +
                   "Release that lock first (stop that process or let it call Exit-GlobalLock), then retry.")
        }
    }
    if ($Heavy) {                                                # the same for the heavy slot
        $own = @(Get-LockRecords | Where-Object { $_.session -eq $me -and [int]$_.pid -ne $PID -and $_.state -eq "holding" -and $_.heavy })
        if ($own.Count) {
            $o = $own[0]
            throw ("lock: heavy request refused (self-deadlock) - this session ($me) already holds the heavy slot " +
                   "in pid $($o.pid) ('$($o.what)', since $($o.since)); the request would wait on itself. " +
                   "Run the step under that lock, or release it first, then retry.")
        }
    }
    $waitStart = Get-Date
    $deadline = $waitStart.AddMinutes($TimeoutMin)
    $ns = $script:GlobalLockNs
    $owned = @()                                                 # the heavy mutex, once this call owns it
    $why = "still not free"
    $fail = {
        foreach ($m in $owned) { try { $m.ReleaseMutex() } catch { }; $m.Dispose() }
        Remove-LockRecord
        throw [System.TimeoutException]::new("global lock ($Class$(if ($Heavy) { ', heavy' })) $why after $TimeoutMin min (see coordination.md)")
    }
    Write-LockRecord "waiting" $Class $Who $What $waitStart $Heavy
    if ($Heavy) {                                                # first, so nothing waits for it while holding a slot
        $mHeavy = New-Object System.Threading.Mutex($false, "Global\${ns}_heavy")
        if (-not (Wait-LockMutex $mHeavy (Get-Date))) {
            $hv = @(Get-GlobalLockStatus | Where-Object { $_.Heavy -and $_.State -eq "holding" -and $_.Pid -ne $PID } |
                    ForEach-Object { "$($_.Who) '$($_.What)'" }) -join "; "
            Write-LockRecord "waiting" $Class $Who $What $waitStart $Heavy "waits for the heavy slot ($(if ($hv) { $hv } else { 'held' }))"
            Write-Host "waiting for the heavy slot (one heavy job at a time: $(if ($hv) { $hv } else { 'held' }); up to $TimeoutMin min)"
            if (-not (Wait-LockMutex $mHeavy $deadline)) { $mHeavy.Dispose(); $why = "heavy slot not free"; & $fail }
        }
        $owned += $mHeavy
    }
    $onWait = {
        param($f)
        $n = "waits for RAM: $(Format-LockGB $f) GB available < $($script:HeavyFreeGB) GB"
        Write-LockRecord "waiting" $Class $Who $What $waitStart $Heavy $n
        Write-Host "lock: heavy request $n (up to $TimeoutMin min)"
    }
    while ($true) {
        if ($Heavy) {
            $free = Wait-LockFreeRam $deadline $onWait
            if ($null -eq $free) { $why = "less than $($script:HeavyFreeGB) GB RAM available"; & $fail }
        }
        $c = Enter-LockClass $Class $deadline
        if (-not $c) { & $fail }
        if (-not $Heavy) { break }
        $free = Get-LockFreeRamGB                                # at the moment of the take
        if ($free -ge $script:HeavyFreeGB) { break }
        Write-LockLog "heavy request of $Who ('$What', pid $PID): available RAM fell to $(Format-LockGB $free) GB while the $Class lock was waited for - lock given back, waiting for RAM"
        Exit-LockClass $c
    }
    $owned += $c.Mutexes
    $gate = $c.Gate; $held = $c.Slots
    Clear-StaleCoordination
    $now = Get-Date
    $ram = if ($Heavy) { "$(Format-LockGB $free) GB available at the take" } else { "" }
    Write-LockRecord "holding" $Class $Who $What $now $Heavy $ram
    if ($Heavy) { Write-LockLog "heavy $Class lock taken by $Who ('$What', pid $PID): $ram" }
    $tagged = if ($Heavy) { "$What [heavy, $(Format-LockGB $free) GB RAM available]" } else { $What }
    $line = "| $Who | $Class | $tagged | since $(Get-LockTime $now 'yyyy-MM-dd HH:mm') (pid $PID, started $(Get-ProcessStarted $PID)) |"
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
    $env:RC_GLOBAL_LOCK_HEAVY = if ($Heavy) { "1" } else { $null }
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
    $env:RC_GLOBAL_LOCK_HEAVY = $null
    Remove-LockRecord
    Exit-LockClass $l
}

# powershell -File lock.ps1 status   (dot-sourcing passes no arguments, so this never runs then)
if ($args.Count -and $args[0] -eq "status") { Format-GlobalLockStatus; exit 0 }
