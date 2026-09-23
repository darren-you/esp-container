#!/usr/bin/env python3
"""Compile the real counter and narrow runtime fault fixtures with wasi-sdk 33."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import counter_guest  # noqa: E402


VARIANTS = {
    "event-read": None,
    "init-loop": "ECONTAINER_INIT_LOOP",
    "event-loop": "ECONTAINER_EVENT_LOOP",
    "stop-loop": "ECONTAINER_STOP_LOOP",
    "wrong-signature": "ECONTAINER_WRONG_SIGNATURE",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wasi-sdk", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    counter_guest.build(args.wasi_sdk, args.output_dir / "counter.wasm")
    clang = args.wasi_sdk / "bin" / "clang"
    for name, symbol in VARIANTS.items():
        command = [
            str(clang), "--target=wasm32-unknown-unknown", "-std=c11", "-O2",
            "-Wall", "-Wextra", "-Werror", "-nostdlib", "-ffreestanding",
            "-fno-builtin", "-fno-exceptions", "-fno-stack-protector",
            *(f"-mno-{feature}" for feature in counter_guest.FEATURES_OFF),
        ]
        if symbol is not None:
            command.append(f"-D{symbol}")
        command += [
            str(ROOT / "tests" / "runtime_guest.c"),
            "-Wl,--no-entry",
            *(f"-Wl,--export={export}" for export in counter_guest.EXPORT_TYPES),
            f"-Wl,--initial-memory={counter_guest.MEMORY_PAGES * counter_guest.PAGE_BYTES}",
            f"-Wl,--max-memory={counter_guest.MEMORY_PAGES * counter_guest.PAGE_BYTES}",
            f"-Wl,-z,stack-size={counter_guest.STACK_BYTES}",
            "-o", str(args.output_dir / f"{name}.wasm"),
        ]
        subprocess.run(command, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
