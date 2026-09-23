#include "esp_container_slots_idf.h"
#include "nvs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { PACKAGE_BASE = 0x500000, PACKAGE_BYTES = 0x3000,
       NVS_BASE = 0x700000, NVS_BYTES = 0x4000, SECTOR_BYTES = 0x1000 };

struct test_semaphore { bool held; };
static struct test_semaphore storage_lock;
static esp_partition_t package_partition = {
    .type = ESP_PARTITION_TYPE_DATA,
    .subtype = ESP_PARTITION_SUBTYPE_DATA_UNDEFINED,
    .address = PACKAGE_BASE,
    .size = PACKAGE_BYTES,
    .erase_size = SECTOR_BYTES,
    .label = "product_pkg",
};
static esp_partition_t nvs_partition = {
    .type = ESP_PARTITION_TYPE_DATA,
    .subtype = ESP_PARTITION_SUBTYPE_DATA_NVS,
    .address = NVS_BASE,
    .size = NVS_BYTES,
    .erase_size = SECTOR_BYTES,
    .label = "product_nvs",
};
static bool package_available = true;
static uint8_t flash[PACKAGE_BYTES];
static uint8_t stored_blob[ECONTAINER_SLOT_BLOB_BYTES];
static uint8_t staged_blob[ECONTAINER_SLOT_BLOB_BYTES];
static size_t stored_size;
static bool namespace_exists, staged;
static bool fail_open, fail_get, fail_set, fail_commit, commit_then_fail;
static unsigned opens, commits, reads, writes, erases;
static nvs_handle_t open_handle;
static nvs_open_mode_t open_mode;

static econtainer_slots_idf_config_t config(void)
{
    econtainer_slots_idf_config_t result = {
        .package_partition_label = "product_pkg",
        .package_partition_offset_bytes = PACKAGE_BASE,
        .package_partition_size_bytes = PACKAGE_BYTES,
        .slots = {{PACKAGE_BASE, SECTOR_BYTES},
                  {PACKAGE_BASE + SECTOR_BYTES, SECTOR_BYTES},
                  {PACKAGE_BASE + 2U * SECTOR_BYTES, SECTOR_BYTES}},
        .nvs_partition_label = "product_nvs",
        .nvs_partition_offset_bytes = NVS_BASE,
        .nvs_partition_size_bytes = NVS_BYTES,
        .nvs_namespace = "product_slots",
        .nvs_key = "state",
        .storage_lock = &storage_lock,
    };
    return result;
}

static void reset(void)
{
    package_partition.type = ESP_PARTITION_TYPE_DATA;
    package_partition.subtype = ESP_PARTITION_SUBTYPE_DATA_UNDEFINED;
    package_partition.readonly = false;
    package_partition.encrypted = false;
    nvs_partition.readonly = false;
    package_available = true;
    storage_lock.held = false;
    namespace_exists = staged = false;
    fail_open = fail_get = fail_set = fail_commit = commit_then_fail = false;
    stored_size = 0;
    opens = commits = reads = writes = erases = 0;
    open_handle = 0;
    memset(flash, 0xff, sizeof flash);
    memset(stored_blob, 0, sizeof stored_blob);
    memset(staged_blob, 0, sizeof staged_blob);
}

int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned ticks)
{
    assert(semaphore == &storage_lock && ticks == 0U);
    if (semaphore->held) return pdFALSE;
    semaphore->held = true;
    return pdTRUE;
}

int xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(semaphore == &storage_lock && semaphore->held);
    semaphore->held = false;
    return pdTRUE;
}

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label)
{
    if (package_available && type == package_partition.type &&
        subtype == package_partition.subtype &&
        strcmp(label, package_partition.label) == 0) return &package_partition;
    if (type == nvs_partition.type && subtype == nvs_partition.subtype &&
        strcmp(label, nvs_partition.label) == 0) return &nvs_partition;
    return NULL;
}

esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset,
                             void *destination, size_t size)
{
    assert(partition == &package_partition && offset + size <= sizeof flash);
    ++reads;
    memcpy(destination, flash + offset, size);
    return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset,
                                    size_t size)
{
    assert(partition == &package_partition && offset + size <= sizeof flash);
    assert(offset % SECTOR_BYTES == 0U && size % SECTOR_BYTES == 0U);
    ++erases;
    memset(flash + offset, 0xff, size);
    return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset,
                              const void *source, size_t size)
{
    assert(partition == &package_partition && offset + size <= sizeof flash);
    ++writes;
    const uint8_t *bytes = source;
    for (size_t index = 0; index < size; ++index) {
        assert(flash[offset + index] == 0xff);
        flash[offset + index] = bytes[index];
    }
    return ESP_OK;
}

esp_err_t nvs_open_from_partition(const char *partition_name,
                                  const char *namespace_name,
                                  nvs_open_mode_t mode, nvs_handle_t *handle)
{
    assert(strcmp(partition_name, "product_nvs") == 0);
    assert(strcmp(namespace_name, "product_slots") == 0);
    assert(open_handle == 0 && handle != NULL);
    if (fail_open) return ESP_FAIL;
    if (mode == NVS_READONLY && !namespace_exists) return ESP_ERR_NVS_NOT_FOUND;
    open_handle = ++opens;
    open_mode = mode;
    *handle = open_handle;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out,
                       size_t *size)
{
    assert(handle == open_handle && open_mode == NVS_READONLY &&
           strcmp(key, "state") == 0 && size != NULL);
    if (fail_get) return ESP_FAIL;
    if (stored_size == 0U) return ESP_ERR_NVS_NOT_FOUND;
    if (out != NULL) {
        assert(*size >= stored_size);
        memcpy(out, stored_blob, stored_size);
    }
    *size = stored_size;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data,
                       size_t size)
{
    assert(handle == open_handle && open_mode == NVS_READWRITE &&
           strcmp(key, "state") == 0 && size == ECONTAINER_SLOT_BLOB_BYTES);
    if (fail_set) return ESP_FAIL;
    memcpy(staged_blob, data, size);
    staged = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == open_handle && open_mode == NVS_READWRITE && staged);
    ++commits;
    if (!fail_commit || commit_then_fail) {
        memcpy(stored_blob, staged_blob, sizeof stored_blob);
        stored_size = sizeof stored_blob;
        namespace_exists = true;
    }
    return fail_commit ? ESP_FAIL : ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == open_handle);
    open_handle = 0;
    staged = false;
}

static void test_binding_guards(void)
{
    econtainer_slots_idf_provider_t provider;
    econtainer_slots_idf_config_t selected = config();
    package_available = false;
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    assert(provider.io.flash_erase == NULL);
    package_available = true;
    package_partition.subtype = ESP_PARTITION_SUBTYPE_DATA_NVS;
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    package_partition.subtype = ESP_PARTITION_SUBTYPE_DATA_UNDEFINED;
    package_partition.readonly = true;
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    package_partition.readonly = false;
    nvs_partition.subtype = ESP_PARTITION_SUBTYPE_DATA_UNDEFINED;
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    nvs_partition.subtype = ESP_PARTITION_SUBTYPE_DATA_NVS;
    nvs_partition.readonly = true;
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    nvs_partition.readonly = false;
    selected.package_partition_offset_bytes++;
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    selected = config();
    selected.slots[2].offset_bytes = PACKAGE_BASE + 2U * SECTOR_BYTES + 1U;
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    selected = config();
    selected.nvs_partition_size_bytes++;
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    selected = config();
    nvs_partition.address = PACKAGE_BASE + SECTOR_BYTES;
    selected.nvs_partition_offset_bytes = nvs_partition.address;
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    nvs_partition.address = NVS_BASE;
    selected = config();
    selected.nvs_namespace = "TooLongOrWrong";
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    selected = config();
    selected.storage_lock = NULL;
    assert(!econtainer_slots_idf_bind(&provider, &selected));
    assert(erases == 0 && writes == 0 && commits == 0);
}

