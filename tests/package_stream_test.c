#include "esp_container_package.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE *file;
    size_t fail_at;
    size_t next_offset;
    size_t largest_read;
} file_reader_t;

static bool read_file(void *context, size_t offset, uint8_t *destination, size_t length)
{
    file_reader_t *reader = context;
    if (length > 512 || offset != reader->next_offset ||
        (reader->fail_at != SIZE_MAX && offset <= reader->fail_at &&
         reader->fail_at - offset < length) ||
        offset > LONG_MAX || fseek(reader->file, (long)offset, SEEK_SET) != 0 ||
        fread(destination, 1, length, reader->file) != length) {
        return false;
    }
    reader->next_offset += length;
    if (length > reader->largest_read) {
        reader->largest_read = length;
    }
    return true;
}

static bool load_public_key(const char *path, uint8_t key[1024], size_t *length)
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

static void print_hex(const uint8_t *data, size_t size_bytes)
{
    for (size_t index = 0; index < size_bytes; ++index) {
        printf("%02x", data[index]);
    }
}

int main(int argc, char **argv)
{
    if (argc < 5 || argc > 6) {
        return 3;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        return 3;
    }
    const long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return 3;
    }
    uint8_t key[1024];
    size_t key_length = 0;
    if (!load_public_key(argv[2], key, &key_length)) {
        fclose(file);
        return 3;
    }
    char *end = NULL;
    const unsigned long cap = strtoul(argv[4], &end, 10);
    if (*end != 0 || cap > SIZE_MAX) {
        fclose(file);
        return 3;
    }
    file_reader_t reader = {
        .file = file,
        .fail_at = SIZE_MAX,
        .next_offset = 0,
        .largest_read = 0,
    };
    if (argc == 6) {
        reader.fail_at = (size_t)strtoul(argv[5], &end, 10);
        if (*end != 0) {
            fclose(file);
            return 3;
        }
    }
    econtainer_package_workspace_t workspace;
    econtainer_package_info_t info;
    const econtainer_package_result_t result = econtainer_package_verify(
        read_file, &reader, (size_t)length, (size_t)cap,
        key, key_length, argv[3], &workspace, &info);
    fclose(file);
    if (result != ECONTAINER_PACKAGE_OK) {
        printf("result=%d\n", (int)result);
        return 2;
    }
    printf("manifest=%zu wasm_offset=%zu wasm_size=%zu max_read=%zu package_sha256=",
           info.manifest_size_bytes, info.wasm_offset_bytes, info.wasm_size_bytes,
           reader.largest_read);
    print_hex(info.package_sha256, sizeof(info.package_sha256));
    printf(" wasm_sha256=");
    print_hex(info.wasm_sha256, sizeof(info.wasm_sha256));
    putchar('\n');
    return 0;
}
