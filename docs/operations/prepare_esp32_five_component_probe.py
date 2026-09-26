#!/usr/bin/env python3
"""在独立实验目录装配精确五仓 ESP32 容量探针，不修改产品源码。"""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil


def replace_once(path: Path, old: str, new: str) -> None:
    content = path.read_text()
    if content.count(old) != 1:
        raise SystemExit(f"{path}: 预期恰好一处待替换内容：{old!r}")
    path.write_text(content.replace(old, new, 1))


def prepare_variant(source: Path, target: Path, previous_probe: Path, key: Path,
                    partition: Path, reduced: bool) -> None:
    if target.exists():
        raise SystemExit(f"目标已存在，拒绝覆盖：{target}")
    shutil.copytree(source, target)
    firmware = target / "firmware"

    component_roots = {
        "esp_frp": "frp-src",
        "mqtt": "mqtt-src",
        "esp_ota": "ota-src/components/esp_ota",
        "esp_container": "container-src/components/esp_container",
    }
    for component_name, source_name in component_roots.items():
        shutil.copytree(source.parent / source_name, firmware / "components" / component_name)
    shutil.copytree(previous_probe / "firmware/managed_components",
                    firmware / "managed_components")

    # 仓外实验改造：产品的 ESP32 前置守卫保持原样。
    replace_once(
        firmware / "CMakeLists.txt",
        'if(IDF_TARGET STREQUAL "esp32")\n    message(FATAL_ERROR\n'
        '        "ESP32 Base image is blocked until P6-03 freezes its own partition table, OTA policy and signed boot chain")\n'
        'elseif(NOT IDF_TARGET STREQUAL "esp32c3")',
        'if(NOT IDF_TARGET STREQUAL "esp32" AND NOT IDF_TARGET STREQUAL "esp32c3")',
    )
    replace_once(
        firmware / "CMakeLists.txt",
        'set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/apps/${ESP_BASE_APP}/main")',
        'set(WAMR_BUILD_INSTRUCTION_METERING 1 CACHE BOOL "Bound guest instructions" FORCE)\n'
        'set(WAMR_BUILD_BULK_MEMORY 0 CACHE BOOL "Avoid implicit constructor invocation" FORCE)\n'
        'set(WAMR_BUILD_SHARED_MEMORY 0 CACHE BOOL "Disallow guest shared memory" FORCE)\n'
        'set(WAMR_BUILD_SHRUNK_MEMORY 0 CACHE BOOL "Preserve standard Wasm page sizes" FORCE)\n'
        'set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/apps/${ESP_BASE_APP}/main")',
    )
    (firmware / "components/device_protocol/idf_component.yml").write_text(
        'dependencies:\n  espressif/cjson:\n    version: "==1.7.19~2"\n'
    )
    (firmware / "integrations/container_binding/idf_component.yml").unlink()
    (firmware / "dependencies.lock").unlink()
    replace_once(
        firmware / "components/ota_operation/include/esp_base_ota_policy.h",
        '#error "ESP32 Base OTA policy needs its frozen layout and signed boot chain"',
        '#define ESP_BASE_OTA_TARGET "esp32/esp_base"\n'
        '#define ESP_BASE_OTA_SIGNATURE_SCHEME "esp_secure_boot_v1_ecdsa_p256"',
    )
    ota_policy = firmware / "components/ota_operation/esp_base_ota_policy.c"
    replace_once(ota_policy, '.ota_1_address_bytes = 0x200000,',
                 '.ota_1_address_bytes = 0x140000,')
    replace_once(ota_policy, '.ota_size_bytes = 0x1e0000,',
                 '.ota_size_bytes = 0x120000,')

    main = firmware / "apps/esp_base/main"
    for name in ("capacity_references.c", "capacity_runtime_probe.c",
                 "runtime_guest_bytes.h"):
        shutil.copy2(previous_probe / "firmware/apps/esp_base/main" / name, main / name)
    replace_once(
        main / "CMakeLists.txt",
        'SRCS "esp_base_main.c"',
        'SRCS "esp_base_main.c" "capacity_references.c" "capacity_runtime_probe.c"',
    )
    replace_once(
        main / "CMakeLists.txt",
        'REQUIRES device_identity',
        'REQUIRES esp_container wasm-micro-runtime device_identity',
    )
    (main / "CMakeLists.txt").write_text(
        (main / "CMakeLists.txt").read_text()
        + '\ntarget_include_directories(${COMPONENT_LIB} PRIVATE '
        '${CMAKE_SOURCE_DIR}/components/esp_container/src)\n'
    )
    original_main = main / "esp_base_main.c"
    replace_once(
        original_main,
        'static const char *TAG = "esp_base";',
        'static const char *TAG = "esp_base";\n'
        'void capacity_references(void);\n'
        'void capacity_runtime_probe(void);\n'
        'void capacity_heap_checkpoint(const char *phase);',
    )
    replace_once(
        original_main,
        'void app_main(void)\n{',
        'void app_main(void)\n{\n'
        '    capacity_references();\n'
        '    capacity_runtime_probe();',
    )
    replace_once(
        original_main,
        'ESP_LOGI(TAG, "ESP_BASE_READY hardware_outputs=untouched provisioning=required");',
        'ESP_LOGI(TAG, "ESP_BASE_READY hardware_outputs=untouched provisioning=required");\n'
        '    capacity_heap_checkpoint("after_base_ready");\n'
        '    capacity_runtime_probe();\n'
        '    capacity_heap_checkpoint("after_base_guest_closed");',
    )

    shutil.copy2(partition, firmware / "partitions/esp32-capacity-probe.csv")
    defaults = firmware / "sdkconfig.defaults.esp32"
    defaults.write_text(
        defaults.read_text()
        + '\n# 仓外 ECDSA v1 测试键与独立 ESP32 几何；仅用于容量探针。\n'
        + 'CONFIG_PARTITION_TABLE_CUSTOM=y\n'
        + 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/esp32-capacity-probe.csv"\n'
        + 'CONFIG_PARTITION_TABLE_MD5=n\n'
        + 'CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y\n'
        + 'CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y\n'
        + 'CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME=y\n'
        + 'CONFIG_SECURE_SIGNED_ON_BOOT_NO_SECURE_BOOT=y\n'
        + 'CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y\n'
        + 'CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y\n'
        + f'CONFIG_SECURE_BOOT_SIGNING_KEY="{key}"\n'
        + 'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=n\n'
        + 'CONFIG_SECURE_BOOT=n\n'
        + 'CONFIG_WAMR_ENABLE_INTERP=y\n'
        + 'CONFIG_WAMR_INTERP_CLASSIC=y\n'
        + 'CONFIG_WAMR_INTERP_LOADER_NORMAL=y\n'
        + 'CONFIG_WAMR_ENABLE_AOT=n\n'
        + 'CONFIG_WAMR_ENABLE_LIBC_BUILTIN=n\n'
        + 'CONFIG_WAMR_ENABLE_LIBC_WASI=n\n'
        + 'CONFIG_WAMR_ENABLE_LIB_PTHREAD=n\n'
        + 'CONFIG_WAMR_ENABLE_MULTI_MODULE=n\n'
        + 'CONFIG_WAMR_ENABLE_SHARED_MEMORY=n\n'
        + 'CONFIG_WAMR_ENABLE_REF_TYPES=n\n'
        + (
            'CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n\n'
            'CONFIG_MBEDTLS_TLS_CLIENT_ONLY=y\n'
            if reduced else ""
        )
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="含五份精确源码归档的独立目录")
    parser.add_argument("previous_probe", type=Path, help="既有精确 C3 探针，仅取公开 fixture 与受锁组件")
    parser.add_argument("key", type=Path, help="仓外 ECDSA v1 临时测试私钥")
    parser.add_argument("partition", type=Path, help="仓外 ESP32 分区 CSV")
    args = parser.parse_args()
    for path in (args.key, args.partition,
                 args.root / "base-src/firmware/CMakeLists.txt",
                 args.previous_probe / "firmware/managed_components"):
        if not path.exists():
            raise SystemExit(f"输入不存在：{path}")
    for name, reduced in (("baseline", False), ("reduced", True)):
        prepare_variant(args.root / "base-src", args.root / name,
                        args.previous_probe, args.key, args.partition, reduced)
    print("两个仓外输入已准备；请顺序执行 idf.py build 并检查实际 sdkconfig/签名。")


if __name__ == "__main__":
    main()
