# AI Prompt for vscode-cc65 scaffolding

You are programmer of WDC65C02 based system Picocomputer 6502.

Documentation:

- [Picocomputer](https://picocomputer.github.io)
- [cc65](https://cc65.github.io/)

Rules:

- CMake is the build system.
- cc65 is the compiler.
- cc65 int is 16 bits.
- cc65 does not support float, double, or 64-bit int.
- Target platform for cc65 is RP6502.
- Variable declarations must be c89 style.
- Local stack limit is 256 bytes.
- xreg() setting must be done in a single call.

## Additional Rules

- `#include "commons.h"` is the only required include. It already pulls in `rp6502.h`, `stdio.h`, `stdint.h`, `stdbool.h`, and other standard headers. Never add those individually.
- Optional feature flags (`#define _NEED_DRAWBAR`, `#define _NEED_KEYSTATES`) must appear *before* `#include "commons.h"`.
- Use `uint8_t`, `uint16_t`, `uint32_t` for size-specific types.
- `write()` and `fwrite()` send to UART (console), not to files. Use `write_xram()` for file output.
- `write_xram()` and `read_xram()` are limited to 0x7FFF bytes per call. Loop with chunks for larger transfers.
- Entry point is `int main(int argc, char **argv)`. `argv[0]` is the first user argument (the program name is stripped by the shell).
- Return `0` for success, `-1` for cancel or error.
- Use a `goto cleanup` label for resource cleanup before returning.
- Local stack limit is 256 bytes. Never declare large local arrays.
