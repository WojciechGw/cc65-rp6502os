param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,
    [string]$Port = "COM4",
    [int]$Baud = 115200
)

if (-not (Test-Path -LiteralPath $FilePath)) {
    Write-Error "File not found: $FilePath"
    exit 1
}

$sp = [System.IO.Ports.SerialPort]::new($Port, $Baud, "None", 8, "One")
$sp.Handshake = "None"
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.ReadTimeout = 1000
$sp.WriteTimeout = 5000
$enc = [Text.Encoding]::GetEncoding(852)
$sp.Encoding = $enc
$sp.NewLine = "`n" # tylko LF, aby uniknąć podwójnych pustych linii

try {
    $sp.Open()
} catch {
    Write-Error "Can't open port $Port : $_"
    exit 1
}

try {
    Start-Sleep -Milliseconds 200
    $sp.DiscardInBuffer()
    $sp.DiscardOutBuffer()
    $text = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $FilePath), $enc)
    [byte[]]$data = $enc.GetBytes($text)
    $b64 = [Convert]::ToBase64String($data)
    $chunkSize = 76
    $totalBytes = 0
    for ($i = 0; $i -lt $b64.Length; $i += $chunkSize) {
        $chunk = $b64.Substring($i, [Math]::Min($chunkSize, $b64.Length - $i))
        $lineBytes = $enc.GetBytes($chunk + "`n") # 76 znaków + LF
        foreach ($b in $lineBytes) {
            $sp.BaseStream.WriteByte($b) # wysyłka bajt po bajcie
            $totalBytes++
        }
    }
    $sp.BaseStream.Flush()
    Write-Host "Sent Base64 ($($b64.Length) chars, ~$totalBytes bytes) to $Port @ $Baud bps from file $FilePath (source $($data.Length) bytes PC852)"
}
finally {
    $sp.Close()
}
