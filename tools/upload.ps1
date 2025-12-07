param(
    [string]$shellextcmdname = 'shell'
)
python3 .\rp6502.py upload -D COM4 ..\build\${shellextcmdname}.rp6502
