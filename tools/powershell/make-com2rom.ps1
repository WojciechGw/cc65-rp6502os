param(
    [string]$shellextstart = '8000',
    [string]$shellextcmdname = 'help'
)

Set-StrictMode -Version Latest

$appver  = (Get-Date).ToString('yyyyMMdd.HHmm')
$srcFile = Join-Path $PSScriptRoot "..\..\src\ext-$shellextcmdname.c"
if (Test-Path $srcFile) {
    (Get-Content $srcFile -Raw) `
        -replace '#define APPVER "[^"]*"', "#define APPVER `"$appver`"" |
        Set-Content $srcFile -NoNewline
}

Push-Location "$PSScriptRoot/../../src/extcmd"
try {
    make CMD=${shellextcmdname} START=${shellextstart}
}
finally {
    Pop-Location
}
