# Flash one of the TAL audio images and capture its boot log in one step.
#
#   .\verify-audio-791.ps1 -Image audiovad [-Seconds 60]
#
# Expects the board to be in WL82 UBOOT1.00 (or U-disk) mode, like
# flash-791.ps1. The serial capture starts first so the interesting part --
# the boot after `rebooting device` -- is not missed, then the filtered
# lines ([RNN_VAD], [TAL_KWS], PR_* notices, panics) are printed.

param(
    [ValidateSet('audiovad', 'audiokws')]
    [string]$Image = 'audiovad',
    [int]$Seconds = 60,
    [string]$Port = 'COM8',
    [int]$Baud = 115200
)

$ErrorActionPreference = 'Stop'

$cap = Start-Job -ScriptBlock {
    param($dir, $sec, $port, $baud)
    & (Join-Path $dir 'capture-791.ps1') -Seconds $sec -Port $port -Baud $baud
} -ArgumentList $PSScriptRoot, ($Seconds + 30), $Port, $Baud

Start-Sleep -Seconds 1
& (Join-Path $PSScriptRoot 'flash-791.ps1') -Image $Image
Wait-Job $cap | Out-Null
Receive-Job $cap | Out-Null
Remove-Job $cap

$log = Get-ChildItem (Join-Path (Split-Path $PSScriptRoot -Parent) 'logs') -Filter "serial-$Port-*.log" |
       Sort-Object LastWriteTime | Select-Object -Last 1
Write-Host "[verify-audio-791] log: $($log.FullName)"
Write-Host '----- interesting lines -----'
Select-String -Path $log.FullName -Pattern 'RNN_VAD|TAL_KWS|example_vad|example_kws|VAD status|panic|Panic|exception|stackoverflow|fault|system reset reason|Device online' |
    ForEach-Object { $_.Line }
