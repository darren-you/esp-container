#ifndef ESP_CONTAINER_PACKAGE_H
#define ESP_CONTAINER_PACKAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    size_t manifest_size_bytes;
    size_t wasm_offset_bytes;
    size_t wasm_size_bytes;
    uint8_t wasm_sha256[32];
    uint8_t package_sha256[32];
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

#ifdef __cplusplus
}
#endif

#endif
