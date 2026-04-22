Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scriptDir 'common.ps1')

$stateDir = Get-StateDir
$log      = Get-LogFile
$pidFile  = Get-PidFile
$state    = Join-Path $stateDir 'state.json'

$daemonPid = Get-DaemonPid
if ($null -ne $daemonPid) {
    Write-Host ("daemon.pid       {0}" -f $daemonPid)
} else {
    Write-Host 'daemon.pid       (not running)'
}

if (Test-Path -LiteralPath $state) {
    Write-Host ("state.json       {0}" -f $state)
    Get-Content -LiteralPath $state
} else {
    Write-Host 'state.json       (none; run /buddy-pair)'
}

if (Test-Path -LiteralPath $log) {
    Write-Host "--- tail $log ---"
    Get-Content -LiteralPath $log -Tail 40
} else {
    Write-Host 'daemon.log       (none yet)'
}
