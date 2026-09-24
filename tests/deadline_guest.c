#include "econtainer_guest.h"

#include <stdint.h>

int32_t econtainer_init(void)
{
    return 0;
}

int32_t econtainer_on_event(const uint8_t *bytes, uint32_t size_bytes)
{
    if (bytes == 0 || size_bytes != 1) return -1;
    if (bytes[0] == 'P') {
        static const uint8_t prior[] = {'p', 'r', 'i', 'o', 'r'};
        return econtainer_log(prior, sizeof(prior));
    }
    if (bytes[0] == 'T')
        return econtainer_timer_start(1000U, 0U) != 0 ? 0 : -5;
    static const uint8_t early[] = {'e', 'a', 'r', 'l', 'y'};
    const int32_t log_status = econtainer_log(early, sizeof(early));
    if (log_status != 0 && log_status != -2) return -2;
    if (bytes[0] == 'D') {
        const uint64_t began_ms = econtainer_monotonic_ms();
        while (econtainer_monotonic_ms() - began_ms < 100U) {
        }
        return econtainer_timer_start(1000U, 0U) != 0 ? 0 : -3;
    }
    if (bytes[0] == 'R') {
        volatile uint32_t total = 0;
        for (uint32_t index = 0; index < 20000000U; ++index) total += index;
        return total != 0U ? 0 : -4;
    }
    return -1;
}

int32_t econtainer_stop(void)
{
    return 0;
}
