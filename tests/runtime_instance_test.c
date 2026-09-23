#include "runtime_internal.h"
#include "esp_container.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ECONTAINER_TEST_RESOURCE_STATS
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <sys/mman.h>
#endif

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static const econtainer_runtime_limits_t limits = {
    .max_wasm_bytes = 512 * 1024,
    .max_memory_pages = 2,
    .stack_size_bytes = 4096,
    .heap_size_bytes = 4096,
    .max_event_bytes = 128,
    .init_instruction_budget = 1000,
    .event_instruction_budget = 1000,
    .stop_instruction_budget = 1000,
};

#ifdef ECONTAINER_TEST_RESOURCE_STATS
typedef struct {
    size_t malloc_bytes;
    mach_vm_size_t virtual_bytes;
    integer_t regions;
} resource_stats_t;

static bool sample_resources(resource_stats_t *sample)
{
    malloc_statistics_t statistics = {0};
    malloc_zone_statistics(malloc_default_zone(), &statistics);
    task_vm_info_data_t vm = {0};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count) !=
        KERN_SUCCESS) return false;
    sample->malloc_bytes = statistics.size_in_use;
    sample->virtual_bytes = vm.virtual_size;
    sample->regions = vm.region_count;
    return sample->malloc_bytes > 0 && sample->virtual_bytes > 0 && sample->regions > 0;
}

static bool resource_probes_calibrated(void)
{
    resource_stats_t before = {0}, allocated = {0};
    if (!sample_resources(&before)) return false;
    void *heap = malloc(65536);
    if (heap == NULL) return false;
    memset(heap, 0x5a, 65536);
    const bool heap_visible = sample_resources(&allocated) &&
                              allocated.malloc_bytes >= before.malloc_bytes + 65536;
    free(heap);
    if (!heap_visible || !sample_resources(&before)) return false;
    void *mapping = mmap(NULL, 65536, PROT_READ | PROT_WRITE,
                         MAP_ANON | MAP_PRIVATE, -1, 0);
    if (mapping == MAP_FAILED) return false;
    const bool mapping_visible = sample_resources(&allocated) &&
                                 allocated.virtual_bytes >= before.virtual_bytes + 65536;
    return munmap(mapping, 65536) == 0 && mapping_visible;
}

static bool resources_stable(const char *label, resource_stats_t after_ten,
                             resource_stats_t after_fifty, resource_stats_t after_hundred)
{
    fprintf(stderr, "%s resources 10/50/100: malloc=%zu/%zu/%zu "
            "virtual=%llu/%llu/%llu regions=%d/%d/%d\n", label,
            after_ten.malloc_bytes, after_fifty.malloc_bytes,
            after_hundred.malloc_bytes,
            (unsigned long long)after_ten.virtual_bytes,
            (unsigned long long)after_fifty.virtual_bytes,
            (unsigned long long)after_hundred.virtual_bytes,
            after_ten.regions, after_fifty.regions, after_hundred.regions);
    /* Existing mappings may split; retained heap/address space must not grow. */
    return after_ten.malloc_bytes == after_fifty.malloc_bytes &&
           after_fifty.malloc_bytes == after_hundred.malloc_bytes &&
           after_ten.virtual_bytes == after_fifty.virtual_bytes &&
           after_fifty.virtual_bytes == after_hundred.virtual_bytes &&
           after_hundred.regions <= after_fifty.regions;
}
#endif

static uint8_t *read_file(const char *path, size_t *size_bytes)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    const long length = ftell(file);
    if (length < 8 || length > 512 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size_bytes = (size_t)length;
    return bytes;
}

