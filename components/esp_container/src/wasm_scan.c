#include "esp_container.h"
#include "runtime_internal.h"

#include <stdbool.h>
#include <string.h>

static bool read_u32(const uint8_t *data, size_t end, size_t *cursor, uint32_t *value)
{
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
        if (*cursor >= end) {
            return false;
        }
        const uint8_t byte = data[(*cursor)++];
        if (shift == 28 && (byte & 0xf0U) != 0) {
            return false;
        }
        result |= (uint32_t)(byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            *value = result;
            return true;
        }
    }
    return false;
}

static bool dangerous_export(const uint8_t *name, size_t length)
{
    static const char *const names[] = {
        "__post_instantiate", "__wasm_call_ctors", "_initialize",
    };
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (strlen(names[index]) == length && memcmp(name, names[index], length) == 0) {
            return true;
        }
    }
    return false;
}

static bool read_name(const uint8_t *wasm, size_t end, size_t *cursor,
                      const uint8_t **name, uint32_t *length)
{
    if (!read_u32(wasm, end, cursor, length) || (size_t)*length > end - *cursor) {
        return false;
    }
    *name = wasm + *cursor;
    *cursor += *length;
    return true;
}

static bool name_is(const uint8_t *name, uint32_t length, const char *expected)
{
    return strlen(expected) == length && memcmp(name, expected, length) == 0;
}

static bool import_type_matches(const uint8_t *wasm, size_t type_begin,
                                size_t type_end, uint32_t type_index,
                                uint32_t capability)
{
    if (type_begin == 0) {
        return false;
    }
    size_t cursor = type_begin;
    uint32_t count = 0;
    if (!read_u32(wasm, type_end, &cursor, &count) || type_index >= count) {
        return false;
    }
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t params = 0;
        uint32_t results = 0;
        if (cursor >= type_end || wasm[cursor++] != 0x60 ||
            !read_u32(wasm, type_end, &cursor, &params) ||
            (size_t)params > type_end - cursor) {
            return false;
        }
        const uint8_t *param_types = wasm + cursor;
        cursor += params;
        if (!read_u32(wasm, type_end, &cursor, &results) ||
            (size_t)results > type_end - cursor) {
            return false;
        }
        const uint8_t *result_types = wasm + cursor;
        cursor += results;
        if (index == type_index) {
            return capability == ECONTAINER_CAP_MONOTONIC_TIME
                       ? params == 0 && results == 1 && result_types[0] == 0x7e
                       : params == 2 && param_types[0] == 0x7f &&
                             param_types[1] == 0x7f && results == 1 &&
                             result_types[0] == 0x7f;
        }
    }
    return false;
}

