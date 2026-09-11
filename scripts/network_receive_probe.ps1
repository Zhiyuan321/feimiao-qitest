param(
    [ValidateRange(1,65535)][int]$Port = 11000,
    [ValidateRange(1,3600)][int]$DurationSeconds = 60,
    [string]$OutputDirectory = ""
)
# Windows 7 / PowerShell 2 / .NET TCP listener. Receive-only: never Write/Send to the peer.
# A Read call is a TCP chunk, NOT a protocol frame. This probe does not infer field meanings.
$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2
if (!$OutputDirectory) { $OutputDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (!(Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
}
$logPath = Join-Path $OutputDirectory ("network-receive-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff") + ".log")
$encoding = New-Object System.Text.UTF8Encoding -ArgumentList $true
$log = New-Object System.IO.StreamWriter -ArgumentList $logPath,$false,$encoding
$log.AutoFlush = $true
$listener = $null
$client = $null
$received = [long]0
$chunks = 0
$connections = 0
$exitCode = 0
function Record([string]$message) {
    $line = (Get-Date -Format "yyyy-MM-ddTHH:mm:ss.fffzzz") + " " + $message
    $log.WriteLine($line)
    Write-Host $line
}
try {
    $listener = New-Object System.Net.Sockets.TcpListener -ArgumentList ([Net.IPAddress]::Any),$Port
    $listener.Start()
    Record "LISTEN IPv4=0.0.0.0 port=$Port duration=${DurationSeconds}s; application TX=0 (receive only)"
    Write-Host "Please keep the instrument connected. The test ends automatically."
    Write-Host "Stop other software listening on this port before running this probe."
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $buffer = New-Object byte[] 4096
    $lastProgress = -1
    while ($watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        if ($listener.Pending()) {
            $incoming = $listener.AcceptTcpClient()
            if ($client -and ($incoming.Client.RemoteEndPoint.Address.Equals($client.Client.RemoteEndPoint.Address))) {
                Record ("REPLACED_SAME_IP old=" + $client.Client.RemoteEndPoint.ToString() + " new=" + $incoming.Client.RemoteEndPoint.ToString())
                $client.Close()
                $client = $null
            }
            if ($client) {
                Record ("EXTRA_CONNECTION_REJECTED peer=" + $incoming.Client.RemoteEndPoint.ToString())
                $incoming.Close()
            } else {
                $client = $incoming
                $connections++
                Record ("CONNECTED peer=" + $client.Client.RemoteEndPoint.ToString())
            }
        }
        if ($client) {
            try {
                if ($client.Available -gt 0) {
                    $count = $client.GetStream().Read($buffer,0,[Math]::Min($buffer.Length,$client.Available))
                    if ($count -gt 0) {
                        $received += $count
                        $chunks++
                        $hex = [BitConverter]::ToString($buffer,0,$count).Replace('-', ' ')
                        Record "RX bytes=$count total=$received hex=$hex"
                    }
                } elseif ($client.Client.Poll(0,[Net.Sockets.SelectMode]::SelectRead)) {
                    Record "DISCONNECTED"
                    $client.Close()
                    $client = $null
                }
            } catch {
                Record ("SOCKET_ERROR " + $_.Exception.Message)
                $client.Close()
                $client = $null
            }
        }
        $seconds = [int][Math]::Floor($watch.Elapsed.TotalSeconds)
        if ($seconds -ge $lastProgress + 5) {
            $lastProgress = $seconds
            Record "PROGRESS elapsed=${seconds}s totalBytes=$received chunks=$chunks connections=$connections"
        }
        if ($received -ge 1048576) {
            Record "CAPTURE_LIMIT 1MiB reached; ending test"
            break
        }
        Start-Sleep -Milliseconds 20
    }
} catch {
    $exitCode = 1
    Record ("ERROR " + $_.Exception.Message)
    Write-Host "If the port is occupied, stop the workstation and vendor software, then retry."
} finally {
    if ($client) { $client.Close() }
    if ($listener) { $listener.Stop() }
    Record "END totalBytes=$received chunks=$chunks connections=$connections applicationTxBytes=0"
    $log.Dispose()
    Write-Host "Log saved: $logPath"
}
exit $exitCode