static bool test_counter(const char *path)
{
    size_t length = 0;
    uint8_t *bytes = read_file(path, &length);
    CHECK(bytes != NULL);
    econtainer_runtime_t *runtime = NULL;
    econtainer_runtime_t *other = NULL;
    CHECK(econtainer_runtime_open(bytes, length, &limits, &runtime) == ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_state(runtime) == ECONTAINER_RUNTIME_LOADED);
    CHECK(econtainer_runtime_open(bytes, length, &limits, &other) == ECONTAINER_RUNTIME_BUSY);
    CHECK(other == NULL);
    memset(bytes, 0, length); /* WAMR must retain its own writable copy. */
    free(bytes);
    CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_INVALID_STATE);
    CHECK(econtainer_runtime_state(runtime) == ECONTAINER_RUNTIME_RUNNING);
    int32_t result = -1;
    const uint8_t first[] = {1, 2, 3};
    const uint8_t second[] = {4, 5};
    CHECK(econtainer_runtime_on_event(runtime, first, sizeof(first), &result) == ECONTAINER_RUNTIME_OK);
    CHECK(result == 3);
    CHECK(econtainer_runtime_on_event(runtime, second, sizeof(second), &result) == ECONTAINER_RUNTIME_OK);
    CHECK(result == 5);
    CHECK(econtainer_runtime_on_event(runtime, NULL, 1, &result) == ECONTAINER_RUNTIME_INVALID_INPUT);
    CHECK(econtainer_runtime_state(runtime) == ECONTAINER_RUNTIME_RUNNING);
    CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_state(runtime) == ECONTAINER_RUNTIME_STOPPED);
    CHECK(econtainer_runtime_on_event(runtime, first, sizeof(first), &result) ==
          ECONTAINER_RUNTIME_INVALID_STATE);
    econtainer_runtime_close(&runtime);
    CHECK(runtime == NULL);
    econtainer_runtime_close(&runtime);
    return true;
}

static bool test_event_copy(const char *path)
{
    size_t length = 0;
    uint8_t *bytes = read_file(path, &length);
    CHECK(bytes != NULL);
    econtainer_runtime_t *runtime = NULL;
    CHECK(econtainer_runtime_open(bytes, length, &limits, &runtime) == ECONTAINER_RUNTIME_OK);
    free(bytes);
    CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
    const uint8_t event[] = {2, 3, 5};
    int32_t result = -1;
    CHECK(econtainer_runtime_on_event(runtime, event, sizeof(event), &result) == ECONTAINER_RUNTIME_OK);
    CHECK(result == 10);
    CHECK(econtainer_runtime_on_event(runtime, NULL, 0, &result) == ECONTAINER_RUNTIME_OK);
    CHECK(result == -1); /* Guest result does not become a host runtime failure. */
    uint8_t oversized[129] = {0};
    CHECK(econtainer_runtime_on_event(runtime, oversized, sizeof(oversized), &result) ==
          ECONTAINER_RUNTIME_INVALID_INPUT);
    CHECK(econtainer_runtime_state(runtime) == ECONTAINER_RUNTIME_RUNNING);
    CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_OK);
    econtainer_runtime_close(&runtime);
    return true;
}

static bool test_loop(const char *path, unsigned entry)
{
    size_t length = 0;
    uint8_t *bytes = read_file(path, &length);
    CHECK(bytes != NULL);
    econtainer_runtime_t *runtime = NULL;
    CHECK(econtainer_runtime_open(bytes, length, &limits, &runtime) == ECONTAINER_RUNTIME_OK);
    free(bytes);
    if (entry == 0) {
        CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_INSTRUCTION_LIMIT);
    }
    else {
        CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
        if (entry == 1) {
            const uint8_t event[] = {1};
            int32_t result = -1;
            CHECK(econtainer_runtime_on_event(runtime, event, sizeof(event), &result) ==
                  ECONTAINER_RUNTIME_INSTRUCTION_LIMIT);
        }
        else {
            CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_INSTRUCTION_LIMIT);
        }
    }
    CHECK(econtainer_runtime_state(runtime) == ECONTAINER_RUNTIME_FAILED);
    CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_INVALID_STATE);
    econtainer_runtime_close(&runtime);
    CHECK(runtime == NULL);
    return true;
}

