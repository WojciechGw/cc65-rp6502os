$prjname = "outs8000"
Remove-Item "$prjname.o"
Remove-Item "$prjname.bin"
ca65 "$prjname.s" -o "$prjname.o"
ld65 "$prjname.o" -C "$prjname.cfg" -o "$prjname.bin"
Set-Location ..\..\tools
python3 .\rp6502.py upload -D COM4 ..\src\rawbin\$prjname.bin
Set-Location ..\src\rawbin
