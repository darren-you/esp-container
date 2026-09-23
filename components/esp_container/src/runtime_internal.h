#ifndef ECONTAINER_RUNTIME_INTERNAL_H
#define ECONTAINER_RUNTIME_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_container.h"

/* Component-private execution seam. Package verification and installation are
 * separate prerequisites; Base must never receive this raw-module interface. */
typedef struct econtainer_runtime econtainer_runtime_t;

typedef struct {
    uint32_t max_wasm_bytes;
    uint32_t max_memory_pages;
    uint32_t stack_size_bytes;
    uint32_t heap_size_bytes;
    uint32_t max_event_bytes;
    uint32_t allowed_capabilities;
    uint32_t max_log_bytes;
    uint32_t max_timers;
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
    ECONTAINER_RUNTIME_NO_LOG,
    ECONTAINER_RUNTIME_NO_TIMER,
    ECONTAINER_RUNTIME_NOT_AUTHORIZED,
} econtainer_runtime_result_t;

typedef enum {
    ECONTAINER_RUNTIME_LOADED,
    ECONTAINER_RUNTIME_RUNNING,
    ECONTAINER_RUNTIME_STOPPED,
    ECONTAINER_RUNTIME_FAILED,
} econtainer_runtime_state_t;

typedef struct {
    uint64_t handle;
    uint32_t skipped_periods;
} econtainer_timer_event_t;

/* Runtime-private handle allocator. A canceled handle is never reissued during
 * this process lifetime. UINT64_MAX is the final valid value; zero is terminal. */
static inline uint64_t econtainer_runtime_issue_timer_handle(uint64_t *next_handle)
{
    if (next_handle == NULL || *next_handle == 0) return 0;
    const uint64_t issued = *next_handle;
    *next_handle = issued + 1U;
    return issued;
}

/* Inspect the raw Wasm memory section. WAMR may normalize page counts after
 * loading, so its export type cannot enforce this 64 KiB-page admission cap. */
bool econtainer_wasm_memory_within_limit(const uint8_t *wasm, size_t wasm_size_bytes,
                                         uint32_t max_memory_pages);
/* econtainer_wasm_check has already checked the exact import names, kinds and
 * signatures before this private query is used. */
bool econtainer_wasm_imported_capabilities(const uint8_t *wasm, size_t wasm_size_bytes,
                                           uint32_t *required_capabilities);

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
/* The single pending log is copied out only between guest entry calls. A short
 * destination leaves it pending. No external callback runs inside a guest
 * entrypoint. */
econtainer_runtime_result_t econtainer_runtime_take_log(econtainer_runtime_t *runtime,
                                                       uint8_t *output,
                                                       size_t output_capacity,
                                                       size_t *log_size_bytes);
/* The owner calls this outside every guest entry. No native timer callback calls
 * into Wasm. It delivers at most one due event with the ordinary event budget;
 * periodic overruns are coalesced into skipped_periods. */
econtainer_runtime_result_t econtainer_runtime_poll_timer(econtainer_runtime_t *runtime,
                                                         econtainer_timer_event_t *event,
                                                         int32_t *guest_result);
/* Allows the owner to sleep until the nearest deadline; no background worker. */
econtainer_runtime_result_t econtainer_runtime_next_timer_deadline(
    econtainer_runtime_t *runtime, uint64_t *deadline_ms);
/* close releases even a failed/trapped instance. NULL and *NULL are harmless.
 * Calls must remain on the single owner thread. */
econtainer_runtime_result_t econtainer_runtime_close(econtainer_runtime_t **runtime);

#endif
