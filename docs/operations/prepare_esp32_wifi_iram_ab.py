#!/usr/bin/env python3
"""从精确 ESP32 认证 QEMU 基线复制工程，只关闭两项 Wi-Fi IRAM 开关。"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil


BASELINE = {
    "firmware/sdkconfig": "8822f1b68067673594f5392928af8ef997bb95bdf19aef30e0ec89414ad1c49f",
    "firmware/build/esp_base.bin": "60c1d4d824fc4ef2237ecd30d27b6d5d30bb46fdfda36878be60a9a734758991",
    "firmware/dependencies.lock": "20ca84c413c1969b003282d2e1cc465c5ab37f6780969953f6eb83f2d7eaa6df",
    "firmware/dependencies.lock.esp32": "1a6ce02674cc5b4973723570b4ac5d23ef2bf8dc2a0c666dc9bc0d9ee925a866",
    "firmware/apps/esp_base/main/capacity_runtime_probe.c": "b1526168e0f0db1a21869a485bd7dc165cdfcbe37e0637ea9d9e0534bf80ea1f",
    "firmware/apps/esp_base/main/runtime_guest_bytes.h": "55820bd5ed692bf229de5a6b022d69b7aa6e2792ac682ddb5d6b9bf504fce603",
    "firmware/apps/esp_base/main/capacity_references.c": "acf1e812c6b7b830d21232a2a9250415b264b4b3e26ee43c4b82a12c53bef3b4",
    "firmware/apps/esp_base/main/aead_4096.bin": "2855df4bd4199f7ce21526c33bcc0b21776adf4d6e5b9f631e43491ea9d30e20",
    "firmware/apps/esp_base/main/aead_65536.bin": "35979812621d6b6778c4086f937991353cfa7f73a4c1aa60dfcfc5507b2542aa",
}
OPTIONS = ("ESP_WIFI_IRAM_OPT", "ESP_WIFI_RX_IRAM_OPT")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="现有精确 ESP32 QEMU probe 目录")
    parser.add_argument("destination", type=Path, help="尚不存在的独立输出目录")
    args = parser.parse_args()
    source = args.source.resolve()
    destination = args.destination.resolve()
    if destination.exists():
        parser.error(f"输出目录已存在：{destination}")
    for relative, expected in BASELINE.items():
        path = source / relative
        if not path.is_file() or sha256(path) != expected:
            parser.error(f"输入摘要不符：{relative}")
    config = (source / "firmware/sdkconfig").read_text()
    if config.count('CONFIG_IDF_TARGET="esp32"') != 1:
        parser.error("输入不是 ESP32 target")
    changed = config
    for option in OPTIONS:
        before = f"CONFIG_{option}=y"
        if changed.count(before) != 1:
            parser.error(f"预期仅一处开启项：{option}")
        changed = changed.replace(before, f"# CONFIG_{option} is not set", 1)
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns("build", "*.log"))
    result = destination / "firmware/sdkconfig"
    result.write_text(changed)
    # ESP-IDF 的 target 锁还记载仓外组件物理目录；搬运工程时只重定向四个本地路径。
    lock = destination / "firmware/dependencies.lock.esp32"
    lock_text = lock.read_text()
    old_prefix = "../../../../private/private/tmp/esp32-auth-capacity-20260927/probe/firmware/components/"
    for component in ("esp_container", "esp_frp", "esp_ota", "mqtt"):
        before = old_prefix + component
        if lock_text.count(before) != 1:
            parser.error(f"target 锁缺少预期路径：{component}")
        lock_text = lock_text.replace(
            before, str(destination / "firmware/components" / component), 1
        )
    lock.write_text(lock_text)
    print(f"baseline_config_sha256={BASELINE['firmware/sdkconfig']}")
    print(f"baseline_signed_app_sha256={BASELINE['firmware/build/esp_base.bin']}")
    print(f"prepared_config_sha256={sha256(result)}")
    print(f"relocated_target_lock_sha256={sha256(lock)}")
    print(f"destination={destination}")


if __name__ == "__main__":
    main()
