/* crypto.h - SHA-256 (FIPS 180-4), CRC-32 (IEEE 802.3) and salted password hashing. */
#ifndef PLUTUS_CRYPTO_H
#define PLUTUS_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t bits;
    uint8_t block[64];
    size_t used;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t n);
void sha256_final(sha256_ctx *c, uint8_t out[32]);
void sha256(const void *data, size_t n, uint8_t out[32]);

uint32_t crc32(const void *data, size_t n);
uint32_t crc32_update(uint32_t crc, const void *data, size_t n);

/* Iterated, salted SHA-256 (a PBKDF-style key stretch). The iteration count
 * makes each login cost real CPU time, which is why LOGIN and REGISTER have
 * a larger CPU burst in the scheduler. */
#define PW_SALT_LEN 16
#define PW_HASH_LEN 32
#define PW_ITERATIONS 20000
void password_hash(const char *pw, const uint8_t salt[PW_SALT_LEN], uint8_t out[PW_HASH_LEN]);
bool password_verify(const char *pw, const uint8_t salt[PW_SALT_LEN], const uint8_t hash[PW_HASH_LEN]);
bool ct_equal(const void *a, const void *b, size_t n); /* constant-time compare */

#endif
