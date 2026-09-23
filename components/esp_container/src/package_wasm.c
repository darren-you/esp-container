#include "esp_container_package.h"
#include "package_crypto.h"

#include <string.h>

#define WASM_PAGE_BYTES 65536U
#define WASM_READ_BYTES 512U

typedef struct {
    size_t begin;
    size_t end;
    bool present;
} wasm_section_t;

typedef struct {
    econtainer_package_read_fn read_fn;
    void *read_context;
    size_t base;
    size_t size;
    econtainer_wasm_workspace_t *workspace;
    size_t cached_begin;
    size_t cached_size;
    bool read_failed;
} wasm_reader_t;

typedef struct {
    uint32_t functions[3];
    uint32_t defined_count;
    uint32_t imported_count;
    uint32_t imported_capabilities;
} wasm_abi_t;

static bool byte_at(wasm_reader_t *reader, size_t offset, uint8_t *value)
{
    if (offset >= reader->size) {
        return false;
    }
    if (reader->cached_size == 0 || offset < reader->cached_begin ||
        offset - reader->cached_begin >= reader->cached_size) {
        const size_t begin = offset / WASM_READ_BYTES * WASM_READ_BYTES;
        const size_t amount = reader->size - begin < WASM_READ_BYTES
                                  ? reader->size - begin : WASM_READ_BYTES;
        if (!reader->read_fn(reader->read_context, reader->base + begin,
                             reader->workspace->block, amount)) {
            reader->read_failed = true;
            reader->cached_size = 0;
            return false;
        }
        reader->cached_begin = begin;
        reader->cached_size = amount;
    }
    *value = reader->workspace->block[offset - reader->cached_begin];
    return true;
}

static bool next_byte(wasm_reader_t *reader, size_t end, size_t *cursor, uint8_t *value)
{
    if (*cursor >= end || !byte_at(reader, *cursor, value)) {
        return false;
    }
    ++*cursor;
    return true;
}

/* Matches the host package tool's five-byte unsigned Wasm LEB boundary. */
static bool next_u32(wasm_reader_t *reader, size_t end, size_t *cursor, uint32_t *value)
{
    uint32_t result = 0;
    for (unsigned index = 0; index < 5; ++index) {
        uint8_t octet = 0;
        if (!next_byte(reader, end, cursor, &octet) ||
            (index == 4 && (octet & 0xf0U) != 0)) {
            return false;
        }
        result |= (uint32_t)(octet & 0x7fU) << (7U * index);
        if ((octet & 0x80U) == 0) {
            *value = result;
            return true;
        }
    }
    return false;
}

static bool name_is(wasm_reader_t *reader, size_t end, size_t *cursor,
                    const char *expected, bool *matches)
{
    uint32_t length = 0;
    if (!next_u32(reader, end, cursor, &length) || (size_t)length > end - *cursor) {
        return false;
    }
    const size_t expected_size = strlen(expected);
    *matches = length == expected_size;
    if (*matches) {
        for (uint32_t index = 0; index < length; ++index) {
            uint8_t byte = 0;
            if (!byte_at(reader, *cursor + index, &byte)) {
                return false;
            }
            if (byte != (uint8_t)expected[index]) {
                *matches = false;
            }
        }
    }
    *cursor += length;
    return true;
}

static bool scan_sections(wasm_reader_t *reader, wasm_section_t sections[13],
                          bool *unsupported)
{
    static const uint8_t header[] = {0, 'a', 's', 'm', 1, 0, 0, 0};
    for (size_t index = 0; index < sizeof(header); ++index) {
        uint8_t byte = 0;
        if (!byte_at(reader, index, &byte) || byte != header[index]) {
            return false;
        }
    }
    size_t cursor = sizeof(header);
    uint8_t last = 0;
    while (cursor < reader->size) {
        uint8_t section = 0;
        uint32_t size = 0;
        if (!next_byte(reader, reader->size, &cursor, &section) ||
            !next_u32(reader, reader->size, &cursor, &size) ||
            (size_t)size > reader->size - cursor) {
            return false;
        }
        const size_t end = cursor + size;
        if (section > 12 || section == 8 || section == 4 || section == 9) {
            *unsupported = true;
            return true;
        }
        if (section != 0) {
            if (section <= last) {
                return false;
            }
            last = section;
            sections[section] = (wasm_section_t){cursor, end, true};
        } else {
            bool target_features = false;
            size_t custom_cursor = cursor;
            if (!name_is(reader, end, &custom_cursor, "target_features",
                         &target_features)) {
                return false;
            }
            if (target_features) {
                *unsupported = true;
                return true;
            }
        }
        cursor = end;
    }
    return sections[1].present && sections[3].present &&
           sections[5].present && sections[7].present && sections[10].present;
}

