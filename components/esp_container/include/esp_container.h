#ifndef ESP_CONTAINER_H
#define ESP_CONTAINER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Raw module shape gate. Package signature and device authorization are separate. */
typedef enum {
    ECONTAINER_WASM_OK = 0,
    ECONTAINER_WASM_INVALID,
    ECONTAINER_WASM_UNSUPPORTED,
} econtainer_wasm_result_t;

enum {
    ECONTAINER_CAP_MONOTONIC_TIME = 1U << 0,
    ECONTAINER_CAP_LOG = 1U << 1,
    ECONTAINER_CAP_TIMER = 1U << 2,
    ECONTAINER_CAP_ALL = ECONTAINER_CAP_MONOTONIC_TIME | ECONTAINER_CAP_LOG |
                         ECONTAINER_CAP_TIMER,
};

econtainer_wasm_result_t econtainer_wasm_check(const uint8_t *wasm, size_t size_bytes);

#ifdef __cplusplus
}
#endif

#endif
