# File sender from PC to Picocomputer 6502
# run "python ctx.py filename_to_send" on PC to send file
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
    while addr < len(data):
        chunk = data[addr:addr + chunk_size]
        count = len(chunk)
        addr16 = addr & 0xFFFF  # IHX address field is 16-bit
        cks = count + ((addr16 >> 8) & 0xFF) + (addr16 & 0xFF) + 0x00
        hexdata = "".join(f"{b:02X}" for b in chunk)
        for b in chunk:
            cks += b
        cks = (-cks) & 0xFF
        yield f":{count:02X}{addr16:04X}00{hexdata}{cks:02X}"
        addr += count
    # EOF record
    yield ":00000001FF"


def send_intel_hex(port: str, baud: int, filepath: str, chunk_size: int = 16,
                   ack_timeout: float = 5.0) -> None:
    filename = os.path.basename(filepath)
    with open(filepath, "rb") as f:
        data = f.read()
    total = len(data)
    checksum = sum(data) & 0xFFFFFFFF
    records = list(intel_hex_records(data, chunk_size))
    n_data_records = (total + chunk_size - 1) // chunk_size  # bez rekordu EOF

    with serial.Serial(port, baudrate=baud, bytesize=serial.EIGHTBITS,
                       parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                       timeout=ack_timeout) as ser:
        print();
        print(f"\u2588\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2510")
        print(f"\u2588  Courier TX \u2014 file sender for Picocomputer 6502       \u2502")
        print(f"\u2588\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2518")
        print(f"Sending '{filename}' ({total} B) to {port} @ {baud} baud ...")
        print(f"Checksum       : {checksum:08X}")

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
        sent = 0
        for i, line in enumerate(records):
            ser.write((line + "\r\n").encode("ascii"))
            ser.flush()
            is_eof = (i == len(records) - 1)  # last record is always the EOF record
            if not is_eof:
                if not wait_for(ser, ACK, ack_timeout, f"ACK '+' record {i}"):
                    return
            if i < n_data_records:
                sent += min(chunk_size, total - i * chunk_size)
                pct = min(100, int(100 * sent / total))
                bar = "\u2588" * (pct * 20 // 100) + "\u2591" * (20 - pct * 20 // 100)
                print(f"\r[{bar}] {pct:3d}%", end="", flush=True)

        # --- koniec transmisji ---
        print()
        ser.write(ETX)
        ser.write(EOT)
        ser.flush()
        print("Done.")


def main() -> None:
    ap = argparse.ArgumentParser(description="Send file as Intel HEX over serial (ACK flow control).")
    ap.add_argument("filepath", help="Input file to send")
    ap.add_argument("--port",    default="COM4",  help="Serial port (default COM4)")
    ap.add_argument("--baud",    type=int, default=115200, help="Baud rate (default 115200)")
    ap.add_argument("--chunk",   type=int, default=16, help="Bytes per record (default 16)")
    ap.add_argument("--timeout", type=float, default=5.0, help="ACK timeout seconds (default 5.0)")
    args = ap.parse_args()
    send_intel_hex(args.port, args.baud, args.filepath, args.chunk, args.timeout)


if __name__ == "__main__":
    main()
