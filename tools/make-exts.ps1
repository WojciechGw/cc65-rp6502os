param(
    [int]$Start = 7500
)

$ErrorActionPreference = 'Stop'

$srcDir = Join-Path $PSScriptRoot '..\src'
$files  = Get-ChildItem -Path $srcDir -Filter 'ext-*.c' -File | Sort-Object Name

if (-not $files) {
    Write-Error "There are no files ext-*.c in catalog: $srcDir"
    exit 1
}

Push-Location "$PSScriptRoot/..//src/extcmd"
foreach ($file in $files) {
    $cmd = $file.BaseName -replace '^ext-', ''
    Write-Host "Executing: make CMD=$cmd START=$Start"
    & make "CMD=$cmd" "START=$Start"

    if ($LASTEXITCODE -ne 0) {
        Write-Error "End with error for CMD=$cmd"
        exit $LASTEXITCODE
    }
}

Pop-Location
