#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_frp_aead.h"
#include "runtime_internal.h"
#include "runtime_guest_bytes.h"

static const char *const TAG = "five-capacity-64k";

static void report_heap(const char *phase)
{
    ESP_LOGI(TAG, "%s free=%u largest=%u min_since_boot=%u", phase,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

static unsigned record_allocations;
static unsigned record_allocated_bytes;
static unsigned record_scan_run;

static void *track_allocate(size_t count, size_t size)
{
    void *block = calloc(count, size);
    if (block) {
        ++record_allocations;
        record_allocated_bytes += (unsigned)(count * size);
    }
    return block;
}

static void track_release(void *block)
{
    free(block);
}

/* The real FRP reader allocates on the validated nonce + length header.
 * Header-only: no TLS, FRPS session, ciphertext, tag, or plaintext delivery. */
static void probe_record_header_lengths(void)
{
    static const uint32_t lengths[] = {
        4096, 32768, 49152, 57344, 61440, 65536,
    };
    const uint8_t key[EFRP_AEAD_KEY_BYTES] = {0};
    const unsigned run = ++record_scan_run;
    for (size_t i = 0; i < sizeof lengths / sizeof lengths[0]; ++i) {
        const uint32_t plain_length = lengths[i];
        const uint32_t body_length = plain_length + EFRP_AEAD_TAG_BYTES;
        uint8_t prefix[16] = {
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
            (uint8_t)(body_length >> 24), (uint8_t)(body_length >> 16),
            (uint8_t)(body_length >> 8), (uint8_t)body_length,
        };
        efrp_aead_reader_t reader = {0};
        size_t consumed = 0;
        record_allocations = record_allocated_bytes = 0;
        const efrp_result_t initialized = efrp_aead_reader_init_chunked(
            &reader, key, track_allocate, track_release);
        efrp_result_t fed = initialized;
        if (initialized == EFRP_OK) {
            fed = efrp_aead_feed(&reader, prefix, sizeof prefix, &consumed);
        }
        const unsigned free_during = (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT);
        const unsigned largest_during = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        const unsigned minimum_during = (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        efrp_aead_reader_destroy(&reader);
        ESP_LOGI(TAG, "frp_scan run=%u plain=%u init=%d feed=%d consumed=%u chunks=%u bytes=%u during_free=%u during_largest=%u min=%u after_free=%u after_largest=%u",
                 run, (unsigned)plain_length, (int)initialized, (int)fed, (unsigned)consumed,
                 record_allocations, record_allocated_bytes, free_during, largest_during,
                 minimum_during, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
}

static void *run_guest(void *argument)
{
    (void)argument;
    const econtainer_runtime_limits_t limits = {
        .max_wasm_bytes = 512 * 1024,
        .max_memory_pages = 1,
        .stack_size_bytes = 4096,
        .max_event_bytes = 128,
        .init_instruction_budget = 1000,
        .event_instruction_budget = 1000,
        .stop_instruction_budget = 1000,
        .max_entry_duration_ms = 1000,
    };
    econtainer_runtime_t *runtime = NULL;
    int32_t guest_result = -1;
    const uint8_t event[] = {1, 2, 3};
    report_heap("before");
    const econtainer_runtime_result_t opened = econtainer_runtime_open(
        counter_guest, sizeof(counter_guest), &limits, &runtime);
    report_heap("opened");
    econtainer_runtime_result_t initialized = opened;
    econtainer_runtime_result_t delivered = opened;
    econtainer_runtime_result_t stopped = opened;
    if (opened == ECONTAINER_RUNTIME_OK) {
        initialized = econtainer_runtime_init(runtime);
        delivered = initialized;
        if (initialized == ECONTAINER_RUNTIME_OK) {
            delivered = econtainer_runtime_on_event(runtime, event, sizeof(event), &guest_result);
        }
        report_heap("after_event");
        probe_record_header_lengths();
        stopped = delivered;
        if (delivered == ECONTAINER_RUNTIME_OK) {
            stopped = econtainer_runtime_stop(runtime);
        }
    }
    econtainer_runtime_close(&runtime);
    report_heap("closed");
    ESP_LOGI(TAG, "result open=%d init=%d event=%d stop=%d guest=%d",
             (int)opened, (int)initialized, (int)delivered, (int)stopped,
             (int)guest_result);
    return NULL;
}

void capacity_runtime_probe(void)
{
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) {
        ESP_LOGE(TAG, "pthread attributes initialization failed");
        return;
    }
    if (pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE) != 0 ||
        pthread_attr_setstacksize(&attributes, 8192) != 0) {
        ESP_LOGE(TAG, "pthread attributes configuration failed");
        pthread_attr_destroy(&attributes);
        return;
    }
    pthread_t thread;
    const int created = pthread_create(&thread, &attributes, run_guest, NULL);
    pthread_attr_destroy(&attributes);
    if (created != 0) {
        ESP_LOGE(TAG, "pthread creation failed: %d", created);
        return;
    }
    const int joined = pthread_join(thread, NULL);
    if (joined != 0) {
        ESP_LOGE(TAG, "pthread join failed: %d", joined);
    }
}

void capacity_heap_checkpoint(const char *phase)
{
    report_heap(phase);
}
