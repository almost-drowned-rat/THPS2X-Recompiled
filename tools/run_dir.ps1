# run_dir.ps1 - per-run folders for test and evidence scripts (dot-source it).
#
#   . tools\run_dir.ps1
#   $out = New-RunDir (Join-Path $Private "evidence\m6\options\$(Get-RunStamp)")   # refuses a folder that exists
#   Remove-RunItem (Join-Path $out "data")                                  # only inside a folder made here
#
# Rule (maintainer, 2026-10-26; CLAUDE.md "Resources"): a test or evidence script writes only into a folder it
# created itself for this run. It never deletes or overwrites anything in private\evidence\ or in another run's
# folder. New-RunDir refuses an existing path instead of clearing it; Remove-RunItem deletes only below a folder
# that New-RunDir created in this PowerShell session, never through a link (junction / symlink).
$script:RunDirs = @()

function Get-RunStamp { (Get-Date).ToString("yyyyMMdd_HHmmss") }

function New-RunDir([Parameter(Mandatory = $true)][string]$Path) {
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (Test-Path -LiteralPath $full) {
        throw "run folder exists: $full - every run writes into a new folder (CLAUDE.md Resources); pick a new name"
    }
    New-Item -ItemType Directory -Force -Path $full | Out-Null
    $script:RunDirs += $full
    return $full
}

function Remove-RunItem([Parameter(Mandatory = $true)][string]$Path) {
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $owner = $script:RunDirs | Where-Object { $full -eq $_ -or $full.StartsWith("$_\", [StringComparison]::OrdinalIgnoreCase) }
    if (-not $owner) { throw "not deleting $full - it is not inside a folder this run created (New-RunDir)" }
    if (-not (Test-Path -LiteralPath $full)) { return }
    $links = @(Get-Item -LiteralPath $full -Force) + @(Get-ChildItem -LiteralPath $full -Recurse -Force -ErrorAction SilentlyContinue) |
        Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }
    if ($links) { throw "not deleting $full - it holds a link: $($links[0].FullName)" }
    Remove-Item -LiteralPath $full -Recurse -Force
}
