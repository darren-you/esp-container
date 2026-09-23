#include "package_crypto.h"

#include <limits.h>
#include <string.h>

static const uint8_t package_domain[] = "ESP-CONTAINER-PRODUCT-V1";

#ifdef ESP_PLATFORM

bool econtainer_package_digest_start(econtainer_package_digest_t *digest)
{
    digest->operation = psa_hash_operation_init();
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&digest->operation, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        psa_hash_abort(&digest->operation);
        return false;
    }
    return true;
}

bool econtainer_package_digest_update(econtainer_package_digest_t *digest,
                                      const uint8_t *data, size_t size_bytes)
{
    return psa_hash_update(&digest->operation, data, size_bytes) == PSA_SUCCESS;
}

bool econtainer_package_digest_finish(econtainer_package_digest_t *digest,
                                      uint8_t output[32])
{
    size_t actual_bytes = 0;
    const bool success = psa_hash_finish(&digest->operation, output, 32,
                                          &actual_bytes) == PSA_SUCCESS && actual_bytes == 32;
    psa_hash_abort(&digest->operation);
    return success;
}

void econtainer_package_digest_abort(econtainer_package_digest_t *digest)
{
    psa_hash_abort(&digest->operation);
}

bool econtainer_package_signature_verify(const uint8_t *public_key_rsa_der,
                                         size_t public_key_size_bytes,
                                         const uint8_t *manifest, size_t manifest_size_bytes,
                                         const uint8_t signature[384])
{
    uint8_t hash[32];
    econtainer_package_digest_t digest;
    if (!econtainer_package_digest_start(&digest)) {
        return false;
    }
    if (!econtainer_package_digest_update(&digest, package_domain, sizeof(package_domain)) ||
        !econtainer_package_digest_update(&digest, manifest, manifest_size_bytes)) {
        econtainer_package_digest_abort(&digest);
        return false;
    }
    if (!econtainer_package_digest_finish(&digest, hash)) {
        return false;
    }
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_PUBLIC_KEY);
    psa_set_key_bits(&attributes, 3072);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_RSA_PSS(PSA_ALG_SHA_256));
    bool success = psa_import_key(&attributes, public_key_rsa_der,
                                  public_key_size_bytes, &key_id) == PSA_SUCCESS &&
                   psa_verify_hash(key_id, PSA_ALG_RSA_PSS(PSA_ALG_SHA_256),
                                   hash, sizeof(hash), signature, 384) == PSA_SUCCESS;
    if (!mbedtls_svc_key_id_is_null(key_id) && psa_destroy_key(key_id) != PSA_SUCCESS) {
        success = false;
    }
    psa_reset_key_attributes(&attributes);
    memset(hash, 0, sizeof(hash));
    return success;
}

#else

#include <openssl/rsa.h>
#include <openssl/x509.h>

bool econtainer_package_digest_start(econtainer_package_digest_t *digest)
{
    digest->context = EVP_MD_CTX_new();
    if (digest->context == NULL) {
        return false;
    }
    if (EVP_DigestInit_ex(digest->context, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free(digest->context);
        digest->context = NULL;
        return false;
    }
    return true;
}

bool econtainer_package_digest_update(econtainer_package_digest_t *digest,
                                      const uint8_t *data, size_t size_bytes)
{
    return EVP_DigestUpdate(digest->context, data, size_bytes) == 1;
}

bool econtainer_package_digest_finish(econtainer_package_digest_t *digest,
                                      uint8_t output[32])
{
    unsigned int length = 0;
    const bool success = EVP_DigestFinal_ex(digest->context, output, &length) == 1 && length == 32;
    EVP_MD_CTX_free(digest->context);
    digest->context = NULL;
    return success;
}

void econtainer_package_digest_abort(econtainer_package_digest_t *digest)
{
    EVP_MD_CTX_free(digest->context);
    digest->context = NULL;
}

bool econtainer_package_signature_verify(const uint8_t *public_key_rsa_der,
                                         size_t public_key_size_bytes,
                                         const uint8_t *manifest, size_t manifest_size_bytes,
                                         const uint8_t signature[384])
{
    if (public_key_size_bytes > LONG_MAX) {
        return false;
    }
    const uint8_t *cursor = public_key_rsa_der;
    EVP_PKEY *key = d2i_PublicKey(EVP_PKEY_RSA, NULL, &cursor,
                                  (long)public_key_size_bytes);
    if (key == NULL) {
        return false;
    }
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    EVP_PKEY_CTX *key_context = NULL;
    const bool success = context != NULL &&
                         cursor == public_key_rsa_der + public_key_size_bytes &&
                         EVP_PKEY_base_id(key) == EVP_PKEY_RSA &&
                         EVP_PKEY_bits(key) == 3072 &&
                         EVP_DigestVerifyInit(context, &key_context, EVP_sha256(), NULL, key) == 1 &&
                         EVP_PKEY_CTX_set_rsa_padding(key_context, RSA_PKCS1_PSS_PADDING) == 1 &&
                         EVP_PKEY_CTX_set_rsa_mgf1_md(key_context, EVP_sha256()) == 1 &&
                         EVP_PKEY_CTX_set_rsa_pss_saltlen(key_context, 32) == 1 &&
                         EVP_DigestVerifyUpdate(context, package_domain, sizeof(package_domain)) == 1 &&
                         EVP_DigestVerifyUpdate(context, manifest, manifest_size_bytes) == 1 &&
                         EVP_DigestVerifyFinal(context, signature, 384) == 1;
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    return success;
}

#endif
