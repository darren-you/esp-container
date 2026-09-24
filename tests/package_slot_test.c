#include "esp_container_package_slot.h"

#include <assert.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FLASH_BASE = 0x10000, SLOT_BYTES = 32768, FLASH_BYTES = 3 * SLOT_BYTES };

typedef struct {
    uint8_t flash[FLASH_BYTES];
    uint8_t blob[ECONTAINER_SLOT_BLOB_BYTES];
    bool blob_present;
    bool locked;
    size_t read_count;
    size_t fail_after_read_count;
    size_t largest_read;
    unsigned wasm_offset_read_count;
    bool fail_wasm_scan_read;
} fake_store_t;

typedef struct {
    const uint8_t *bytes;
    size_t length;
} source_t;

static const econtainer_slots_geometry_t geometry = {
    .partition_offset_bytes = FLASH_BASE,
    .partition_size_bytes = FLASH_BYTES,
    .erase_unit_bytes = 4096,
    .write_unit_bytes = 4,
    .slots = {
        {FLASH_BASE, SLOT_BYTES},
        {FLASH_BASE + SLOT_BYTES, SLOT_BYTES},
        {FLASH_BASE + 2 * SLOT_BYTES, SLOT_BYTES},
    },
};

static bool fake_lock(void *context)
{
    fake_store_t *store = context;
    if (store->locked) return false;
    store->locked = true;
    return true;
}

static void fake_unlock(void *context)
{
    fake_store_t *store = context;
    assert(store->locked);
    store->locked = false;
}

static econtainer_slot_blob_result_t fake_read_blob(
    void *context, uint8_t blob[ECONTAINER_SLOT_BLOB_BYTES])
{
    const fake_store_t *store = context;
    assert(store->locked);
    if (!store->blob_present) return ECONTAINER_SLOT_BLOB_NOT_FOUND;
    memcpy(blob, store->blob, ECONTAINER_SLOT_BLOB_BYTES);
    return ECONTAINER_SLOT_BLOB_FOUND;
}

static bool fake_write_blob(void *context,
                            const uint8_t blob[ECONTAINER_SLOT_BLOB_BYTES])
{
    fake_store_t *store = context;
    assert(store->locked);
    memcpy(store->blob, blob, ECONTAINER_SLOT_BLOB_BYTES);
    store->blob_present = true;
    return true;
}

static bool flash_bounds(uint32_t offset, size_t length)
{
    return offset >= FLASH_BASE &&
           (uint64_t)offset + length <= FLASH_BASE + FLASH_BYTES;
}

static bool fake_flash_read(void *context, uint32_t offset,
                            uint8_t *destination, size_t length)
{
    fake_store_t *store = context;
    assert(store->locked);
    ++store->read_count;
    if (offset == FLASH_BASE + 3072U) {
        ++store->wasm_offset_read_count;
    }
    if (!flash_bounds(offset, length) ||
        (store->fail_wasm_scan_read && offset == FLASH_BASE + 3072U &&
         store->wasm_offset_read_count == 3U) ||
        (store->fail_after_read_count != 0 &&
         store->read_count >= store->fail_after_read_count)) return false;
    if (length > store->largest_read) store->largest_read = length;
    memcpy(destination, store->flash + offset - FLASH_BASE, length);
    return true;
}

static bool fake_flash_erase(void *context, uint32_t offset, uint32_t length)
{
    fake_store_t *store = context;
    assert(store->locked);
    if (!flash_bounds(offset, length) || length != SLOT_BYTES) return false;
    memset(store->flash + offset - FLASH_BASE, 0xff, length);
    return true;
}

static bool fake_flash_write(void *context, uint32_t offset,
                             const uint8_t *source, size_t length)
{
    fake_store_t *store = context;
    assert(store->locked);
    if (!flash_bounds(offset, length) || length % 4 != 0) return false;
    for (size_t index = 0; index < length; ++index) {
        uint8_t *destination = store->flash + offset - FLASH_BASE + index;
        if ((*destination & source[index]) != source[index]) return false;
        *destination &= source[index];
    }
    return true;
}

static bool source_read(void *context, size_t offset,
                        uint8_t *destination, size_t length)
{
    const source_t *source = context;
    if (offset > source->length || length > source->length - offset) return false;
    memcpy(destination, source->bytes + offset, length);
    return true;
}

static bool read_file(const char *path, uint8_t **bytes, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) return false;
    const long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET) != 0) return false;
    *bytes = malloc((size_t)size);
    if (*bytes == NULL) return false;
    *length = (size_t)size;
    const bool okay = fread(*bytes, 1, *length, file) == *length;
    fclose(file);
    return okay;
}

