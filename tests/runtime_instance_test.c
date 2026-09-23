#include "runtime_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    for (unsigned index = 0; index < 8; ++index) {
        econtainer_runtime_t *runtime = NULL;
        CHECK(econtainer_runtime_open(bytes, length, &limits, &runtime) == ECONTAINER_RUNTIME_OK);
        CHECK(econtainer_runtime_init(runtime) == ECONTAINER_RUNTIME_OK);
        CHECK(econtainer_runtime_stop(runtime) == ECONTAINER_RUNTIME_OK);
        econtainer_runtime_close(&runtime);
        CHECK(runtime == NULL);
    }
    free(bytes);
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr, "usage: %s counter event-read init-loop event-loop stop-loop wrong-signature\n",
                argv[0]);
        return 2;
    }
    const bool passed = test_counter(argv[1]) && test_event_copy(argv[2]) &&
                        test_event_allocation_failure(argv[2]) &&
                        test_loop(argv[3], 0) && test_loop(argv[4], 1) &&
                        test_loop(argv[5], 2) &&
                        test_wrong_abi_and_release(argv[6], argv[1]) &&
                        test_repeated_release(argv[1]);
    if (passed) {
        fprintf(stderr, "runtime instance: counter/event copy/three budgets/ABI/release passed\n");
    }
    return passed ? 0 : 1;
}
