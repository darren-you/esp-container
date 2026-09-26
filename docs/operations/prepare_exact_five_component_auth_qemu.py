from pathlib import Path
import shutil
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

root = Path('/private/tmp/esp32c3-five-dynamic-exact-20260927')
previous = Path('/private/tmp/esp-p6-exact-c3-20260926/probe-base')
source = root / 'base-src'
target = root / 'probe'
if target.exists():
    raise SystemExit('probe already exists')
shutil.copytree(source, target)
firmware = target / 'firmware'
for name, origin in {
    'esp_frp': root / 'frp-src',
    'mqtt': root / 'mqtt-src',
    'esp_ota': root / 'ota-src/components/esp_ota',
    'esp_container': root / 'container-src/components/esp_container',
}.items():
    shutil.copytree(origin, firmware / 'components' / name)
for name in ('espressif__cjson', 'wasm-micro-runtime'):
    shutil.copytree(previous / 'firmware/managed_components' / name,
                    firmware / 'managed_components' / name)
(firmware / 'dependencies.lock').unlink()
shutil.copy2(previous / 'test-key.pem', target / 'test-key.pem')
main = firmware / 'apps/esp_base/main'
for name in ('qemu_adc2_stub.c', 'runtime_guest_bytes.h'):
    shutil.copy2(previous / 'firmware/apps/esp_base/main' / name, main / name)
shutil.copy2(root / 'capacity_runtime_probe.c', main / 'capacity_runtime_probe.c')
refs = (previous / 'firmware/apps/esp_base/main/capacity_references.c').read_text()
refs = refs.replace('#include "esp_container_slots_idf.h"',
                    '#include "esp_container_slots_idf.h"\n'
                    '#include "esp_base_container_binding.h"')
refs = refs.replace('    (void)wasm_runtime_init();',
                    '    (void)esp_base_container_reconcile(NULL, NULL, NULL, NULL, NULL);\n'
                    '    (void)wasm_runtime_init();')
(main / 'capacity_references.c').write_text(refs)

def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if text.count(before) != 1:
        raise SystemExit(f'{path}: expected one occurrence {before!r}, got {text.count(before)}')
    path.write_text(text.replace(before, after, 1))

cmake = main / 'CMakeLists.txt'
replace_once(cmake, 'SRCS "esp_base_main.c"',
             'SRCS "esp_base_main.c" "capacity_references.c" '
             '"capacity_runtime_probe.c" "qemu_adc2_stub.c" '
             'EMBED_FILES "aead_4096.bin" "aead_65536.bin"')
replace_once(cmake, 'REQUIRES device_identity',
             'REQUIRES esp_frp esp_container wasm-micro-runtime container_binding device_identity')
cmake.write_text(cmake.read_text() +
                 '\ntarget_include_directories(${COMPONENT_LIB} PRIVATE '
                 '${CMAKE_SOURCE_DIR}/components/esp_container/src)\n')
app = main / 'esp_base_main.c'
replace_once(app, 'static const char *TAG = "esp_base";',
             'static const char *TAG = "esp_base";\n'
             'void capacity_references(void);\n'
             'void capacity_runtime_probe(void);\n'
             'void capacity_heap_checkpoint(const char *phase);')
replace_once(app, 'void app_main(void)\n{',
             'void app_main(void)\n{\n'
             '    capacity_references();\n'
             '    capacity_runtime_probe();')
replace_once(app,
             'ESP_LOGI(TAG, "ESP_BASE_READY hardware_outputs=untouched provisioning=required");',
             'ESP_LOGI(TAG, "ESP_BASE_READY hardware_outputs=untouched provisioning=required");\n'
             '    capacity_heap_checkpoint("after_base_ready");\n'
             '    capacity_runtime_probe();\n'
             '    capacity_heap_checkpoint("after_base_guest_closed");')
config = firmware / 'sdkconfig.defaults.esp32c3'
replace_once(config, 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y',
             'CONFIG_ESP_CONSOLE_UART_DEFAULT=y\n'
             'CONFIG_ESP_CONSOLE_UART_NUM=0\n'
             'CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y')
config.write_text(config.read_text() +
    '\n# QEMU-only signed ABI 2 and WAMR Classic capacity probe.\n'
    'CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y\n'
    'CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y\n'
    'CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y\n'
    'CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y\n'
    'CONFIG_SECURE_BOOT_SIGNING_KEY="/private/tmp/esp32c3-five-dynamic-exact-20260927/probe/test-key.pem"\n'
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
key = bytes(range(32))
for size, nonce_start in ((4096, 1), (65536, 33)):
    nonce = bytes(range(nonce_start, nonce_start + 12))
    plaintext = bytes((i * 37 + 11) & 255 for i in range(size))
    header = (size + 16).to_bytes(4, 'big')
    wire = nonce + header + AESGCM(key).encrypt(nonce, plaintext, nonce + header)
    assert len(wire) == size + 32
    (main / f'aead_{size}.bin').write_bytes(wire)
print('prepared', target)