static bool test_event_allocation_failure(const char *path)
{
    size_t length = 0;
    uint8_t *bytes = read_file(path, &length);
    CHECK(bytes != NULL);
    econtainer_runtime_limits_t pressure = limits;
    pressure.max_event_bytes = pressure.heap_size_bytes;
    econtainer_runtime_t *runtime = NULL;
    CHECK(econtainer_runtime_open(bytes, length, &pressure, &runtime) == ECONTAINER_RUNTIME_OK);
    free(bytes);
    CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
    uint8_t event[4096] = {0};
    int32_t result = -1;
    CHECK(econtainer_runtime_on_event(runtime, event, sizeof(event), &result) ==
          ECONTAINER_RUNTIME_NO_MEMORY);
    CHECK(econtainer_runtime_state(runtime) == ECONTAINER_RUNTIME_RUNNING);
    const uint8_t small_event[] = {2, 3, 5};
    CHECK(econtainer_runtime_on_event(runtime, small_event, sizeof(small_event), &result) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(result == 10);
    CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_OK);
    econtainer_runtime_close(&runtime);
    return true;
}

static bool test_wrong_abi_and_release(const char *wrong_path, const char *counter_path)
{
    size_t length = 0;
    uint8_t *bytes = read_file(wrong_path, &length);
    CHECK(bytes != NULL);
    econtainer_runtime_t *runtime = NULL;
    CHECK(econtainer_runtime_open(bytes, length, &limits, &runtime) == ECONTAINER_RUNTIME_BAD_ABI);
    CHECK(runtime == NULL);
    free(bytes);
    bytes = read_file(counter_path, &length);
    CHECK(bytes != NULL);
    econtainer_runtime_limits_t too_small = limits;
    too_small.max_memory_pages = 1;
    CHECK(econtainer_runtime_open(bytes, length, &too_small, &runtime) ==
          ECONTAINER_RUNTIME_BAD_ABI);
    CHECK(runtime == NULL);
    too_small = limits;
    too_small.init_instruction_budget = 0;
    CHECK(econtainer_runtime_open(bytes, length, &too_small, &runtime) ==
          ECONTAINER_RUNTIME_INVALID_INPUT);
    CHECK(runtime == NULL);
    CHECK(econtainer_runtime_open(bytes, length, &limits, &runtime) == ECONTAINER_RUNTIME_OK);
    free(bytes);
    CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_OK);
    econtainer_runtime_close(&runtime);
    return true;
}

static bool test_repeated_release(const char *counter_path)
{
    size_t length = 0;
    uint8_t *bytes = read_file(counter_path, &length);
    CHECK(bytes != NULL);
#ifdef ECONTAINER_TEST_RESOURCE_STATS
    resource_stats_t after_ten = {0}, after_fifty = {0}, after_hundred = {0};
#endif
    for (unsigned index = 0; index < 100; ++index) {
        econtainer_runtime_t *runtime = NULL;
        CHECK(econtainer_runtime_open(bytes, length, &limits, &runtime) == ECONTAINER_RUNTIME_OK);
        CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
        const uint8_t event[] = {1, 2, 3};
        int32_t result = -1;
        CHECK(econtainer_runtime_on_event(runtime, event, sizeof event, &result) ==
              ECONTAINER_RUNTIME_OK);
        CHECK(result == 3);
        CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_OK);
        CHECK(econtainer_runtime_close(&runtime) == ECONTAINER_RUNTIME_OK);
        CHECK(runtime == NULL);
#ifdef ECONTAINER_TEST_RESOURCE_STATS
        if (index == 9U) CHECK(sample_resources(&after_ten));
        if (index == 49U) CHECK(sample_resources(&after_fifty));
        if (index == 99U) CHECK(sample_resources(&after_hundred));
#endif
    }
#ifdef ECONTAINER_TEST_RESOURCE_STATS
    CHECK(resources_stable("counter", after_ten, after_fifty, after_hundred));
#endif
    free(bytes);
    return true;
}

