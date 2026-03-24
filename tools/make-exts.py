from pathlib import Path
import argparse
import os
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--start", type=int, default=7500)
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    src_dir = (script_dir / ".." / "src").resolve()
    extcmd_dir = (script_dir / ".." / "src" / "extcmd").resolve()

    files = sorted(src_dir.glob("ext-*.c"))

    if not files:
        print(f"There are no files ext-*.c in catalog: {src_dir}", file=sys.stderr)
        return 1

    old_cwd = Path.cwd()
    try:
        os.chdir(extcmd_dir)

        for file in files:
            cmd = file.stem.removeprefix("ext-")
            print(f"Executing: make CMD={cmd} START={args.start}")

            result = subprocess.run(
                ["make", f"CMD={cmd}", f"START={args.start}"],
                check=False,
            )

            if result.returncode != 0:
                print(f"End with error for CMD={cmd}", file=sys.stderr)
                return result.returncode

    finally:
        os.chdir(old_cwd)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
