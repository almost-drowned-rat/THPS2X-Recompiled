# stream_check.ps1 - dot-source: what a test run should log first.
#   Get-StreamState     # "" when no game stream is open, else what was found (NVENC sessions, stream clients)
#   Get-DisplayProfile  # the active screens: adapter, monitor, mode
# An open stream (NVENC encodes every frame on the GPU) changes timings and can change the display mode, so test
# reports name it; screenshots compare only within one display setup.

function Get-StreamState {
    $found = @()
    $smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if ($smi) {
        $n = @((& $smi.Source --query-gpu=encoder.stats.sessionCount,encoder.stats.averageFps --format=csv,noheader,nounits 2>$null) -split ",\s*")
        if ($n.Count -ge 2 -and [int]$n[0] -gt 0) { $found += "NVENC sessions $($n[0]) at $($n[1]) fps" }
    }
    $p = @(Get-Process parsecd, moonlight* -ErrorAction SilentlyContinue | ForEach-Object ProcessName | Sort-Object -Unique)
    if ($p.Count) { $found += "stream client running: $($p -join ', ')" }
    return ($found -join "; ")
}

function Get-DisplayProfile {
    Add-Type -AssemblyName System.Windows.Forms
    $screens = @([System.Windows.Forms.Screen]::AllScreens | ForEach-Object {
        "$($_.DeviceName) $($_.Bounds.Width)x$($_.Bounds.Height) primary=$($_.Primary)" })
    $gpu = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | ForEach-Object Name)
    return "display $($screens -join ', ') on $($gpu -join ', ')"
}
