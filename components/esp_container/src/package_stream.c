#include "esp_container_package.h"
#include "package_crypto.h"

#include <limits.h>
#include <string.h>

#define TAR_BLOCK_BYTES 512U
#define TAR_RECORD_BYTES 10240U

typedef struct {
    const uint8_t *data;
    size_t size_bytes;
    size_t cursor;
} econtainer_json_t;

typedef struct {
    uint32_t wasm_size_bytes;
    uint8_t wasm_sha256[32];
} econtainer_manifest_t;

typedef struct {
    econtainer_package_read_fn read_fn;
    void *read_context;
    size_t size_bytes;
    size_t cursor;
    econtainer_package_digest_t digest;
} econtainer_package_stream_t;

static bool json_literal(econtainer_json_t *json, const char *literal)
{
    const size_t size_bytes = strlen(literal);
    if (size_bytes > json->size_bytes - json->cursor ||
        memcmp(json->data + json->cursor, literal, size_bytes) != 0) {
        return false;
    }
    json->cursor += size_bytes;
    return true;
}

static bool json_uint32(econtainer_json_t *json, bool allow_zero, uint32_t *number)
{
    if (json->cursor == json->size_bytes) {
        return false;
    }
    uint64_t value = 0;
    size_t digits = 0;
    const bool starts_zero = json->data[json->cursor] == '0';
    while (json->cursor < json->size_bytes && json->data[json->cursor] >= '0' &&
           json->data[json->cursor] <= '9') {
        value = value * 10U + (uint64_t)(json->data[json->cursor++] - '0');
        if (++digits > 10 || value > UINT32_MAX) {
            return false;
        }
    }
    if (digits == 0 || (starts_zero && digits != 1) || (!allow_zero && value == 0)) {
        return false;
    }
    *number = (uint32_t)value;
    return true;
}

static bool json_identifier(econtainer_json_t *json, const uint8_t **start,
                            size_t *size_bytes)
{
    if (!json_literal(json, "\"")) {
        return false;
    }
    const size_t begin = json->cursor;
    bool previous_hyphen = false;
    while (json->cursor < json->size_bytes) {
        const uint8_t byte = json->data[json->cursor];
        const bool alphanumeric = (byte >= 'a' && byte <= 'z') ||
                                  (byte >= '0' && byte <= '9');
        if (!alphanumeric && byte != '-') {
            break;
        }
        if (byte == '-' && (json->cursor == begin || previous_hyphen)) {
            return false;
        }
        previous_hyphen = byte == '-';
        ++json->cursor;
    }
    if (json->cursor == begin || previous_hyphen) {
        return false;
    }
    *start = json->data + begin;
    *size_bytes = json->cursor - begin;
    return json_literal(json, "\"");
}

static bool json_identifier_key(econtainer_json_t *json, const char *key)
{
    return json_literal(json, "\"") && json_literal(json, key) &&
           json_literal(json, "\":");
}

static bool json_number_key(econtainer_json_t *json, const char *key,
                            bool allow_zero, uint32_t *number)
{
    return json_identifier_key(json, key) && json_uint32(json, allow_zero, number);
}

static bool json_identifier_value(econtainer_json_t *json, const char *key,
                                  const uint8_t **start, size_t *size_bytes)
{
    return json_identifier_key(json, key) && json_identifier(json, start, size_bytes);
}

static bool json_caps(econtainer_json_t *json)
{
    if (!json_identifier_key(json, "required_capabilities") || !json_literal(json, "[")) {
        return false;
    }
    const uint8_t *previous = NULL;
    size_t previous_size = 0;
    for (unsigned count = 0; count < 16; ++count) {
        if (json_literal(json, "]")) {
            return true;
        }
        const uint8_t *current = NULL;
        size_t current_size = 0;
        if (!json_identifier(json, &current, &current_size)) {
            return false;
        }
        if (previous != NULL) {
            const size_t common = previous_size < current_size ? previous_size : current_size;
            const int comparison = memcmp(previous, current, common);
            if (comparison > 0 || (comparison == 0 && previous_size >= current_size)) {
                return false;
            }
        }
        previous = current;
        previous_size = current_size;
        if (json_literal(json, "]")) {
            return true;
        }
        if (!json_literal(json, ",")) {
            return false;
        }
    }
    return false;
}

