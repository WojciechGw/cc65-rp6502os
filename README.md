# razemOS — RP6502 Shell [WDC65C02S native]

**Project status: IN PROGRESS**

Native WDC65C02S shell for Rumbledethumps' RP6502 Picocomputer.
https://picocomputer.github.io/os.html

Based on Jason Howard's ideas & code:
https://github.com/jthwho/rp6502-shell

All files needed to run the project are in the `release/latest/` folder.

---

## Getting started

Copy to the USB drive:
- `razemos.rp6502` — the shell; can be installed as boot ROM
- `hass.rp6502` — Handy ASSembler for 65C02S
- `hello.asm` — example source file for hass
- `ctx.py` / `crx.py` — PC-side file transfer scripts (Python)

The startup screen is displayed while waiting for a Wi-Fi connection.
Without an active Wi-Fi connection or manually set RTC, `time` and `date`
show the RP6502 system time.

---

## Memory

User programs running under razemOS have access to `$8000–$FCFF` (~31 KB).
Programs can be created as `.com` files (cc65) or `.rp6502` ROMs (hass or cc65).

---

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| F1  | help |
| F2  | keyboard visualiser |
| F3  | date, time and calendar |
| F4  | directory of active drive |
| ↑   | recall last command |

---

## Commands

### Internal commands
Built into the shell. Case sensitive.

| Command  | Description |
|----------|-------------|
| `bload`  | load binary file to RAM or XRAM |
| `bsave`  | save RAM or XRAM region to binary file |
| `brun`   | load binary file to RAM and run it |
| `cart`   | launch a ROM by filename (without extension) |
| `cd`     | change active directory |
| `chmod`  | set file attributes |
| `cls`    | reset/clear terminal |
| `com`    | load `.com` binary at given address and run |
| `copy`   | copy a single file |
| `cp`     | copy or move files (wildcards supported) |
| `drive`  | set active drive |
| `exit`   | exit to the system monitor |
| `launcher` | register or deregister razemOS as system launcher |
| `list`   | display text file contents |
| `ls`     | list active directory |
| `mem`    | show available RAM (lowest/highest address and size) |
| `mkdir`  | create directory |
| `phi2`   | show CPU clock frequency |
| `rename` | rename or move a file or directory |
| `rm`     | remove file(s) — wildcards supported |
| `run`    | execute code at given address |
| `stat`   | show file or directory info |
| `time`   | show local date and time |

### ROM commands
`.com` files embedded in `razemos.rp6502`. The `.com` extension can be omitted
(type `dir` instead of `dir.com`). Case insensitive.

Commands placed in `MSC0:/SHELL/` take precedence over ROM commands, allowing
updates without reflashing.

| Command     | Description |
|-------------|-------------|
| `crx`       | file receiver — download from PC to RP6502 over UART |
| `ctx`       | file sender — upload from RP6502 to PC over UART |
| `date`      | clock, calendar and RTC management |
| `dir`       | directory listing with optional wildcards and sorting |
| `drives`    | list available drives |
| `help`      | show help; `help <command>` for command-specific info |
| `hex`       | hex dump of a file |
| `keyboard`  | interactive keyboard state visualiser |
| `label`     | show or set active drive volume label |
| `pack`      | create or extract ZIP archives |
| `peek`      | memory viewer (RAM or XRAM) |
| `roms`      | tile browser for `.rp6502` files — navigate and launch |
| `ss-matrix` | screensaver: Matrix rain |
| `ss-noise`  | screensaver: character noise |
| `view`      | display monochrome BMP (640×480×1bpp) |

#### Command details

**`dir`**
```
dir                 list all files
dir *.rp6502        filter by wildcard
dir /dd             sort by date descending (newest first)
dir /da             sort by date ascending
dir /sd             sort by size descending
dir /sa             sort by size ascending
```

**`date`**
```
date                current date and time
date /s yyyy-mm-dd hh:mm:ss   set RTC
date /a             time, date and calendar
date /c             calendar of current month
date /c /p yyyy mm  calendar of specific month
date /c /n          current month and neighbours
date /c /q          current quarter
date /c /y          current year
```

**`pack`**
```
pack <dirname>        create ZIP archive (STORE, no compression)
pack <dirname> /d     create ZIP archive (DEFLATE, 4 KB LZ77 window)
pack /x <file.zip>    extract ZIP archive to <file> directory
```
Output archive: `<dirname>.zip`. Extraction overwrites existing files.

**`view`**
```
view image.bmp              display BMP file
view /x 0x2000              display XRAM from given address as bitmap
view image.bmp /d           display and dump XRAM to image.bin
```

**`hex`**
```
hex file.bin                full hex dump
hex file.bin 0x0600 512     512 bytes starting at offset 0x0600
```

**`peek`**
```
peek 0xA000 128             show 128 bytes of RAM at 0xA000
peek 0xF000 256 /X          show 256 bytes of XRAM at 0xF000
```

**`cart`**
```
cart hass                   launch hass assembler (interactive)
cart hass source.asm        assemble source.asm → out.bin
cart hass source.asm -o result.bin
```

### Standalone applications

| Application | Description |
|-------------|-------------|
| `hass`      | Handy ASSembler for WDC65C02S (ROM: `hass.rp6502`) |

Full documentation: `ROM:hass-manual-en.txt` or `ROM:hass-manual-pl.txt`

---

## File transfer

**PC → RP6502** (receive a file on the Picocomputer):
1. Run `crx` on the shell
2. Run `ctx.py <file>` on the PC

**RP6502 → PC** (send a file from the Picocomputer):
1. Run `ctx` on the shell
2. Run `crx.py <file>` on the PC

Protocol: Intel HEX over UART.

---

## Building from source

Requirements: cc65, CMake, Pico SDK.

```
mkdir build && cd build
cmake ..
make
```

External `.com` commands:
```
cd src/extcmd
make CMD=<name>     # e.g. make CMD=pack
```
Output: `src/extcmd/build/<name>.com`
