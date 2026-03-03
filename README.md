# RP6502 OS Shell [65C02S native]

This is native 65C02S Shell for Picocomputer.

## Memory
Memory area available for user programs running under OS Shell environment is $8000 - $FD00 (33515 bytes)

## Keyboard shortcuts
F1  help informations
F2  keyboard visualiser
F3  current date/time and calendar
LEFT  change active drive to previous if available
RIGHT change active drive to next if available
UP  recall last command
DOWN  a directory of active drive/catalog

## Commands

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
external commands are *.com files in <MSC0:-MSC7:>/SHELL) (case insensitive)

* calendar - calendar application
* courier  - in/out file transfer application 
* dir      - show active drive directory, wildcards allowed
* hass     - Handy ASSembler 65C02S
* help     - show help informations (same as <F1> key)
* keyboard - keyboard state visualiser
* label    - show or set active drive's volume label