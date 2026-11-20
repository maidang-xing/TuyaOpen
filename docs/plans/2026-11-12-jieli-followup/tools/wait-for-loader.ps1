# Wait for the AC791 to enumerate as a JIELI loader and exit.
#
#   .\wait-for-loader.ps1 [-Minutes 480]
#
# Detection only: it does not flash. The flash is run by hand once the loader
# is reported present, so a board put into UPDATE mode for another purpose is
# not overwritten by a stale automatic action.

param([int]$Minutes = 480)

$ErrorActionPreference = 'Stop'
Write-Host "[loader-wait] watching up to $Minutes min for WL82 UBOOT1.00 ..."
$deadline = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $deadline) {
    $loader = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -match 'WL82 UBOOT|JIELI' } |
        Select-Object -First 1
    if ($loader) {
        Write-Host "[loader-wait] loader present: $($loader.FriendlyName)"
        exit 0
    }
    Start-Sleep -Seconds 2
}
Write-Host '[loader-wait] timeout: board never entered UPDATE mode'
exit 2