static bool test_failure_reopen(const char *counter_path, const char *init_loop_path,
                                const char *event_loop_path, const char *stop_loop_path,
                                const char *stop_fail_path)
{
    const char *paths[] = {init_loop_path, event_loop_path, stop_loop_path, stop_fail_path};
    uint8_t *fault_bytes[4] = {0};
    size_t fault_sizes[4] = {0};
    for (unsigned index = 0; index < 4; ++index) {
        fault_bytes[index] = read_file(paths[index], &fault_sizes[index]);
        CHECK(fault_bytes[index] != NULL);
    }
    size_t counter_size = 0;
    uint8_t *counter = read_file(counter_path, &counter_size);
    CHECK(counter != NULL);
#ifdef ECONTAINER_TEST_RESOURCE_STATS
    resource_stats_t after_ten = {0}, after_fifty = {0}, after_hundred = {0};
#endif
    for (unsigned index = 0; index < 100; ++index) {
        const unsigned fault = index % 4U;
        econtainer_runtime_t *runtime = NULL;
        CHECK(econtainer_runtime_open(fault_bytes[fault], fault_sizes[fault], &limits,
                                      &runtime) == ECONTAINER_RUNTIME_OK);
        if (fault == 0U) {
            CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_INSTRUCTION_LIMIT);
        }
        else {
            CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
            const uint8_t event[] = {1};
            int32_t result = -1;
            if (fault == 1U) {
                CHECK(econtainer_runtime_on_event(runtime, event, sizeof event, &result) ==
                      ECONTAINER_RUNTIME_INSTRUCTION_LIMIT);
            }
            else {
                CHECK(econtainer_runtime_on_event(runtime, event, sizeof event, &result) ==
                      ECONTAINER_RUNTIME_OK);
                CHECK(econtainer_runtime_stop(runtime) ==
                      (fault == 2U ? ECONTAINER_RUNTIME_INSTRUCTION_LIMIT :
                                     ECONTAINER_RUNTIME_GUEST_FAILURE));
            }
        }
        CHECK(econtainer_runtime_state(runtime) == ECONTAINER_RUNTIME_FAILED);
        CHECK(econtainer_runtime_close(&runtime) == ECONTAINER_RUNTIME_OK);
        CHECK(runtime == NULL);
        CHECK(econtainer_runtime_open(counter, counter_size, &limits, &runtime) ==
              ECONTAINER_RUNTIME_OK);
        CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
        const uint8_t event[] = {1, 2, 3};
        int32_t result = -1;
        CHECK(econtainer_runtime_on_event(runtime, event, sizeof event, &result) ==
              ECONTAINER_RUNTIME_OK);
        CHECK(result == 3);
        CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_OK);
        CHECK(econtainer_runtime_close(&runtime) == ECONTAINER_RUNTIME_OK);
        CHECK(runtime == NULL);
#ifdef ECONTAINER_TEST_RESOURCE_STATS
        if (index == 9U) CHECK(sample_resources(&after_ten));
        if (index == 49U) CHECK(sample_resources(&after_fifty));
        if (index == 99U) CHECK(sample_resources(&after_hundred));
#endif
    }
#ifdef ECONTAINER_TEST_RESOURCE_STATS
    CHECK(resources_stable("failure/reopen", after_ten, after_fifty, after_hundred));
#endif
    free(counter);
    for (unsigned index = 0; index < 4; ++index) free(fault_bytes[index]);
    return true;
}

