#ifndef TEST_NVS_H
#define TEST_NVS_H

#include <stddef.h>
#include "esp_partition.h"

typedef unsigned nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
esp_err_t nvs_open_from_partition(const char *partition_name,
                                  const char *namespace_name,
                                  nvs_open_mode_t mode, nvs_handle_t *handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out,
                       size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data,
                       size_t size);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);

#endif
