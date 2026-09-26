// SPDX-License-Identifier: Apache-2.0
// QEMU-only capability observation after FRP REGISTERED and the first Pong.
// Allocated addresses are classified, never read or written.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_frp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"

#define PROBE_BLOCK_BYTES 4096U
#define PROBE_BLOCK_COUNT 16U

static const char *const TAG = "five-capacity-32bit";

static void report_heap(const char *phase, unsigned index, const void *address,
                        bool byte_accessible, bool iram_alias)
{
    ESP_LOGI(TAG,
             "32bit phase=%s index=%u address=%p byte_accessible=%d iram_alias=%d "
             "free8=%u largest8=%u free32=%u largest32=%u",
             phase, index, address, address ? (int)byte_accessible : -1,
             address ? (int)iram_alias : -1,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_32BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_32BIT));
}

bool capacity_registered_32bit_probe(efrp_client_t *client)
{
    void *blocks[PROBE_BLOCK_COUNT] = {0};
    unsigned allocated = 0;
    bool all_word_only_iram = true;
    efrp_status_t before = {0};
    efrp_result_t result = efrp_get_status(client, &before);
    if (result != EFRP_OK || before.phase != EFRP_PHASE_READY ||
        before.ready_sessions != 1 || before.pongs < 1) {
        ESP_LOGE(TAG, "32bit phase=precondition api=%d client_phase=%d ready=%u pongs=%u",
                 (int)result, (int)before.phase, (unsigned)before.ready_sessions,
                 (unsigned)before.pongs);
        return false;
    }
    report_heap("before", 0, NULL, false, false);

    for (unsigned index = 0; index < PROBE_BLOCK_COUNT; ++index) {
        void *const block = heap_caps_malloc(PROBE_BLOCK_BYTES, MALLOC_CAP_32BIT);
        if (block == NULL) {
            report_heap("allocation_failed", index + 1, NULL, false, false);
            break;
        }
        blocks[index] = block;
        ++allocated;
        const void *const last = (const unsigned char *)block + PROBE_BLOCK_BYTES - 1;
        const bool byte_accessible =
            esp_ptr_byte_accessible(block) && esp_ptr_byte_accessible(last);
        const bool iram_alias = esp_ptr_in_iram(block) && esp_ptr_in_iram(last);
        all_word_only_iram = all_word_only_iram && !byte_accessible && iram_alias;
        report_heap("allocated", index + 1, block, byte_accessible, iram_alias);
    }

    for (unsigned count = allocated; count > 0; --count) {
        heap_caps_free(blocks[count - 1]);
        blocks[count - 1] = NULL;
        report_heap("released", count, NULL, false, false);
    }
    report_heap("after", allocated, NULL, false, false);
    ESP_LOGI(TAG, "32bit phase=summary allocated=%u requested=%u bytes=%u "
             "all_word_only_iram=%u word_only_iram_64k=%u",
             allocated, PROBE_BLOCK_COUNT, allocated * PROBE_BLOCK_BYTES,
             all_word_only_iram ? 1u : 0u,
             allocated == PROBE_BLOCK_COUNT && all_word_only_iram ? 1u : 0u);

    efrp_status_t after = {0};
    result = efrp_get_status(client, &after);
    ESP_LOGI(TAG, "32bit phase=frp_after api=%d client_phase=%d ready=%u pongs=%u",
             (int)result, (int)after.phase, (unsigned)after.ready_sessions,
             (unsigned)after.pongs);
    return result == EFRP_OK && after.phase == EFRP_PHASE_READY &&
           after.ready_sessions == 1 && after.pongs >= before.pongs;
}
