#!/usr/bin/env python3

# razemOScmd.py
# Build and optionally deploy razemOS .com shell extensions for Picocomputer 6502
#
# Replaces (without removing):
#   make-com.py         — build one + upload via ctx.py  to MSC0:/SHELL
#   make-com2shell.py   — build one + upload via ctx.py  to /SHELL
#   make-coms.py        — build all, no upload
#   make-coms2shell.py  — build all + upload via rp6502.py
#
# Copyright (c) 2026 WojciechGw
#
# Usage examples:
#   razemOScmd.py tree                         build tree.com
#   razemOScmd.py tree --upload                build + upload via ctx.py to MSC0:/SHELL
#   razemOScmd.py tree --upload --shell /SHELL upload to /SHELL
#   razemOScmd.py --all                        build all ext-*.c
#   razemOScmd.py --all --clean                clean artefacts, then build all
#   razemOScmd.py --all --upload               build all + upload via rp6502.py
#   razemOScmd.py --all --upload --uploader ctx  build all + upload each via ctx.py

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
    from serial import SerialException
except ImportError:
    serial = None          # type: ignore[assignment]
    SerialException = OSError

PROMPT_TIMEOUT_MS = 5000

# ---------------------------------------------------------------------------
# Serial helpers
# ---------------------------------------------------------------------------