static bool json_sha256(econtainer_json_t *json, uint8_t output[32])
{
    if (!json_literal(json, "\"") || json->size_bytes - json->cursor < 65) {
        return false;
    }
    for (unsigned index = 0; index < 32; ++index) {
        uint8_t octets[2];
        for (unsigned part = 0; part < 2; ++part) {
            const uint8_t byte = json->data[json->cursor++];
            if (byte >= '0' && byte <= '9') {
                octets[part] = (uint8_t)(byte - '0');
            } else if (byte >= 'a' && byte <= 'f') {
                octets[part] = (uint8_t)(byte - 'a' + 10);
            } else {
                return false;
            }
        }
        output[index] = (uint8_t)((octets[0] << 4) | octets[1]);
    }
    return json_literal(json, "\"");
}

static bool parse_manifest(const uint8_t *data, size_t size_bytes, const char *expected_key_id,
                           size_t max_wasm_bytes, econtainer_manifest_t *manifest)
{
    econtainer_json_t json = {.data = data, .size_bytes = size_bytes, .cursor = 0};
    uint32_t number = 0;
    const uint8_t *identifier = NULL;
    size_t identifier_size = 0;
    if (!json_literal(&json, "{") ||
        !json_number_key(&json, "data_schema_version", false, &number) ||
        !json_literal(&json, ",") ||
        !json_number_key(&json, "guest_abi_version", false, &number) ||
        !json_literal(&json, ",\"limits\":{") ||
        !json_number_key(&json, "event_queue_limit", false, &number) ||
        !json_literal(&json, ",") ||
        !json_number_key(&json, "host_call_timeout_ms", false, &number) ||
        !json_literal(&json, ",") ||
        !json_number_key(&json, "instruction_budget", false, &number) ||
        !json_literal(&json, ",") ||
        !json_number_key(&json, "memory_limit_bytes", false, &number) ||
        !json_literal(&json, ",") ||
        !json_number_key(&json, "stack_limit_bytes", false, &number) ||
        !json_literal(&json, ",") ||
        !json_number_key(&json, "storage_limit_bytes", true, &number) ||
        !json_literal(&json, "},") ||
        !json_number_key(&json, "package_format_version", false, &number) || number != 1 ||
        !json_literal(&json, ",\"payload\":{") ||
        !json_identifier_key(&json, "path") ||
        !json_literal(&json, "\"app.wasm\",") ||
        !json_identifier_key(&json, "sha256") ||
        !json_sha256(&json, manifest->wasm_sha256) ||
        !json_literal(&json, ",") ||
        !json_number_key(&json, "size_bytes", false, &manifest->wasm_size_bytes) ||
        manifest->wasm_size_bytes < 8 || manifest->wasm_size_bytes > max_wasm_bytes ||
        !json_literal(&json, "},") ||
        !json_identifier_value(&json, "product_id", &identifier, &identifier_size) ||
        !json_literal(&json, ",") ||
        !json_identifier_value(&json, "product_version", &identifier, &identifier_size) ||
        !json_literal(&json, ",") ||
        !json_caps(&json) ||
        !json_literal(&json, ",") ||
        !json_identifier_value(&json, "runtime_profile", &identifier, &identifier_size) ||
        !json_literal(&json, ",\"signature_algorithm\":\"rsa-3072-pss-sha256\",") ||
        !json_identifier_value(&json, "signing_key_id", &identifier, &identifier_size) ||
        identifier_size != strlen(expected_key_id) ||
        memcmp(identifier, expected_key_id, identifier_size) != 0 ||
        !json_literal(&json, "}") || json.cursor != size_bytes) {
        return false;
    }
    return true;
}

