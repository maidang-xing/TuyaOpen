# Capture COM8 (the AC791 log UART) to a timestamped file.
#
#   .\capture-791.ps1 [-Seconds 120] [-Port COM8] [-Baud 115200]
#
# Writes ..\logs\serial-<port>-<timestamp>.log while streaming. Start it
# BEFORE flashing: the interesting part is usually the boot right after
# `rebooting device`, and a crash drops the USB stack, which used to take the
# log with it.

param(
    [int]$Seconds = 120,
    [string]$Port = 'COM8',
    [int]$Baud = 115200,
    [switch]$Follow
)

$ErrorActionPreference = 'Stop'

$root   = Split-Path $PSScriptRoot -Parent
$logDir = Join-Path $root 'logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log   = Join-Path $logDir "serial-$Port-$stamp.log"

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200
$sp.DtrEnable   = $false
$sp.RtsEnable   = $false
try {
    $sp.Open()
} catch {
    throw "cannot open ${Port}: $($_.Exception.Message)"
}

Write-Host "[capture-791] ${Port}@${Baud} -> $log"

$sw      = New-Object System.IO.StreamWriter($log, $false, [System.Text.Encoding]::UTF8)
$sw.AutoFlush = $true
$end     = (Get-Date).AddSeconds($Seconds)
$pending = New-Object System.Text.StringBuilder
$lastFlush = Get-Date

try {
    while ($Follow -or (Get-Date) -lt $end) {
        try {
            $chunk = $sp.ReadExisting()
            if ($chunk) {
                [void]$pending.Append($chunk)
            }
        } catch {
            # transient read timeout, keep going
        }

        if ($pending.Length -gt 0 -and ((Get-Date) - $lastFlush).TotalMilliseconds -ge 200) {
            $text = $pending.ToString()
            [void]$pending.Clear()
            $sw.Write($text)
            $lastFlush = Get-Date
            # Mirror the lines that matter to the console.
            foreach ($line in ($text -split "`n")) {
                if ($line -match 'PREVIEW|panic|Panic|exception|Exception|assert|Assert|fault|Fault|ERROR|\[camera\]|\[I2CSCAN\]') {
                    Write-Host $line.TrimEnd()
                }
            }
        }
        Start-Sleep -Milliseconds 50
    }
} finally {
    if ($pending.Length -gt 0) { $sw.Write($pending.ToString()) }
    $sw.Close()
    if ($sp.IsOpen) { $sp.Close() }
}

Write-Host "[capture-791] done, log: $log"
