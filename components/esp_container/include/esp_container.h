#ifndef ESP_CONTAINER_H
#define ESP_CONTAINER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initial no-import module gate. A valid package/signature is a separate prerequisite. */
typedef enum {
    ECONTAINER_WASM_OK = 0,
    ECONTAINER_WASM_INVALID,
    ECONTAINER_WASM_UNSUPPORTED,
} econtainer_wasm_result_t;

econtainer_wasm_result_t econtainer_wasm_check(const uint8_t *wasm, size_t size_bytes);

#ifdef __cplusplus
}
#endif

#endif
