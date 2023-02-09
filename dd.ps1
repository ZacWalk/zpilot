# z-pilot dev driver.
#
#   .\dd.ps1 build              configure + build (Release by default)
#   .\dd.ps1 run [args...]      build, then launch the game with args
#   .\dd.ps1 test               build, run the scripted capture, verify the frames
#   .\dd.ps1 clean              delete the build tree
#
# Extra args after `run` are forwarded to z-pilot.exe, e.g. `.\dd.ps1 run -capture`.
# Use `-Config Debug` for a debug build.

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'run', 'test', 'clean', 'help')]
    [string] $Command = 'build',

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]] $Rest,

    [ValidateSet('Debug', 'Release')]
    [string] $Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $root "build\$Config"
$exe = Join-Path $buildDir 'z-pilot.exe'
$script:vsReady = $false

function Fail([string] $message) {
    Write-Host "FAIL  $message" -ForegroundColor Red
    exit 1
}

function Enter-MsvcEnv {
    if ($script:vsReady) { return }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { Fail 'vswhere.exe not found - Visual Studio is required.' }
    $vs = & $vswhere -latest -products * -property installationPath
    if (-not $vs) { Fail 'No Visual Studio installation found.' }

    $script:cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path $script:cmake)) { $script:cmake = 'cmake' }
    $script:ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    if (-not (Test-Path $script:ninja)) {
        $script:ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
    }
    if (-not $script:ninja) { Fail 'ninja not found.' }

    # vcvars silently short circuits when a partial MSVC env is already present.
    Remove-Item Env:VSCMD_VER, Env:VSCMD_ARG_TGT_ARCH, Env:VSCMD_ARG_HOST_ARCH `
        -ErrorAction SilentlyContinue
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
    }
    $script:vsReady = $true
}

function Invoke-Build {
    Enter-MsvcEnv
    & $script:cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" `
        "-DCMAKE_MAKE_PROGRAM=$script:ninja" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
    if ($LASTEXITCODE -ne 0) { Fail 'cmake configure failed.' }
    & $script:cmake --build $buildDir
    if ($LASTEXITCODE -ne 0) { Fail 'build failed.' }
    Write-Host "built  $exe" -ForegroundColor Green
}

function Invoke-Run {
    Invoke-Build
    $args = @($Rest | Where-Object { $_ -ne '--' })
    Write-Host "run    $exe $($args -join ' ')" -ForegroundColor Cyan
    Push-Location $buildDir
    try {
        & $exe @args
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($code -ne 0) { Fail "z-pilot exited with code $code." }
}

# Reads a PNG and reports mean / peak luminance so a black or frozen frame fails.
function Measure-Frame([string] $path) {
    Add-Type -AssemblyName System.Drawing
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
    } finally {
        $bmp.Dispose()
    }
}

function Invoke-Test {
    Invoke-Build

    $shots = Join-Path $buildDir 'shots'
    Remove-Item (Join-Path $shots '*.png') -ErrorAction SilentlyContinue

    Write-Host 'test   scripted capture fly-through' -ForegroundColor Cyan
    $p = Start-Process -FilePath $exe -ArgumentList '-capture' -WorkingDirectory $buildDir -PassThru
    if (-not $p.WaitForExit(120000)) {
        $p.Kill()
        Fail 'capture run hung (>120s).'
    }
    if ($p.ExitCode -ne 0) { Fail "capture run exited with code $($p.ExitCode)." }

    $expected = @('01_title', '02_starmap', '03_briefing', '04_descent', '05_shaft',
                  '06_props', '07_deep', '08_tractor', '09_field', '10_help',
                  '11_starmap2', '12_briefing2', '13_hazards', '14_results',
                  '15_starmap3')
    $failures = 0
    foreach ($name in $expected) {
        $file = Join-Path $shots "$name.png"
        if (-not (Test-Path $file)) {
            Write-Host ("  {0,-12} MISSING" -f $name) -ForegroundColor Red
            $failures++
            continue
        }
        $m = Measure-Frame $file
        # A working frame is never uniformly black, and always has bright HUD strokes.
        $ok = ($m.Peak -gt 60) -and ($m.Mean -gt 1.0) -and ($m.W -ge 320)
        Write-Host ("  {0,-12} {1,4}x{2} mean {3,6:N2} peak {4,3} {5}" -f `
                $name, $m.W, $m.H, $m.Mean, [int]$m.Peak, $(if ($ok) { 'ok' } else { 'BLANK' })) `
            -ForegroundColor $(if ($ok) { 'DarkGray' } else { 'Red' })
        if (-not $ok) { $failures++ }
    }

    if ($failures -gt 0) { Fail "$failures frame check(s) failed." }
    Write-Host "PASS   $($expected.Count) frames rendered  ($shots)" -ForegroundColor Green
}

function Invoke-Clean {
    $tree = Join-Path $root 'build'
    if (Test-Path $tree) {
        Remove-Item $tree -Recurse -Force
        Write-Host "cleaned $tree" -ForegroundColor Green
    } else {
        Write-Host 'nothing to clean' -ForegroundColor DarkGray
    }
}

function Show-Help {
    Get-Content $PSCommandPath | Select-Object -First 10 | ForEach-Object { $_ -replace '^# ?', '' }
}

switch ($Command) {
    'build' { Invoke-Build }
    'run' { Invoke-Run }
    'test' { Invoke-Test }
    'clean' { Invoke-Clean }
    'help' { Show-Help }
}
