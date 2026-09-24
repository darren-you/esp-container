#include "econtainer_guest.h"

/* Explicit Wasm loads/stores cover address zero without C null-pointer UB. */
static uint32_t read_byte(uint32_t offset)
{
    uint32_t result;
    __asm__ volatile("local.get %1\n\ti32.load8_u 0\n\tlocal.set %0"
                       : "=r"(result) : "r"(offset) : "memory");
    return result;
}

static void write_byte(uint32_t offset, uint32_t value)
{
    __asm__ volatile("local.get %1\n\tlocal.get %0\n\ti32.store8 0"
                       : : "r"(value), "r"(offset) : "memory");
}

int32_t econtainer_init(void)
{
    /* The entire standard page, including its last byte, remains writable. */
    for (uint32_t offset = 0; offset < 65536; ++offset)
        write_byte(offset, offset ^ 0xa5U);
    for (uint32_t offset = 0; offset < 65536; ++offset)
        if (read_byte(offset) != ((offset ^ 0xa5U) & 0xffU)) return -1;
    return __builtin_wasm_memory_size(0) == 1 ? 0 : -1;
}

int32_t econtainer_on_event(const uint8_t *bytes, uint32_t size_bytes)
{
    if (bytes != econtainer_event_buffer || size_bytes == 0) return -2;
    switch (bytes[0]) {
        case 'P': return (int32_t)__builtin_wasm_memory_size(0);
        case 'G': return (int32_t)__builtin_wasm_memory_grow(0, 1);
        case 'E': return size_bytes == ECONTAINER_EVENT_BUFFER_BYTES
                            ? bytes[size_bytes - 1] : -3;
        case 'Z':
            for (uint32_t index = size_bytes; index < ECONTAINER_EVENT_BUFFER_BYTES; ++index)
                if (bytes[index] != 0) return -4;
            return 0;
        case 'L':
            write_byte(65535, 77);
            return (int32_t)read_byte(65535);
        case 'O': return (int32_t)read_byte(65536);
        case 'S': write_byte(65536, 1); return -5;
        case 'U': {
            uint32_t result;
            __asm__ volatile("local.get %1\n\ti32.load 0\n\tlocal.set %0"
                               : "=r"(result) : "r"(65533) : "memory");
            return (int32_t)result;
        }
        default: return -6;
    }
}

int32_t econtainer_stop(void) { return 0; }
