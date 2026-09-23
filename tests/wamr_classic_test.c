#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_container.h"
#include "wasm_export.h"

/* Standard Wasm v1 fixtures with no imports, start or implicit constructors. */
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

static bool exercise(uint8_t *bytes, uint32_t size, const char *export_name,
                     bool expect_success)
{
    char error[128] = {0};
    if (econtainer_wasm_check(bytes, size) != ECONTAINER_WASM_OK) {
        fprintf(stderr, "%s: scanner rejected fixture\n", export_name);
        return false;
    }
    wasm_module_t module = wasm_runtime_load(bytes, size, error, sizeof(error));
    if (module == NULL) {
        fprintf(stderr, "%s: load failed: %s\n", export_name, error);
        return false;
    }
    wasm_module_inst_t instance = wasm_runtime_instantiate(module, 4096, 0, error, sizeof(error));
    if (instance == NULL) {
        fprintf(stderr, "%s: instantiate failed: %s\n", export_name, error);
        wasm_runtime_unload(module);
        return false;
    }
    wasm_exec_env_t environment = wasm_runtime_create_exec_env(instance, 4096);
    wasm_function_inst_t function = wasm_runtime_lookup_function(instance, export_name);
    bool passed = false;
    if (environment != NULL && function != NULL) {
        uint32_t result[1] = {UINT32_MAX};
        wasm_runtime_set_instruction_count_limit(environment, 1000);
        const bool call_ok = wasm_runtime_call_wasm(environment, function, 0, result);
        const char *exception = wasm_runtime_get_exception(instance);
        passed = expect_success
                     ? call_ok && result[0] == 0 && exception == NULL
                     : !call_ok && exception != NULL &&
                           strcmp(exception, "Exception: instruction limit exceeded") == 0;
        fprintf(stderr, "%s: call_ok=%d result=%u exception=%s\n", export_name,
                (int)call_ok, (unsigned)result[0], exception ? exception : "none");
    }
    if (environment != NULL) {
        wasm_runtime_destroy_exec_env(environment);
    }
    wasm_runtime_deinstantiate(instance);
    wasm_runtime_unload(module);
    return passed;
}

int main(void)
{
    if (!wasm_runtime_init()) {
        fprintf(stderr, "WAMR initialization failed\n");
        return 1;
    }
    const bool normal = exercise(return_zero, sizeof(return_zero), "run", true);
    const bool bounded = exercise(endless_loop, sizeof(endless_loop), "looping", false);
    wasm_runtime_destroy();
    return normal && bounded ? 0 : 1;
}
