#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_container.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "bh_platform.h"
#include "wasm_export.h"

#if WASM_ENABLE_INTERP != 1 || WASM_ENABLE_FAST_INTERP != 0 || \
    WASM_ENABLE_AOT != 0 || WASM_ENABLE_INSTRUCTION_METERING != 1 || \
    WASM_ENABLE_LIBC_WASI != 0 || WASM_ENABLE_LIB_PTHREAD != 0 || \
    WASM_ENABLE_BULK_MEMORY != 0
#error "The C3 probe requires the bounded WAMR Classic profile"
#endif

static const char *const TAG = "container-probe";

/* Freestanding standard Wasm v1 fixtures: () -> i32, no imports/start. */
static uint8_t return_zero[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x07, 0x01, 0x03, 0x72, 0x75, 0x6e, 0x00, 0x00,
    0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x00, 0x0b,
};
static uint8_t endless_loop[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0b, 0x01, 0x07, 0x6c, 0x6f, 0x6f, 0x70, 0x69, 0x6e, 0x67, 0x00, 0x00,
    0x0a, 0x0b, 0x01, 0x09, 0x00, 0x03, 0x40, 0x0c, 0x00, 0x0b, 0x41, 0x00, 0x0b,
};

static void report_heap(const char *phase)
{
    ESP_LOGI(TAG, "%s free=%u largest=%u", phase,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static bool exercise(uint8_t *bytes, uint32_t size, const char *export_name, bool expect_success)
{
    char error[128] = {0};
    if (econtainer_wasm_check(bytes, size) != ECONTAINER_WASM_OK) {
        ESP_LOGE(TAG, "module scan rejected %s", export_name);
        return false;
    }
    wasm_module_t module = wasm_runtime_load(bytes, size, error, sizeof(error));
    if (!module) {
        ESP_LOGE(TAG, "load %s: %s", export_name, error);
        return false;
    }
    wasm_module_inst_t instance = wasm_runtime_instantiate(module, 4096, 0, error, sizeof(error));
    if (!instance) {
        ESP_LOGE(TAG, "instantiate %s: %s", export_name, error);
        wasm_runtime_unload(module);
        return false;
    }
    wasm_exec_env_t environment = wasm_runtime_create_exec_env(instance, 4096);
    wasm_function_inst_t function = wasm_runtime_lookup_function(instance, export_name);
    bool passed = false;
    if (environment && function) {
        uint32_t result[1] = {UINT32_MAX};
        wasm_runtime_set_instruction_count_limit(environment, 1000);
        const bool call_ok = wasm_runtime_call_wasm(environment, function, 0, result);
        const char *exception = wasm_runtime_get_exception(instance);
        passed = expect_success
                     ? call_ok && result[0] == 0 && exception == NULL
                     : !call_ok && exception != NULL &&
                           strcmp(exception, "Exception: instruction limit exceeded") == 0;
        ESP_LOGI(TAG, "%s call_ok=%d result=%u exception=%s", export_name,
                 (int)call_ok, (unsigned)result[0],
                 exception ? exception : "none");
    }
    if (environment) {
        wasm_runtime_destroy_exec_env(environment);
    }
    wasm_runtime_deinstantiate(instance);
    wasm_runtime_unload(module);
    return passed;
}

static void *probe_thread(void *unused)
{
    (void)unused;
    RuntimeInitArgs args;
    memset(&args, 0, sizeof(args));
    args.mem_alloc_type = Alloc_With_Allocator;
    args.mem_alloc_option.allocator.malloc_func = (void *)os_malloc;
    args.mem_alloc_option.allocator.realloc_func = (void *)os_realloc;
    args.mem_alloc_option.allocator.free_func = (void *)os_free;
    report_heap("before");
    if (!wasm_runtime_full_init(&args)) {
        ESP_LOGE(TAG, "WAMR initialization failed");
        return NULL;
    }
    const bool normal = exercise(return_zero, sizeof(return_zero), "run", true);
    const bool bounded = exercise(endless_loop, sizeof(endless_loop), "looping", false);
    wasm_runtime_destroy();
    report_heap("after");
    ESP_LOGI(TAG, "normal=%d instruction_limit=%d", (int)normal, (int)bounded);
    return NULL;
}

void app_main(void)
{
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) {
        ESP_LOGE(TAG, "probe thread attributes initialization failed");
        return;
    }
    if (pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE) != 0 ||
        pthread_attr_setstacksize(&attributes, 8192) != 0) {
        ESP_LOGE(TAG, "probe thread configuration failed");
        pthread_attr_destroy(&attributes);
        return;
    }

    pthread_t thread;
    const int create_result = pthread_create(&thread, &attributes, probe_thread, NULL);
    pthread_attr_destroy(&attributes);
    if (create_result != 0) {
        ESP_LOGE(TAG, "probe thread creation failed: %d", create_result);
        return;
    }
    const int join_result = pthread_join(thread, NULL);
    if (join_result != 0) {
        ESP_LOGE(TAG, "probe thread join failed: %d", join_result);
    }
}
