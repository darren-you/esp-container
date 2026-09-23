#include "runtime_internal.h"

#include "esp_container.h"
#include "wasm_export.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct econtainer_runtime {
    econtainer_runtime_limits_t limits;
    econtainer_runtime_state_t state;
    uint8_t *wasm_copy;
    wasm_module_t module;
    wasm_module_inst_t instance;
    wasm_exec_env_t environment;
    wasm_function_inst_t init_function;
    wasm_function_inst_t event_function;
    wasm_function_inst_t stop_function;
    bool wamr_initialized;
};

/* WAMR init/destroy owns process-global state. The caller serializes methods
 * on the accepted instance; the atomic claim also rejects a second opener. */
static atomic_bool runtime_claimed = false;

static bool limits_valid(const econtainer_runtime_limits_t *limits)
{
    return limits != NULL && limits->max_wasm_bytes > 0 &&
           limits->max_memory_pages > 0 && limits->stack_size_bytes > 0 &&
           limits->heap_size_bytes > 0 && limits->max_event_bytes > 0 &&
           limits->max_event_bytes <= limits->heap_size_bytes &&
           limits->init_instruction_budget > 0 &&
           limits->event_instruction_budget > 0 &&
           limits->stop_instruction_budget > 0;
}

static bool function_type_matches(wasm_func_type_t type, uint32_t param_count)
{
    if (type == NULL || wasm_func_type_get_param_count(type) != param_count ||
        wasm_func_type_get_result_count(type) != 1 ||
        wasm_func_type_get_result_valkind(type, 0) != WASM_I32) {
        return false;
    }
    for (uint32_t index = 0; index < param_count; ++index) {
        if (wasm_func_type_get_param_valkind(type, index) != WASM_I32) {
            return false;
        }
    }
    return true;
}

static bool module_abi_matches(wasm_module_t module)
{
    const int32_t count = wasm_runtime_get_export_count(module);
    if (count != 4) {
        return false;
    }
    bool init_found = false;
    bool event_found = false;
    bool stop_found = false;
    bool memory_found = false;
    for (int32_t index = 0; index < count; ++index) {
        wasm_export_t export_type;
        wasm_runtime_get_export_type(module, index, &export_type);
        if (export_type.name == NULL) {
            return false;
        }
        if (strcmp(export_type.name, "econtainer_init") == 0) {
            if (init_found || export_type.kind != WASM_IMPORT_EXPORT_KIND_FUNC ||
                !function_type_matches(export_type.u.func_type, 0)) {
                return false;
            }
            init_found = true;
        }
        else if (strcmp(export_type.name, "econtainer_on_event") == 0) {
            if (event_found || export_type.kind != WASM_IMPORT_EXPORT_KIND_FUNC ||
                !function_type_matches(export_type.u.func_type, 2)) {
                return false;
            }
            event_found = true;
        }
        else if (strcmp(export_type.name, "econtainer_stop") == 0) {
            if (stop_found || export_type.kind != WASM_IMPORT_EXPORT_KIND_FUNC ||
                !function_type_matches(export_type.u.func_type, 0)) {
                return false;
            }
            stop_found = true;
        }
        else if (strcmp(export_type.name, "memory") == 0) {
            if (memory_found || export_type.kind != WASM_IMPORT_EXPORT_KIND_MEMORY ||
                export_type.u.memory_type == NULL ||
                wasm_memory_type_get_shared(export_type.u.memory_type)) {
                return false;
            }
            memory_found = true;
        }
        else {
            return false;
        }
    }
    return init_found && event_found && stop_found && memory_found;
}

static void release_runtime(econtainer_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    if (runtime->environment != NULL) {
        wasm_runtime_destroy_exec_env(runtime->environment);
    }
    if (runtime->instance != NULL) {
        wasm_runtime_deinstantiate(runtime->instance);
    }
    if (runtime->module != NULL) {
        wasm_runtime_unload(runtime->module);
    }
    free(runtime->wasm_copy);
    if (runtime->wamr_initialized) {
        wasm_runtime_destroy();
    }
    free(runtime);
    atomic_store(&runtime_claimed, false);
}

