#include "esp_container.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static const uint8_t empty_module[] = {0, 'a', 's', 'm', 1, 0, 0, 0};

int main(void)
{
    assert(econtainer_wasm_check(empty_module, sizeof(empty_module)) == ECONTAINER_WASM_OK);
    assert(econtainer_wasm_check(NULL, 0) == ECONTAINER_WASM_INVALID);
    const uint8_t start[] = {0, 'a', 's', 'm', 1, 0, 0, 0, 8, 1, 0};
    assert(econtainer_wasm_check(start, sizeof(start)) == ECONTAINER_WASM_UNSUPPORTED);
    const uint8_t import[] = {0, 'a', 's', 'm', 1, 0, 0, 0, 2, 1, 1};
    assert(econtainer_wasm_check(import, sizeof(import)) == ECONTAINER_WASM_UNSUPPORTED);
    const char *name = "__post_instantiate";
    uint8_t exported[8 + 2 + 1 + 1 + 18 + 2] = {0, 'a', 's', 'm', 1, 0, 0, 0};
    exported[8] = 7;
    exported[9] = 22;
    exported[10] = 1;
    exported[11] = 18;
    memcpy(exported + 12, name, 18);
    exported[30] = 0;
    exported[31] = 0;
    assert(econtainer_wasm_check(exported, sizeof(exported)) == ECONTAINER_WASM_UNSUPPORTED);
    const uint8_t truncated[] = {0, 'a', 's', 'm', 1, 0, 0, 0, 1, 5, 0};
    assert(econtainer_wasm_check(truncated, sizeof(truncated)) == ECONTAINER_WASM_INVALID);
    return 0;
}
