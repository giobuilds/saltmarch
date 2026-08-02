/* sha256.c  --  FIPS 180-4, straight (AUTH_PLAN Phase 1) */

#include "sha256.h"
#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t ror(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static void compress(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64], a, b, c, d, e, f, g, h;
    int      i;

    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i * 4]     << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] <<  8) |
               ((uint32_t)block[i * 4 + 3]);

    for (i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; i++) {
        uint32_t s1  = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        uint32_t ch  = (e & f) ^ ((~e) & g);
        uint32_t t1  = h + s1 + ch + K[i] + w[i];
        uint32_t s0  = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2  = s0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256_init(Sha256 *s)
{
    s->state[0] = 0x6a09e667u; s->state[1] = 0xbb67ae85u;
    s->state[2] = 0x3c6ef372u; s->state[3] = 0xa54ff53au;
    s->state[4] = 0x510e527fu; s->state[5] = 0x9b05688cu;
    s->state[6] = 0x1f83d9abu; s->state[7] = 0x5be0cd19u;
    s->bits = 0u;
    s->used = 0u;
    memset(s->buf, 0, sizeof(s->buf));
}

void sha256_update(Sha256 *s, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    s->bits += (uint64_t)len * 8u;

    while (len > 0) {
        size_t take = SHA256_BLOCK_BYTES - s->used;
        if (take > len) take = len;

        memcpy(s->buf + s->used, p, take);
        s->used += take;
        p       += take;
        len     -= take;

        if (s->used == SHA256_BLOCK_BYTES) {
            compress(s->state, s->buf);
            s->used = 0u;
        }
    }
}

void sha256_final(Sha256 *s, uint8_t out[SHA256_DIGEST_BYTES])
{
    uint64_t bits = s->bits;
    int      i;

    s->buf[s->used++] = 0x80u;
    if (s->used > SHA256_BLOCK_BYTES - 8) {
        memset(s->buf + s->used, 0, SHA256_BLOCK_BYTES - s->used);
        compress(s->state, s->buf);
        s->used = 0u;
    }
    memset(s->buf + s->used, 0, SHA256_BLOCK_BYTES - 8 - s->used);

    for (i = 0; i < 8; i++)
        s->buf[SHA256_BLOCK_BYTES - 1 - i] = (uint8_t)(bits >> (8 * i));
    compress(s->state, s->buf);

    for (i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(s->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->state[i] >>  8);
        out[i * 4 + 3] = (uint8_t)(s->state[i]);
    }

    /* The state is a rolling image of the message; a credential's hash
     * context is not something to leave on a stack that is about to be
     * reused. Cheap, and it costs nothing to be tidy here. */
    memset(s, 0, sizeof(*s));
}

void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_BYTES])
{
    Sha256 s;
    sha256_init(&s);
    sha256_update(&s, data, len);
    sha256_final(&s, out);
}

int ct_equal(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    uint8_t        diff = 0u;
    size_t         i;

    /* No early exit, and no branch on the data: OR the differences
     * together and look once. */
    for (i = 0; i < n; i++) diff = (uint8_t)(diff | (x[i] ^ y[i]));
    return diff == 0u;
}
