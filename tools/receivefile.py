import argparse
import sys
from typing import Optional, Tuple

try:
    import serial  # pyserial
except ImportError:
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    sys.exit(1)


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


def receive_intel_hex(port: str, baud: int, outfile: str, timeout: float = 1.0) -> None:
    with serial.Serial(port, baudrate=baud, bytesize=serial.EIGHTBITS,
                       parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                       timeout=timeout) as ser, open(outfile, "wb") as f:
        print(f"Receiving Intel HEX on {port} @ {baud} into {outfile} ...")
        while True:
            line_bytes = ser.readline()
            if not line_bytes:
                continue
            try:
                line = line_bytes.decode("ascii").strip()
            except UnicodeDecodeError:
                continue
            rec = parse_hex_line(line)
            if rec is None:
                continue
            rtype, data = rec
            if rtype == 0x00:  # data
                if data:
                    f.write(data)
            elif rtype == 0x01:  # EOF
                print("EOF received.")
                break
            else:
                # ignore other record types
                pass
        print("Done.")


def main() -> None:
    ap = argparse.ArgumentParser(description="Receive Intel HEX over serial and save to file.")
    ap.add_argument("outfile", help="Path to output file")
    ap.add_argument("--port", default="COM4", help="Serial port (default COM4)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200)")
    ap.add_argument("--timeout", type=float, default=1.0, help="Read timeout seconds")
    args = ap.parse_args()
    receive_intel_hex(args.port, args.baud, args.outfile, args.timeout)


if __name__ == "__main__":
    main()
