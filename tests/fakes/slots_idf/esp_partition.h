#ifndef TEST_ESP_PARTITION_H
#define TEST_ESP_PARTITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 0x1102

typedef enum {
    ESP_PARTITION_TYPE_APP = 0,
    ESP_PARTITION_TYPE_DATA = 1,
} esp_partition_type_t;
typedef enum {
    ESP_PARTITION_SUBTYPE_DATA_NVS = 2,
    ESP_PARTITION_SUBTYPE_DATA_UNDEFINED = 6,
} esp_partition_subtype_t;
typedef struct {
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address;
    uint32_t size;
    uint32_t erase_size;
    char label[17];
    bool encrypted;
    bool readonly;
} esp_partition_t;

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label);
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset,
                             void *destination, size_t size);
esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset,
                                    size_t size);
esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset,
                              const void *source, size_t size);

#endif
