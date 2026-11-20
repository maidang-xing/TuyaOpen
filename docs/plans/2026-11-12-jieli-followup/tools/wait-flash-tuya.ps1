# Wait for the JIELI loader, flash one of the TuyaOpen SPI-preview images,
# then capture the 115200 log.
#
#   .\wait-flash-tuya.ps1 -Image spilineirqdma [-Minutes 120] [-Seconds 90]
#
# Detection only until the loader appears; the flash itself uses
# flash-791.ps1, so the image is SHA-checked against images\ first.

param(
    [Parameter(Mandatory = $true)]
    [string]$Image,
    [int]$Minutes = 120,
    [int]$Seconds = 90
)

$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot

Write-Host "[wait-tuya] waiting up to $Minutes min for WL82 UBOOT1.00 ..."
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
    Write-Host '[wait-tuya] timeout: board never entered UPDATE mode'
    exit 2
}

Write-Host "[wait-tuya] loader present, flashing $Image ..."
& (Join-Path $tools 'flash-791.ps1') -Image $Image
if ($LASTEXITCODE -ne 0) {
    Write-Host "[wait-tuya] flash failed (exit $LASTEXITCODE)"
    exit 3
}

Write-Host "[wait-tuya] capture ${Seconds}s at 115200 ..."
& (Join-Path $tools 'capture-791.ps1') -Seconds $Seconds -Baud 115200
Write-Host '[wait-tuya] done'
