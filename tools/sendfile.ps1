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
$enc = [Text.Encoding]::ASCII
$sp.Encoding = $enc
$sp.NewLine = "`r`n"

function New-HexRecord {
    param(
        [byte[]]$Data,
        [int]$Address = 0,
        [byte]$Type = 0
    )
    $count = [byte]$Data.Length
    $sum = [int]$count + (($Address -shr 8) -band 0xFF) + ($Address -band 0xFF) + $Type
    $hexData = ""
    foreach ($b in $Data) { $sum += $b; $hexData += "{0:X2}" -f $b }
    $ck = (-$sum) -band 0xFF  # two's complement
    return ":{0:X2}{1:X4}{2:X2}{3}{4:X2}" -f $count, $Address, $Type, $hexData, $ck
}

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
    [byte[]]$data = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $FilePath))
    $chunkSize = 16
    $totalBytes = 0
    $addr = 0
    while ($addr -lt $data.Length) {
        $len = [Math]::Min($chunkSize, $data.Length - $addr)
        $slice = New-Object byte[] $len
        [Array]::Copy($data, $addr, $slice, 0, $len)
        $line = New-HexRecord -Data $slice -Address $addr -Type 0
        $lineBytes = $enc.GetBytes($line + "`r`n")
        foreach ($b in $lineBytes) {
            $sp.BaseStream.WriteByte($b)
        }
        $totalBytes += $lineBytes.Length
        $addr += $len
    }
    # EOF record
    $eofLine = New-HexRecord -Data @() -Address 0 -Type 1
    foreach ($b in $enc.GetBytes($eofLine + "`r`n")) {
        $sp.BaseStream.WriteByte($b)
        $totalBytes++
    }
    $sp.BaseStream.Flush()
    Write-Host "Sent Intel HEX ($totalBytes bytes on wire) to $Port @ $Baud bps from file $FilePath (source $($data.Length) bytes)"
}
finally {
    $sp.Close()
}
