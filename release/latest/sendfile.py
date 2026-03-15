# Sendfile to Picocomputer 6502
# run courier-rx on Picocomputer' OS Shell to receive file
#
# Copyright (c) 2026 WojciechGw
#

import argparse
import os
import sys
import time
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


def intel_hex_records(data: bytes, chunk_size: int = 16) -> Iterable[str]:
    addr = 0
    while addr < len(data):
        chunk = data[addr:addr + chunk_size]
        count = len(chunk)
        cks = count + ((addr >> 8) & 0xFF) + (addr & 0xFF) + 0x00
        hexdata = "".join(f"{b:02X}" for b in chunk)
        for b in chunk:
            cks += b
        cks = (-cks) & 0xFF
        yield f":{count:02X}{addr:04X}00{hexdata}{cks:02X}"
        addr += count
    # EOF record
    yield ":00000001FF"


def send_intel_hex(port: str, baud: int, filepath: str, chunk_size: int = 16, delay: float = 0.0) -> None:
    filename = os.path.basename(filepath)
    with open(filepath, "rb") as f:
        data = f.read()
    with serial.Serial(port, baudrate=baud, bytesize=serial.EIGHTBITS,
                       parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                       timeout=1) as ser:
        print(f"Sending '{filename}' ({len(data)} bytes) to {port} @ {baud} baud ...")

        # --- naglowek ---
        ser.write(SOT)
        ser.write(SOH)
        ser.write(filename.encode("ascii"))
        ser.write(EOH)

        # --- dane IntelHEX ---
        ser.write(STX)
        for line in intel_hex_records(data, chunk_size):
            ser.write((line + "\r\n").encode("ascii"))
            if delay > 0:
                ser.flush()
                time.sleep(delay)

        # --- koniec transmisji ---
        ser.write(ETX)
        ser.write(EOT)
        ser.flush()
        print("Done.")


def main() -> None:
    ap = argparse.ArgumentParser(description="Send file as Intel HEX over serial.")
    ap.add_argument("filepath", help="Input file to send")
    ap.add_argument("--port", default="COM4", help="Serial port (default COM4)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200)")
    ap.add_argument("--chunk", type=int, default=16, help="Bytes per record (default 16)")
    ap.add_argument("--delay", type=float, default=0.001, help="Delay (s) after each line (default 0.001)")
    args = ap.parse_args()
    send_intel_hex(args.port, args.baud, args.filepath, args.chunk, args.delay)


if __name__ == "__main__":
    main()