static bool test_repeated_native_release(const char *path)
{
    size_t length = 0;
    uint8_t *bytes = read_file(path, &length);
    CHECK(bytes != NULL);
    econtainer_runtime_limits_t authorized = limits;
    authorized.allowed_capabilities = ECONTAINER_CAP_MONOTONIC_TIME | ECONTAINER_CAP_LOG;
    authorized.max_log_bytes = 16;
#ifdef ECONTAINER_TEST_RESOURCE_STATS
    resource_stats_t after_ten = {0}, after_fifty = {0}, after_hundred = {0};
#endif
    for (unsigned index = 0; index < 100; ++index) {
        econtainer_runtime_t *runtime = NULL;
        CHECK(econtainer_runtime_open(bytes, length, &authorized, &runtime) ==
              ECONTAINER_RUNTIME_OK);
        CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
        uint8_t log[16] = {0};
        size_t log_size = 0;
        CHECK(econtainer_runtime_take_log(runtime, log, sizeof log, &log_size) ==
              ECONTAINER_RUNTIME_OK);
        CHECK(log_size == 4 && memcmp(log, "init", 4) == 0);
        const uint8_t event[] = {1, 'a', 'b', 'c'};
        int32_t result = -1;
        CHECK(econtainer_runtime_on_event(runtime, event, sizeof event, &result) ==
              ECONTAINER_RUNTIME_OK);
        CHECK(result == 0);
        CHECK(econtainer_runtime_take_log(runtime, log, sizeof log, &log_size) ==
              ECONTAINER_RUNTIME_OK);
        CHECK(log_size == 3 && memcmp(log, "abc", 3) == 0);
        CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_OK);
        CHECK(econtainer_runtime_close(&runtime) == ECONTAINER_RUNTIME_OK);
        CHECK(runtime == NULL);
#ifdef ECONTAINER_TEST_RESOURCE_STATS
        if (index == 9U) CHECK(sample_resources(&after_ten));
        if (index == 49U) CHECK(sample_resources(&after_fifty));
        if (index == 99U) CHECK(sample_resources(&after_hundred));
#endif
    }
#ifdef ECONTAINER_TEST_RESOURCE_STATS
    CHECK(resources_stable("native", after_ten, after_fifty, after_hundred));
#endif
    free(bytes);
    return true;
}

