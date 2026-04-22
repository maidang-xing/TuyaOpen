Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$pluginRoot = (Resolve-Path (Join-Path $scriptDir '..')).Path
. (Join-Path $scriptDir 'common.ps1')

$py = Resolve-Python
& $py.Exe @($py.Args + @(
    (Join-Path $scriptDir 'install-hooks.py'),
    '--merge',
    '--plugin-root', $pluginRoot
)) @args
exit $LASTEXITCODE
