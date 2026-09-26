#!/usr/bin/env bash
# QEMU-only official FRPS; no device flash or persistent listener.
set -euo pipefail

if [[ $# -ne 3 || -z "${IDF_PATH:-}" ]]; then
    printf 'usage: IDF_PATH=/path/to/locked-idf %s <probe-dir> <frp-go-module-dir> <port>\n' "$0" >&2
    exit 2
fi
probe_dir=$1
go_module_dir=$2
port=$3
fixture_dir=$probe_dir/fixture
output_dir=$(dirname "$probe_dir")
script_dir=$(cd "$(dirname "$0")" && pwd)

source "$IDF_PATH/export.sh" >/dev/null 2>&1
(
    cd "$go_module_dir"
    go list -m github.com/fatedier/frp | grep -Fx 'github.com/fatedier/frp v0.71.0'
    go build -mod=readonly -o "$fixture_dir/frps-server" "$fixture_dir/qemu_frps_fixture.go"
)
"$fixture_dir/frps-server" -port "$port" \
    -cert "$fixture_dir/frps_server.pem" -key "$fixture_dir/frps_server_key.pem" \
    > "$output_dir/frps.log" 2>&1 &
server_pid=$!
cleanup() {
    kill -TERM "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT
for ((attempt = 0; attempt < 100; ++attempt)); do
    if grep -q '^QEMU_FRPS_READY' "$output_dir/frps.log"; then break; fi
    if ! kill -0 "$server_pid" 2>/dev/null; then cat "$output_dir/frps.log"; exit 1; fi
    sleep 0.05
done
grep -q '^QEMU_FRPS_READY' "$output_dir/frps.log"
python3 "$script_dir/frp_chunked_qemu.py" "$probe_dir/firmware" "$output_dir/qemu.log" 90
if grep -Eq 'Guru Meditation Error|panic.ed|assert failed' "$output_dir/qemu.log"; then
    printf 'QEMU panic; inspect %s\n' "$output_dir/qemu.log" >&2
    exit 1
fi
grep -q 'probe_summary runs=2' "$output_dir/qemu.log"
grep -q 'frps phase=result' "$output_dir/qemu.log"
cleanup
trap - EXIT
grep -E '^QEMU_FRPS_(READY|STOPPED)' "$output_dir/frps.log"
grep 'frps phase=result' "$output_dir/qemu.log"
printf 'QEMU trace: %s\n' "$output_dir/qemu.log"
