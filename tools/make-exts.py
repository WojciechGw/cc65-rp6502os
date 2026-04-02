# Build all ext-*.c shell extensions for Picocomputer 6502
# Equivalent of make-exts.ps1
#
# Copyright (c) 2026 WojciechGw
#

import argparse
import glob
import os
import re
import subprocess
import sys
from datetime import datetime

def main() -> None:
    ap = argparse.ArgumentParser(description="Build all ext-*.c shell extensions.")
    ap.add_argument("--start", default="7500", help="Load address START (default 7500)")
    args = ap.parse_args()

    tools_dir  = os.path.dirname(os.path.abspath(__file__))
    src_dir    = os.path.normpath(os.path.join(tools_dir, "..", "src"))
    extcmd_dir = os.path.join(src_dir, "extcmd")
    build_dir  = os.path.join(extcmd_dir, "build")
    map_dir    = os.path.join(extcmd_dir, "map")

    sources = sorted(glob.glob(os.path.join(src_dir, "ext-*.c")))
    if not sources:
        print(f"ERROR: no ext-*.c files found in {src_dir}", file=sys.stderr)
        sys.exit(1)

    # Remove old build artefacts
    for path in glob.glob(os.path.join(build_dir, "*.com")):
        os.remove(path)
    for path in glob.glob(os.path.join(map_dir, "*.map")):
        os.remove(path)

    appver_re = re.compile(r'#define APPVER "[^"]*"')

    for src in sources:
        cmd = re.sub(r'^ext-', '', os.path.splitext(os.path.basename(src))[0])
        appver = datetime.now().strftime("%Y%m%d.%H%M")

        text = open(src, "r", encoding="utf-8").read()
        new_text = appver_re.sub(f'#define APPVER "{appver}"', text)
        if new_text != text:
            open(src, "w", encoding="utf-8", newline="").write(new_text)

        print(f"Executing: make CMD={cmd} START={args.start}")
        result = subprocess.run(
            ["make", f"CMD={cmd}", f"START={args.start}"],
            cwd=extcmd_dir
        )
        if result.returncode != 0:
            print(f"ERROR: make failed for CMD={cmd}", file=sys.stderr)
            sys.exit(result.returncode)

if __name__ == "__main__":
    main()
