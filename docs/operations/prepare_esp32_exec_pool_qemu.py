#!/usr/bin/env python3
"""从 ESP32 Wi-Fi IRAM-off 精确 QEMU 输入装配只读内存能力探针。"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil


HERE = Path(__file__).resolve().parent
BASELINE = {
    "firmware/sdkconfig": "b1f4e090370e5ac7b20a05cf84faf941f39c847df7b18eccc1e777e90586d6ff",
    "firmware/build/esp_base.bin": "d57ca7be871f59b6366b8614e1349dffb3cba9506c5438571956e1bfae6c4e32",
    "firmware/dependencies.lock.esp32": "ccc359c214e1ef183affff4b2e09d59c70cdeba6f3ce92cc909c4401d97a8203",
    "firmware/apps/esp_base/main/CMakeLists.txt": "c5a5a1e1747fb9c90bb7a4a9fa88203cd0069fd7f1f17d6296d4b9d6de25a8c9",
    "firmware/apps/esp_base/main/capacity_runtime_probe.c": "b1526168e0f0db1a21869a485bd7dc165cdfcbe37e0637ea9d9e0534bf80ea1f",
    "firmware/apps/esp_base/main/aead_4096.bin": "2855df4bd4199f7ce21526c33bcc0b21776adf4d6e5b9f631e43491ea9d30e20",
    "firmware/apps/esp_base/main/aead_65536.bin": "35979812621d6b6778c4086f937991353cfa7f73a4c1aa60dfcfc5507b2542aa",
}
COMPONENTS = ("esp_container", "esp_frp", "esp_ota", "mqtt")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def replace_once(path: Path, old: str, new: str) -> None:
    data = path.read_text()
    if data.count(old) != 1:
        raise SystemExit(f"预期恰好一处装配接点：{path} {old!r}")
    path.write_text(data.replace(old, new, 1))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="已冻结 Wi-Fi IRAM-off ESP32 probe 目录")
    parser.add_argument("destination", type=Path, help="尚不存在的独立仓外目录")
    args = parser.parse_args()
    source, destination = args.source.resolve(), args.destination.resolve()
    if destination.exists() or destination.is_relative_to(source):
        parser.error("目标目录已存在或嵌套在输入目录")
    for relative, expected in BASELINE.items():
        path = source / relative
        if not path.is_file() or sha256(path) != expected:
            parser.error(f"输入摘要不符：{relative}")
    fixture = HERE / "esp32_exec_pool_probe.c"
    if not fixture.is_file():
        parser.error(f"缺少内存能力 fixture：{fixture}")

    shutil.copytree(source, destination, ignore=shutil.ignore_patterns("build", "*.log"))
    firmware = destination / "firmware"
    lock = firmware / "dependencies.lock.esp32"
    old_prefix = "../../../../private/private/tmp/esp32-auth-wifi-iram-off-exact-20260927/probe/firmware/components/"
    data = lock.read_text()
    for component in COMPONENTS:
        before = old_prefix + component
        if data.count(before) != 1:
            parser.error(f"target 锁缺少本地组件路径：{component}")
        data = data.replace(before, str(firmware / "components" / component), 1)
    lock.write_text(data)

    main_dir = firmware / "apps/esp_base/main"
    shutil.copy2(fixture, main_dir / "exec_pool_probe.c")
    cmake = main_dir / "CMakeLists.txt"
    replace_once(cmake, '"capacity_runtime_probe.c" EMBED_FILES',
                 '"capacity_runtime_probe.c" "exec_pool_probe.c" EMBED_FILES')
    replace_once(cmake, 'REQUIRES esp_frp', 'REQUIRES esp_hw_support esp_frp')
    probe = main_dir / "capacity_runtime_probe.c"
    replace_once(probe, 'static const char *const TAG = "five-capacity-auth";',
                 'static const char *const TAG = "five-capacity-auth";\n'
                 'void capacity_exec_pool_probe(void);')
    replace_once(probe, 'report_heap("guest_event_done");\n'
                 '        if (delivered == ECONTAINER_RUNTIME_OK && guest_result == 3) {',
                 'report_heap("guest_event_done");\n'
                 '        if (s_run == 2 && delivered == ECONTAINER_RUNTIME_OK && guest_result == 3)\n'
                 '            capacity_exec_pool_probe();\n'
                 '        if (delivered == ECONTAINER_RUNTIME_OK && guest_result == 3) {')
    print(f"source_config_sha256={BASELINE['firmware/sdkconfig']}")
    print(f"source_signed_app_sha256={BASELINE['firmware/build/esp_base.bin']}")
    print(f"candidate_config_sha256={sha256(firmware / 'sdkconfig')}")
    print(f"relocated_lock_sha256={sha256(lock)}")
    print(f"fixture_sha256={sha256(fixture)}")
    print(f"destination={destination}")


if __name__ == "__main__":
    main()
