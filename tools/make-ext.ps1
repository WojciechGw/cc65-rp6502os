param(
    [string]$shellextstart = '8000',
    [string]$shellextcmdname = 'help'
)

Set-StrictMode -Version Latest

Push-Location "$PSScriptRoot/..//src/extcmd"
try {
    make CMD=${shellextcmdname} START=${shellextstart}
}
finally {
    Pop-Location
}
