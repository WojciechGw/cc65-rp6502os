# RP6502 OS Shell [65C02S native]
* project status: <B>IN PROGRESS</B>
* next stage: text editor & more utilities</br>

This is a native 65C02S Shell 
for Rumbledethumps' RP6502 Picocomputer OS</br>
https://picocomputer.github.io/os.html#

based on Jason Howard's ideas & code</br>
https://github.com/jthwho/rp6502-shell

All you need to test this project is in 'release/latest' folder.

## Preparing

* The shell.rp6502 file can be installed as boot ROM
* The startup screen appears while waiting for a Wi-Fi connection and has a timeout set; without an active Wi-Fi connection or a manually set time, the 'time' and 'calendar' commands display data according to the current RP6502 system time

## Memory
Memory area available for user programs running under OS Shell environment is $7B00-$FD00 (33280 bytes).</br>
User can create .exe programs with external OS shell command 'hass' (Handy ASSembler WDC65C02S).

## OS Files
* shell.rp6502
* hello.asm - test source for hass
* font.asm - test source for hass
* sendfile.py - send file from PC to RP6502 as Intel HEX over serial, run crx.com on OS Shell to receive
* receivefile.py - send file from RP6502 to PC as Intel HEX over serial, run ctx.com on OS Shell to send

## Keyboard shortcuts
* F1 - help informations
* F2 - keyboard visualiser
* F3 - current date/time and calendar
* LEFT - change active drive to previous if available
* RIGHT - change active drive to next if available
* UP - recall last command
* DOWN - a directory of active drive/catalog

## OS commands

### Internal
internal commands are case sensitive
* bload  - load binary file to RAM/XRAM
* bsave  - save RAM/XRAM to binary file
* brun   - load binary file to RAM and run
* cd     - change active directory
* chmod  - set file attributes
* cls    - reset terminal
* com    - load .com binary and run
* cp     - copy file
* cpm    - copy/move multiple files, wildcards allowed
* drive  - set active drive
* drives - show available drives
* exit   - exit to the system monitor
* hex    - dump file contents to screen
* list   - show a file content
* ls     - list active directory
* mem    - show memory informations : lowest and highest RAM address and size
* mkdir  - create directory
* mv     - move/rename a file or directory
* peek   - memory viewer
* phi2   - show CPU clock frequency
* rename - rename a file or directory
* rm     - remove a file/files, wildcards allowed
* run    - run code at address
* stat   - show file/directory info
* time   - show local date and time

### Internal ROM commands
internal ROM commands are *.com files in shell.rp6502 ROM: (RP6502 BIGROM)</br>
There's no need to include the .com extension - ie. just type 'dir' instead of 'dir.com' (case insensitive)</br>

* calendar - calendar application
* crx      - download file transfer (PC => RP6502)
* ctx      - upload file transfer (RP6502 => PC)
* dir      - show active drive directory, wildcards allowed
* help     - show help informations (same as F1 key)
* keyboard - keyboard state visualiser
* label    - show or set active drive's volume label

### External commands
this kind of external commands are *.com files in MSC0:/SHELL directory</br>
There's no need to include the .com extension - ie. just type 'hass' instead of 'hass.com' (case insensitive)</br>

* hass     - Handy ASSembler for 65C02S

NOTE:
These commands take precedence over commands in “ROM:”.</br>
If the same command is also present in “ROM:”, the one in the “MSC0:/SHELL” directory will be executed.</br>
This allows you to update the command code and place it in “ROM:” once the changes are complete or simply overwrite the "ROM:" commands with your own version on the USB drive.</br>

Enjoy !

## Addendum

### .com and .exe file format (project)
* .com programs for native OS
* .exe programs demands whole Picocomputer free memory like .rp6502 (dedicated loader and RESVEC)
#### header of file (only once)
FF    - constant FF for .com FE for .exe</br>
byte  - file format version</br>
#### segment of data (at least one)
FF FF   - data segment start</br>
byte    - target memory type 00 - RAM 01 - XRAM</br>
byte    - reserved (idea: number of memory bank, only if target memory is RAM)</br>
LSB MSB - begin address for data</br>
LSB MSB - RESVEC for .exe, run address for .com if it differs from FF FF (this matters only if target memory is RAM)</br>
LSB MSB - size of data</br>
data</br>
next segments if needed</br>
00 EOF
