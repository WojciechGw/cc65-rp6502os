# File receiver from Picocomputer 6502
# run "ctx filename_to_send" on Picocomputer' OS Shell to send file
#
# Copyright (c) 2026 WojciechGw
#

import argparse
import sys
from typing import Optional, Tuple

try:
    import serial  # pyserial
except ImportError:
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    sys.exit(1)

try:
    import msvcrt
    _HAS_MSVCRT = True
except ImportError:
    _HAS_MSVCRT = False

try:
    import termios, tty, select as _select
    _HAS_TERMIOS = True
except ImportError:
    _HAS_TERMIOS = False


def _esc_pressed() -> bool:
    """Non-blocking check for Escape key (Windows and Linux)."""
    if _HAS_MSVCRT:
        while msvcrt.kbhit():
            if msvcrt.getch() == b'\x1b':
                return True
        return False
    if _HAS_TERMIOS:
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            if _select.select([sys.stdin], [], [], 0)[0]:
                ch = sys.stdin.read(1)
                if ch == '\x1b':
                    return True
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
    return False


def parse_hex_line(line: str) -> Optional[Tuple[int, bytes]]:
    """Parse Intel HEX line. Returns (record_type, data) or None on error."""
    line = line.strip()
    if not line.startswith(":") or len(line) < 11:
        return None
    try:
        bc = int(line[1:3], 16)
        addr_hi = int(line[3:5], 16)
        addr_lo = int(line[5:7], 16)
        rec_type = int(line[7:9], 16)
    except ValueError:
        return None
    data_hex = line[9:-2]
    if len(data_hex) != bc * 2:
        return None
    try:
        checksum = int(line[-2:], 16)
    except ValueError:
        return None

    data = bytes(int(data_hex[i:i + 2], 16) for i in range(0, len(data_hex), 2))
    total = bc + addr_hi + addr_lo + rec_type + sum(data) + checksum
    if (total & 0xFF) != 0:
        return None
    return rec_type, data


def receive_stream(ser: "serial.Serial", check_cancel=None) -> Optional[bytes]:
    """Receive one complete Intel HEX stream until EOF record. Returns accumulated data bytes, or None if cancelled."""
    buf = bytearray()
    while True:
        if check_cancel and check_cancel():
            return None
        line_bytes = ser.readline()
        if not line_bytes:
            continue
        try:
            line = line_bytes.decode("ascii").strip()
        except UnicodeDecodeError:
            continue
        # strip any leading junk (e.g. ANSI escape sequences) before the colon
        colon_idx = line.find(":")
        if colon_idx < 0:
            continue
        line = line[colon_idx:]
        rec = parse_hex_line(line)
        if rec is None:
            continue
        rtype, data = rec
        if rtype == 0x00:
            buf.extend(data)
        elif rtype == 0x01:  # EOF — end of this stream
            break
    return bytes(buf)


def parse_header(data: bytes) -> Tuple[str, int, Optional[int]]:
    """Parse header block: null-terminated filename + 4-byte LE filesize + 4-byte LE checksum."""
    null_pos = data.index(0)
    name = data[:null_pos].decode("ascii", errors="replace")
    size_bytes = data[null_pos + 1: null_pos + 5]
    filesize = int.from_bytes(size_bytes, "little")
    ck_bytes = data[null_pos + 5: null_pos + 9]
    checksum = int.from_bytes(ck_bytes, "little") if len(ck_bytes) == 4 else None
    return name, filesize, checksum

BAR_WIDTH = 47
def draw_progress(done: int, total: int) -> None:
    if total > 0:
        pct = min(100, int(100 * done / total))
        filled = min(BAR_WIDTH, int(BAR_WIDTH * done / total))
    else:
        pct = 0
        filled = 0
    bar = "\u2588" * filled + "\u2591" * (BAR_WIDTH - filled)
    print(f"\r\u2588 [{bar}] {pct:3d}%", end="", flush=True)


def main() -> None:
    ap = argparse.ArgumentParser(description="Receive Courier TX (header + data streams) and save to file.")
    ap.add_argument("--outfile", "-o", default=None, help="Override output filename from header")
    ap.add_argument("--port",    default="COM4",  help="Serial port (default COM4)")
    ap.add_argument("--baud",    type=int, default=115200, help="Baud rate (default 115200)")
    ap.add_argument("--timeout", type=float, default=2.0,  help="Read timeout seconds (default 2.0)")
    args = ap.parse_args()

    with serial.Serial(args.port, baudrate=args.baud, bytesize=serial.EIGHTBITS,
                       parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                       timeout=args.timeout) as ser:

        # Phase 1 — header stream
        print();
        print(f"\u2588\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2510")
        print(f"\u2588  Courier RX \u2014 file receiver for Picocomputer 6502     \u2502")
        print(f"\u2588\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2518")
        print(f"\u2588 Waiting for header on {args.port} @ {args.baud} ... (Esc to cancel)")
        ser.timeout = 0.1
        hdr_data = receive_stream(ser, check_cancel=_esc_pressed)
        if hdr_data is None:
            print("\u2588 Cancelled.")
            return
        ser.timeout = args.timeout
        filename, filesize, hdr_checksum = parse_header(hdr_data)
        outfile = args.outfile if args.outfile else filename
        print(f"\u2588 Filename  : {filename}")
        print(f"\u2588 File size : {filesize} B")
        if hdr_checksum is not None:
            print(f"\u2588 Checksum  : {hdr_checksum:08X}")
        print(f"\u2588 Saving to : {outfile}")

        # Phase 2 — file data stream with live progress
        buf = bytearray()
        cancelled = False
        draw_progress(0, filesize)
        ser.timeout = 0.1
        while True:
            if _esc_pressed():
                cancelled = True
                ser.write(b'\x1b')
                break
            line_bytes = ser.readline()
            if not line_bytes:
                continue
            try:
                line = line_bytes.decode("ascii").strip()
            except UnicodeDecodeError:
                continue
            colon_idx = line.find(":")
            if colon_idx < 0:
                continue
            rec = parse_hex_line(line[colon_idx:])
            if rec is None:
                continue
            rtype, data = rec
            if rtype == 0x00:
                buf.extend(data)
                draw_progress(len(buf), filesize)
            elif rtype == 0x01:
                break

        draw_progress(len(buf), filesize)
        print()

        if cancelled:
            print("\u2588 Cancelled. No data was saved.")
            return

        with open(outfile, "wb") as f:
            f.write(buf)
        print(f"\u2588 Done. {len(buf)} bytes written to {outfile}.")
        if hdr_checksum is not None:
            calc = sum(buf) & 0xFFFFFFFF
            if calc == hdr_checksum:
                print(f"\u2588 Checksum OK  : {calc:08X}")
            else:
                print(f"\u2588 Checksum FAIL: expected {hdr_checksum:08X}, got {calc:08X}")
        print()


if __name__ == "__main__":
    main()
