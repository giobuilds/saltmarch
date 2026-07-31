#ifndef SHA256_H
#define SHA256_H

/* =========================================================
 * sha256.h  --  One hash, for credentials at rest
 *               (AUTH_PLAN Phase 1)
 *
 * WHY THIS EXISTS AT ALL, given that hand-rolling cryptography is how
 * this goes quietly wrong. AUTH_PLAN's argument is worth restating,
 * because the thing it rules out is not this:
 *
 *   "A key derivation function exists to defend low-entropy human
 *    secrets. A 256-bit machine-generated token has no entropy problem,
 *    so it needs no Argon2, no scrypt, no bcrypt — a plain hash at rest
 *    and a constant-time comparison are enough."
 *
 * So what is needed is a hash, not a KDF. A KDF is a tuning problem
 * with a security argument attached and it is where hand-rolling
 * actually fails. SHA-256 is a fixed function with published test
 * vectors: an implementation is either bit-for-bit correct or it is
 * visibly wrong, and `tests/test_accounts.c` checks it against NIST's
 * own vectors including the million-'a' one.
 *
 * The alternative was a dependency. This project links SDL3, SDL3_ttf
 * and ws2_32; the server links neither SDL, and adding libsodium or
 * mbedTLS to hash 32 bytes at rest would be the largest packaging
 * change in the tree for the smallest reason in it. When passwords
 * arrive they bring TLS and a real KDF with them, and that is the point
 * at which a dependency is worth its weight (AUTH_PLAN Phase 3).
 *
 * NOT FOR: passwords, key derivation, message authentication, or
 * anything where the input is guessable. If you are reaching for this
 * to protect something a human chose, stop and read AUTH_PLAN.md.
 * ========================================================= */

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

/* Compare `n` bytes without an early exit.
 *
 * A byte-at-a-time memcmp on a credential is a timing oracle: the
 * attacker learns how many leading bytes were right from how long the
 * answer took, which turns 2^256 guesses into 32 x 256. Returns 1 for
 * equal, 0 otherwise, in time that depends only on `n`. */
int ct_equal(const void *a, const void *b, size_t n);

#endif /* SHA256_H */
