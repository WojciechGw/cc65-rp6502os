param(
    [string]$shellextstart = 'A000',
    [string]$shellextcmdname = 'help'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$promptTimeoutMs = 5000

function Wait-ForBracket([System.IO.Ports.SerialPort]$port, [int]$timeoutMs) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        try {
            $ch = [char]$port.ReadChar()
            if ($ch -eq ']') {
                return $true
            }
        }
        catch [System.TimeoutException] {
            # try till timeout
        }
    }
    return $false
}

function Send-LineAndWait([System.IO.Ports.SerialPort]$port, [string]$text, [string]$label) {
    $maxRetries = 2
    for ($i = 0; $i -le $maxRetries; $i++) {
        try {
            $port.WriteLine($text)
            if (-not (Wait-ForBracket -port $port -timeoutMs $promptTimeoutMs)) {
                Write-Warning "Timeout for ']' after sent '$label'"
            }
            return
        }
        catch {
            if ($i -lt $maxRetries) {
                Start-Sleep -Milliseconds 200
                continue
            }
            throw
        }
    }
}

Push-Location "$PSScriptRoot/..//src/extcmd"
try {
    make CMD=${shellextcmdname} START=${shellextstart}
}
finally {
    Pop-Location
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
        Send-LineAndWait -port $serialPort -text "exit" -label "exit"
        Start-Sleep -Milliseconds 400
        Send-LineAndWait -port $serialPort -text "0:" -label "0:"
        Start-Sleep -Milliseconds 400
        Send-LineAndWait -port $serialPort -text "cd /SHELL" -label "cd /SHELL"
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

    python3 rp6502.py upload -D COM4 ..\src\extcmd\build\${shellextcmdname}.com
    #python3 rp6502.py shellext -D COM4 ..\src\extcmd\build\${shellextcmdname}.com

   try {
        $serialPort.Open()
        Start-Sleep -Milliseconds 400
        Send-LineAndWait -port $serialPort -text "shell" -label "shell"
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
