Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-StateDir {
    if ($env:TUYA_POCKET_BUDDY_STATE_DIR) {
        return (New-Item -ItemType Directory -Force -Path $env:TUYA_POCKET_BUDDY_STATE_DIR).FullName
    }
    $root = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { Join-Path $HOME 'AppData\Local' }
    $dir  = Join-Path $root 'tuya-pocket-buddy'
    return (New-Item -ItemType Directory -Force -Path $dir).FullName
}

function Get-VenvDir { Join-Path (Get-StateDir) 'venv' }
function Get-LogFile { Join-Path (Get-StateDir) 'daemon.log' }
function Get-PidFile { Join-Path (Get-StateDir) 'daemon.pid' }

function Resolve-Python {
    $candidates = @('py','python','python3','python3.12','python3.11','python3.10')
    foreach ($name in $candidates) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if (-not $cmd) { continue }
        # Prefer the launcher with an explicit version spec when available.
        $args = if ($name -eq 'py') { @('-3') } else { @() }
        $version = & $cmd @args -c 'import sys;print(f"{sys.version_info[0]}.{sys.version_info[1]}")' 2>$null
        if ($LASTEXITCODE -ne 0) { continue }
        $parts = $version.Trim().Split('.')
        if ([int]$parts[0] -ge 4 -or ([int]$parts[0] -eq 3 -and [int]$parts[1] -ge 10)) {
            return [pscustomobject]@{ Exe = $cmd.Path; Args = $args }
        }
    }
    throw 'tuya-pocket-buddy: Python >= 3.10 is required but was not found on PATH.'
}

function Get-DaemonPid {
    $pidFile = Get-PidFile
    if (-not (Test-Path -LiteralPath $pidFile)) { return $null }
    try {
        $raw = Get-Content -LiteralPath $pidFile -ErrorAction Stop
    } catch { return $null }
    $pidValue = 0
    if (-not [int]::TryParse($raw.Trim(), [ref]$pidValue)) { return $null }
    try {
        $proc = Get-Process -Id $pidValue -ErrorAction Stop
        return $proc.Id
    } catch {
        return $null
    }
}
