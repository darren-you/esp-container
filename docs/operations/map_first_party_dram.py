import re
import sys
from pathlib import Path

map_file = Path(sys.argv[1])
components = {
    'main', 'device_identity', 'device_protocol', 'remote_config',
    'wifi_runtime', 'safety_runtime', 'time_runtime', 'ota_operation',
    'esp_frp', 'mqtt', 'esp_ota', 'esp_container', 'esp_base_container_binding',
}
rows = []
pending = None
section_line = re.compile(r'^\s*(\.(?:bss|data)(?:\.[^\s]+)?)\s*$')
full_line = re.compile(r'^\s*(\.(?:bss|data)(?:\.[^\s]+)?)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(.+)$')
body_line = re.compile(r'^\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(.+)$')
for line in map_file.read_text(errors='replace').splitlines():
    match = full_line.match(line)
    if match:
        section, address, size, origin = match.groups()
        pending = None
    else:
        match = section_line.match(line)
        if match:
            pending = match.group(1)
            continue
        match = body_line.match(line) if pending else None
        if not match:
            pending = None
            continue
        section = pending
        address, size, origin = match.groups()
        pending = None
    if not origin.startswith('esp-idf/'):
        continue
    component = origin.split('/')[1]
    if component not in components:
        continue
    if component == 'mqtt' and not origin.endswith('(emqtt.c.obj)'):
        continue
    if component == 'main' and not origin.endswith('(esp_base_main.c.obj)'):
        continue
    address_int = int(address, 16)
    size_int = int(size, 16)
    if size_int <= 0 or not (0x3fc80000 <= address_int < 0x3fd00000):
        continue
    rows.append((size_int, address_int, section, origin))
for size, address, section, origin in sorted(rows, reverse=True)[:30]:
    print(f'{size:7d} 0x{address:08x} {section:42s} {origin}')
