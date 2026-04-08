#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

import serial
from serial import SerialException


PROMPT_TIMEOUT_MS = 5000
SERIAL_PORT = "COM4"
BAUDRATE = 115200


def wait_for_prompt(port: serial.Serial, timeout_ms: int, prompt: str) -> bool:
    deadline = time.monotonic() + (timeout_ms / 1000.0)
    prompt_byte = prompt.encode("ascii", errors="ignore")

    while time.monotonic() < deadline:
        try:
            data = port.read(1)
            if data == prompt_byte:
                return True
        except serial.SerialTimeoutException:
            pass

    return False


def send_line_and_wait(port: serial.Serial, text: str, label: str, prompt: str) -> None:
    max_retries = 2

    for attempt in range(max_retries + 1):
        try:
            port.write((text + "\r").encode("ascii", errors="ignore"))
            port.flush()

            if not wait_for_prompt(port, PROMPT_TIMEOUT_MS, prompt):
                print(f"Warning: Timeout for '{prompt}' after sending '{label}'", file=sys.stderr)
            return

        except Exception:
            if attempt < max_retries:
                time.sleep(0.2)
                continue
            raise


def update_appver(src_file: Path, appver: str) -> None:
    if not src_file.exists():
        return

    content = src_file.read_text(encoding="utf-8")
    updated = re.sub(
        r'#define APPVER "[^"]*"',
        f'#define APPVER "{appver}"',
        content,
    )
    src_file.write_text(updated, encoding="utf-8", newline="")


def run_command(args: list[str], cwd: Path | None = None) -> int:
    result = subprocess.run(args, cwd=str(cwd) if cwd else None)
    return result.returncode


def make_command() -> str:
    for candidate in ("make", "mingw32-make"):
        try:
            result = subprocess.run(
                [candidate, "--version"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
            if result.returncode == 0:
                return candidate
        except FileNotFoundError:
            pass

    return "make"


def open_serial_port() -> serial.Serial:
    port = serial.Serial(
        port=SERIAL_PORT,
        baudrate=BAUDRATE,
        parity=serial.PARITY_NONE,
        bytesize=serial.EIGHTBITS,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.2,
        write_timeout=3.0,
        dsrdtr=False,
        rtscts=False,
    )
    port.dtr = True
    port.rts = True
    return port


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--start", default="8000")
    parser.add_argument("--cmd", default="help")
    args = parser.parse_args()

    script_root = Path(__file__).resolve().parent
    appver = datetime.now().strftime("%Y%m%d.%H%M")

    src_file = (script_root / ".." / "src" / f"ext-{args.cmd}.c").resolve()
    update_appver(src_file, appver)

    extcmd_dir = (script_root / ".." / "src" / "extcmd").resolve()
    rp6502_dir = script_root.resolve()
    rp6502_py = (script_root / "rp6502.py").resolve()
    rp6502_cfg = (script_root / ".." / ".rp6502").resolve()
    com_file = (script_root / ".." / "src" / "extcmd" / "build" / f"{args.cmd}.com").resolve()

    make_exe = make_command()

    rc = run_command(
        [make_exe, f"CMD={args.cmd}", f"START={args.start}"],
        cwd=extcmd_dir,
    )
    if rc != 0:
        return rc

    serial_available = False

    try:
        serial_port = open_serial_port()
        try:
            serial_available = True
            time.sleep(0.6)
            send_line_and_wait(serial_port, "exit", "exit", "]")
            time.sleep(0.4)
            send_line_and_wait(serial_port, "0:", "0:", "]")
            time.sleep(0.4)
            send_line_and_wait(serial_port, "cd /SHELL", "cd /SHELL", "]")
            time.sleep(0.4)
        finally:
            serial_port.close()
            serial_port.dispose = getattr(serial_port, "close", None)
    except (SerialException, OSError) as exc:
        print(f"Warning: {SERIAL_PORT} is not available: {exc}", file=sys.stderr)

    rc = run_command(
        [
            sys.executable,
            str(rp6502_py),
            "-c",
            str(rp6502_cfg),
            "upload",
            str(com_file),
        ],
        cwd=rp6502_dir,
    )
    if rc != 0:
        return rc

    if serial_available:
        try:
            serial_port = open_serial_port()
            try:
                time.sleep(0.4)
                send_line_and_wait(serial_port, "cd /", "cd /", "]")
                time.sleep(0.4)
                send_line_and_wait(serial_port, "shell", "shell", ">")
            finally:
                serial_port.close()
                serial_port.dispose = getattr(serial_port, "close", None)
        except (SerialException, OSError) as exc:
            print(f"Warning: {SERIAL_PORT} is not available: {exc}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