int main(int argc, char **argv)
{
    if (argc != 4) return 3;
    uint8_t *package = NULL;
    uint8_t *key = NULL;
    size_t package_size = 0;
    size_t key_size = 0;
    if (!read_file(argv[1], &package, &package_size) ||
        !read_file(argv[2], &key, &key_size) || package_size > SLOT_BYTES) return 3;

    fake_store_t *store = calloc(1, sizeof(*store));
    if (store == NULL) return 3;
    memset(store->flash, 0xff, sizeof(store->flash));
    const econtainer_slots_io_t io = {
        .lock = fake_lock, .unlock = fake_unlock,
        .read_blob = fake_read_blob, .write_blob = fake_write_blob,
        .flash_read = fake_flash_read, .flash_erase = fake_flash_erase,
        .flash_write = fake_flash_write, .context = store,
    };
    econtainer_slot_firmware_set_t firmware_set = {.bootable_count = 1};
    memset(firmware_set.bootable_firmware_sha256[0], 0x11, 32);
    memset(firmware_set.running_firmware_sha256, 0x11, 32);
    econtainer_slot_binding_t bindings[ECONTAINER_SLOT_BINDING_COUNT] = {0};
    bindings[0].present = true;
    memset(bindings[0].firmware_sha256, 0x11, 32);
    assert(econtainer_slots_initialize(&io, &geometry, &firmware_set, bindings) ==
           ECONTAINER_SLOTS_OK);

    econtainer_slot_operation_t operation = {0};
    operation.operation_id[0] = 1;
    memcpy(operation.target_firmware_sha256, firmware_set.running_firmware_sha256, 32);
    assert(SHA256(package, package_size, operation.package_sha256) != NULL);
    operation.package_size_bytes = (uint32_t)package_size;
    operation.guest_abi_version = 1;
    operation.data_schema_version = 1;
    if (strcmp(argv[3], "schema") == 0) operation.data_schema_version = 2;

    econtainer_slots_state_t state;
    assert(econtainer_slots_reserve(&io, &geometry, 1, &firmware_set,
                                    &operation, &state) == ECONTAINER_SLOTS_OK);
    assert(state.phase == ECONTAINER_SLOT_WRITING);
    if (strcmp(argv[3], "changed-copy") == 0) {
        operation.data_schema_version = 2;
    }
    econtainer_package_workspace_t package_workspace;
    econtainer_wasm_workspace_t wasm_workspace;
    econtainer_package_info_t verified_info;
    memset(&verified_info, 0xa5, sizeof(verified_info));
    econtainer_package_slot_validation_t validation = {
        .expected_product_id = strcmp(argv[3], "product") == 0 ? "other" : "counter",
        .public_key_rsa_der = key,
        .public_key_size_bytes = key_size,
        .expected_key_id = strcmp(argv[3], "key-id") == 0 ? "other" : "test-key",
        .max_wasm_bytes = 1024,
        .wasm_authorization = {
            .allowed_capabilities = 0,
            .max_memory_bytes = strcmp(argv[3], "memory") == 0 ? 65535U : 65536U,
            .max_stack_bytes = 4096,
        },
        .max_event_queue_limit = strcmp(argv[3], "queue") == 0 ? 7U : 8U,
        .max_instruction_budget = strcmp(argv[3], "budget") == 0 ? 99999U : 100000U,
        .max_host_call_timeout_ms = strcmp(argv[3], "timeout") == 0 ? 99U : 100U,
        .max_storage_limit_bytes = 0,
        .package_workspace = &package_workspace,
        .wasm_workspace = &wasm_workspace,
        .verified_info = &verified_info,
    };
    if (strcmp(argv[3], "read-fault") == 0) {
        /* Hash readback uses 256-byte chunks; fail on the first validator read. */
        store->fail_after_read_count = 1U + package_size / 256U;
    }
    if (strcmp(argv[3], "wasm-read-fault") == 0) {
        /* The third read of this offset belongs to the post-signature Wasm scan. */
        store->fail_wasm_scan_read = true;
    }
    const source_t source = {package, package_size};
    const econtainer_slots_result_t result = econtainer_slots_write_and_prepare(
        &io, &geometry, state.sequence, source_read, (void *)&source,
        econtainer_package_slot_validate, &validation, &state);
    if (strcmp(argv[3], "valid") == 0 || strcmp(argv[3], "changed-copy") == 0) {
        assert(result == ECONTAINER_SLOTS_OK);
        assert(state.phase == ECONTAINER_SLOT_PREPARED);
        assert(verified_info.data_schema_version == 1);
        assert(verified_info.event_queue_limit == 8);
        assert(verified_info.instruction_budget == 100000);
        assert(verified_info.host_call_timeout_ms == 100);
        assert(verified_info.storage_limit_bytes == 0);
        assert(verified_info.product_version_size_bytes == 6);
        assert(memcmp(package_workspace.manifest + verified_info.product_id_offset_bytes,
                      "counter", verified_info.product_id_size_bytes) == 0);
        assert(memcmp(package_workspace.manifest + verified_info.product_version_offset_bytes,
                      "v0-1-0", verified_info.product_version_size_bytes) == 0);
    } else {
        if (strcmp(argv[3], "read-fault") == 0 ||
            strcmp(argv[3], "wasm-read-fault") == 0) {
            assert(result == ECONTAINER_SLOTS_IO_FAILED);
        } else {
            assert(result == ECONTAINER_SLOTS_UNTRUSTED);
        }
        assert(state.phase == ECONTAINER_SLOT_WRITING);
        const econtainer_package_info_t empty = {0};
        assert(memcmp(&verified_info, &empty, sizeof(empty)) == 0);
    }
    assert(!store->locked);
    assert(store->largest_read <= 512);
    if (strcmp(argv[3], "wasm-read-fault") == 0) {
        assert(store->wasm_offset_read_count == 3U);
    }
    printf("result=%d phase=%d max_read=%zu\n", (int)result, (int)state.phase,
           store->largest_read);
    free(store);
    free(key);
    free(package);
    return 0;
}
