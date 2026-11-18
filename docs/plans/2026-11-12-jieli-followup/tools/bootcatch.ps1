param(
    [int]$Seconds = 50,
    [string]$Port = 'COM8',
    [string]$Out = 'D:\tuya_proj\TuyaOpen\docs\plans\2026-11-12-jieli-followup\logs\bootcatch.log'
)

$end = (Get-Date).AddSeconds($Seconds)
$sw = New-Object System.IO.StreamWriter($Out, $false, [System.Text.Encoding]::UTF8)
$sw.AutoFlush = $true
$sw.WriteLine("[bootcatch] start $(Get-Date -Format HH:mm:ss)")

while ((Get-Date) -lt $end) {
    $sp = $null
    try {
        $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
        $sp.ReadTimeout = 200
        $sp.DtrEnable = $false
        $sp.RtsEnable = $false
        $sp.Open()
        $sw.WriteLine("[bootcatch] port open $(Get-Date -Format HH:mm:ss.fff)")
        while ((Get-Date) -lt $end) {
            try {
                $chunk = $sp.ReadExisting()
                if ($chunk) { $sw.Write($chunk) }
            } catch {
            }
            Start-Sleep -Milliseconds 40
        }
    } catch {
        $sw.WriteLine("[bootcatch] reopen $(Get-Date -Format HH:mm:ss.fff)")
        Start-Sleep -Milliseconds 250
    } finally {
        if ($sp) { try { $sp.Close() } catch {} }
    }
}

$sw.WriteLine("[bootcatch] end $(Get-Date -Format HH:mm:ss)")
$sw.Close()