static bool value_type(uint8_t value)
{
    return value == 0x7f || value == 0x7e || value == 0x7d || value == 0x7c;
}

static bool type_matches(wasm_reader_t *reader, wasm_section_t section,
                         uint32_t wanted, const uint8_t *params, size_t params_size,
                         const uint8_t *results, size_t results_size)
{
    size_t cursor = section.begin;
    uint32_t count = 0;
    bool found = false;
    if (!section.present || !next_u32(reader, section.end, &cursor, &count) ||
        count > section.end - cursor) {
        return false;
    }
    for (uint32_t index = 0; index < count; ++index) {
        uint8_t form = 0;
        uint32_t param_count = 0;
        uint32_t result_count = 0;
        if (!next_byte(reader, section.end, &cursor, &form) || form != 0x60 ||
            !next_u32(reader, section.end, &cursor, &param_count) ||
            param_count > section.end - cursor) {
            return false;
        }
        bool match = index == wanted && param_count == params_size;
        for (uint32_t part = 0; part < param_count; ++part) {
            uint8_t type = 0;
            if (!next_byte(reader, section.end, &cursor, &type) || !value_type(type)) {
                return false;
            }
            if (match && type != params[part]) {
                match = false;
            }
        }
        if (!next_u32(reader, section.end, &cursor, &result_count) ||
            result_count > section.end - cursor) {
            return false;
        }
        if (result_count != results_size) {
            match = false;
        }
        for (uint32_t part = 0; part < result_count; ++part) {
            uint8_t type = 0;
            if (!next_byte(reader, section.end, &cursor, &type) || !value_type(type)) {
                return false;
            }
            if (match && type != results[part]) {
                match = false;
            }
        }
        if (index == wanted) {
            found = match;
        }
    }
    return cursor == section.end && found;
}

