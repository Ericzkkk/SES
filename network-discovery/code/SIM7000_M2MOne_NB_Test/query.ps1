param([Parameter(Mandatory=$true)][string]$PortName,[string[]]$Commands=@('AT','AT+CPIN?','AT+CIMI','AT+CEREG=?','AT+MCELLLOCK?','AT+NCELLLOCK?','AT+CGDCONT?','AT+CEER'),[int]$TimeoutSeconds=8)
$ErrorActionPreference='Stop'
$port=[System.IO.Ports.SerialPort]::new($PortName,9600,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$port.DtrEnable=$false
$port.RtsEnable=$false
try {
  $port.Open()
  foreach ($cmd in $Commands) {
    $port.DiscardInBuffer()
    Write-Output ">>> $cmd"
    $port.Write($cmd+"`r")
    $started=Get-Date
    $reply=''
    $terminal = if ($cmd -match '^AT\+HTTPACTION=') { '(?m)^(\+HTTPACTION:.*|ERROR|\+CME ERROR:.*)\r?$' } else { '(?m)^(OK|ERROR|\+CME ERROR:.*)\r?$' }
    while (((Get-Date)-$started).TotalSeconds -lt $TimeoutSeconds) {
      $reply += $port.ReadExisting()
      if ($reply -match $terminal) { break }
      Start-Sleep -Milliseconds 100
    }
    if ($cmd -eq 'AT+CIMI') { $reply=[regex]::Replace($reply,'(?m)^(\d{5})\d{9,11}\r?$','$1 [subscriber digits redacted]') }
    Write-Output $reply
    if ($reply -notmatch $terminal) { Write-Output 'TIMEOUT: stopped; do not overlap commands.'; break }
  }
} finally { if($port.IsOpen){$port.Close()};$port.Dispose();Write-Output 'PORT CLOSED' }
