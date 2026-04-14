# razemOS — RP6502 [WDC65C02S native]

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

The startup screen is displayed while waiting for a Wi-Fi connection (RTC update over NTP).

---

## Memory

User programs running under razemOS have access to `$8000–$FCFF` (~31 KB).
Programs can be created (hass) as `.com` files intended to be external shell commands
or `.exe` files as executables (last two bytes LSB,MSB must pointed to .exe start address, look at hello.asm)
or standard `.rp6502` RP6502 ROMs (BIG ROMS - run by command `cart` or `roms`).

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
commands updates without rebuilding whole `razemos.rp6502`.

| Command     | Description |
|-------------|-------------|
| `crx`       | file receiver — download from PC to RP6502 over UART |
| `ctx`       | file sender — upload from RP6502 to PC over UART |
| `date`      | clock, calendar and RTC management |
| `dir`       | directory listing with optional wildcards and sorting |
| `drives`    | list available drives |
| `help`      | show help; `help <command>` or type command press `F1` for command-specific info |
| `hex`       | hex dump of a file |
| `keyboard`  | interactive keyboard state visualiser |
| `label`     | show or set active drive volume label |
| `pack`      | create or extract ZIP archives (modes STORE without compression or DEFLATE with compression) |
| `peek`      | memory viewer (RAM or XRAM) |
| `roms`      | tile browser for `.rp6502` files — navigate and launch |
| `ss-matrix` | screensaver: Matrix Rain |
| `ss-noise`  | screensaver: Character Noise |
| `tree`      | display directory tree with subdirectories |
| `view`      | display monochrome BMP (640×480×1bpp), dump framebuffer to `.bin` file. |

### Standalone applications

| Application | Description |
|-------------|-------------|
| `hass`      | Handy ASSembler for WDC65C02S (ROM: `hass.rp6502`) Full documentation: `ROM:hass-manual-en.txt` or `ROM:hass-manual-pl.txt` |

---

## Building shell extensions (`razemOScmd.py`)

`tools/razemOScmd.py` builds and optionally uploads razemOS `.com` shell extension commands.
It replaces the older `make-com.py`, `make-coms.py`, `make-com2shell.py`, `make-coms2shell.py` scripts (which are kept for compatibility).

### Usage

```
razemOScmd.py <command> [options]        build a single command
razemOScmd.py --all [options]            build all ext-*.c commands
razemOScmd.py --clean [options]          remove build artefacts (.com, .map)
```

### Examples

| Command | Description |
|---------|-------------|
| `razemOScmd.py tree` | build `tree.com` |
| `razemOScmd.py tree --upload` | build + upload via `ctx.py` to `MSC0:/SHELL` |
| `razemOScmd.py tree --upload --shell /SHELL` | build + upload to `/SHELL` |
| `razemOScmd.py --all` | build all `ext-*.c` commands |
| `razemOScmd.py --all --clean` | clean artefacts, then build all |
| `razemOScmd.py --all --upload` | build all + upload via `rp6502.py` |
| `razemOScmd.py --all --upload --uploader ctx` | build all + upload each via `ctx.py` |
| `razemOScmd.py --clean` | remove all `.com` and `.map` artefacts only |

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--upload` | off | upload `.com` after building |
| `--uploader ctx\|rp6502` | `ctx` (single) / `rp6502` (all) | upload method |
| `--shell <path>` | `MSC0:/SHELL` | destination path for `ctx.py` upload |
| `--start <addr>` | `8000` | load address passed to `make` |
| `--port <port>` | `COM4` | serial port for `ctx.py` upload |
| `--baud <rate>` | `115200` | baud rate for serial upload |
| `--clean` | off | remove `.com` and `.map` artefacts before building |

### Requirements

- Python 3
- `pyserial` — required only for `--uploader ctx` (`pip install pyserial`)
- `make` or `mingw32-make` in PATH

---

## File transfer

**PC → RP6502** (receive a file on the Picocomputer):
1. Run `crx` on the shell
2. Run `ctx.py <file>` on the PC

**RP6502 → PC** (send a file from the Picocomputer):
1. Run `ctx filename` on the shell
2. Run `crx.py` on the PC

Protocol: Intel HEX over UART (with CRC32).

---