static bool scan_imports(wasm_reader_t *reader, const wasm_section_t sections[13],
                         wasm_abi_t *abi, bool *unsupported)
{
    const wasm_section_t imports = sections[2];
    if (!imports.present) {
        return true;
    }
    size_t cursor = imports.begin;
    uint32_t count = 0;
    if (!next_u32(reader, imports.end, &cursor, &count)) {
        return false;
    }
    if (count > 4) {
        *unsupported = true;
        return true;
    }
    static const uint8_t log_params[] = {0x7f, 0x7f};
    static const uint8_t start_params[] = {0x7f, 0x7f};
    static const uint8_t cancel_params[] = {0x7e};
    static const uint8_t i32_result[] = {0x7f};
    static const uint8_t i64_result[] = {0x7e};
    uint32_t seen = 0;
    for (uint32_t index = 0; index < count; ++index) {
        bool module = false;
        bool clock_name = false;
        bool log_name = false;
        bool start_name = false;
        bool cancel_name = false;
        uint8_t kind = 0;
        uint32_t type_index = 0;
        if (!name_is(reader, imports.end, &cursor, "econtainer", &module)) {
            return false;
        }
        const size_t field_begin = cursor;
        if (!name_is(reader, imports.end, &cursor, "monotonic_ms", &clock_name)) {
            return false;
        }
        if (!clock_name) {
            cursor = field_begin;
            if (!name_is(reader, imports.end, &cursor, "log", &log_name)) {
                return false;
            }
        }
        if (!clock_name && !log_name) {
            cursor = field_begin;
            if (!name_is(reader, imports.end, &cursor, "timer_start", &start_name))
                return false;
        }
        if (!clock_name && !log_name && !start_name) {
            cursor = field_begin;
            if (!name_is(reader, imports.end, &cursor, "timer_cancel", &cancel_name))
                return false;
        }
        if (!next_byte(reader, imports.end, &cursor, &kind) ||
            !next_u32(reader, imports.end, &cursor, &type_index)) {
            return false;
        }
        if (!module || kind != 0) {
            *unsupported = true;
            return true;
        }
        const uint32_t cap = clock_name ? ECONTAINER_CAP_MONOTONIC_TIME
                              : log_name ? ECONTAINER_CAP_LOG
                              : start_name || cancel_name ? ECONTAINER_CAP_TIMER : 0;
        const uint32_t seen_bit = cancel_name ? 8U : cap;
        if (cap == 0 || (seen & seen_bit) != 0) {
            *unsupported = true;
            return true;
        }
        const uint8_t *params = log_name ? log_params
                                : start_name ? start_params
                                : cancel_name ? cancel_params : NULL;
        const size_t params_size = log_name ? sizeof(log_params)
                                   : start_name ? sizeof(start_params)
                                   : cancel_name ? sizeof(cancel_params) : 0;
        const uint8_t *result = log_name || cancel_name ? i32_result : i64_result;
        if (!type_matches(reader, sections[1], type_index,
                          params, params_size, result, 1)) {
            *unsupported = true;
            return true;
        }
        seen |= seen_bit;
        abi->imported_capabilities |= cap;
        ++abi->imported_count;
    }
    return cursor == imports.end;
}

static bool scan_exports(wasm_reader_t *reader, wasm_section_t section,
                         wasm_abi_t *abi, bool *unsupported)
{
    size_t cursor = section.begin;
    uint32_t count = 0;
    if (!next_u32(reader, section.end, &cursor, &count)) {
        return false;
    }
    if (count != 4) {
        *unsupported = true;
        return true;
    }
    uint32_t found = 0;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t length = 0;
        if (!next_u32(reader, section.end, &cursor, &length) ||
            length > section.end - cursor) {
            return false;
        }
        static const char *const names[] = {
            "econtainer_init", "econtainer_on_event", "econtainer_stop", "memory",
        };
        int target = -1;
        for (int item = 0; item < 4; ++item) {
            if (length != strlen(names[item])) {
                continue;
            }
            bool same = true;
            for (uint32_t part = 0; part < length; ++part) {
                uint8_t byte = 0;
                if (!byte_at(reader, cursor + part, &byte)) {
                    return false;
                }
                if (byte != (uint8_t)names[item][part]) {
                    same = false;
                }
            }
            if (same) {
                target = item;
                break;
            }
        }
        cursor += length;
        uint8_t kind = 0;
        uint32_t wasm_index = 0;
        if (!next_byte(reader, section.end, &cursor, &kind) ||
            !next_u32(reader, section.end, &cursor, &wasm_index)) {
            return false;
        }
        if (target < 0 || (found & (1U << target)) != 0 ||
            kind != (target == 3 ? 2 : 0) || (target == 3 && wasm_index != 0)) {
            *unsupported = true;
            return true;
        }
        found |= 1U << target;
        if (target < 3) {
            abi->functions[target] = wasm_index;
        }
    }
    return cursor == section.end && found == 15;
}

static bool function_type(wasm_reader_t *reader, wasm_section_t section,
                          uint32_t wanted, uint32_t type_count,
                          uint32_t *type_index,
                          uint32_t *defined_count)
{
    size_t cursor = section.begin;
    uint32_t count = 0;
    if (!next_u32(reader, section.end, &cursor, &count) ||
        count > section.end - cursor || wanted >= count) {
        return false;
    }
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t current = 0;
        if (!next_u32(reader, section.end, &cursor, &current) ||
            current >= type_count) {
            return false;
        }
        if (index == wanted) {
            *type_index = current;
        }
    }
    *defined_count = count;
    return cursor == section.end;
}

