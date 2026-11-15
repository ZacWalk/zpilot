# Runs zpilot's scripted capture and checks every reference frame it writes.
# A missing or black frame is evidence that the renderer broke; a frame on a different
# screen than its name says comes back as the exit code, and means the script did.
param(
    [Parameter(Mandatory)] [string] $Exe,
    [Parameter(Mandatory)] [string] $WorkDir,
    [switch] $Headless
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
$main = Join-Path $PSScriptRoot '..\src\Main.cpp'
$expected = @(Select-String -Path $main -Pattern 'L"shots/([^"]+)\.png"' -AllMatches |
    ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value })
if ($expected.Count -eq 0) {
    Write-Host "FAIL  no shots found in $main"
    exit 1
}

$run = Join-Path $WorkDir ('captures\run-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $run -ErrorAction Stop
$shots = Join-Path $run 'shots'

# The third act ends a run, so the capture writes a save file: its own, beside
# the shots. The player's log in %APPDATA% must come through untouched.
$runProfile = Join-Path $shots 'profile.dat'
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

$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = (Resolve-Path -LiteralPath $Exe).Path
$start.ArgumentList.Add($(if ($Headless) { '--capture-headless' } else { '-capture' }))
$start.WorkingDirectory = $run
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$p = [Diagnostics.Process]::new()
$p.StartInfo = $start
$null = $p.Start()
$stdout = $p.StandardOutput.ReadToEndAsync()
$stderr = $p.StandardError.ReadToEndAsync()
# A desktop draws the 1700 frames in 10-20 seconds. A runner without a GPU draws
# the window on a software adapter instead, which is slower.
if (-not $p.WaitForExit(600000)) {
    $p.Kill($true)
    $p.WaitForExit()
    Write-Host 'FAIL  capture run hung (more than 600 s)'
    exit 1
}
$output = $stdout.GetAwaiter().GetResult()
$errors = $stderr.GetAwaiter().GetResult()
Write-Host $output
if ($errors) { [Console]::Error.WriteLine($errors) }
$exitCode = $p.ExitCode
$p.Dispose()
# From 100 up, the exit code names the first shot that caught the wrong screen
# or planet (kWrongShotExit in src/Main.cpp) - a key the script pressed did not land.
$wrong = $exitCode - 100
if ($wrong -ge 0 -and $wrong -lt $expected.Count) {
    Write-Host "FAIL  $($expected[$wrong]) is not on the screen or planet kShots names for it ($shots)"
    exit 1
}
if ($exitCode -ne 0) {
    Write-Host "FAIL  capture run exited with code $exitCode"
    exit 1
}

$failures = 0
$checks = 0
if ($Headless) {
    $checks++
    if (-not $output.Contains('capture: hidden window verified')) {
        Write-Host 'FAIL  capture did not verify its hidden native window'
        $failures++
    }
}
$checks++
if (-not (Test-Path $runProfile) -or (Get-Item $runProfile).Length -le 16) {
    Write-Host "  profile.dat    MISSING - the third act wrote no save file to $shots"
    $failures++
}
foreach ($profile in $playerProfiles) {
    $checks++
    if ((Get-Stamp $profile) -ne $playerBefore[$profile]) {
        Write-Host "  profile.dat    the capture wrote the player's save file, $profile"
        $failures++
    }
}
foreach ($name in $expected) {
    $checks++
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
    Write-Host "FAIL  $checks checks, $failures failed ($shots)"
    exit 1
}
Write-Host "PASS  $checks checks, 0 failed; $($expected.Count) frames checked, save file kept in $shots"
