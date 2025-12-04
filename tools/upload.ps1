# Copy-Item '..\build\180.rp6502' -Destination '..\build\darts180.rp6502' -Force -Confirm:$false
python3 .\rp6502.py upload -D COM4 ..\build\shell.rp6502
