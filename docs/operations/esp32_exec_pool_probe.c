/* 仓外 QEMU 容量 fixture：仅保留地址和堆读数，不读写 32BIT-only 申请区域。 */
#include <stdbool.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"

#define PROBE_BLOCKS 16U
#define PROBE_BLOCK_BYTES 4096U
#define PROBE_CAPS (MALLOC_CAP_EXEC | MALLOC_CAP_32BIT)

static const char *const TAG = "exec-pool-probe";

typedef struct {
    unsigned exec_free;
    unsigned exec_largest;
    unsigned bit8_free;
    unsigned bit8_largest;
} heap_point_t;

typedef struct {
    uintptr_t address;
    const char *region;
    const char *end_region;
    bool byte_accessible;
    bool executable;
    heap_point_t heap;
} block_point_t;

static heap_point_t snapshot(void)
{
    return (heap_point_t){
        .exec_free = (unsigned)heap_caps_get_free_size(PROBE_CAPS),
        .exec_largest = (unsigned)heap_caps_get_largest_free_block(PROBE_CAPS),
        .bit8_free = (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        .bit8_largest = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
    };
}

/* 顺序先判重叠的 D/IRAM，再判宽泛的 IRAM／DRAM 范围。 */
static const char *region(const void *pointer)
{
    if (esp_ptr_in_diram_dram(pointer)) return "D/IRAM-DRAM-alias";
    if (esp_ptr_in_diram_iram(pointer)) return "D/IRAM-IRAM-alias";
    if (esp_ptr_in_iram(pointer)) return "IRAM-only";
    if (esp_ptr_in_dram(pointer)) return "DRAM-only";
    if (esp_ptr_in_rtc_dram_fast(pointer)) return "RTC-fast";
    return "unknown";
}

static void probe_alignment(unsigned alignment)
{
    void *blocks[PROBE_BLOCKS] = {0};
    block_point_t points[PROBE_BLOCKS] = {0};
    const heap_point_t before = snapshot();
    heap_point_t failed = {0};
    unsigned allocated = 0;
    bool allocation_failed = false;
    void *fallback = NULL;
    bool fallback_succeeded = false;
    block_point_t fallback_point = {0};

    for (; allocated < PROBE_BLOCKS; ++allocated) {
        void *pointer = heap_caps_aligned_alloc(alignment, PROBE_BLOCK_BYTES, PROBE_CAPS);
        if (!pointer) {
            allocation_failed = true;
            failed = snapshot();
            break;
        }
        blocks[allocated] = pointer;
        const uintptr_t address = (uintptr_t)pointer;
        points[allocated] = (block_point_t){
            .address = address,
            .region = region(pointer),
            .end_region = region((const void *)(address + PROBE_BLOCK_BYTES - 1U)),
            .byte_accessible = esp_ptr_byte_accessible(pointer),
            .executable = esp_ptr_executable(pointer),
            .heap = snapshot(),
        };
    }

    /* 严格页对齐失败后，在相同已占用状态下试一次 4 字节对齐，区分总池与对齐限制。 */
    if (allocation_failed && alignment == 4096U) {
        fallback = heap_caps_aligned_alloc(4U, PROBE_BLOCK_BYTES, PROBE_CAPS);
        if (fallback) {
            fallback_succeeded = true;
            const uintptr_t address = (uintptr_t)fallback;
            fallback_point = (block_point_t){
                .address = address,
                .region = region(fallback),
                .end_region = region((const void *)(address + PROBE_BLOCK_BYTES - 1U)),
                .byte_accessible = esp_ptr_byte_accessible(fallback),
                .executable = esp_ptr_executable(fallback),
                .heap = snapshot(),
            };
            heap_caps_free(fallback);
            fallback = NULL;
        }
    }

    for (unsigned index = allocated; index > 0; --index) {
        heap_caps_free(blocks[index - 1U]);
    }
    const heap_point_t after = snapshot();
    ESP_LOGI(TAG,
             "summary align=%u block_size=%u requested=%u allocated=%u failed=%d fallback_4byte=%d before_exec=%u/%u before_8bit=%u/%u failure_exec=%u/%u failure_8bit=%u/%u after_exec=%u/%u after_8bit=%u/%u",
             alignment, PROBE_BLOCK_BYTES, PROBE_BLOCKS, allocated, allocation_failed,
             fallback_succeeded,
             before.exec_free, before.exec_largest, before.bit8_free, before.bit8_largest,
             failed.exec_free, failed.exec_largest, failed.bit8_free, failed.bit8_largest,
             after.exec_free, after.exec_largest, after.bit8_free, after.bit8_largest);
    for (unsigned index = 0; index < allocated; ++index) {
        const block_point_t *point = &points[index];
        ESP_LOGI(TAG,
                 "block align=%u index=%u address=0x%08x end=0x%08x region=%s end_region=%s word_aligned=%d page_aligned=%d byte_accessible=%d executable=%d exec=%u/%u bit8=%u/%u",
                 alignment, index + 1U, (unsigned)point->address,
                 (unsigned)(point->address + PROBE_BLOCK_BYTES - 1U),
                 point->region, point->end_region,
                 (int)(point->address % 4U == 0U),
                 (int)(point->address % PROBE_BLOCK_BYTES == 0U),
                 (int)point->byte_accessible, (int)point->executable,
                 point->heap.exec_free, point->heap.exec_largest,
                 point->heap.bit8_free, point->heap.bit8_largest);
    }
    if (fallback_succeeded) {
        ESP_LOGI(TAG,
                 "fallback after_page_failure address=0x%08x end=0x%08x region=%s end_region=%s word_aligned=%d page_aligned=%d byte_accessible=%d executable=%d exec=%u/%u bit8=%u/%u",
                 (unsigned)fallback_point.address,
                 (unsigned)(fallback_point.address + PROBE_BLOCK_BYTES - 1U),
                 fallback_point.region, fallback_point.end_region,
                 (int)(fallback_point.address % 4U == 0U),
                 (int)(fallback_point.address % PROBE_BLOCK_BYTES == 0U),
                 (int)fallback_point.byte_accessible, (int)fallback_point.executable,
                 fallback_point.heap.exec_free, fallback_point.heap.exec_largest,
                 fallback_point.heap.bit8_free, fallback_point.heap.bit8_largest);
    }
}

void capacity_exec_pool_probe(void)
{
    probe_alignment(4U);     /* 32BIT 能力所需的 4 字节对齐。 */
    probe_alignment(4096U);  /* 严格页对齐，以区分可用字节与对齐碎片。 */
}
