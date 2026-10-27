# dump_png.ps1 - convert renderer frame dumps (RECOMP_GL_DUMP, .bmp) to 4:3 PNGs.
#
#   powershell -ExecutionPolicy Bypass -File tools\dump_png.ps1 <file.bmp | folder> [-Width 640] [-Height 480]
#
# Screenshots are the presented front buffer (640x480 since M4); older dumps of the 1280x480 AA back
# buffer are scaled to 640x480 to look like the window. Truncated dumps (run killed
# while writing) are skipped. The PNG goes next to the BMP; an existing PNG is kept, never overwritten.
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$Width = 640,
    [int]$Height = 480
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$files = if (Test-Path $Path -PathType Container) { Get-ChildItem $Path -Filter *.bmp } else { Get-Item $Path }
foreach ($f in $files) {
    $png = [IO.Path]::ChangeExtension($f.FullName, ".png")
    if (Test-Path -LiteralPath $png) {      # never overwrite (CLAUDE.md Resources: a run's evidence stays as written)
        Write-Output "skip (PNG exists, kept): $([IO.Path]::GetFileName($png))"
        continue
    }
    try {
        $src = [System.Drawing.Image]::FromFile($f.FullName)
    } catch {
        Write-Output "skip (unreadable/truncated): $($f.Name)"
        continue
    }
    $dst = New-Object System.Drawing.Bitmap $Width, $Height
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($src, 0, 0, $Width, $Height)
    $dst.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $dst.Dispose(); $src.Dispose()
    Write-Output $png
}
