#!/usr/bin/env python3
import argparse
import pathlib
import subprocess
import sys
import time
from contextlib import contextmanager

import serial  # pip install pyserial

PROMPT_TIMEOUT_MS = 5000

@contextmanager
def serial_port(name: str, baud: int = 115200):
    port = serial.Serial(
        port=name,
        baudrate=baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.2,
        write_timeout=3,
        dsrdtr=False,
        rtscts=False,
    )
    try:
        port.dtr = True
        port.rts = True
        yield port
    finally:
        port.close()


def wait_for_prompt(port: serial.Serial, prompt: str, timeout_ms: int) -> bool:
    deadline = time.time() + timeout_ms / 1000
    while time.time() < deadline:
        ch = port.read(1)
        if ch:
            if ch.decode(errors="ignore") == prompt:
                return True
        else:
            continue  # timeout on read; keep looping
    return False


def send_line_and_wait(port: serial.Serial, text: str, label: str, prompt: str, delay_ms: int):
    max_retries = 2
    for attempt in range(max_retries + 1):
        try:
            port.write((text + "\r").encode())
            port.flush()
            if not wait_for_prompt(port, prompt, PROMPT_TIMEOUT_MS):
                sys.stderr.write(f"Warning: Timeout for '{prompt}' after sending '{label}'\n")
            return
        except Exception as exc:  # serial.SerialTimeoutException or others
            if attempt < max_retries:
                time.sleep(delay_ms / 1000)
                continue
            raise exc


def send_line(port: serial.Serial, text: str, delay_ms: int = 400):
    max_retries = 2
    for attempt in range(max_retries + 1):
        try:
            port.write((text + "\r").encode())
            port.flush()
            return
        except Exception as exc:
            if attempt < max_retries:
                time.sleep(delay_ms / 1000)
                continue
            raise exc


def main():
    parser = argparse.ArgumentParser(description="Install rp6502 shell extension over COM port")
    parser.add_argument("--port", default="COM4", help="Serial port name (default: COM4)")
    parser.add_argument("--shellextcmdname", default="shell", help="Extension command name")
    parser.add_argument("--shellreboot", choices=["Y", "N"], default="Y", help="Reboot after install?")
    args = parser.parse_args()

    project_root = pathlib.Path(__file__).resolve().parent
    build_path = project_root.parent / "build" / f"{args.shellextcmdname}.rp6502"

    # Initial cleanup on device
    try:
        with serial_port(args.port) as port:
            time.sleep(0.5)
            send_line_and_wait(port, "exit", "exit", "]", 400)
            time.sleep(0.4)
            send_line_and_wait(port, "0:", "0:", "]", 400)
            time.sleep(0.4)
            send_line_and_wait(port, "cd /", "cd /", "]", 400)
            time.sleep(0.4)
    except Exception as exc:
        sys.stderr.write(f"Warning: {args.port} is not available ({exc})\n")

    # Upload binary
    upload_script = project_root / "rp6502.py"
    upload_cmd = ["python3", str(upload_script), "upload", "-D", args.port, str(build_path)]
    try:
        subprocess.run(upload_cmd, check=True)
    except FileNotFoundError:
        sys.stderr.write(f"Upload failed: rp6502.py not found at {upload_script}\n")
        return
    except subprocess.CalledProcessError as exc:
        sys.stderr.write(f"Upload failed with exit code {exc.returncode}\n")
        return

    # Install on device
    try:
        with serial_port(args.port) as port:
            time.sleep(0.4)
            send_line_and_wait(port, "set boot -", "set boot -", "]", 400)
            time.sleep(0.4)
            send_line_and_wait(port, f"remove {args.shellextcmdname}", f"remove {args.shellextcmdname}", "]", 400)
            time.sleep(0.4)
            send_line_and_wait(port, f"install {args.shellextcmdname}.rp6502", f"install {args.shellextcmdname}", "]", 400)
            time.sleep(0.4)
            send_line_and_wait(port, f"set boot {args.shellextcmdname}", f"set boot {args.shellextcmdname}", "]", 400)
            time.sleep(0.4)
            if args.shellreboot != "Y":
                send_line_and_wait(port, "shell", "shell", ">", 400)
            else:
                send_line(port, "reboot")
    except Exception as exc:
        sys.stderr.write(f"Warning: {args.port} is not available ({exc})\n")


if __name__ == "__main__":
    main()
