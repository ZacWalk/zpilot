# Runs zpilot's scripted capture and checks every reference frame it writes.
# The capture exits cleanly even when nothing was drawn, so a missing or black
# frame is the evidence that the renderer broke; a frame caught on a different
# screen than its name says comes back as the exit code, and means the script did.
param(
    [Parameter(Mandatory)] [string] $Exe,
    [Parameter(Mandatory)] [string] $WorkDir
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# Mean and peak luminance over a sparse grid of pixels.
function Measure-Frame([string] $path) {
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $sum = 0.0; $peak = 0; $n = 0
        for ($y = 4; $y -lt $bmp.Height; $y += 17) {
            for ($x = 4; $x -lt $bmp.Width; $x += 17) {
                $p = $bmp.GetPixel($x, $y)
                $l = 0.2126 * $p.R + 0.7152 * $p.G + 0.0722 * $p.B
                $sum += $l
                if ($l -gt $peak) { $peak = $l }
                $n++
            }
        }
        [pscustomobject]@{ Mean = $sum / $n; Peak = $peak; W = $bmp.Width; H = $bmp.Height }
    }
    finally {
        $bmp.Dispose()
    }
}

# kShots in src/Main.cpp is the list of frames; read it rather than repeat it.
$main = Join-Path $PSScriptRoot '../src/Main.cpp'
$expected = @(Select-String -Path $main -Pattern 'L"shots/([^"]+)\.png"' -AllMatches |
    ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value })
if ($expected.Count -eq 0) {
    Write-Host "FAIL  no shots found in $main"
    exit 1
}

$shots = Join-Path $WorkDir 'shots'
Remove-Item (Join-Path $shots '*.png') -ErrorAction SilentlyContinue

# The third act ends a run, so the capture writes a save file: its own, beside
# the shots. The player's log in %APPDATA% must come through untouched.
$runProfile = Join-Path $shots 'profile.dat'
Remove-Item $runProfile -ErrorAction SilentlyContinue
$playerProfiles = @(
    (Join-Path $env:APPDATA 'zpilot\profile.dat'),
    (Join-Path $env:APPDATA 'z-pilot\profile.dat')
)
function Get-Stamp([string] $path) {
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        '{0}:{1}' -f (Get-Item -LiteralPath $path).LastWriteTimeUtc.Ticks, (Get-FileHash -LiteralPath $path).Hash
    } else { $null }
}
$playerBefore = @{}
foreach ($profile in $playerProfiles) { $playerBefore[$profile] = Get-Stamp $profile }

$p = Start-Process -FilePath $Exe -ArgumentList '-capture' -WorkingDirectory $WorkDir -PassThru
$null = $p.Handle   # without a cached handle, ExitCode is unreadable after a non-waiting start
# A GPU renders the 1700 frames in about half a minute. A CI runner has none, so
# Direct3D renders them in software, which has taken from two to four minutes.
if (-not $p.WaitForExit(600000)) {
    $p.Kill()
    Write-Host 'FAIL  capture run hung (more than 600 s)'
    exit 1
}
# From 100 up, the exit code names the first shot that caught the wrong screen
# or planet (kWrongShotExit in src/Main.cpp) - a key the script pressed did not land.
$wrong = $p.ExitCode - 100
if ($wrong -ge 0 -and $wrong -lt $expected.Count) {
    Write-Host "FAIL  $($expected[$wrong]) is not on the screen or planet kShots names for it ($shots)"
    exit 1
}
if ($p.ExitCode -ne 0) {
    Write-Host "FAIL  capture run exited with code $($p.ExitCode)"
    exit 1
}

$failures = 0
if (-not (Test-Path $runProfile) -or (Get-Item $runProfile).Length -le 16) {
    Write-Host "  profile.dat    MISSING - the third act wrote no save file to $shots"
    $failures++
}
foreach ($profile in $playerProfiles) {
    if ((Get-Stamp $profile) -ne $playerBefore[$profile]) {
        Write-Host "  profile.dat    the capture wrote the player's save file, $profile"
        $failures++
    }
}
foreach ($name in $expected) {
    $file = Join-Path $shots "$name.png"
    if (-not (Test-Path $file)) {
        Write-Host ('  {0,-14} MISSING' -f $name)
        $failures++
        continue
    }
    $m = Measure-Frame $file
    # A working frame is never uniformly black, and always has bright HUD strokes.
    $ok = ($m.Peak -gt 60) -and ($m.Mean -gt 1.0) -and ($m.W -ge 320)
    Write-Host ('  {0,-14} {1,4}x{2} mean {3,6:N2} peak {4,3} {5}' -f
        $name, $m.W, $m.H, $m.Mean, [int]$m.Peak, $(if ($ok) { 'ok' } else { 'BLANK' }))
    if (-not $ok) { $failures++ }
}

if ($failures -gt 0) {
    Write-Host "FAIL  $failures checks failed ($shots)"
    exit 1
}
Write-Host "PASS  $($expected.Count) frames rendered, save file kept in $shots"