static bool tar_size(const uint8_t header[512], uint32_t *size_bytes)
{
    uint64_t value = 0;
    for (size_t index = 124; index < 135; ++index) {
        const uint8_t byte = header[index];
        if (byte < '0' || byte > '7') {
            return false;
        }
        value = value * 8U + (uint64_t)(byte - '0');
        if (value > UINT32_MAX) {
            return false;
        }
    }
    if (header[135] != 0) {
        return false;
    }
    *size_bytes = (uint32_t)value;
    return true;
}

static bool tar_octal(uint8_t *destination, size_t digits, uint32_t number)
{
    for (size_t index = digits; index != 0; --index) {
        destination[index - 1] = (uint8_t)('0' + (number & 7U));
        number >>= 3;
    }
    destination[digits] = 0;
    return number == 0;
}

static bool tar_header_matches(const uint8_t actual[512], const char *name,
                               uint32_t size_bytes)
{
    uint8_t expected[512] = {0};
    const size_t name_size = strlen(name);
    if (name_size >= 100) {
        return false;
    }
    memcpy(expected, name, name_size);
    memcpy(expected + 100, "0000644", 7);
    memcpy(expected + 108, "0000000", 7);
    memcpy(expected + 116, "0000000", 7);
    if (!tar_octal(expected + 124, 11, size_bytes)) {
        return false;
    }
    memcpy(expected + 136, "00000000000", 11);
    memset(expected + 148, ' ', 8);
    expected[156] = '0';
    memcpy(expected + 257, "ustar", 5);
    memcpy(expected + 263, "00", 2);
    unsigned checksum = 0;
    for (size_t index = 0; index < sizeof(expected); ++index) {
        checksum += expected[index];
    }
    if (!tar_octal(expected + 148, 6, checksum)) {
        return false;
    }
    expected[155] = ' ';
    return memcmp(actual, expected, sizeof(expected)) == 0;
}

static econtainer_package_result_t stream_read(econtainer_package_stream_t *stream,
                                                uint8_t *destination, size_t size_bytes)
{
    if (size_bytes > TAR_BLOCK_BYTES || size_bytes > stream->size_bytes - stream->cursor) {
        return ECONTAINER_PACKAGE_INVALID;
    }
    if (!stream->read_fn(stream->read_context, stream->cursor, destination, size_bytes)) {
        return ECONTAINER_PACKAGE_READ_FAILED;
    }
    if (!econtainer_package_digest_update(&stream->digest, destination, size_bytes)) {
        return ECONTAINER_PACKAGE_INVALID;
    }
    stream->cursor += size_bytes;
    return ECONTAINER_PACKAGE_OK;
}

static econtainer_package_result_t stream_bytes(econtainer_package_stream_t *stream,
                                                 uint8_t *destination, size_t size_bytes)
{
    for (size_t offset = 0; offset < size_bytes;) {
        const size_t amount = size_bytes - offset < TAR_BLOCK_BYTES
                                  ? size_bytes - offset : TAR_BLOCK_BYTES;
        const econtainer_package_result_t result =
            stream_read(stream, destination + offset, amount);
        if (result != ECONTAINER_PACKAGE_OK) {
            return result;
        }
        offset += amount;
    }
    return ECONTAINER_PACKAGE_OK;
}

static econtainer_package_result_t stream_zeros(econtainer_package_stream_t *stream,
                                                 uint8_t block[512], size_t size_bytes)
{
    while (size_bytes != 0) {
        const size_t amount = size_bytes < TAR_BLOCK_BYTES ? size_bytes : TAR_BLOCK_BYTES;
        const econtainer_package_result_t result = stream_read(stream, block, amount);
        if (result != ECONTAINER_PACKAGE_OK) {
            return result;
        }
        for (size_t index = 0; index < amount; ++index) {
            if (block[index] != 0) {
                return ECONTAINER_PACKAGE_INVALID;
            }
        }
        size_bytes -= amount;
    }
    return ECONTAINER_PACKAGE_OK;
}

