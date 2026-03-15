# RP6502 OS Shell [65C02S native]
* project status: <B>IN PROGRESS</B>
* next stage: text editor & more utilities</br>

This is a native 65C02S Shell 
for Rumbledethumps' RP6502 Picocomputer OS</br>
https://picocomputer.github.io/os.html#

based on Jason Howard's ideas & code</br>
https://github.com/jthwho/rp6502-shell

All you need to test this project is in 'release/latest' folder.

## Memory
Memory area available for user programs running under OS Shell environment is $8000-$FD00 (33515 bytes).</br>
User can build .exe programs 'in place' with external OS shell command 'hass' (Handy ASSembler).

## OS Files
* shell.rp6502
* hello.asm - test source for hass
* font.asm - test source for hass
* sendfile.py - send file from PC to RP6502 as Intel HEX over serial, run crx.com on OS Shell to receive
</br>

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

### External
external commands are *.com files in ROM: (case insensitive)</br>
There's no need to include the .com extension - just type 'dir' instead of 'dir.com'</br>

* calendar - calendar application
* crx      - download file transfer (PC => RP6502)
* ctx      - upload file transfer (RP6502 => PC)
* dir      - show active drive directory, wildcards allowed
* hass     - Handy ASSembler for 65C02S
* help     - show help informations (same as F1 key)
* keyboard - keyboard state visualiser
* label    - show or set active drive's volume label

Enjoy !
