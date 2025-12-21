param(
    [Parameter(Mandatory = $true)]
    [string]$OutFile,
    [string]$Port = "COM4",
    [int]$Baud = 115200
)

$sp = [System.IO.Ports.SerialPort]::new($Port, $Baud, "None", 8, "One")
$sp.Handshake = "None"
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.ReadTimeout = 1000
$sp.WriteTimeout = 1000
$sp.Encoding = [Text.Encoding]::ASCII
$sp.NewLine = "`r`n"

function Parse-HexRecord {
    param([string]$Line)
    $line = $Line.Trim()
    if (-not $line.StartsWith(":")) { return $null }
    if ($line.Length -lt 11) { return $null }
    try {
        $bc   = [Convert]::ToInt32($line.Substring(1,2),16)
        $type = [Convert]::ToInt32($line.Substring(7,2),16)
        $dataHex = $line.Substring(9, [Math]::Min($bc*2, $line.Length-11))
        if ($dataHex.Length -ne $bc*2) { return $null }
        $ckHex = $line.Substring(9 + $bc*2, 2)
        $sum = $bc + [Convert]::ToInt32($line.Substring(3,2),16) + [Convert]::ToInt32($line.Substring(5,2),16) + $type
        $data = New-Object byte[] $bc
        for($i=0; $i -lt $bc; $i++){
            $b = [Convert]::ToInt32($dataHex.Substring($i*2,2),16)
            $data[$i] = [byte]$b
            $sum += $b
        }
        $ck = [Convert]::ToInt32($ckHex,16)
        $sum += $ck
        if( ($sum -band 0xFF) -ne 0 ) { return $null }
        return [pscustomobject]@{ Type=$type; Data=$data }
    } catch { return $null }
}

try {
    $sp.Open()
} catch {
    Write-Error "Can't open port $Port : $_"
    exit 1
}

$fs = $null
try {
    $fs = [IO.File]::Open($OutFile, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
    Write-Host "Receiving Intel HEX on $Port @ $Baud to $OutFile ..."
    while ($true) {
        try {
            $line = $sp.ReadLine()
        } catch [TimeoutException] {
            continue
        }
        $rec = Parse-HexRecord -Line $line
        if (-not $rec) { continue }
        if ($rec.Type -eq 0) {
            if ($rec.Data.Length -gt 0) { $fs.Write($rec.Data,0,$rec.Data.Length) }
        } elseif ($rec.Type -eq 1) {
            Write-Host "EOF received."
            break
        } else {
            # ignorujemy inne typy
        }
    }
    Write-Host "Done."
}
finally {
    if ($fs) { $fs.Close() }
    $sp.Close()
}
