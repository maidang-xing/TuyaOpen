# Wait for the AC791 to appear as a JIELI loader, then flash the vendor image
# and capture the boot log. Built for the vendor demo, which has no
# reboot-to-loader hook: the board must be put into UPDATE mode by hand.
#
#   .\wait-and-flash-vendor.ps1 [-Minutes 15]
#
# The flash uses ..\tools\flash-vendor-791.ps1 (staged app.bin + audlogo/ui_res),
# then captures COM8 at 1 Mbaud for 90 s into ..\logs\serial-*.log.

param(
    [int]$Minutes = 15,
    [string]$ComPort = 'COM8',
    [int]$Baud = 1000000
)

$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root  = Split-Path $tools -Parent

Write-Host "[wait] waiting up to $Minutes min for WL82 UBOOT1.00 ..."
$deadline = (Get-Date).AddMinutes($Minutes)
$loader = $null
while ((Get-Date) -lt $deadline) {
    $loader = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -match 'WL82 UBOOT|JIELI' } |
        Select-Object -First 1
    if ($loader) { break }
    Start-Sleep -Seconds 2
}
if (-not $loader) {
    Write-Host '[wait] timeout: board never entered UPDATE mode'
    exit 2
}

Write-Host "[wait] loader present: $($loader.FriendlyName)"
& (Join-Path $tools 'flash-vendor-791.ps1')

Write-Host '[wait] starting 90 s capture on the log UART ...'
& (Join-Path $tools 'capture-791.ps1') -Seconds 90 -Port $ComPort -Baud $Baud
Write-Host '[wait] done'
