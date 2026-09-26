#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 3 ]]; then
    echo "用法：$0 <已准备的 C3 QEMU probe> <QEMU runner> <日志目录>" >&2
    exit 2
fi

probe=$1
runner=$2
log_root=$3
if [[ -e "$log_root" ]]; then
    echo "日志目录已存在：$log_root" >&2
    exit 2
fi
mkdir -p "$log_root"

export IDF_PATH=/Users/darrenyou/.cache/darren-space/esp-idf-578cf89
source "$IDF_PATH/export.sh" >/dev/null

idf.py -C "$probe/firmware" -D ESP_BASE_CONTAINER_BINDING_PROBE=ON \
    build >"$log_root/build.log" 2>&1
python -m espsecure verify-signature --version 2 \
    --keyfile "$probe/test-key.pem" "$probe/firmware/build/esp_base.bin" \
    >"$log_root/signature.log" 2>&1
shasum -a 256 "$probe/firmware/sdkconfig" "$probe/firmware/build/esp_base.bin" \
    "$probe/firmware/build/esp_base.elf" >"$log_root/sha256.txt"

frps_test_pid=
cleanup() {
    if [[ -n "$frps_test_pid" ]]; then
        kill -TERM "$frps_test_pid" 2>/dev/null || true
        wait "$frps_test_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

"$probe/fixture/frps-server" -port 29173 \
    -cert "$probe/fixture/frps_server.pem" \
    -key "$probe/fixture/frps_server_key.pem" >"$log_root/frps.log" 2>&1 &
frps_test_pid=$!
ready=0
for attempt in 1 2 3 4 5; do
    if rg -q QEMU_FRPS_READY "$log_root/frps.log"; then ready=1; break; fi
    sleep 1
done
[[ $ready == 1 ]]

python3 "$runner" "$probe/firmware" "$log_root/qemu.log" 20
rg 'frps phase=|32bit phase=|probe_summary' "$log_root/qemu.log" \
    >"$log_root/key-events.log" || true
cat "$log_root/key-events.log"
