#!/usr/bin/env python3
"""在仓外装配 ESP32 五组件签名 QEMU 认证容量探针。"""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil

from cryptography.hazmat.primitives.ciphers.aead import AESGCM


HERE = Path(__file__).resolve().parent


def replace_once(path: Path, before: str, after: str) -> None:
    content = path.read_text()
    count = content.count(before)
    if count != 1:
        raise SystemExit(f"{path}: 预期一处 {before!r}，实际 {count} 处")
    path.write_text(content.replace(before, after, 1))


def prepare(source: Path, target: Path, key: Path,
            managed_cache: Path | None) -> None:
    if target.exists():
        raise SystemExit(f"实验目录已存在，拒绝覆盖：{target}")
    for path in (
        source / "base-src/firmware/CMakeLists.txt",
        source / "frp-src/idf_component.yml",
        source / "mqtt-src/idf_component.yml",
        source / "ota-src/components/esp_ota/idf_component.yml",
        source / "container-src/components/esp_container/idf_component.yml",
        HERE / "runtime_guest_bytes.h",
        HERE / "esp32_capacity_references.c",
        key,
    ):
        if not path.exists():
            raise SystemExit(f"输入不存在：{path}")
    if key.resolve().is_relative_to(source.resolve()) or key.resolve().is_relative_to(target.resolve()):
        raise SystemExit("测试私钥必须在源目录和实验工程之外")

    shutil.copytree(source / "base-src", target)
    firmware = target / "firmware"
    for name, origin in {
        "esp_frp": source / "frp-src",
        "mqtt": source / "mqtt-src",
        "esp_ota": source / "ota-src/components/esp_ota",
        "esp_container": source / "container-src/components/esp_container",
    }.items():
        shutil.copytree(origin, firmware / "components" / name)
    if managed_cache:
        expected_hashes = {
            "espressif__cjson": "e788323270d90738662d66fffa910bfe1fba019bba087f01557e70c40485b469",
            "wasm-micro-runtime": "a799be27248cadffdee6f6fae988dbdf3f5d423baab0b1008d74b8581b5ed507",
        }
        for name, expected_hash in expected_hashes.items():
            component = managed_cache / name
            if not component.is_dir():
                raise SystemExit(f"受锁组件缓存不存在：{component}")
            if (component / ".component_hash").read_text().strip() != expected_hash:
                raise SystemExit(f"受锁组件缓存摘要不符：{component}")
            shutil.copytree(component, firmware / "managed_components" / name)

    main = firmware / "apps/esp_base/main"
    shutil.copy2(HERE / "runtime_guest_bytes.h", main / "runtime_guest_bytes.h")
    shutil.copy2(HERE / "frp_authenticated_capacity_probe.c",
                 main / "capacity_runtime_probe.c")
    references = (HERE / "esp32_capacity_references.c").read_text()
    if references.count('#include "esp_container_slots_idf.h"') != 1 or \
            references.count('    (void)wasm_runtime_init();') != 1:
        raise SystemExit("强制链接 fixture 结构已变")
    references = references.replace(
        '#include "esp_container_slots_idf.h"',
        '#include "esp_container_slots_idf.h"\n#include "esp_base_container_binding.h"',
    ).replace(
        '    (void)wasm_runtime_init();',
        '    (void)esp_base_container_reconcile(NULL, NULL, NULL, NULL, NULL);\n'
        '    (void)wasm_runtime_init();',
    )
    (main / "capacity_references.c").write_text(references)

    cmake = main / "CMakeLists.txt"
    replace_once(cmake, 'SRCS "esp_base_main.c"',
                 'SRCS "esp_base_main.c" "capacity_references.c" '
                 '"capacity_runtime_probe.c" '
                 'EMBED_FILES "aead_4096.bin" "aead_65536.bin"')
    replace_once(cmake, 'REQUIRES device_identity',
                 'REQUIRES esp_frp esp_container wasm-micro-runtime '
                 'container_binding device_identity')
    cmake.write_text(cmake.read_text() +
                     '\ntarget_include_directories(${COMPONENT_LIB} PRIVATE '
                     '${CMAKE_SOURCE_DIR}/components/esp_container/src)\n')

    app = main / "esp_base_main.c"
    replace_once(app, 'static const char *TAG = "esp_base";',
                 'static const char *TAG = "esp_base";\n'
                 'void capacity_references(void);\n'
                 'void capacity_runtime_probe(void);\n'
                 'void capacity_heap_checkpoint(const char *phase);')
    replace_once(app, 'void app_main(void)\n{',
                 'void app_main(void)\n{\n'
                 '    capacity_references();\n'
                 '    capacity_runtime_probe();')
    replace_once(
        app,
        'ESP_LOGI(TAG, "ESP_BASE_READY hardware_outputs=untouched provisioning=required");',
        'ESP_LOGI(TAG, "ESP_BASE_READY hardware_outputs=untouched provisioning=required");\n'
        '    capacity_heap_checkpoint("after_base_ready");\n'
        '    capacity_runtime_probe();\n'
        '    capacity_heap_checkpoint("after_base_guest_closed");',
    )

    defaults = firmware / "sdkconfig.defaults.esp32"
    defaults.write_text(defaults.read_text() +
        '\n# 仅仓外测试键签名与单页 ABI 2 容量探针。\n'
        'CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y\n'
        'CONFIG_SECURE_SIGNED_APPS_ECDSA_SCHEME=y\n'
        'CONFIG_SECURE_SIGNED_ON_BOOT_NO_SECURE_BOOT=y\n'
        'CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y\n'
        'CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y\n'
        f'CONFIG_SECURE_BOOT_SIGNING_KEY="{key.resolve()}"\n'
        'CONFIG_SECURE_BOOT=n\n'
        'CONFIG_WAMR_ENABLE_INTERP=y\n'
        'CONFIG_WAMR_INTERP_CLASSIC=y\n'
        'CONFIG_WAMR_INTERP_LOADER_NORMAL=y\n'
        'CONFIG_WAMR_ENABLE_AOT=n\n'
        'CONFIG_WAMR_ENABLE_LIBC_BUILTIN=n\n'
        'CONFIG_WAMR_ENABLE_LIBC_WASI=n\n'
        'CONFIG_WAMR_ENABLE_LIB_PTHREAD=n\n'
        'CONFIG_WAMR_ENABLE_MULTI_MODULE=n\n'
        'CONFIG_WAMR_ENABLE_SHARED_MEMORY=n\n'
        'CONFIG_WAMR_ENABLE_REF_TYPES=n\n')

    aes_key = bytes(range(32))
    for size, nonce_start in ((4096, 1), (65536, 33)):
        nonce = bytes(range(nonce_start, nonce_start + 12))
        plaintext = bytes((offset * 37 + 11) & 255 for offset in range(size))
        header = (size + 16).to_bytes(4, "big")
        wire = nonce + header + AESGCM(aes_key).encrypt(
            nonce, plaintext, nonce + header)
        assert len(wire) == size + 32
        (main / f"aead_{size}.bin").write_bytes(wire)
    print(f"prepared {target}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="五份精确 Git 归档的仓外目录")
    parser.add_argument("target", type=Path, help="新建的仓外实验工程目录")
    parser.add_argument("key", type=Path, help="仓外 ECDSA P-256 临时测试私钥")
    parser.add_argument("--managed-cache", type=Path,
                        help="可选的受锁 WAMR/cJSON 本地缓存目录")
    args = parser.parse_args()
    prepare(args.source, args.target, args.key, args.managed_cache)


if __name__ == "__main__":
    main()
