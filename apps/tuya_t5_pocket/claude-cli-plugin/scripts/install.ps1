Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$pluginRoot = (Resolve-Path (Join-Path $scriptDir '..')).Path
. (Join-Path $scriptDir 'common.ps1')

$py     = Resolve-Python
$venv   = Get-VenvDir

if (-not (Test-Path -LiteralPath $venv)) {
    Write-Host "tuya-pocket-buddy: creating venv at $venv"
    & $py.Exe @($py.Args + @('-m','venv', $venv))
    if ($LASTEXITCODE -ne 0) { throw 'venv creation failed' }
}

$venvPython = Join-Path $venv 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    # Fallback to POSIX-style venv layout if Windows layout is absent.
    $venvPython = Join-Path $venv 'bin/python'
}

& $venvPython -m pip install --upgrade --quiet pip
if ($LASTEXITCODE -ne 0) { throw 'pip upgrade failed' }
& $venvPython -m pip install --quiet (Join-Path $pluginRoot 'daemon')
if ($LASTEXITCODE -ne 0) { throw 'pip install of daemon failed' }

Write-Host 'tuya-pocket-buddy: merging hooks into ~/.claude/settings.json'
& $py.Exe @($py.Args + @(
    (Join-Path $scriptDir 'install-hooks.py'),
    '--merge',
    '--plugin-root', $pluginRoot
))
if ($LASTEXITCODE -ne 0) { throw 'hook merge failed' }

Write-Host 'tuya-pocket-buddy: install complete. Next: /buddy-pair then /buddy-start'
