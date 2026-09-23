#include <stdint.h>

/* Host test modules only. Deliberate loops exercise each entrypoint's budget. */
#if defined(ECONTAINER_INIT_LOOP) || defined(ECONTAINER_EVENT_LOOP) || \
    defined(ECONTAINER_STOP_LOOP)
static volatile uint32_t spin_count;
#endif
static uint32_t event_count;

int32_t econtainer_init(void)
{
#if defined(ECONTAINER_INIT_LOOP)
    for (;;) {
        ++spin_count;
    }
#endif
    event_count = 0;
    return 0;
}

#if defined(ECONTAINER_WRONG_SIGNATURE)
int32_t econtainer_on_event(uint32_t size_bytes)
#else
int32_t econtainer_on_event(const uint8_t *bytes, uint32_t size_bytes)
#endif
{
#if defined(ECONTAINER_EVENT_LOOP)
    (void)bytes;
    (void)size_bytes;
    for (;;) {
        ++spin_count;
    }
#endif
#if defined(ECONTAINER_WRONG_SIGNATURE)
    return (int32_t)size_bytes;
#else
    if (size_bytes == 0) {
        return -1;
    }
    if (size_bytes > 0 && bytes == 0) {
        return -1;
    }
    for (uint32_t index = 0; index < size_bytes; ++index) {
        event_count += bytes[index];
    }
    return (int32_t)event_count;
#endif
}

int32_t econtainer_stop(void)
{
#if defined(ECONTAINER_STOP_LOOP)
    for (;;) {
        ++spin_count;
    }
#endif
#if defined(ECONTAINER_STOP_FAIL)
    return -1;
#else
    event_count = 0;
    return 0;
#endif
}
