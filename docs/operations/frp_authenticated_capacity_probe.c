#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_frp_aead.h"
#include "runtime_internal.h"
#include "runtime_guest_bytes.h"

static const char *const TAG = "five-capacity-auth";
static const uint8_t s_key[EFRP_AEAD_KEY_BYTES] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
};
extern const uint8_t aead_4096_bin_start[] asm("_binary_aead_4096_bin_start");
extern const uint8_t aead_4096_bin_end[] asm("_binary_aead_4096_bin_end");
extern const uint8_t aead_65536_bin_start[] asm("_binary_aead_65536_bin_start");
extern const uint8_t aead_65536_bin_end[] asm("_binary_aead_65536_bin_end");

typedef struct { void *pointer; size_t size; } allocation_t;
static allocation_t s_allocations[EFRP_AEAD_RX_MAX_CHUNKS];
static size_t s_alloc_count, s_release_count, s_failed_count, s_live_bytes, s_peak_bytes;
static unsigned s_run;
static unsigned s_failures;

static unsigned free_bytes(void) { return (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT); }
static unsigned largest_bytes(void) { return (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }
static void report_heap(const char *phase)
{
    ESP_LOGI(TAG, "heap phase=%s free=%u largest=%u min=%u", phase, free_bytes(), largest_bytes(),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}
static void reset_allocations(void)
{
    memset(s_allocations, 0, sizeof s_allocations);
    s_alloc_count = s_release_count = s_failed_count = s_live_bytes = s_peak_bytes = 0;
}
static void *track_allocate(size_t count, size_t size)
{
    void *pointer = calloc(count, size);
    if (!pointer) { ++s_failed_count; return NULL; }
    for (size_t i = 0; i < EFRP_AEAD_RX_MAX_CHUNKS; ++i) {
        if (s_allocations[i].pointer) continue;
        s_allocations[i] = (allocation_t){pointer, count * size};
        ++s_alloc_count;
        s_live_bytes += count * size;
        if (s_live_bytes > s_peak_bytes) s_peak_bytes = s_live_bytes;
        return pointer;
    }
    free(pointer);
    ++s_failed_count;
    return NULL;
}
static void track_release(void *pointer)
{
    for (size_t i = 0; i < EFRP_AEAD_RX_MAX_CHUNKS; ++i) {
        if (s_allocations[i].pointer != pointer) continue;
        s_live_bytes -= s_allocations[i].size;
        s_allocations[i] = (allocation_t){0};
        ++s_release_count;
        free(pointer);
        return;
    }
    ++s_failures;
    ESP_LOGE(TAG, "untracked FRP release");
}
static uint8_t expected_byte(size_t offset)
{
    return (uint8_t)((offset * 37U + 11U) & 0xffU);
}
static void probe_record(const char *label, const uint8_t *wire, size_t wire_length,
                         size_t plain_length, bool tamper)
{
    reset_allocations();
    const unsigned before_free = free_bytes(), before_largest = largest_bytes();
    efrp_aead_reader_t reader = {0};
    const efrp_result_t initialized = efrp_aead_reader_init_chunked(
        &reader, s_key, track_allocate, track_release);
    efrp_result_t fed = initialized;
    size_t offset = 0, compared = 0;
    if (initialized == EFRP_OK) {
        const size_t limit = wire_length - (tamper ? 1U : 0U);
        while (offset < limit) {
            size_t consumed = 0;
            size_t take = limit - offset;
            if (take > 1024U) take = 1024U;
            fed = efrp_aead_feed(&reader, wire + offset, take, &consumed);
            offset += consumed;
            if (fed != EFRP_OK || consumed != take) break;
        }
        if (fed == EFRP_OK && tamper) {
            const uint8_t bad_tag_byte = wire[wire_length - 1] ^ 1U;
            size_t consumed = 0;
            fed = efrp_aead_feed(&reader, &bad_tag_byte, 1, &consumed);
            offset += consumed;
        }
    }
    const unsigned during_free = free_bytes(), during_largest = largest_bytes();
    if (fed == EFRP_OK && offset == wire_length) {
        while (compared < plain_length) {
            const uint8_t *chunk = NULL;
            size_t length = 0;
            if (efrp_aead_plaintext(&reader, &chunk, &length) != EFRP_OK ||
                !chunk || !length || length > plain_length - compared) break;
            bool same = true;
            for (size_t i = 0; i < length; ++i) {
                if (chunk[i] != expected_byte(compared + i)) { same = false; break; }
            }
            if (!same || efrp_aead_consume_plaintext(&reader, length) != EFRP_OK) break;
            compared += length;
        }
    }
    const efrp_result_t finished = efrp_aead_finish(&reader);
    const uint64_t records = reader.records;
    efrp_aead_reader_destroy(&reader);
    efrp_aead_reader_destroy(&reader);
    const unsigned after_free = free_bytes(), after_largest = largest_bytes();
    const bool released = s_live_bytes == 0 && s_alloc_count == s_release_count;
    const bool authenticated = fed == EFRP_OK && offset == wire_length &&
        records == 1 && finished == EFRP_OK && compared == plain_length;
    const bool expected = tamper ? fed == EFRP_AUTHENTICATION_FAILED && released :
        (plain_length == 65536 && s_run == 2) ?
            ((authenticated && released) || (fed == EFRP_NO_MEMORY && released)) : authenticated && released;
    if (!expected) ++s_failures;
    ESP_LOGI(TAG,
             "frp_record run=%u label=%s plain=%u init=%d feed=%d consumed=%u records=%u compare=%u auth=%d expected=%d alloc=%u released=%u alloc_fail=%u peak=%u live=%u before=%u/%u during=%u/%u after=%u/%u",
             s_run, label, (unsigned)plain_length, (int)initialized, (int)fed,
             (unsigned)offset, (unsigned)records, (unsigned)compared,
             authenticated, expected, (unsigned)s_alloc_count, (unsigned)s_release_count,
             (unsigned)s_failed_count, (unsigned)s_peak_bytes, (unsigned)s_live_bytes,
             before_free, before_largest, during_free, during_largest, after_free, after_largest);
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
    ++s_run;
    report_heap("before_guest");
    const econtainer_runtime_result_t opened = econtainer_runtime_open(
        counter_guest, sizeof(counter_guest), &limits, &runtime);
    report_heap("guest_opened");
    econtainer_runtime_result_t initialized = opened, delivered = opened, stopped = opened;
    if (opened == ECONTAINER_RUNTIME_OK) {
        initialized = econtainer_runtime_init(runtime);
        delivered = initialized;
        if (initialized == ECONTAINER_RUNTIME_OK) {
            delivered = econtainer_runtime_on_event(runtime, event, sizeof event, &guest_result);
        }
        report_heap("guest_event_done");
        if (delivered == ECONTAINER_RUNTIME_OK && guest_result == 3) {
            if (s_run == 1) {
                probe_record("early_max", aead_65536_bin_start,
                             (size_t)(aead_65536_bin_end - aead_65536_bin_start), 65536, false);
            } else {
                for (unsigned repeat = 0; repeat < 2; ++repeat) {
                    probe_record(repeat == 0 ? "ready_4k_first" : "ready_4k_repeat",
                                 aead_4096_bin_start,
                                 (size_t)(aead_4096_bin_end - aead_4096_bin_start), 4096, false);
                }
                probe_record("ready_4k_bad_tag", aead_4096_bin_start,
                             (size_t)(aead_4096_bin_end - aead_4096_bin_start), 4096, true);
                probe_record("ready_max", aead_65536_bin_start,
                             (size_t)(aead_65536_bin_end - aead_65536_bin_start), 65536, false);
            }
        } else {
            ++s_failures;
        }
        stopped = delivered;
        if (delivered == ECONTAINER_RUNTIME_OK) stopped = econtainer_runtime_stop(runtime);
    } else {
        ++s_failures;
    }
    const econtainer_runtime_result_t closed = econtainer_runtime_close(&runtime);
    report_heap("guest_closed");
    if (initialized != ECONTAINER_RUNTIME_OK || delivered != ECONTAINER_RUNTIME_OK ||
        stopped != ECONTAINER_RUNTIME_OK || closed != ECONTAINER_RUNTIME_OK ||
        guest_result != 3) ++s_failures;
    ESP_LOGI(TAG, "result run=%u open=%d init=%d event=%d stop=%d close=%d guest=%d failures=%u",
             s_run, (int)opened, (int)initialized, (int)delivered, (int)stopped,
             (int)closed, (int)guest_result, s_failures);
    return NULL;
}
void capacity_runtime_probe(void)
{
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) { ++s_failures; return; }
    if (pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE) != 0 ||
        pthread_attr_setstacksize(&attributes, 8192) != 0) {
        ++s_failures;
        pthread_attr_destroy(&attributes);
        return;
    }
    pthread_t thread;
    const int created = pthread_create(&thread, &attributes, run_guest, NULL);
    pthread_attr_destroy(&attributes);
    if (created != 0) { ++s_failures; return; }
    if (pthread_join(thread, NULL) != 0) ++s_failures;
    ESP_LOGI(TAG, "probe_summary runs=%u failures=%u", s_run, s_failures);
}
void capacity_heap_checkpoint(const char *phase)
{
    report_heap(phase);
}
