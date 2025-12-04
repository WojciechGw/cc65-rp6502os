# tools/helper.ps1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 1. Wejdź do katalogu /src
Push-Location "$PSScriptRoot/..//src/extcmd"
try {
    # 2. make CMD=help
    make CMD=help
}
finally {
    Pop-Location
}

# 3. Wejdź do katalogu tools
Push-Location "$PSScriptRoot"
try {
    # 4. python3 rp6502.py upload -D COM4 ..\src\extcmd\build\help.com
    python3 rp6502.py upload -D COM4 ..\src\extcmd\build\help.com
}
finally {
    Pop-Location
}
