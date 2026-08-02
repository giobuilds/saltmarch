#ifndef SHA256_H
#define SHA256_H

/* sha256.h  --  One hash, for credentials at rest
 * (AUTH_PLAN Phase 1) */

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_BYTES 32
#define SHA256_BLOCK_BYTES  64

typedef struct {
    uint32_t state[8];
    uint64_t bits;                        /* message length in bits    */
    size_t   used;                        /* bytes in `buf`            */
    uint8_t  buf[SHA256_BLOCK_BYTES];
} Sha256;

void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const void *data, size_t len);
void sha256_final(Sha256 *s, uint8_t out[SHA256_DIGEST_BYTES]);

/* The whole message in one call. */
void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_BYTES]);

/* Compare `n` bytes without an early exit. */
int ct_equal(const void *a, const void *b, size_t n);

#endif /* SHA256_H */
