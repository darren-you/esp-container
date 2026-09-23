#include "econtainer_guest.h"

#include <stdint.h>

static uint64_t started_ms;

int32_t econtainer_init(void)
{
    static const uint8_t message[] = {'i', 'n', 'i', 't'};
    started_ms = econtainer_monotonic_ms();
    return econtainer_log(message, sizeof(message));
}

int32_t econtainer_on_event(const uint8_t *bytes, uint32_t size_bytes)
{
    if (size_bytes == 0 || bytes == 0) {
        return -10;
    }
    switch (bytes[0]) {
    case 1:
        return econtainer_log(bytes + 1, size_bytes - 1);
    case 2: {
        static const uint8_t first[] = {'f', 'i', 'r', 's', 't'};
        static const uint8_t second[] = {'s', 'e', 'c', 'o', 'n', 'd'};
        if (econtainer_log(first, sizeof(first)) != 0) {
            return -11;
        }
        return econtainer_log(second, sizeof(second));
    }
    case 3:
        return econtainer_log((const uint8_t *)(uintptr_t)0xfffffff0U, 1);
    case 4:
        return econtainer_log(bytes, 17);
    case 5:
        return econtainer_monotonic_ms() >= started_ms ? 0 : -12;
    default:
        return -13;
    }
}

int32_t econtainer_stop(void)
{
    return econtainer_monotonic_ms() >= started_ms ? 0 : -14;
}
