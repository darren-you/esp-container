#ifndef ECONTAINER_RUNTIME_INTERNAL_H
#define ECONTAINER_RUNTIME_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Component-private execution seam. Package verification and installation are
 * separate prerequisites; Base must never receive this raw-module interface. */
typedef struct econtainer_runtime econtainer_runtime_t;

typedef struct {
    uint32_t max_wasm_bytes;
    uint32_t max_memory_pages;
    uint32_t stack_size_bytes;
    uint32_t heap_size_bytes;
    uint32_t max_event_bytes;
    int32_t init_instruction_budget;
    int32_t event_instruction_budget;
    int32_t stop_instruction_budget;
} econtainer_runtime_limits_t;

typedef enum {
    ECONTAINER_RUNTIME_OK = 0,
    ECONTAINER_RUNTIME_INVALID_INPUT,
    ECONTAINER_RUNTIME_BUSY,
    ECONTAINER_RUNTIME_INVALID_STATE,
    ECONTAINER_RUNTIME_BAD_WASM,
    ECONTAINER_RUNTIME_BAD_ABI,
    ECONTAINER_RUNTIME_NO_MEMORY,
    ECONTAINER_RUNTIME_ENGINE_FAILURE,
    ECONTAINER_RUNTIME_INSTRUCTION_LIMIT,
    ECONTAINER_RUNTIME_GUEST_FAILURE,
} econtainer_runtime_result_t;

typedef enum {
    ECONTAINER_RUNTIME_LOADED,
    ECONTAINER_RUNTIME_RUNNING,
    ECONTAINER_RUNTIME_STOPPED,
    ECONTAINER_RUNTIME_FAILED,
} econtainer_runtime_state_t;

/* Inspect the raw Wasm memory section. WAMR may normalize page counts after
 * loading, so its export type cannot enforce this 64 KiB-page admission cap. */
bool econtainer_wasm_memory_within_limit(const uint8_t *wasm, size_t wasm_size_bytes,
                                         uint32_t max_memory_pages);

/* These calls have one serialized owner. On ESP-IDF the owner must be a
 * pthread_create thread: WAMR's espidf os_self_thread uses pthread_self, which
 * asserts on a plain xTaskCreate FreeRTOS task. out must point to NULL on
 * entry. open makes its own writable Wasm copy and owns WAMR until close;
 * it never executes a guest entrypoint. */
econtainer_runtime_result_t econtainer_runtime_open(const uint8_t *wasm,
                                                   size_t wasm_size_bytes,
                                                   const econtainer_runtime_limits_t *limits,
                                                   econtainer_runtime_t **out);
econtainer_runtime_result_t econtainer_runtime_init(econtainer_runtime_t *runtime);
econtainer_runtime_result_t econtainer_runtime_on_event(econtainer_runtime_t *runtime,
                                                       const uint8_t *event,
                                                       size_t event_size_bytes,
                                                       int32_t *guest_result);
/* guest_result is written only on ECONTAINER_RUNTIME_OK and is not a host
 * status code. stop is idempotent after success; a trapped instance is failed
 * and can only be closed. */
econtainer_runtime_result_t econtainer_runtime_stop(econtainer_runtime_t *runtime);
econtainer_runtime_state_t econtainer_runtime_state(const econtainer_runtime_t *runtime);
/* close releases even a failed/trapped instance. NULL and *NULL are harmless.
 * There are no host capabilities, async callbacks or in-flight references yet. */
void econtainer_runtime_close(econtainer_runtime_t **runtime);

#endif
