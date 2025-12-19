#install ROM
param(
    [string]$shellextcmdname = 'shell',
    [string]$shellreboot = 'N'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$promptTimeoutMs = 5000

function Wait-ForPrompt([System.IO.Ports.SerialPort]$port, [int]$timeoutMs, [string]$prompt) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        try {
            $ch = [char]$port.ReadChar()
            if ($ch -eq $prompt) {
                return $true
            }
        }
        catch [System.TimeoutException] {
            # try till timeout
        }
    }
    return $false
}

function Send-LineAndWait([System.IO.Ports.SerialPort]$port, [string]$text, [string]$label, [string]$prompt) {
    $maxRetries = 2
    for ($i = 0; $i -le $maxRetries; $i++) {
        try {
            $port.WriteLine($text)
            if (-not (Wait-ForPrompt -port $port -timeoutMs $promptTimeoutMs $prompt )) {
                Write-Warning "Timeout for ''$prompt'' after sent '$label'"
            }
            return
        }
        catch {
            if ($i -lt $maxRetries) {
                Start-Sleep -Milliseconds 400
                continue
            }
            throw
        }
    }
}

Push-Location "$PSScriptRoot"
try {
    $serialPort = [System.IO.Ports.SerialPort]::new("COM4", 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serialPort.NewLine = "`r"
    $serialPort.ReadTimeout = 200
    $serialPort.WriteTimeout = 3000
    $serialPort.DtrEnable = $true
    $serialPort.RtsEnable = $true
    try {
        $serialPort.Open()
        Start-Sleep -Milliseconds 400
        Send-LineAndWait -port $serialPort -text "exit" -label "exit" -prompt "]"        
        Start-Sleep -Milliseconds 400
        Send-LineAndWait -port $serialPort -text "0:" -label "0:" -prompt "]"
        Start-Sleep -Milliseconds 400
        Send-LineAndWait -port $serialPort -text "cd /" -label "cd /" -prompt "]"
        Start-Sleep -Milliseconds 400
        $serialPort.Close()
    }
    catch {
        Write-Warning "COM4: is not available $_"
    }
    finally {
        if ($serialPort) {
            $serialPort.Dispose()
        }
    }

    python3 .\rp6502.py upload -D COM4 ..\build\${shellextcmdname}.rp6502

    try {
        $serialPort.Open()
        Start-Sleep -Milliseconds 400
        Send-LineAndWait -port $serialPort -text "set boot -" -label "set boot -" -prompt "]"
        Start-Sleep -Milliseconds 400
        Send-LineAndWait -port $serialPort -text "remove ${shellextcmdname}" -label "remove ${shellextcmdname}" -prompt "]"
        Start-Sleep -Milliseconds 400
        Send-LineAndWait -port $serialPort -text "install ${shellextcmdname}.rp6502" -label "install ${shellextcmdname}" -prompt "]"
        Start-Sleep -Milliseconds 400
        Send-LineAndWait -port $serialPort -text "set boot ${shellextcmdname}" -label "set boot ${shellextcmdname}" -prompt "]"
        Start-Sleep -Milliseconds 400
        if($shellreboot -ne 'Y'){
            Send-LineAndWait -port $serialPort -text "shell" -label "shell" -prompt ">"
        } else {
            Send-LineAndWait -port $serialPort -text "reboot" -label "reboot"
        }
        $serialPort.Close()
    }
    catch {
        Write-Warning "COM4: is not available $_"
    }
    finally {
        if ($serialPort) {
            $serialPort.Dispose()
        }
    }

}

finally {
    Pop-Location
}
