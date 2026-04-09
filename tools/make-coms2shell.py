#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path


def remove_matching_files(directory: Path, pattern: str) -> None:
    if not directory.exists():
        return
    for path in directory.glob(pattern):
        if path.is_file():
            path.unlink()


def update_appver(source_file: Path, appver: str) -> None:
    content = source_file.read_text(encoding="utf-8")
    updated = re.sub(
        r'#define APPVER "[^"]*"',
        f'#define APPVER "{appver}"',
        content,
    )
    source_file.write_text(updated, encoding="utf-8", newline="")


def run_command(args: list[str], cwd: Path | None = None) -> int:
    result = subprocess.run(args, cwd=str(cwd) if cwd else None)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--start", type=str, default="A000")
    args = parser.parse_args()

    script_root = Path(__file__).resolve().parent

    src_dir = (script_root / ".." / "src").resolve()
    files = sorted(src_dir.glob("ext-*.c"), key=lambda p: p.name)

    if not files:
        print(f"There are no files ext-*.c in catalog: {src_dir}", file=sys.stderr)
        return 1

    extcmd_dir = (script_root / ".." / "src" / "extcmd").resolve()
    build_dir = extcmd_dir / "build"
    map_dir = extcmd_dir / "map"

    remove_matching_files(build_dir, "*.com")
    remove_matching_files(map_dir, "*.map")

    work_dir = (script_root / ".." / "src" / "extcmd").resolve()
    rp6502_py = (script_root / "rp6502.py").resolve()
    rp6502_cfg = (script_root / ".." / ".rp6502").resolve()

    python_cmd = sys.executable

    for file in files:
        cmd = re.sub(r"^ext-", "", file.stem)

        appver = datetime.now().strftime("%Y%m%d.%H%M")
        update_appver(file, appver)

        print(f"Executing: make CMD={cmd} START={args.start}")
        rc = run_command(["make", f"CMD={cmd}", f"START={args.start}"], cwd=work_dir)
        if rc != 0:
            print(f"End with error for CMD={cmd}", file=sys.stderr)
            return rc

        com_file = work_dir / "build" / f"{cmd}.com"
        rc = run_command(
            [
                python_cmd,
                str(rp6502_py),
                "-c",
                str(rp6502_cfg),
                "upload",
                str(com_file),
            ],
            cwd=work_dir,
        )
        if rc != 0:
            print(f"End with error for CMD={cmd}", file=sys.stderr)
            return rc

    return 0


if __name__ == "__main__":
    sys.exit(main())
