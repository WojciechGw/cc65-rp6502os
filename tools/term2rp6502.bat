@echo off
wt -w 0 new-tab --title "RP6502 Terminal" powershell -NoExit -Command "python3 'c:\@prg\sources\cc65-rp6502os\tools\rp6502.py' -c 'c:\@prg\sources\cc65-rp6502os\.rp6502' term"
