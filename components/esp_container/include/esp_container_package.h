#ifndef ESP_CONTAINER_PACKAGE_H
#define ESP_CONTAINER_PACKAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_container.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ECONTAINER_PACKAGE_MANIFEST_MAX_BYTES 4096U
#define ECONTAINER_PACKAGE_SIGNATURE_BYTES 384U
#define ECONTAINER_PACKAGE_WASM_MAX_BYTES (512U * 1024U)

/* Reads exact bytes from an already stored candidate package. Never writes storage. */
typedef bool (*econtainer_package_read_fn)(void *context, size_t offset,
                                            uint8_t *destination, size_t length);

typedef struct {
    uint8_t manifest[ECONTAINER_PACKAGE_MANIFEST_MAX_BYTES];
    uint8_t signature[ECONTAINER_PACKAGE_SIGNATURE_BYTES];
    uint8_t block[512];
} econtainer_package_workspace_t;

typedef struct {
    size_t package_size_bytes;
    size_t manifest_size_bytes;
    /* Identifier slices refer to workspace->manifest while it remains intact. */
    size_t product_id_offset_bytes;
    size_t product_id_size_bytes;
    size_t product_version_offset_bytes;
    size_t product_version_size_bytes;
    size_t wasm_offset_bytes;
    size_t wasm_size_bytes;
    uint8_t wasm_sha256[32];
    uint8_t package_sha256[32];
    /* Signed requests only. None of these fields grants device permissions. */
    uint32_t guest_abi_version;
    uint32_t data_schema_version;
    uint32_t memory_limit_bytes;
    uint32_t stack_limit_bytes;
    uint32_t event_queue_limit;
    uint32_t instruction_budget;
    uint32_t host_call_timeout_ms;
    uint32_t storage_limit_bytes;
    uint32_t requested_capabilities;
    bool has_unknown_capability;
    bool is_classic_profile;
} econtainer_package_info_t;

typedef enum {
    ECONTAINER_PACKAGE_OK = 0,
    ECONTAINER_PACKAGE_INVALID,
    ECONTAINER_PACKAGE_UNTRUSTED,
    ECONTAINER_PACKAGE_READ_FAILED,
} econtainer_package_result_t;

/*
 * public_key_rsa_der is an independently trusted RSA-3072 PKCS#1 RSAPublicKey.
 * The package never supplies a trust anchor. expected_key_id binds that key to
 * the signed manifest. A successful result proves bytes returned by read_fn
 * during this call only; installation must still authorize product/profile/ABI,
 * statically check Wasm, and bind the verified storage snapshot before use.
 */
econtainer_package_result_t econtainer_package_verify(
    econtainer_package_read_fn read_fn, void *read_context,
    size_t package_size_bytes, size_t max_wasm_bytes,
    const uint8_t *public_key_rsa_der, size_t public_key_size_bytes,
    const char *expected_key_id, econtainer_package_workspace_t *workspace,
    econtainer_package_info_t *info);

typedef struct {
    /* Supplied independently by the platform, never copied from manifest. */
    uint32_t allowed_capabilities;
    uint32_t max_memory_bytes;
    uint32_t max_stack_bytes;
} econtainer_wasm_authorization_t;

typedef enum {
    ECONTAINER_PACKAGE_WASM_OK = 0,
    ECONTAINER_PACKAGE_WASM_INVALID,
    ECONTAINER_PACKAGE_WASM_UNSUPPORTED,
    ECONTAINER_PACKAGE_WASM_NOT_AUTHORIZED,
    ECONTAINER_PACKAGE_WASM_READ_FAILED,
} econtainer_package_wasm_result_t;

typedef struct {
    uint8_t block[512];
} econtainer_wasm_workspace_t;

/*
 * Run only after package_verify succeeded, with its info and the SAME stable,
 * immutable package snapshot. The caller must independently authorize product,
 * profile, operation, storage and resource policy; this function only checks
 * v1 Classic Wasm ABI, imports and the shown signed requests against the
 * independently supplied authorization. It never installs, executes or writes.
 * Every callback read is <=512 bytes; the whole Wasm is never copied to RAM.
 * Changing storage between verify, this scan and activation invalidates the
 * result and must be prevented by the caller's storage binding.
 */
econtainer_package_wasm_result_t econtainer_package_wasm_check(
    econtainer_package_read_fn read_fn, void *read_context,
    const econtainer_package_info_t *verified_info,
    const econtainer_wasm_authorization_t *authorization,
    econtainer_wasm_workspace_t *workspace);

#ifdef __cplusplus
}
#endif

#endif
