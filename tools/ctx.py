# File sender from PC to Picocomputer 6502
# run "python ctx.py filename_to_send" on PC to send file
#
# Copyright (c) 2026 WojciechGw
#

import argparse
import os
import sys
import time
import zlib
from typing import Iterable

try:
    import serial  # pyserial
except ImportError:
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# Bajty protokolu
SOT = b'\x01'  # Start of Transmission
EOT = b'\x02'  # End of Transmission
SOH = b'\x03'  # Start of Header
EOH = b'\x04'  # End of Header
STX = b'\x05'  # Start of TeXt (dane IHX)
ETX = b'\x06'  # End of TeXt

ACK   = b'\x00'  # Potwierdzenie odbioru rekordu (NUL — niewidoczny na konsoli VGA)
READY = b'\x00'  # Plik otwarty na Picocomputerze, gotowy na dane


def wait_for(ser, marker: bytes, timeout: float, label: str) -> bool:
    """Czytaj bajty aż do 'marker' (pomijaj ANSI i inne śmieci). False = timeout."""
    deadline = time.monotonic() + timeout
    while True:
        if time.monotonic() > deadline:
            print(f"\nERROR: timeout waiting for {label}", file=sys.stderr)
            return False
        byte = ser.read(1)
        if byte == marker:
            return True


def intel_hex_records(data: bytes, chunk_size: int = 16) -> Iterable[str]:
    addr = 0
    prev_ela = -1
    while addr < len(data):
        # Emit Extended Linear Address record when upper 16 bits change
        ela = addr >> 16
        if ela != prev_ela:
            ela_cks = (-(0x02 + 0x04 + ((ela >> 8) & 0xFF) + (ela & 0xFF))) & 0xFF
            yield f":02000004{ela:04X}{ela_cks:02X}"
            prev_ela = ela
        chunk = data[addr:addr + chunk_size]
        count = len(chunk)
        addr16 = addr & 0xFFFF
        cks = count + ((addr16 >> 8) & 0xFF) + (addr16 & 0xFF) + 0x00
        hexdata = "".join(f"{b:02X}" for b in chunk)
        for b in chunk:
            cks += b
        cks = (-cks) & 0xFF
        yield f":{count:02X}{addr16:04X}00{hexdata}{cks:02X}"
        addr += count
    # EOF record
    yield ":00000001FF"

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

def send_intel_hex(port: str, baud: int, filepath: str, chunk_size: int = 16,
                   ack_timeout: float = 30.0) -> None:
    filename = os.path.basename(filepath)
    with open(filepath, "rb") as f:
        data = f.read()
    total = len(data)
    checksum = zlib.crc32(data) & 0xFFFFFFFF
    with serial.Serial(port, baudrate=baud, bytesize=serial.EIGHTBITS,
                       parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                       timeout=ack_timeout) as ser:
        print();
        print(f"\u2588\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2510")
        print(f"\u2588  Courier TX \u2014 file sender for Picocomputer 6502       \u2502")
        print(f"\u2588\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2518")
        print(f"\u2588 Sending '{filename}' ({total} B)")
        print(f"\u2588 to {port} @ {baud} baud")
        print(f"\u2588 file checksum (CRC32) : {checksum:08X}")

        # --- naglowek ---
        ser.write(SOT)
        ser.write(SOH)
        ser.write(filename.encode("ascii"))
        ser.write(EOH)
        ser.write(bytes([total & 0xFF, (total >> 8) & 0xFF,
                         (total >> 16) & 0xFF, (total >> 24) & 0xFF]))
        ser.write(bytes([checksum & 0xFF, (checksum >> 8) & 0xFF,
                         (checksum >> 16) & 0xFF, (checksum >> 24) & 0xFF]))
        ser.write(STX)
        ser.flush()

        # --- czekaj na READY '>' — plik otwarty po stronie Picocomputera ---
        if not wait_for(ser, READY, ack_timeout, "READY '>'"):
            return

        # --- dane IntelHEX z ACK flow control ---
        # Stream generator directly — do NOT call list() to avoid large memory allocation
        sent = 0
        data_record_idx = 0
        for line in intel_hex_records(data, chunk_size):
            is_eof = line.startswith(":00000001")
            is_ela = line.startswith(":02000004")   # ELA record (type 04), no ACK expected
            ser.write((line + "\r\n").encode("ascii"))
            ser.flush()
            if not is_eof and not is_ela:
                if not wait_for(ser, ACK, ack_timeout, f"ACK record"):
                    return
            if not is_ela and not is_eof:
                sent += min(chunk_size, total - data_record_idx * chunk_size)
                data_record_idx += 1
                pct = min(100, int(100 * sent / total))
                bar = "\u2588" * (pct * BAR_WIDTH // 100) + "\u2591" * (BAR_WIDTH - pct * BAR_WIDTH // 100)
                print(f"\r\u2588 [{bar}] {pct:3d}%", end="", flush=True)

        # --- koniec transmisji ---
        print()
        ser.write(ETX)
        ser.write(EOT)
        ser.flush()
        print("\u2588 Done.\r\n")


def main() -> None:
    ap = argparse.ArgumentParser(description="Send file as Intel HEX over serial (ACK flow control).")
    ap.add_argument("filepath", help="Input file to send")
    ap.add_argument("--port",    default="COM4",  help="Serial port (default COM4)")
    ap.add_argument("--baud",    type=int, default=115200, help="Baud rate (default 115200)")
    ap.add_argument("--chunk",   type=int, default=16, help="Bytes per record (default 16)")
    ap.add_argument("--timeout", type=float, default=30.0, help="ACK timeout seconds (default 30.0)")
    args = ap.parse_args()
    send_intel_hex(args.port, args.baud, args.filepath, args.chunk, args.timeout)


if __name__ == "__main__":
    main()
