#include "esp_container_package.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    FILE *file;
    size_t largest_read;
    size_t fail_at;
    size_t flip_at;
} file_reader_t;

static bool read_file(void *context, size_t offset, uint8_t *destination, size_t length)
{
    file_reader_t *reader = context;
    if (length == 0 || length > 512 || offset > LONG_MAX ||
        (reader->fail_at != SIZE_MAX && offset <= reader->fail_at &&
         reader->fail_at - offset < length) ||
        fseek(reader->file, (long)offset, SEEK_SET) != 0 ||
        fread(destination, 1, length, reader->file) != length) {
        return false;
    }
    if (reader->flip_at != SIZE_MAX && offset <= reader->flip_at &&
        reader->flip_at - offset < length) {
        destination[reader->flip_at - offset] ^= 1U;
    }
    if (length > reader->largest_read) {
        reader->largest_read = length;
    }
    return true;
}

static bool load_key(const char *path, uint8_t key[1024], size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    *length = fread(key, 1, 1024, file);
    const bool success = *length != 0 && feof(file) != 0;
    fclose(file);
    return success;
}

static bool number(const char *text, uint32_t *value)
{
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (*text == 0 || *end != 0 || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 7 || argc > 9) {
        return 3;
    }
    uint32_t max_wasm = 0;
    econtainer_wasm_authorization_t grant = {0};
    if (!number(argv[4], &max_wasm) ||
        !number(argv[5], &grant.allowed_capabilities) ||
        !number(argv[6], &grant.max_memory_bytes)) {
        return 3;
    }
    grant.max_stack_bytes = 4096;
    file_reader_t reader = {.fail_at = SIZE_MAX, .flip_at = SIZE_MAX};
    size_t fail_at = SIZE_MAX;
    size_t flip_at = SIZE_MAX;
    if (argc >= 8) {
        uint32_t target = 0;
        if (!number(argv[7], &target)) {
            return 3;
        }
        /* The failure is enabled only after the signed package was checked. */
        fail_at = target;
    }
    if (argc == 9) {
        uint32_t target = 0;
        if (!number(argv[8], &target)) {
            return 3;
        }
        flip_at = target;
    }
    reader.file = fopen(argv[1], "rb");
    if (reader.file == NULL || fseek(reader.file, 0, SEEK_END) != 0) {
        return 3;
    }
    const long package_length = ftell(reader.file);
    uint8_t key[1024];
    size_t key_length = 0;
    if (package_length < 0 || !load_key(argv[2], key, &key_length)) {
        fclose(reader.file);
        return 3;
    }
    econtainer_package_workspace_t package_workspace;
    econtainer_package_info_t info;
    const econtainer_package_result_t package_result = econtainer_package_verify(
        read_file, &reader, (size_t)package_length, max_wasm,
        key, key_length, argv[3], &package_workspace, &info);
    if (package_result != ECONTAINER_PACKAGE_OK) {
        printf("package_result=%d\n", (int)package_result);
        fclose(reader.file);
        return 2;
    }
    const size_t verify_max_read = reader.largest_read;
    reader.largest_read = 0;
    reader.fail_at = fail_at;
    reader.flip_at = flip_at;
    econtainer_wasm_workspace_t wasm_workspace;
    const econtainer_package_wasm_result_t wasm_result = econtainer_package_wasm_check(
        read_file, &reader, &info, &grant, &wasm_workspace);
    printf("wasm_result=%d verify_max_read=%zu wasm_max_read=%zu abi=%u declared=%u profile=%u\n",
           (int)wasm_result, verify_max_read, reader.largest_read,
           (unsigned)info.guest_abi_version,
           (unsigned)info.requested_capabilities, (unsigned)info.is_classic_profile);
    fclose(reader.file);
    return wasm_result == ECONTAINER_PACKAGE_WASM_OK ? 0 : 2;
}
