#ifndef ECONTAINER_PACKAGE_CRYPTO_H
#define ECONTAINER_PACKAGE_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "psa/crypto.h"
typedef struct {
    psa_hash_operation_t operation;
} econtainer_package_digest_t;
#else
#include <openssl/evp.h>
typedef struct {
    EVP_MD_CTX *context;
} econtainer_package_digest_t;
#endif

bool econtainer_package_digest_start(econtainer_package_digest_t *digest);
bool econtainer_package_digest_update(econtainer_package_digest_t *digest,
                                      const uint8_t *data, size_t size_bytes);
bool econtainer_package_digest_finish(econtainer_package_digest_t *digest,
                                      uint8_t output[32]);
void econtainer_package_digest_abort(econtainer_package_digest_t *digest);

bool econtainer_package_signature_verify(const uint8_t *public_key_rsa_der,
                                         size_t public_key_size_bytes,
                                         const uint8_t *manifest, size_t manifest_size_bytes,
                                         const uint8_t signature[384]);

#endif
