import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description='仅对显式指定的仓外 C 容量探针插入 QEMU 头部采样')
parser.add_argument('probe_c', type=Path, help='待修改的 capacity_runtime_probe.c 文件')
source = parser.parse_args().probe_c
if source.name != 'capacity_runtime_probe.c' or not source.is_file():
    parser.error('请传入已有的 capacity_runtime_probe.c 文件')
text = source.read_text()
before = '''        const size_t limit = wire_length - (tamper ? 1U : 0U);
        while (offset < limit) {'''
after = '''        const size_t limit = wire_length - (tamper ? 1U : 0U);
        if (s_run == 2 && plain_length == 65536 && !tamper) {
            size_t header_consumed = 0;
            const unsigned header_before_free = free_bytes();
            const unsigned header_before_largest = largest_bytes();
            fed = efrp_aead_feed(&reader, wire, 16, &header_consumed);
            offset += header_consumed;
            ESP_LOGI(TAG,
                     "header_only feed=%d consumed=%u alloc=%u live=%u before=%u/%u after=%u/%u",
                     (int)fed, (unsigned)header_consumed, (unsigned)s_alloc_count,
                     (unsigned)s_live_bytes, header_before_free, header_before_largest,
                     free_bytes(), largest_bytes());
            if (fed != EFRP_OK || header_consumed != 16 || s_alloc_count != 0 ||
                s_live_bytes != 0 || free_bytes() != header_before_free ||
                largest_bytes() != header_before_largest) ++s_failures;
        }
        while (fed == EFRP_OK && offset < limit) {'''
assert text.count(before) == 1
source.write_text(text.replace(before, after, 1))