static bool test_host_api(const char *host_api_path)
{
    size_t length = 0;
    uint8_t *bytes = read_file(host_api_path, &length);
    CHECK(bytes != NULL);
    uint32_t imports = 0;
    CHECK(econtainer_wasm_imported_capabilities(bytes, length, &imports));
    CHECK(imports == (ECONTAINER_CAP_MONOTONIC_TIME | ECONTAINER_CAP_LOG));
    econtainer_runtime_t *runtime = NULL;
    CHECK(econtainer_runtime_open(bytes, length, &limits, &runtime) ==
          ECONTAINER_RUNTIME_NOT_AUTHORIZED);
    econtainer_runtime_limits_t authorized = limits;
    authorized.allowed_capabilities = ECONTAINER_CAP_MONOTONIC_TIME;
    CHECK(econtainer_runtime_open(bytes, length, &authorized, &runtime) ==
          ECONTAINER_RUNTIME_NOT_AUTHORIZED);
    authorized.allowed_capabilities |= ECONTAINER_CAP_LOG;
    CHECK(econtainer_runtime_open(bytes, length, &authorized, &runtime) ==
          ECONTAINER_RUNTIME_INVALID_INPUT);
    authorized.max_log_bytes = 16;
    CHECK(econtainer_runtime_open(bytes, length, &authorized, &runtime) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
    uint8_t log[16] = {0};
    size_t log_size = 99;
    CHECK(econtainer_runtime_take_log(runtime, log, 2, &log_size) ==
          ECONTAINER_RUNTIME_INVALID_INPUT);
    CHECK(log_size == 99);
    CHECK(econtainer_runtime_take_log(runtime, log, sizeof(log), &log_size) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(log_size == 4 && memcmp(log, "init", 4) == 0);
    CHECK(econtainer_runtime_take_log(runtime, log, sizeof(log), &log_size) ==
          ECONTAINER_RUNTIME_NO_LOG);
    int32_t result = 99;
    uint8_t first[] = {1, 'a', 'b', 'c'};
    CHECK(econtainer_runtime_on_event(runtime, first, sizeof(first), &result) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(result == 0);
    memset(first, 'x', sizeof(first));
    CHECK(econtainer_runtime_take_log(runtime, log, sizeof(log), &log_size) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(log_size == 3 && memcmp(log, "abc", 3) == 0);
    const uint8_t twice[] = {2};
    CHECK(econtainer_runtime_on_event(runtime, twice, sizeof(twice), &result) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(result == -2);
    CHECK(econtainer_runtime_take_log(runtime, log, sizeof(log), &log_size) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(log_size == 5 && memcmp(log, "first", 5) == 0);
    const uint8_t too_long[] = {4};
    CHECK(econtainer_runtime_on_event(runtime, too_long, sizeof(too_long), &result) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(result == -1);
    const uint8_t time_event[] = {5};
    CHECK(econtainer_runtime_on_event(runtime, time_event, sizeof(time_event), &result) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(result == 0);
    CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_close(&runtime) == ECONTAINER_RUNTIME_OK);
    CHECK(runtime == NULL);

    CHECK(econtainer_runtime_open(bytes, length, &authorized, &runtime) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_take_log(runtime, log, sizeof(log), &log_size) ==
          ECONTAINER_RUNTIME_OK);
    const uint8_t invalid_pointer[] = {3};
    econtainer_runtime_result_t invalid_result = econtainer_runtime_on_event(
        runtime, invalid_pointer, sizeof(invalid_pointer), &result);
    CHECK(invalid_result == ECONTAINER_RUNTIME_ENGINE_FAILURE);
    CHECK(econtainer_runtime_state(runtime) == ECONTAINER_RUNTIME_FAILED);
    CHECK(econtainer_runtime_close(&runtime) == ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_open(bytes, length, &authorized, &runtime) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
    CHECK(econtainer_runtime_take_log(runtime, log, sizeof(log), &log_size) ==
          ECONTAINER_RUNTIME_OK);
    CHECK(log_size == 4 && memcmp(log, "init", 4) == 0);
    CHECK(econtainer_runtime_close(&runtime) == ECONTAINER_RUNTIME_OK);

    /* An exact module/function/signature is required before WAMR loading. */
    uint8_t *bad = malloc(length);
    CHECK(bad != NULL);
    memcpy(bad, bytes, length);
    bool found = false;
    for (size_t index = 0; index + 10 < length; ++index) {
        if (memcmp(bad + index, "econtainer", 10) == 0) {
            bad[index] = 'x';
            found = true;
            break;
        }
    }
    CHECK(found);
    CHECK(econtainer_runtime_open(bad, length, &authorized, &runtime) ==
          ECONTAINER_RUNTIME_BAD_WASM);
    memcpy(bad, bytes, length);
    found = false;
    const uint8_t clock_type[] = {0x60, 0x00, 0x01, 0x7e};
    for (size_t index = 0; index + sizeof(clock_type) <= length; ++index) {
        if (memcmp(bad + index, clock_type, sizeof(clock_type)) == 0) {
            bad[index + 3] = 0x7f;
            found = true;
            break;
        }
    }
    CHECK(found);
    CHECK(econtainer_wasm_check(bad, length) == ECONTAINER_WASM_UNSUPPORTED);
    CHECK(econtainer_runtime_open(bad, length, &authorized, &runtime) ==
          ECONTAINER_RUNTIME_BAD_WASM);
    free(bad);
    free(bytes);
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 9) {
        fprintf(stderr, "usage: %s counter event-read init-loop event-loop stop-loop stop-fail wrong-signature host-api\n",
                argv[0]);
        return 2;
    }
#ifdef ECONTAINER_TEST_RESOURCE_STATS
    if (!resource_probes_calibrated()) {
        fprintf(stderr, "resource probe calibration failed\n");
        return 1;
    }
#endif
    const bool passed = test_counter(argv[1]) && test_event_copy(argv[2]) &&
                        test_event_allocation_failure(argv[2]) &&
                        test_loop(argv[3], 0) && test_loop(argv[4], 1) &&
                        test_loop(argv[5], 2) &&
                        test_wrong_abi_and_release(argv[7], argv[1]) &&
                        test_repeated_release(argv[1]) &&
                        test_failure_reopen(argv[1], argv[3], argv[4], argv[5], argv[6]) &&
                        test_repeated_native_release(argv[8]) &&
                        test_host_api(argv[8]);
    if (passed) {
        fprintf(stderr, "runtime instance: 100 counter, 100 native and 100 failure/reopen cycles passed\n");
    }
    return passed ? 0 : 1;
}
