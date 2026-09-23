#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static bool call_counter(wasm_exec_env_t environment, wasm_module_inst_t instance,
                         const char *name, uint32_t argc, uint32_t *arguments,
                         uint32_t expected)
{
    wasm_function_inst_t function = wasm_runtime_lookup_function(instance, name);
    if (function == NULL) {
        fprintf(stderr, "counter: missing %s\n", name);
        return false;
    }
    wasm_runtime_set_instruction_count_limit(environment, 1000);
    if (!wasm_runtime_call_wasm(environment, function, argc, arguments) ||
        wasm_runtime_get_exception(instance) != NULL || arguments[0] != expected) {
        fprintf(stderr, "counter: %s failed, result=%u, exception=%s\n", name,
                (unsigned)arguments[0], wasm_runtime_get_exception(instance)
                    ? wasm_runtime_get_exception(instance) : "none");
        return false;
    }
    return true;
}

static bool exercise_counter(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "counter: cannot open Wasm file\n");
        if (file != NULL) fclose(file);
        return false;
    }
    const long length = ftell(file);
    if (length < 8 || length > 512 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "counter: invalid Wasm length\n");
        fclose(file);
        return false;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "counter: cannot read Wasm file\n");
        free(bytes);
        fclose(file);
        return false;
    }
    fclose(file);
    char error[128] = {0};
    bool passed = false;
    wasm_module_t module = NULL;
    wasm_module_inst_t instance = NULL;
    wasm_exec_env_t environment = NULL;
    if (econtainer_wasm_check(bytes, (size_t)length) != ECONTAINER_WASM_OK ||
        (module = wasm_runtime_load(bytes, (uint32_t)length, error, sizeof(error))) == NULL ||
        (instance = wasm_runtime_instantiate(module, 4096, 0, error, sizeof(error))) == NULL ||
        (environment = wasm_runtime_create_exec_env(instance, 4096)) == NULL) {
        fprintf(stderr, "counter: load/instantiate failed: %s\n", error);
        goto done;
    }
    uint32_t args[2] = {UINT32_MAX, 0};
    if (!wasm_runtime_validate_app_addr(instance, 1024, 3) ||
        !call_counter(environment, instance, "econtainer_init", 0, args, 0)) {
        goto done;
    }
    args[0] = 1024;
    args[1] = 3;
    if (!call_counter(environment, instance, "econtainer_on_event", 2, args, 3)) {
        goto done;
    }
    args[0] = 1024;
    args[1] = 2;
    if (!call_counter(environment, instance, "econtainer_on_event", 2, args, 5)) {
        goto done;
    }
    args[0] = 0;
    args[1] = 1;
    if (!call_counter(environment, instance, "econtainer_on_event", 2, args, UINT32_MAX)) {
        goto done;
    }
    args[0] = UINT32_MAX;
    if (!call_counter(environment, instance, "econtainer_stop", 0, args, 0)) {
        goto done;
    }
    passed = true;
    fprintf(stderr, "counter: init/event(3)/event(2)/invalid/stop passed\n");
done:
    if (environment != NULL) wasm_runtime_destroy_exec_env(environment);
    if (instance != NULL) wasm_runtime_deinstantiate(instance);
    if (module != NULL) wasm_runtime_unload(module);
    free(bytes);
    return passed;
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "usage: %s [counter.wasm]\n", argv[0]);
        return 2;
    }
    if (!wasm_runtime_init()) {
        fprintf(stderr, "WAMR initialization failed\n");
        return 1;
    }
    const bool normal = exercise(return_zero, sizeof(return_zero), "run", true);
    const bool bounded = exercise(endless_loop, sizeof(endless_loop), "looping", false);
    const bool counter = argc == 2 ? exercise_counter(argv[1]) : true;
    wasm_runtime_destroy();
    return normal && bounded && counter ? 0 : 1;
}
