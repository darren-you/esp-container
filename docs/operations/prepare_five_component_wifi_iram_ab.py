"""准备仅关闭两项 Wi-Fi IRAM 开关的独立 ESP32-C3 QEMU 对照目录。"""

import argparse
import hashlib
from pathlib import Path
import shutil


BASELINE_CONFIG_SHA256 = "cb4911792bf9fc1191e4dfc90ff04e483e880ed6800a455f590e594c5ce6b62d"
BASELINE_SIGNED_APP_SHA256 = "93fb2b027f5bf8d0acae828e4812cd5665acc2803f21621ea2b7ee169051efdc"
OPTIONS = ("ESP_WIFI_IRAM_OPT", "ESP_WIFI_RX_IRAM_OPT")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="existing exact FRP QEMU probe directory")
    parser.add_argument("destination", type=Path, help="new output directory")
    args = parser.parse_args()
    source = args.source.resolve()
    destination = args.destination.resolve()
    if destination.exists():
        parser.error(f"destination already exists: {destination}")
    firmware = source / "firmware"
    config = firmware / "sdkconfig"
    app = firmware / "build" / "esp_base.bin"
    if sha256(config) != BASELINE_CONFIG_SHA256:
        parser.error("source sdkconfig differs from exact QEMU baseline")
    if sha256(app) != BASELINE_SIGNED_APP_SHA256:
        parser.error("source signed app differs from exact QEMU baseline")
    original = config.read_text()
    changed = original
    for option in OPTIONS:
        before = f"CONFIG_{option}=y"
        after = f"# CONFIG_{option} is not set"
        if changed.count(before) != 1:
            parser.error(f"expected exactly one enabled {option}")
        changed = changed.replace(before, after, 1)
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns("build", "*.log"))
    (destination / "firmware" / "sdkconfig").write_text(changed)
    print("wifi_iram_ab prepare")
    print(f"  source_app_sha256     {BASELINE_SIGNED_APP_SHA256}")
    print(f"  source_config_sha256  {BASELINE_CONFIG_SHA256}")
    print(f"  target_config_sha256  {sha256(destination / 'firmware' / 'sdkconfig')}")
    print(f"  target                {destination}")


if __name__ == "__main__":
    main()