static void test_provider_io(void)
{
    econtainer_slots_idf_config_t selected = config();
    econtainer_slots_idf_provider_t provider;
    assert(econtainer_slots_idf_bind(&provider, &selected));
    assert(provider.geometry.write_unit_bytes == 4U);
    assert(provider.io.lock(provider.io.context));
    assert(!provider.io.lock(provider.io.context));
    uint8_t blob[ECONTAINER_SLOT_BLOB_BYTES] = {0};
    assert(provider.io.read_blob(provider.io.context, blob) == ECONTAINER_SLOT_BLOB_NOT_FOUND);
    assert(provider.io.flash_erase(provider.io.context, PACKAGE_BASE, SECTOR_BYTES));
    assert(!provider.io.flash_erase(provider.io.context, PACKAGE_BASE + 1U, SECTOR_BYTES));
    const uint8_t bytes[16] = {1, 2, 3, 4};
    assert(provider.io.flash_write(provider.io.context, PACKAGE_BASE, bytes, sizeof bytes));
    assert(!provider.io.flash_write(provider.io.context, PACKAGE_BASE + PACKAGE_BYTES, bytes, sizeof bytes));
    assert(!provider.io.flash_write(provider.io.context, PACKAGE_BASE + 1U, bytes, sizeof bytes));
    uint8_t observed[16] = {0};
    assert(provider.io.flash_read(provider.io.context, PACKAGE_BASE, observed, sizeof observed));
    assert(memcmp(observed, bytes, sizeof bytes) == 0);
    assert(!provider.io.flash_read(provider.io.context, NVS_BASE, observed, sizeof observed));
    memset(blob, 0x55, sizeof blob);
    assert(provider.io.write_blob(provider.io.context, blob));
    assert(commits == 1 && opens == 1);
    memset(blob, 0, sizeof blob);
    assert(provider.io.read_blob(provider.io.context, blob) == ECONTAINER_SLOT_BLOB_FOUND);
    assert(blob[0] == 0x55 && opens == 2);
    stored_size = 20U;
    assert(provider.io.read_blob(provider.io.context, blob) == ECONTAINER_SLOT_BLOB_READ_FAILED);
    stored_size = ECONTAINER_SLOT_BLOB_BYTES;
    fail_get = true;
    assert(provider.io.read_blob(provider.io.context, blob) == ECONTAINER_SLOT_BLOB_READ_FAILED);
    fail_get = false;
    fail_commit = commit_then_fail = true;
    assert(!provider.io.write_blob(provider.io.context, blob));
    assert(commits == 2 && stored_size == ECONTAINER_SLOT_BLOB_BYTES);
    provider.io.unlock(provider.io.context);
    assert(!storage_lock.held && writes == 1 && erases == 1 && reads == 1);
}

static void test_slot_engine_with_idf_provider(void)
{
    econtainer_slots_idf_config_t selected = config();
    econtainer_slots_idf_provider_t provider;
    assert(econtainer_slots_idf_bind(&provider, &selected));
    econtainer_slot_binding_t bindings[ECONTAINER_SLOT_BINDING_COUNT] = {0};
    bindings[0].present = true;
    memset(bindings[0].firmware_sha256, 0x11, sizeof bindings[0].firmware_sha256);
    assert(econtainer_slots_initialize(&provider.io, &provider.geometry, bindings) == ECONTAINER_SLOTS_OK);
    assert(commits == 1 && stored_size == ECONTAINER_SLOT_BLOB_BYTES && !storage_lock.held);
    econtainer_slots_state_t state;
    assert(econtainer_slots_load(&provider.io, &provider.geometry, &state) == ECONTAINER_SLOTS_OK);
    assert(state.sequence == 1U && state.bindings[0].present && state.phase == ECONTAINER_SLOT_IDLE);
    assert(erases == 0 && writes == 0);
}

int main(void)
{
    reset();
    test_binding_guards();
    reset();
    test_provider_io();
    reset();
    test_slot_engine_with_idf_provider();
    reset();
    package_partition.encrypted = true;
    econtainer_slots_idf_config_t selected = config();
    econtainer_slots_idf_provider_t provider;
    assert(econtainer_slots_idf_bind(&provider, &selected));
    assert(provider.geometry.write_unit_bytes == 16U);
    puts("  IDF provider partition guards, Flash/NVS commit and slot engine passed");
}
