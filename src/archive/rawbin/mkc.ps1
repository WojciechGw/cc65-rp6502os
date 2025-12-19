$prjname = "hello"
$startaddr = 0x9000
Remove-Item "$prjname.map"
Remove-Item "$prjname.bin"
cl65 --target rp6502 -Os -o "$prjname.bin" "$prjname.c" -m "$prjname.map" -C "$prjname.cfg" --start-addr $startaddr
Set-Location ..\..\tools
python3 .\rp6502.py upload -D COM4 ..\src\rawbin\$prjname.bin
Set-Location ..\src\rawbin
