#!/bin/zsh
set -eu
probe_root=/private/tmp/esp-p6-exact-c3-20260926
prior_root=/private/tmp/esp-p6-frp-chunked-20260926
mkdir -p "$probe_root/probe-base"
rsync -a "$probe_root/base-src/" "$probe_root/probe-base/"
firmware="$probe_root/probe-base/firmware"
rsync -a "$probe_root/frp-src/" "$firmware/components/esp_frp/"
rsync -a "$probe_root/mqtt-src/" "$firmware/components/mqtt/"
rsync -a "$probe_root/ota-src/components/esp_ota/" "$firmware/components/esp_ota/"
rsync -a "$probe_root/container-src/components/esp_container/" "$firmware/components/esp_container/"
rsync -a "$prior_root/firmware/managed_components/" "$firmware/managed_components/"
cp "$prior_root/test-key.pem" "$probe_root/probe-base/test-key.pem"
cp "$prior_root/firmware/sdkconfig" "$firmware/sdkconfig"
cp "$prior_root/firmware/components/device_protocol/idf_component.yml" "$firmware/components/device_protocol/idf_component.yml"
rm "$firmware/integrations/container_binding/idf_component.yml"
rm "$firmware/dependencies.lock"
for fixture in CMakeLists.txt esp_base_main.c capacity_references.c capacity_runtime_probe.c qemu_adc2_stub.c runtime_guest_bytes.h; do
  cp "$prior_root/firmware/apps/esp_base/main/$fixture" "$firmware/apps/esp_base/main/$fixture"
done
python3 - <<'PY'
from pathlib import Path
root = Path('/private/tmp/esp-p6-exact-c3-20260926/probe-base/firmware')
cmake = root / 'CMakeLists.txt'
source = cmake.read_text()
needle = '    set(WAMR_BUILD_SHARED_MEMORY 0 CACHE BOOL "Disallow guest shared memory" FORCE)\n'
assert source.count(needle) == 1
source = source.replace(needle, needle + '    set(WAMR_BUILD_SHRUNK_MEMORY 0 CACHE BOOL "Preserve standard Wasm page sizes" FORCE)\n', 1)
cmake.write_text(source)
config = root / 'sdkconfig'
source = config.read_text()
old = '/private/tmp/esp-p6-current-c3-20260926/probe-base/test-key.pem'
new = '/private/tmp/esp-p6-exact-c3-20260926/probe-base/test-key.pem'
assert source.count(old) == 1
config.write_text(source.replace(old, new, 1))
PY
printf '%s\n' 'assembled_exact_components=base:6976bc43 frp:533e294 mqtt:9d6d95e ota:5da4a0d container:8eb805f' > "$probe_root/assembly.txt"
shasum -a 256 "$firmware/CMakeLists.txt" "$firmware/apps/esp_base/main/esp_base_main.c" "$firmware/apps/esp_base/main/capacity_runtime_probe.c" "$firmware/sdkconfig"