static econtainer_wasm_result_t scan_module(const uint8_t *wasm, size_t size_bytes,
                                            uint32_t *required_capabilities)
{
    static const uint8_t header[8] = {0, 'a', 's', 'm', 1, 0, 0, 0};
    if (wasm == NULL || size_bytes < sizeof(header) || memcmp(wasm, header, sizeof(header)) != 0) {
        return ECONTAINER_WASM_INVALID;
    }
    size_t cursor = sizeof(header);
    uint8_t last_section = 0;
    size_t type_begin = 0;
    size_t type_end = 0;
    size_t import_begin = 0;
    size_t import_end = 0;
    uint32_t capabilities = 0;
    while (cursor < size_bytes) {
        const uint8_t section = wasm[cursor++];
        uint32_t length = 0;
        if (!read_u32(wasm, size_bytes, &cursor, &length) || (size_t)length > size_bytes - cursor) {
            return ECONTAINER_WASM_INVALID;
        }
        const size_t end = cursor + (size_t)length;
        if (section > 12) {
            return ECONTAINER_WASM_UNSUPPORTED;
        }
        if (section != 0) {
            if (section <= last_section) {
                return ECONTAINER_WASM_INVALID;
            }
            last_section = section;
        }
        if (section == 8) {
            return ECONTAINER_WASM_UNSUPPORTED;
        }
        if (section == 1) {
            type_begin = cursor;
            type_end = end;
        }
        if (section == 2) {
            import_begin = cursor;
            import_end = end;
        }
        if (section == 7) {
            uint32_t count = 0;
            if (!read_u32(wasm, end, &cursor, &count)) {
                return ECONTAINER_WASM_INVALID;
            }
            for (uint32_t index = 0; index < count; ++index) {
                uint32_t name_length = 0;
                uint32_t target = 0;
                if (!read_u32(wasm, end, &cursor, &name_length) ||
                    (size_t)name_length > end - cursor) {
                    return ECONTAINER_WASM_INVALID;
                }
                if (dangerous_export(wasm + cursor, name_length)) {
                    return ECONTAINER_WASM_UNSUPPORTED;
                }
                cursor += name_length;
                if (cursor >= end || wasm[cursor++] > 3 ||
                    !read_u32(wasm, end, &cursor, &target)) {
                    return ECONTAINER_WASM_INVALID;
                }
                (void)target;
            }
            if (cursor != end) {
                return ECONTAINER_WASM_INVALID;
            }
        }
        cursor = end;
    }
    if (import_begin != 0) {
        cursor = import_begin;
        uint32_t count = 0;
        if (!read_u32(wasm, import_end, &cursor, &count)) {
            return ECONTAINER_WASM_INVALID;
        }
        if (count > 2) {
            return ECONTAINER_WASM_UNSUPPORTED;
        }
        for (uint32_t index = 0; index < count; ++index) {
            const uint8_t *module = NULL;
            const uint8_t *field = NULL;
            uint32_t module_size = 0;
            uint32_t field_size = 0;
            uint32_t type_index = 0;
            if (!read_name(wasm, import_end, &cursor, &module, &module_size) ||
                !read_name(wasm, import_end, &cursor, &field, &field_size) ||
                cursor >= import_end) {
                return ECONTAINER_WASM_INVALID;
            }
            const uint8_t kind = wasm[cursor++];
            if (kind != 0) {
                return ECONTAINER_WASM_UNSUPPORTED;
            }
            if (!read_u32(wasm, import_end, &cursor, &type_index)) {
                return ECONTAINER_WASM_INVALID;
            }
            if (!name_is(module, module_size, "econtainer")) {
                return ECONTAINER_WASM_UNSUPPORTED;
            }
            const uint32_t capability = name_is(field, field_size, "monotonic_ms")
                                            ? ECONTAINER_CAP_MONOTONIC_TIME
                                            : name_is(field, field_size, "log")
                                                  ? ECONTAINER_CAP_LOG : 0;
            if (capability == 0 || (capabilities & capability) != 0 ||
                !import_type_matches(wasm, type_begin, type_end, type_index,
                                     capability)) {
                return ECONTAINER_WASM_UNSUPPORTED;
            }
            capabilities |= capability;
        }
        if (cursor != import_end) {
            return ECONTAINER_WASM_INVALID;
        }
    }
    if (required_capabilities != NULL) {
        *required_capabilities = capabilities;
    }
    return ECONTAINER_WASM_OK;
}

econtainer_wasm_result_t econtainer_wasm_check(const uint8_t *wasm, size_t size_bytes)
{
    return scan_module(wasm, size_bytes, NULL);
}

bool econtainer_wasm_imported_capabilities(const uint8_t *wasm, size_t size_bytes,
                                           uint32_t *required_capabilities)
{
    return required_capabilities != NULL &&
           scan_module(wasm, size_bytes, required_capabilities) == ECONTAINER_WASM_OK;
}

bool econtainer_wasm_memory_within_limit(const uint8_t *wasm, size_t size_bytes,
                                         uint32_t max_memory_pages)
{
    if (max_memory_pages == 0 ||
        econtainer_wasm_check(wasm, size_bytes) != ECONTAINER_WASM_OK) {
        return false;
    }
    size_t cursor = 8;
    while (cursor < size_bytes) {
        const uint8_t section = wasm[cursor++];
        uint32_t length = 0;
        if (!read_u32(wasm, size_bytes, &cursor, &length) ||
            (size_t)length > size_bytes - cursor) {
            return false;
        }
        const size_t end = cursor + (size_t)length;
        if (section == 5) {
            uint32_t count = 0;
            uint32_t flags = 0;
            uint32_t minimum = 0;
            uint32_t maximum = 0;
            return read_u32(wasm, end, &cursor, &count) && count == 1 &&
                   read_u32(wasm, end, &cursor, &flags) && flags == 1 &&
                   read_u32(wasm, end, &cursor, &minimum) && minimum > 0 &&
                   read_u32(wasm, end, &cursor, &maximum) &&
                   minimum <= maximum && maximum <= max_memory_pages &&
                   cursor == end;
        }
        cursor = end;
    }
    return false;
}
