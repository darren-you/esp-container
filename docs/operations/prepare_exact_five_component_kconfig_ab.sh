#!/usr/bin/env bash
set -euo pipefail

# 在 mac-work-1 上先生成前一节的精确五仓 QEMU 探针，再运行本脚本。
# 除两项 Kconfig 外，保持组件源码、fixture 和测试签名键逐字节一致。
source_probe=/private/tmp/esp-p6-exact-c3-20260926/probe-base
ab_root=/private/tmp/esp-p6-exact-kconfig-ab-20260926
test -f "$source_probe/firmware/sdkconfig"
test -f "$source_probe/tools/check_sdk.py"
mkdir -p "$ab_root"

for variant in baseline client-only sta-only combined; do
    target="$ab_root/$variant"
    if test -e "$target"; then
        printf '目标已存在：%s\n' "$target" >&2
        exit 1
    fi
    mkdir -p "$target"
    rsync -a --exclude=/firmware/build/ "$source_probe/" "$target/"
done

python3 - "$ab_root" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
for variant in ('client-only', 'sta-only', 'combined'):
    config = root / variant / 'firmware' / 'sdkconfig'
    lines = config.read_text().splitlines(keepends=True)
    replacements = {}
    if variant in ('client-only', 'combined'):
        replacements.update({
            'CONFIG_MBEDTLS_TLS_SERVER=y': '# CONFIG_MBEDTLS_TLS_SERVER is not set',
            'CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT=y': '# CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT is not set',
            '# CONFIG_MBEDTLS_TLS_CLIENT_ONLY is not set': 'CONFIG_MBEDTLS_TLS_CLIENT_ONLY=y',
        })
    if variant in ('sta-only', 'combined'):
        replacements['CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y'] = '# CONFIG_ESP_WIFI_SOFTAP_SUPPORT is not set'
    for old, new in replacements.items():
        matches = [index for index, line in enumerate(lines) if line.rstrip('\n') == old]
        if len(matches) != 1:
            raise SystemExit(f'{variant}: 预期一个 {old}，实际 {len(matches)}')
        lines[matches[0]] = new + '\n'
    config.write_text(''.join(lines))
PY

for variant in client-only sta-only combined; do
    diff -qr -x build -x sdkconfig -x sdkconfig.old \
        "$ab_root/baseline" "$ab_root/$variant"
done
printf '%s\n' '四份输入已准备；后续依次运行 idf.py build，避免组件缓存 index.lock 竞争。'