econtainer_runtime_result_t econtainer_runtime_open(const uint8_t *wasm,
                                                   size_t wasm_size_bytes,
                                                   const econtainer_runtime_limits_t *limits,
                                                   econtainer_runtime_t **out)
{
    if (out == NULL || *out != NULL || wasm == NULL ||
        wasm_size_bytes == 0 || wasm_size_bytes > UINT32_MAX ||
        !limits_valid(limits) || wasm_size_bytes > limits->max_wasm_bytes) {
        return ECONTAINER_RUNTIME_INVALID_INPUT;
    }
    if (econtainer_wasm_check(wasm, wasm_size_bytes) != ECONTAINER_WASM_OK) {
        return ECONTAINER_RUNTIME_BAD_WASM;
    }
    if (!econtainer_wasm_memory_within_limit(wasm, wasm_size_bytes,
                                             limits->max_memory_pages)) {
        return ECONTAINER_RUNTIME_BAD_ABI;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong(&runtime_claimed, &expected, true)) {
        return ECONTAINER_RUNTIME_BUSY;
    }
    econtainer_runtime_t *runtime = calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        atomic_store(&runtime_claimed, false);
        return ECONTAINER_RUNTIME_NO_MEMORY;
    }
    runtime->limits = *limits;
    runtime->state = ECONTAINER_RUNTIME_LOADED;
    runtime->wasm_copy = malloc(wasm_size_bytes);
    if (runtime->wasm_copy == NULL) {
        release_runtime(runtime);
        return ECONTAINER_RUNTIME_NO_MEMORY;
    }
    memcpy(runtime->wasm_copy, wasm, wasm_size_bytes);
    if (!wasm_runtime_init()) {
        release_runtime(runtime);
        return ECONTAINER_RUNTIME_ENGINE_FAILURE;
    }
    runtime->wamr_initialized = true;
    char error[128] = {0};
    runtime->module = wasm_runtime_load(runtime->wasm_copy, (uint32_t)wasm_size_bytes,
                                        error, sizeof(error));
    if (runtime->module == NULL) {
        release_runtime(runtime);
        return ECONTAINER_RUNTIME_BAD_WASM;
    }
    if (!module_abi_matches(runtime->module)) {
        release_runtime(runtime);
        return ECONTAINER_RUNTIME_BAD_ABI;
    }
    InstantiationArgs args = {
        .default_stack_size = limits->stack_size_bytes,
        .host_managed_heap_size = limits->heap_size_bytes,
        .max_memory_pages = 0, /* raw Wasm memory limits were checked above */
    };
    runtime->instance = wasm_runtime_instantiate_ex(runtime->module, &args,
                                                    error, sizeof(error));
    if (runtime->instance == NULL) {
        release_runtime(runtime);
        return ECONTAINER_RUNTIME_ENGINE_FAILURE;
    }
    runtime->environment = wasm_runtime_create_exec_env(runtime->instance,
                                                         limits->stack_size_bytes);
    runtime->init_function = wasm_runtime_lookup_function(runtime->instance,
                                                           "econtainer_init");
    runtime->event_function = wasm_runtime_lookup_function(runtime->instance,
                                                            "econtainer_on_event");
    runtime->stop_function = wasm_runtime_lookup_function(runtime->instance,
                                                           "econtainer_stop");
    if (runtime->environment == NULL || runtime->init_function == NULL ||
        runtime->event_function == NULL || runtime->stop_function == NULL) {
        release_runtime(runtime);
        return ECONTAINER_RUNTIME_ENGINE_FAILURE;
    }
    *out = runtime;
    return ECONTAINER_RUNTIME_OK;
}

static econtainer_runtime_result_t invoke(econtainer_runtime_t *runtime,
                                          wasm_function_inst_t function,
                                          int32_t budget, uint32_t argc,
                                          uint32_t *arguments, int32_t *result)
{
    wasm_runtime_clear_exception(runtime->instance);
    wasm_runtime_set_instruction_count_limit(runtime->environment, budget);
    if (!wasm_runtime_call_wasm(runtime->environment, function, argc, arguments)) {
        const char *exception = wasm_runtime_get_exception(runtime->instance);
        runtime->state = ECONTAINER_RUNTIME_FAILED;
        return exception != NULL &&
                       strcmp(exception, "Exception: instruction limit exceeded") == 0
                   ? ECONTAINER_RUNTIME_INSTRUCTION_LIMIT
                   : ECONTAINER_RUNTIME_ENGINE_FAILURE;
    }
    if (wasm_runtime_get_exception(runtime->instance) != NULL) {
        runtime->state = ECONTAINER_RUNTIME_FAILED;
        return ECONTAINER_RUNTIME_ENGINE_FAILURE;
    }
    *result = (int32_t)arguments[0];
    return ECONTAINER_RUNTIME_OK;
}

