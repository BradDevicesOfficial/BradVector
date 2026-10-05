#ifndef BRAD_SEC_H
#define BRAD_SEC_H

#include <stdint.h>
#include <stddef.h>

#define BRAD_SEC_MAX_KEY_SIZE    64
#define BRAD_SEC_MAX_CERT_SIZE   4096

enum brad_sec_level {
    BRAD_SEC_BASE       = 0,
    BRAD_SEC_RAM        = 1,
    BRAD_SEC_X          = 2,
    BRAD_SEC_DISPLAY    = 3,
};

struct brad_sec_key {
    uint8_t               data[BRAD_SEC_MAX_KEY_SIZE];
    unsigned              size;
    unsigned              algo;      
};

struct brad_sec_attestation {
    uint8_t               nonce[32];
    uint8_t               signature[64];
    uint8_t               device_id[16];
    uint64_t              timestamp;
};

int  brad_sec_init(enum brad_sec_level level);
int  brad_sec_encrypt(enum brad_sec_level level,
                      const void *plain, size_t len,
                      void *cipher, size_t *out_len);
int  brad_sec_decrypt(enum brad_sec_level level,
                      const void *cipher, size_t len,
                      void *plain, size_t *out_len);
int  brad_sec_hash(const void *data, size_t len,
                   uint8_t *hash, size_t *hash_len);
int  brad_sec_sign(const struct brad_sec_key *key,
                   const void *data, size_t len,
                   uint8_t *sig, size_t *sig_len);
int  brad_sec_verify(const struct brad_sec_key *key,
                     const void *data, size_t len,
                     const uint8_t *sig, size_t sig_len);
int  brad_sec_attest(struct brad_sec_attestation *attest);
int  brad_sec_verify_attest(struct brad_sec_attestation *attest);
int  brad_sec_secure_boot(const uint8_t *fw_image,
                          size_t fw_size);

#endif
