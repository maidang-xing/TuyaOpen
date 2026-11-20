# Flash the vendor demo DevKitBoard image (wl82) with isd_download.
#
#   .\flash-vendor-791.ps1
#
# The vendor demo firmware has no "reboot to loader" hook, so the board must
# already be in loader/UDISK mode: power-cycle it (or hold UPDATE+RESET) right
# before running this. When the vendor app is running normally, isd_download
# reports "Device Offline".
#
# The staging below is the one the demo build left behind:
#   chip/wl82/AC79_AIoT_SDK/cpu/wl82/tools
# with app.bin = vendor-demo-video1-lbuf1 (LBUF_NUMB=1, test_buf1 aligned(32)).

param(
    [string]$Tools = 'D:\tuya_proj\TuyaOpen\.platform-worktrees\05-camera\chip\wl82\AC79_AIoT_SDK\cpu\wl82\tools'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path (Join-Path $Tools 'isd_download.exe'))) {
    throw "isd_download.exe not found under $Tools"
}
if (-not (Test-Path (Join-Path $Tools 'audlogo'))) {
    throw "audlogo resources not found under $Tools"
}

$logDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir ("flash-vendor-791-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')

Push-Location $Tools
try {
    & .\isd_download.exe isd_config.ini -gen2 -tonorflash -dev wl82 -boot 0x1c02000 `
        -div1 -wait 5000 -uboot uboot.boot -app app.bin cfg_tool.bin -res audlogo ui_res `
        -reboot 2000 -extend-bin 2>&1 |
        Tee-Object -FilePath $log | Select-Object -Last 12
    if ($LASTEXITCODE -ne 0) {
        throw "isd_download exit $LASTEXITCODE (log: $log)"
    }
} finally {
    Pop-Location
}

Write-Host "[flash-vendor-791] done, log: $log"
