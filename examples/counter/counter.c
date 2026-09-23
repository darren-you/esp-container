#include "econtainer_guest.h"

#include <stdint.h>

static uint32_t event_count;

int32_t econtainer_init(void)
{
    event_count = 0;
    return 0;
}

int32_t econtainer_on_event(const uint8_t *bytes, uint32_t size_bytes)
{
    if (size_bytes != 0 && bytes == 0) {
        return -1;
    }
    if (size_bytes > INT32_MAX - event_count) {
        return -2;
    }
    event_count += size_bytes;
    return (int32_t)event_count;
}

int32_t econtainer_stop(void)
{
    event_count = 0;
    return 0;
}
