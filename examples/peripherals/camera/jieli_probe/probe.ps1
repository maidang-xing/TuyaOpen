# Jieli AC791 camera probe driver.
#
# One camera path at a time: J1 DVP (camera0/video0) or J4 SPI
# (camera1/video1+spi2). The board wires PA2/PA3/PA4 and PH0-PH3 to both
# connectors, so the Kconfig choice is exclusive and the two images are built
# separately.
#
# Examples:
#   .\probe.ps1 -Path dvp -Action build
#   .\probe.ps1 -Path dvp -Action flash -Port COM8
#   .\probe.ps1 -Path dvp -Action capture -Port COM8 -Seconds 45
#   .\probe.ps1 -Path dvp -Action archive
#
# flash/capture touch a real serial port. Only run them while the board and the
# port are yours; the archive action keeps the built image and its SHA256 under
# <worktree>\.artifacts\jieli-camera so a rollback copy survives a clean build.

param(
    [ValidateSet('dvp', 'spi')]
    [string]$Path = 'dvp',

    [ValidateSet('build', 'hash', 'flash', 'capture', 'archive')]
    [string]$Action = 'build',

    [string]$Port = 'COM8',
    [int]$Seconds = 45,
    [string]$Log = ''
)

$ErrorActionPreference = 'Stop'

$ExampleDir = $PSScriptRoot
$Root = (Resolve-Path (Join-Path $ExampleDir '..\..\..\..')).Path
if (-not $env:OPEN_SDK_PYTHON) {
    $env:OPEN_SDK_PYTHON = Join-Path $Root '..\..\.venv\Scripts\python.exe'
}
$env:OPEN_SDK_ROOT = $Root
$Python = $env:OPEN_SDK_PYTHON
$TosPy = Join-Path $Root 'tos.py'
$ConfigName = "JIELI_AC79_DevKitBoard_$Path.config"
$ArtifactDir = Join-Path $Root '.artifacts\jieli-camera'
$Bin = Join-Path $ExampleDir 'dist\jieli_probe_1.0.0\jieli_probe_QIO_1.0.0.bin'

function Get-BinHash {
    if (-not (Test-Path $Bin)) {
        throw "no built image at $Bin; run -Action build first"
    }
    (Get-FileHash -Algorithm SHA256 $Bin).Hash.ToLower()
}

function Invoke-Build {
    Push-Location $ExampleDir
    try {
        & $Python $TosPy config choice -c $ConfigName
        if ($LASTEXITCODE -ne 0) { throw "config choice failed" }
        & $Python $TosPy build
        if ($LASTEXITCODE -ne 0) { throw "build failed" }
    } finally {
        Pop-Location
    }
    Write-Host ("SHA256 {0}  {1}" -f (Get-BinHash), $Bin)
}

switch ($Action) {
    'build' { Invoke-Build }
    'hash'  { Write-Host ("SHA256 {0}  {1}" -f (Get-BinHash), $Bin) }
    'archive' {
        $hash = Get-BinHash
        New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null
        $dest = Join-Path $ArtifactDir ("jieli_probe_{0}_{1}.bin" -f $Path, $hash.Substring(0, 8))
        Copy-Item -Force $Bin $dest
        $hash | Out-File -Encoding ascii "$dest.sha256"
        Write-Host ("archived {0}`nSHA256 {1}" -f $dest, $hash)
    }
    'flash' {
        if (-not [System.IO.Ports.SerialPort]::GetPortNames() -contains $Port) {
            throw "$Port is not present; check the cable before flashing"
        }
        $hash = Get-BinHash
        Write-Host ("flashing {0} (SHA256 {1}) to {2}" -f $Bin, $hash, $Port)
        Push-Location $ExampleDir
        try {
            & $Python $TosPy flash -p $Port
            if ($LASTEXITCODE -ne 0) { throw "flash failed" }
        } finally {
            Pop-Location
        }
    }
    'capture' {
        if (-not $Log) {
            $Log = Join-Path $ArtifactDir ("capture_{0}_{1:yyyyMMdd-HHmmss}.log" -f $Path, (Get-Date))
        }
        New-Item -ItemType Directory -Force -Path (Split-Path $Log) | Out-Null
        $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
        $sp.ReadTimeout = 1000
        $sp.Open()
        Write-Host "capturing $Port for $Seconds s -> $Log"
        $deadline = (Get-Date).AddSeconds($Seconds)
        $sw = [Diagnostics.Stopwatch]::StartNew()
        try {
            while ((Get-Date) -lt $deadline) {
                try {
                    $line = $sp.ReadLine()
                    $stamp = $sw.ElapsedMilliseconds
                    Add-Content -Encoding utf8 $Log ("[{0,8} ms] {1}" -f $stamp, $line.TrimEnd())
                    Write-Host $line
                } catch [TimeoutException] {
                }
            }
        } finally {
            $sp.Close()
        }
    }
}
