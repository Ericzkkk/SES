param([Parameter(Mandatory=$true)][string]$PortName)
$ErrorActionPreference = 'Stop'
$port = [System.IO.Ports.SerialPort]::new($PortName,9600,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$port.ReadTimeout = 1000
$port.DtrEnable = $false
$port.RtsEnable = $false
$logPath = Join-Path $PSScriptRoot ('live-' + $PortName + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$writer = [System.IO.StreamWriter]::new($logPath,$false)
try {
  $port.Open()
  $port.DtrEnable = $true
  Start-Sleep -Milliseconds 200
  $port.DtrEnable = $false
  $start = Get-Date
  $buffer = ''
  $triggered = $false
  while (((Get-Date)-$start).TotalSeconds -lt 1100) {
    $chunk = $port.ReadExisting()
    if ($chunk) {
      [Console]::Write($chunk)
      $writer.Write($chunk)
      $writer.Flush()
      $buffer += $chunk
      if (!$triggered -and $buffer.Contains('NB-IOT TEST READY: SEND !')) {
        $port.Write('!')
        $triggered = $true
      }
      if ($buffer.Contains('TEST COMPLETE') -or $buffer.Contains('STARTUP FAILED')) { break }
      if ($buffer.Length -gt 4000) { $buffer=$buffer.Substring($buffer.Length-2000) }
    }
    Start-Sleep -Milliseconds 100
  }
} finally {
  if ($port.IsOpen) { $port.Close() }
  $port.Dispose()
  $writer.Dispose()
  Write-Output "`nPORT CLOSED. Log: $logPath"
}