def _open_serial(port_name: str, baudrate: int):
    if serial is None:
        raise RuntimeError("pyserial is not installed (pip install pyserial)")
    port = serial.Serial(
        port=port_name,
        baudrate=baudrate,
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


def _wait_for_prompt(port, timeout_ms: int, prompt: str) -> bool:
    deadline = time.monotonic() + timeout_ms / 1000.0
    prompt_bytes = prompt.encode("ascii", errors="ignore")
    while time.monotonic() < deadline:
        try:
            if port.read(1) == prompt_bytes:
                return True
        except Exception:
            pass
    return False


def _send_and_wait(port, text: str, label: str, prompt: str) -> None:
    for attempt in range(3):
        try:
            port.write((text + "\r").encode("ascii", errors="ignore"))
            port.flush()
            if not _wait_for_prompt(port, PROMPT_TIMEOUT_MS, prompt):
                print(f"  Warning: timeout waiting for '{prompt}' after '{label}'",
                      file=sys.stderr)
            return
        except Exception:
            if attempt < 2:
                time.sleep(0.2)
            else:
                raise

# ---------------------------------------------------------------------------
# Build helpers
# ---------------------------------------------------------------------------

def _find_make() -> str:
    for candidate in ("make", "mingw32-make"):
        try:
            r = subprocess.run(
                [candidate, "--version"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if r.returncode == 0:
                return candidate
        except FileNotFoundError:
            pass
    return "make"


def _run(args: list[str], cwd: Path | None = None) -> int:
    return subprocess.run(args, cwd=str(cwd) if cwd else None).returncode


def _update_appver(src: Path, appver: str) -> None:
    if not src.exists():
        return
    content = src.read_text(encoding="utf-8")
    updated = re.sub(
        r'#define APPVER "[^"]*"',
        f'#define APPVER "{appver}"',
        content,
    )
    src.write_text(updated, encoding="utf-8", newline="")


def _clean_artefacts(extcmd_dir: Path) -> None:
    removed = 0
    for pattern in ("build/*.com", "map/*.map"):
        for f in extcmd_dir.glob(pattern):
            if f.is_file():
                f.unlink()
                removed += 1
    if removed:
        print(f"  Cleaned {removed} artefact(s).")


def _build_one(cmd: str, start: str, extcmd_dir: Path, src_dir: Path, make_exe: str) -> int:
    appver = datetime.now().strftime("%Y%m%d.%H%M")
    _update_appver(src_dir / f"ext-{cmd}.c", appver)
    print(f"Building: {cmd}  (START={start})")
    return _run([make_exe, f"CMD={cmd}", f"START={start}"], cwd=extcmd_dir)

# ---------------------------------------------------------------------------
# Upload helpers
# ---------------------------------------------------------------------------

def _upload_ctx(
    com_file: Path,
    shell_path: str,
    port_name: str,
    baudrate: int,
    ctx_py: Path,
) -> bool:
    """Navigate to shell_path over serial, send file via ctx.py, then cls."""
    serial_ok = False
    try:
        port = _open_serial(port_name, baudrate)
        try:
            serial_ok = True
            time.sleep(0.2)
            _send_and_wait(port, "0:", "0:", ">")
            time.sleep(0.2)
            _send_and_wait(port, f"cd {shell_path}", f"cd {shell_path}", ">")
        finally:
            port.close()
    except (SerialException, OSError, RuntimeError) as exc:
        print(f"  Warning: serial unavailable ({exc})", file=sys.stderr)

    rc = _run([sys.executable, str(ctx_py), str(com_file)], cwd=ctx_py.parent)
    if rc != 0:
        return False

    if serial_ok:
        try:
            port = _open_serial(port_name, baudrate)
            try:
                time.sleep(0.2)
                _send_and_wait(port, "cls", "cls", ">")
            finally:
                port.close()
        except (SerialException, OSError, RuntimeError):
            pass

    return True


def _upload_rp6502(
    com_file: Path,
    rp6502_py: Path,
    rp6502_cfg: Path,
    cwd: Path,
) -> bool:
    """Upload via rp6502.py."""
    rc = _run(
        [sys.executable, str(rp6502_py), "-c", str(rp6502_cfg), "upload", str(com_file)],
        cwd=cwd,
    )
    return rc == 0

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        prog="razemOScmd.py",
        description="Build and deploy razemOS .com shell extensions.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
examples:
  razemOScmd.py tree                          build tree.com
  razemOScmd.py tree --upload                 build + upload via ctx.py to MSC0:/SHELL
  razemOScmd.py tree --upload --shell /SHELL  upload to /SHELL
  razemOScmd.py --all                         build all ext-*.c commands
  razemOScmd.py --all --clean                 clean artefacts, then build all
  razemOScmd.py --all --upload                build all + upload via rp6502.py
  razemOScmd.py --all --upload --uploader ctx build all + upload each via ctx.py
        """,
    )

    parser.add_argument(
        "cmd", nargs="?",
        help="command to build, e.g. tree, dir, help  (omit when using --all)",
    )
    parser.add_argument(
        "--all", action="store_true",
        help="build all ext-*.c commands",
    )
    parser.add_argument(
        "--upload", action="store_true",
        help="upload .com after building",
    )
    parser.add_argument(
        "--uploader", choices=["ctx", "rp6502"], default=None,
        help="upload method: ctx (default for single cmd) or rp6502 (default for --all)",
    )
    parser.add_argument(
        "--shell", default="MSC0:/SHELL",
        help="shell directory for ctx.py upload (default: MSC0:/SHELL)",
    )
    parser.add_argument(
        "--start", default="8000",
        help="load address passed to make (default: 8000)",
    )
    parser.add_argument(
        "--port", default="COM4",
        help="serial port for ctx.py upload (default: COM4)",
    )
    parser.add_argument(
        "--baud", type=int, default=115200,
        help="baud rate (default: 115200)",
    )
    parser.add_argument(
        "--clean", action="store_true",
        help="remove all .com and .map artefacts (can be used standalone)",
    )

    args = parser.parse_args()

    if not args.all and not args.cmd and not args.clean:
        parser.error("provide a command name, --all, or --clean")

    script_root = Path(__file__).resolve().parent
    src_dir    = (script_root / ".." / "src").resolve()
    extcmd_dir = (script_root / ".." / "src" / "extcmd").resolve()
    ctx_py     = (script_root / "ctx.py").resolve()
    rp6502_py  = (script_root / "rp6502.py").resolve()
    rp6502_cfg = (script_root / ".." / ".rp6502").resolve()

    if args.clean:
        _clean_artefacts(extcmd_dir)
        if not args.all and not args.cmd:
            return 0

    make_exe = _find_make()

    # Resolve list of commands to process
    if args.all:
        sources = sorted(src_dir.glob("ext-*.c"), key=lambda p: p.name)
        if not sources:
            print(f"ERROR: no ext-*.c files found in {src_dir}", file=sys.stderr)
            return 1
        cmds = [re.sub(r"^ext-", "", f.stem) for f in sources]
    else:
        cmds = [args.cmd]

    # Default upload method
    uploader = args.uploader or ("rp6502" if args.all else "ctx")

    # Build (and optionally upload) each command
    for cmd in cmds:
        rc = _build_one(cmd, args.start, extcmd_dir, src_dir, make_exe)
        if rc != 0:
            print(f"ERROR: build failed for '{cmd}'", file=sys.stderr)
            return rc

        if args.upload:
            com_file = extcmd_dir / "build" / f"{cmd}.com"
            print(f"Uploading: {com_file.name}  (via {uploader})")
            if uploader == "ctx":
                ok = _upload_ctx(com_file, args.shell, args.port, args.baud, ctx_py)
            else:
                ok = _upload_rp6502(com_file, rp6502_py, rp6502_cfg, script_root)
            if not ok:
                print(f"ERROR: upload failed for '{cmd}'", file=sys.stderr)
                return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