static bool scan_memory(wasm_reader_t *reader, wasm_section_t section,
                        uint32_t limit_bytes)
{
    size_t cursor = section.begin;
    uint32_t count = 0;
    uint32_t flags = 0;
    uint32_t minimum = 0;
    uint32_t maximum = 0;
    return next_u32(reader, section.end, &cursor, &count) && count == 1 &&
           next_u32(reader, section.end, &cursor, &flags) && flags == 1 &&
           next_u32(reader, section.end, &cursor, &minimum) && minimum > 0 &&
           next_u32(reader, section.end, &cursor, &maximum) &&
           minimum <= maximum && maximum <= limit_bytes / WASM_PAGE_BYTES &&
           cursor == section.end;
}

static bool scan_code(wasm_reader_t *reader, wasm_section_t section,
                      uint32_t defined_count)
{
    size_t cursor = section.begin;
    uint32_t count = 0;
    if (!next_u32(reader, section.end, &cursor, &count) || count != defined_count ||
        count > section.end - cursor) {
        return false;
    }
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t size = 0;
        if (!next_u32(reader, section.end, &cursor, &size) ||
            size < 2 || size > section.end - cursor) {
            return false;
        }
        uint8_t end_opcode = 0;
        if (!byte_at(reader, cursor + size - 1U, &end_opcode) || end_opcode != 0x0b) {
            return false;
        }
        cursor += size;
    }
    return cursor == section.end;
}

static econtainer_package_wasm_result_t verify_payload_digest(wasm_reader_t *reader,
                                     const uint8_t expected[32])
{
    econtainer_package_digest_t digest;
    if (!econtainer_package_digest_start(&digest)) {
        return ECONTAINER_PACKAGE_WASM_INVALID;
    }
    uint8_t block[WASM_READ_BYTES];
    for (size_t offset = 0; offset < reader->size;) {
        const size_t amount = reader->size - offset < sizeof(block)
                                  ? reader->size - offset : sizeof(block);
        if (!reader->read_fn(reader->read_context, reader->base + offset,
                             block, amount)) {
            econtainer_package_digest_abort(&digest);
            return ECONTAINER_PACKAGE_WASM_READ_FAILED;
        }
        if (!econtainer_package_digest_update(&digest, block, amount)) {
            econtainer_package_digest_abort(&digest);
            return ECONTAINER_PACKAGE_WASM_INVALID;
        }
        offset += amount;
    }
    uint8_t actual[32];
    if (!econtainer_package_digest_finish(&digest, actual) ||
        memcmp(actual, expected, sizeof(actual)) != 0) {
        return ECONTAINER_PACKAGE_WASM_INVALID;
    }
    return ECONTAINER_PACKAGE_WASM_OK;
}

