#!/usr/bin/env python3
"""删除 ESP32 QEMU 串口日志中的运行时设备标识及终端控制符。"""

import argparse
from pathlib import Path
import re


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    data = args.source.read_text(errors="replace")
    data = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", data)
    data = re.sub(r"(?<=boot_id=)[0-9a-f-]{36}", "<redacted>", data)
    data = re.sub(r"(?<=device_id=)[0-9a-f-]{36}", "<redacted>", data)
    args.destination.write_text("\n".join(line.rstrip() for line in data.splitlines()) + "\n")


if __name__ == "__main__":
    main()
