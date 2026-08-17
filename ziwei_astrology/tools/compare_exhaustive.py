#!/usr/bin/env python3
"""Stream a complete Ziwei corpus from Dart and C++ side by side."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import TextIO


def stop(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def finish(
    process: subprocess.Popen[str], label: str, errors: TextIO
) -> None:
    try:
        code = process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        stop(process)
        raise RuntimeError(f"{label} did not exit after closing its output")
    errors.seek(0)
    stderr = errors.read()
    if code != 0:
        raise RuntimeError(f"{label} exited with {code}: {stderr.strip()}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dart-root", required=True, type=Path)
    parser.add_argument("--cpp", required=True, type=Path)
    parser.add_argument("--mode", required=True, type=int, choices=range(3))
    parser.add_argument("--start-year", type=int, default=1984)
    parser.add_argument("--end-year", type=int, default=2043)
    parser.add_argument("--max-records", type=int)
    parser.add_argument(
        "--finite",
        action="store_true",
        help="compare the exact 60x12x30x12x2 finite-state corpus",
    )
    args = parser.parse_args()

    script = Path(__file__).with_name("generate_ziwei_core_oracles.dart")
    package_config = args.dart_root / ".dart_tool" / "package_config.json"
    if not package_config.is_file() or not args.cpp.is_file():
        parser.error("Dart package config or C++ producer is missing")

    common = [str(args.mode)]
    if not args.finite:
        common.extend([str(args.start_year), str(args.end_year)])
    if args.max_records is not None:
        common.append(str(args.max_records))
    dart_errors = tempfile.TemporaryFile(mode="w+", encoding="utf-8")
    cpp_errors = tempfile.TemporaryFile(mode="w+", encoding="utf-8")
    dart = subprocess.Popen(
        [
            "dart",
            f"--packages={package_config}",
            str(script),
            "finite" if args.finite else "exhaustive",
            *common,
        ],
        cwd=args.dart_root,
        stdout=subprocess.PIPE,
        stderr=dart_errors,
        text=True,
        encoding="utf-8",
        bufsize=1,
    )
    cpp = subprocess.Popen(
        [str(args.cpp), *(["finite"] if args.finite else []), *common],
        stdout=subprocess.PIPE,
        stderr=cpp_errors,
        text=True,
        encoding="utf-8",
        bufsize=1,
    )
    assert dart.stdout is not None and cpp.stdout is not None

    records = 0
    try:
        while True:
            dart_line = dart.stdout.readline()
            cpp_line = cpp.stdout.readline()
            if not dart_line or not cpp_line:
                if dart_line != cpp_line:
                    raise RuntimeError(
                        f"producer length mismatch after {records} records"
                    )
                break
            if dart_line != cpp_line:
                raise RuntimeError(
                    "mismatch at record "
                    f"{records}\nDart: {dart_line.rstrip()}\nC++:  {cpp_line.rstrip()}"
                )
            if not dart_line.startswith("#"):
                records += 1
        finish(dart, "Dart oracle", dart_errors)
        finish(cpp, "C++ candidate", cpp_errors)
    except BaseException:
        stop(dart)
        stop(cpp)
        raise
    finally:
        dart_errors.close()
        cpp_errors.close()

    corpus = (
        "the 60x12x30x12x2 finite-state corpus"
        if args.finite
        else f"the physical-calendar window {args.start_year}..{args.end_year}"
    )
    print(f"matched {records} charts for Rat-hour mode {args.mode} over {corpus}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"exhaustive Ziwei comparison failed: {error}", file=sys.stderr)
        raise SystemExit(1)