econtainer_package_wasm_result_t econtainer_package_wasm_check(
    econtainer_package_read_fn read_fn, void *read_context,
    const econtainer_package_info_t *verified_info,
    const econtainer_wasm_authorization_t *authorization,
    econtainer_wasm_workspace_t *workspace)
{
    if (read_fn == NULL || verified_info == NULL || authorization == NULL ||
        workspace == NULL || verified_info->wasm_size_bytes < 8 ||
        verified_info->wasm_size_bytes > ECONTAINER_PACKAGE_WASM_MAX_BYTES ||
        verified_info->wasm_offset_bytes > verified_info->package_size_bytes ||
        verified_info->wasm_size_bytes >
            verified_info->package_size_bytes - verified_info->wasm_offset_bytes ||
        (verified_info->requested_capabilities & (uint32_t)~ECONTAINER_CAP_ALL) != 0 ||
        verified_info->memory_limit_bytes < WASM_PAGE_BYTES ||
        verified_info->stack_limit_bytes == 0 ||
        (authorization->allowed_capabilities & (uint32_t)~ECONTAINER_CAP_ALL) != 0 ||
        authorization->max_memory_bytes < WASM_PAGE_BYTES ||
        authorization->max_stack_bytes == 0) {
        return ECONTAINER_PACKAGE_WASM_INVALID;
    }
    if (verified_info->guest_abi_version != 1 || !verified_info->is_classic_profile ||
        verified_info->has_unknown_capability) {
        return ECONTAINER_PACKAGE_WASM_UNSUPPORTED;
    }
    if ((verified_info->requested_capabilities & ~authorization->allowed_capabilities) != 0 ||
        verified_info->memory_limit_bytes > authorization->max_memory_bytes ||
        verified_info->stack_limit_bytes > authorization->max_stack_bytes) {
        return ECONTAINER_PACKAGE_WASM_NOT_AUTHORIZED;
    }
    wasm_reader_t reader = {
        .read_fn = read_fn, .read_context = read_context,
        .base = verified_info->wasm_offset_bytes,
        .size = verified_info->wasm_size_bytes, .workspace = workspace,
    };
    wasm_section_t sections[13] = {0};
    bool unsupported = false;
    if (!scan_sections(&reader, sections, &unsupported)) {
        return reader.read_failed ? ECONTAINER_PACKAGE_WASM_READ_FAILED
                                  : ECONTAINER_PACKAGE_WASM_INVALID;
    }
    if (unsupported) {
        return ECONTAINER_PACKAGE_WASM_UNSUPPORTED;
    }
    wasm_abi_t abi = {0};
    if (!scan_imports(&reader, sections, &abi, &unsupported) ||
        !scan_exports(&reader, sections[7], &abi, &unsupported)) {
        return reader.read_failed ? ECONTAINER_PACKAGE_WASM_READ_FAILED
                                  : ECONTAINER_PACKAGE_WASM_INVALID;
    }
    if (unsupported) {
        return ECONTAINER_PACKAGE_WASM_UNSUPPORTED;
    }
    if ((abi.imported_capabilities & ~verified_info->requested_capabilities) != 0) {
        return ECONTAINER_PACKAGE_WASM_INVALID;
    }
    if (abi.functions[0] == abi.functions[1] ||
        abi.functions[0] == abi.functions[2] ||
        abi.functions[1] == abi.functions[2]) {
        return ECONTAINER_PACKAGE_WASM_UNSUPPORTED;
    }
    static const uint8_t i32_result[] = {0x7f};
    static const uint8_t event_params[] = {0x7f, 0x7f};
    size_t type_cursor = sections[1].begin;
    uint32_t type_count = 0;
    if (!next_u32(&reader, sections[1].end, &type_cursor, &type_count)) {
        return reader.read_failed ? ECONTAINER_PACKAGE_WASM_READ_FAILED
                                  : ECONTAINER_PACKAGE_WASM_INVALID;
    }
    for (unsigned index = 0; index < 3; ++index) {
        if (abi.functions[index] < abi.imported_count) {
            return ECONTAINER_PACKAGE_WASM_UNSUPPORTED;
        }
        uint32_t type_index = 0;
        uint32_t defined_count = 0;
        if (!function_type(&reader, sections[3],
                           abi.functions[index] - abi.imported_count,
                           type_count,
                           &type_index, &defined_count) ||
            !type_matches(&reader, sections[1], type_index,
                          index == 1 ? event_params : NULL,
                          index == 1 ? sizeof(event_params) : 0,
                          i32_result, sizeof(i32_result))) {
            return reader.read_failed ? ECONTAINER_PACKAGE_WASM_READ_FAILED
                                      : ECONTAINER_PACKAGE_WASM_INVALID;
        }
        abi.defined_count = defined_count;
    }
    if (!scan_memory(&reader, sections[5], verified_info->memory_limit_bytes) ||
        !scan_code(&reader, sections[10], abi.defined_count)) {
        return reader.read_failed ? ECONTAINER_PACKAGE_WASM_READ_FAILED
                                  : ECONTAINER_PACKAGE_WASM_INVALID;
    }
    return verify_payload_digest(&reader, verified_info->wasm_sha256);
}