econtainer_runtime_result_t econtainer_runtime_init(econtainer_runtime_t *runtime)
{
    if (runtime == NULL || runtime->state != ECONTAINER_RUNTIME_LOADED) {
        return ECONTAINER_RUNTIME_INVALID_STATE;
    }
    uint32_t arguments[2] = {0};
    int32_t result = -1;
    econtainer_runtime_result_t status = invoke(runtime, runtime->init_function,
                                                runtime->limits.init_instruction_budget,
                                                0, arguments, &result);
    if (status != ECONTAINER_RUNTIME_OK) {
        return status;
    }
    if (result != 0) {
        runtime->state = ECONTAINER_RUNTIME_FAILED;
        return ECONTAINER_RUNTIME_GUEST_FAILURE;
    }
    runtime->state = ECONTAINER_RUNTIME_RUNNING;
    return ECONTAINER_RUNTIME_OK;
}

econtainer_runtime_result_t econtainer_runtime_on_event(econtainer_runtime_t *runtime,
                                                       const uint8_t *event,
                                                       size_t event_size_bytes,
                                                       int32_t *guest_result)
{
    if (runtime == NULL || runtime->state != ECONTAINER_RUNTIME_RUNNING) {
        return ECONTAINER_RUNTIME_INVALID_STATE;
    }
    if (guest_result == NULL || event_size_bytes > runtime->limits.max_event_bytes ||
        (event_size_bytes > 0 && event == NULL)) {
        return ECONTAINER_RUNTIME_INVALID_INPUT;
    }
    uint64_t offset = 0;
    if (event_size_bytes > 0) {
        void *guest_address = NULL;
        offset = wasm_runtime_module_malloc(runtime->instance, event_size_bytes,
                                            &guest_address);
        if (offset == 0 || offset > UINT32_MAX || guest_address == NULL) {
            if (offset != 0) {
                wasm_runtime_module_free(runtime->instance, offset);
            }
            return ECONTAINER_RUNTIME_NO_MEMORY;
        }
        memcpy(guest_address, event, event_size_bytes);
    }
    uint32_t arguments[2] = {(uint32_t)offset, (uint32_t)event_size_bytes};
    int32_t result = 0;
    const econtainer_runtime_result_t status = invoke(
        runtime, runtime->event_function, runtime->limits.event_instruction_budget,
        2, arguments, &result);
    if (offset != 0) {
        wasm_runtime_module_free(runtime->instance, offset);
    }
    if (status == ECONTAINER_RUNTIME_OK) {
        *guest_result = result;
    }
    return status;
}

econtainer_runtime_result_t econtainer_runtime_stop(econtainer_runtime_t *runtime)
{
    if (runtime == NULL) {
        return ECONTAINER_RUNTIME_INVALID_STATE;
    }
    if (runtime->state == ECONTAINER_RUNTIME_STOPPED) {
        return ECONTAINER_RUNTIME_OK;
    }
    if (runtime->state != ECONTAINER_RUNTIME_RUNNING) {
        return ECONTAINER_RUNTIME_INVALID_STATE;
    }
    uint32_t arguments[2] = {0};
    int32_t result = -1;
    econtainer_runtime_result_t status = invoke(runtime, runtime->stop_function,
                                                runtime->limits.stop_instruction_budget,
                                                0, arguments, &result);
    if (status != ECONTAINER_RUNTIME_OK) {
        return status;
    }
    if (result != 0) {
        runtime->state = ECONTAINER_RUNTIME_FAILED;
        return ECONTAINER_RUNTIME_GUEST_FAILURE;
    }
    runtime->state = ECONTAINER_RUNTIME_STOPPED;
    return ECONTAINER_RUNTIME_OK;
}

econtainer_runtime_state_t econtainer_runtime_state(const econtainer_runtime_t *runtime)
{
    return runtime == NULL ? ECONTAINER_RUNTIME_FAILED : runtime->state;
}

void econtainer_runtime_close(econtainer_runtime_t **runtime)
{
    if (runtime == NULL || *runtime == NULL) {
        return;
    }
    release_runtime(*runtime);
    *runtime = NULL;
}
