param(
    [string]$shellextcmdname = 'shell'
)
python3 .\rp6502.py -c ..\.rp6502 upload ..\build\${shellextcmdname}.rp6502
