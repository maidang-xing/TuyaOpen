Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDir 'common.ps1')

$existing = Get-DaemonPid
if ($null -ne $existing) {
    Write-Host "tuya-pocket-buddy: already running (pid $existing)"
    exit 0
}

$venv = Get-VenvDir
$venvPython = Join-Path $venv 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    $venvPython = Join-Path $venv 'bin/python'
}
if (-not (Test-Path -LiteralPath $venvPython)) {
    Write-Error 'tuya-pocket-buddy: venv missing. Run /buddy-install first.'
    exit 1
}

$log = Get-LogFile
$pidFile = Get-PidFile

$proc = Start-Process -FilePath $venvPython `
    -ArgumentList @('-m','tuya_pocket_buddy','run') `
    -WindowStyle Hidden `
    -RedirectStandardOutput $log `
    -RedirectStandardError  $log `
    -PassThru

Set-Content -LiteralPath $pidFile -Value $proc.Id
Write-Host "tuya-pocket-buddy: started (pid $($proc.Id))"
