#include "econtainer_guest.h"

#include <stdint.h>

static uint64_t one_shot;
static uint64_t periodic;

int32_t econtainer_init(void)
{
    return 0;
}

static uint64_t read_handle(const uint8_t *bytes)
{
    uint64_t handle = 0;
    for (uint32_t index = 0; index < 8; ++index)
        handle |= (uint64_t)bytes[index] << (index * 8);
    return handle;
}

int32_t econtainer_on_event(const uint8_t *bytes, uint32_t size_bytes)
{
    uint64_t handle = 0;
    uint32_t skipped = 0;
    if (econtainer_timer_event_decode(bytes, size_bytes, &handle, &skipped)) {
        if (handle == one_shot) return skipped == 0 ? 10 : -10;
        if (handle == periodic) return 20 + (int32_t)skipped;
        return -11;
    }
    if (size_bytes == 0 || bytes == 0) return -12;
    if (bytes[0] == 'A') {
        one_shot = econtainer_timer_start(20, 0);
        periodic = econtainer_timer_start(20, 100);
        return one_shot != 0 && periodic != 0 &&
               econtainer_timer_start(20, 0) == 0 &&
               econtainer_timer_start(0, 0) == 0 &&
               econtainer_timer_start(86400001U, 0) == 0 ? 0 : -13;
    }
    if (bytes[0] == 'B') {
        const uint64_t temporary = econtainer_timer_start(1, 0);
        return temporary != 0 && econtainer_timer_cancel(temporary) == 0 &&
               econtainer_timer_cancel(temporary) == -1 ? 0 : -14;
    }
    if (bytes[0] == 'C' && size_bytes == 9)
        return econtainer_timer_cancel(read_handle(bytes + 1));
    return -15;
}

int32_t econtainer_stop(void)
{
    return econtainer_timer_start(1, 0) == 0 ? 0 : -16;
}
