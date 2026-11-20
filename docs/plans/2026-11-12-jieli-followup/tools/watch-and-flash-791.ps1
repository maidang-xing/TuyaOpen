# Watches for the AC791 to become flashable and immediately runs the KWS
# flash + boot-log capture. Meant to be run under the Monitor tool: each line
# it prints is an event; it exits after a successful flash+capture. Flashing
# needs a board in either app MSD (VID_3654, healthy) or UBOOT (VID_4C4A)
# mode; a crashed app leaves VID_3654 at Problem code 10, which this ignores.
param(
    [string]$Image    = 'audiokws',
    [int]   $Seconds  = 45,
    [int]   $Interval = 20
)
$ErrorActionPreference = 'Continue'
$verify = Join-Path $PSScriptRoot 'verify-audio-791.ps1'

function Get-FlashableDevice {
    Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId -match 'VID_4C4A' -or
            ($_.InstanceId -match 'VID_3654' -and $_.Status -eq 'OK')
        } | Select-Object -First 1
}

$lastState = ''
while ($true) {
    $dev = Get-FlashableDevice
    if ($dev) {
        $now = Get-Date -Format 'HH:mm:ss'
        Write-Output "[$now] flashable device: $($dev.FriendlyName) [$($dev.Status)]"
        & $verify -Image $Image -Seconds $Seconds 2>&1 |
            Where-Object { $_ -match 'flash-791|verify-audio-791|FLASH|Device Online|Download completed|attempt' } |
            ForEach-Object { Write-Output "[$now] $_" }
        if ($LASTEXITCODE -eq 0) {
            Write-Output "[$now] FLASH+CAPTURE OK ($Image)"
            exit 0
        }
        Write-Output "[$now] flash attempt failed (exit $LASTEXITCODE), still watching"
        Start-Sleep -Seconds $Interval
        continue
    }
    $state = 'absent'
    if ($state -ne $lastState) {
        Write-Output "[$(Get-Date -Format 'HH:mm:ss')] no flashable board yet (VID_3654 code 10 or absent); waiting"
        $lastState = $state
    }
    Start-Sleep -Seconds $Interval
}
