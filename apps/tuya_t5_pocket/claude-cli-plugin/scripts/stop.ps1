Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDir 'common.ps1')

$pidValue = Get-DaemonPid
$pidFile = Get-PidFile

if ($null -eq $pidValue) {
    Write-Host 'tuya-pocket-buddy: not running'
    if (Test-Path -LiteralPath $pidFile) { Remove-Item -LiteralPath $pidFile -Force }
    exit 0
}

Stop-Process -Id $pidValue -Force -ErrorAction SilentlyContinue
for ($i = 0; $i -lt 10; $i++) {
    Start-Sleep -Milliseconds 300
    if (-not (Get-Process -Id $pidValue -ErrorAction SilentlyContinue)) { break }
}

if (Test-Path -LiteralPath $pidFile) { Remove-Item -LiteralPath $pidFile -Force }
Write-Host "tuya-pocket-buddy: stopped (pid $pidValue)"