static econtainer_package_result_t stream_header(econtainer_package_stream_t *stream,
                                                  uint8_t block[512], const char *name,
                                                  uint32_t *size_bytes)
{
    const econtainer_package_result_t result = stream_read(stream, block, TAR_BLOCK_BYTES);
    if (result != ECONTAINER_PACKAGE_OK) {
        return result;
    }
    return tar_size(block, size_bytes) && tar_header_matches(block, name, *size_bytes)
               ? ECONTAINER_PACKAGE_OK : ECONTAINER_PACKAGE_INVALID;
}

econtainer_package_result_t econtainer_package_verify(
    econtainer_package_read_fn read_fn, void *read_context,
    size_t package_size_bytes, size_t max_wasm_bytes,
    const uint8_t *public_key_rsa_der, size_t public_key_size_bytes,
    const char *expected_key_id, econtainer_package_workspace_t *workspace,
    econtainer_package_info_t *info)
{
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
    }
    if (read_fn == NULL || public_key_rsa_der == NULL || public_key_size_bytes == 0 ||
        public_key_size_bytes > 1024 || expected_key_id == NULL || workspace == NULL ||
        info == NULL || max_wasm_bytes < 8 ||
        max_wasm_bytes > ECONTAINER_PACKAGE_WASM_MAX_BYTES ||
        package_size_bytes % TAR_BLOCK_BYTES != 0 ||
        package_size_bytes > max_wasm_bytes + ECONTAINER_PACKAGE_MANIFEST_MAX_BYTES + 8192U) {
        return ECONTAINER_PACKAGE_INVALID;
    }
    econtainer_package_stream_t stream = {
        .read_fn = read_fn, .read_context = read_context,
        .size_bytes = package_size_bytes, .cursor = 0,
    };
    if (!econtainer_package_digest_start(&stream.digest)) {
        return ECONTAINER_PACKAGE_INVALID;
    }
    econtainer_package_result_t result = ECONTAINER_PACKAGE_INVALID;
    econtainer_package_digest_t wasm_digest;
    bool wasm_digest_active = false;
    uint32_t member_size = 0;
    econtainer_manifest_t manifest = {0};

    result = stream_header(&stream, workspace->block, "manifest.json", &member_size);
    if (result != ECONTAINER_PACKAGE_OK) goto done;
    if (member_size == 0 || member_size > ECONTAINER_PACKAGE_MANIFEST_MAX_BYTES) {
        result = ECONTAINER_PACKAGE_INVALID;
        goto done;
    }
    const size_t manifest_size_bytes = member_size;
    result = stream_bytes(&stream, workspace->manifest, manifest_size_bytes);
    if (result != ECONTAINER_PACKAGE_OK) goto done;
    result = stream_zeros(&stream, workspace->block,
                          (TAR_BLOCK_BYTES - member_size % TAR_BLOCK_BYTES) % TAR_BLOCK_BYTES);
    if (result != ECONTAINER_PACKAGE_OK) goto done;
    if (!parse_manifest(workspace->manifest, manifest_size_bytes, expected_key_id,
                        max_wasm_bytes, &manifest)) {
        result = ECONTAINER_PACKAGE_INVALID;
        goto done;
    }

    result = stream_header(&stream, workspace->block, "signature.bin", &member_size);
    if (result != ECONTAINER_PACKAGE_OK) goto done;
    if (member_size != ECONTAINER_PACKAGE_SIGNATURE_BYTES) {
        result = ECONTAINER_PACKAGE_INVALID;
        goto done;
    }
    result = stream_bytes(&stream, workspace->signature, member_size);
    if (result != ECONTAINER_PACKAGE_OK) goto done;
    result = stream_zeros(&stream, workspace->block, TAR_BLOCK_BYTES - member_size);
    if (result != ECONTAINER_PACKAGE_OK) goto done;
    if (!econtainer_package_signature_verify(public_key_rsa_der, public_key_size_bytes,
                                             workspace->manifest, manifest_size_bytes,
                                             workspace->signature)) {
        result = ECONTAINER_PACKAGE_UNTRUSTED;
        goto done;
    }

    result = stream_header(&stream, workspace->block, "app.wasm", &member_size);
    if (result != ECONTAINER_PACKAGE_OK) goto done;
    if (member_size != manifest.wasm_size_bytes || member_size < 8 ||
        member_size > max_wasm_bytes) {
        result = ECONTAINER_PACKAGE_INVALID;
        goto done;
    }
    const size_t wasm_offset_bytes = stream.cursor;
    if (!econtainer_package_digest_start(&wasm_digest)) {
        result = ECONTAINER_PACKAGE_INVALID;
        goto done;
    }
    wasm_digest_active = true;
    for (size_t remaining = member_size; remaining != 0;) {
        const size_t amount = remaining < TAR_BLOCK_BYTES ? remaining : TAR_BLOCK_BYTES;
        result = stream_read(&stream, workspace->block, amount);
        if (result != ECONTAINER_PACKAGE_OK) goto done;
        if (remaining == member_size) {
            static const uint8_t wasm_header[8] = {0, 'a', 's', 'm', 1, 0, 0, 0};
            if (memcmp(workspace->block, wasm_header, sizeof(wasm_header)) != 0) {
                result = ECONTAINER_PACKAGE_INVALID;
                goto done;
            }
        }
        if (!econtainer_package_digest_update(&wasm_digest, workspace->block, amount)) {
            result = ECONTAINER_PACKAGE_INVALID;
            goto done;
        }
        remaining -= amount;
    }
    uint8_t actual_wasm_sha256[32];
    if (!econtainer_package_digest_finish(&wasm_digest, actual_wasm_sha256)) {
        result = ECONTAINER_PACKAGE_INVALID;
        wasm_digest_active = false;
        goto done;
    }
    wasm_digest_active = false;
    if (memcmp(actual_wasm_sha256, manifest.wasm_sha256, 32) != 0) {
        result = ECONTAINER_PACKAGE_INVALID;
        goto done;
    }
    result = stream_zeros(&stream, workspace->block,
                          (TAR_BLOCK_BYTES - member_size % TAR_BLOCK_BYTES) % TAR_BLOCK_BYTES);
    if (result != ECONTAINER_PACKAGE_OK) goto done;

    const size_t end_minimum = stream.cursor + 2U * TAR_BLOCK_BYTES;
    const size_t canonical_size =
        ((end_minimum + TAR_RECORD_BYTES - 1U) / TAR_RECORD_BYTES) * TAR_RECORD_BYTES;
    if (canonical_size != package_size_bytes) {
        result = ECONTAINER_PACKAGE_INVALID;
        goto done;
    }
    result = stream_zeros(&stream, workspace->block, package_size_bytes - stream.cursor);
    if (result != ECONTAINER_PACKAGE_OK) goto done;
    if (!econtainer_package_digest_finish(&stream.digest, info->package_sha256)) {
        memset(info, 0, sizeof(*info));
        memset(workspace->manifest, 0, sizeof(workspace->manifest));
        memset(workspace->signature, 0, sizeof(workspace->signature));
        return ECONTAINER_PACKAGE_INVALID;
    }
    info->manifest_size_bytes = manifest_size_bytes;
    info->wasm_offset_bytes = wasm_offset_bytes;
    info->wasm_size_bytes = member_size;
    memcpy(info->wasm_sha256, actual_wasm_sha256, sizeof(info->wasm_sha256));
    return ECONTAINER_PACKAGE_OK;

done:
    if (wasm_digest_active) {
        econtainer_package_digest_abort(&wasm_digest);
    }
    econtainer_package_digest_abort(&stream.digest);
    memset(info, 0, sizeof(*info));
    memset(workspace->manifest, 0, sizeof(workspace->manifest));
    memset(workspace->signature, 0, sizeof(workspace->signature));
    return result;
}
