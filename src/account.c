/* account.c  --  The sidecar (AUTH_PLAN Phase 1) */

#ifdef _WIN32
/* Before any CRT header, which is the only place either of these has an
 * effect. */
#  define _CRT_RAND_S
#  define _CRT_SECURE_NO_WARNINGS
#endif

#include "account.h"
#include "sha256.h"
#include "simlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void account_store_init(AccountStore *s)
{
    memset(s, 0, sizeof(*s));
    s->next_id           = 1u;
    s->registration_open = 1;
}

/* ---- randomness -------------------------------------------- */

int account_random(void *buf, size_t n)
{
#ifdef _WIN32
    /* rand_s is backed by RtlGenRandom. Chosen over BCryptGenRandom to
     * avoid adding bcrypt.lib to a link line that is currently ws2_32
     * and nothing else. */
    unsigned char *p = (unsigned char *)buf;
    size_t         i;

    for (i = 0; i < n; i++) {
        unsigned int v = 0u;
        if (rand_s(&v) != 0) return 0;
        p[i] = (unsigned char)(v & 0xFFu);
    }
    return 1;
#else
    FILE  *f = fopen("/dev/urandom", "rb");
    size_t got;

    if (!f) return 0;
    got = fread(buf, 1, n, f);
    fclose(f);
    return got == n;
#endif
}

/* ---- hex ---------------------------------------------------- */

void account_hex(char *out, const void *bytes, size_t n)
{
    static const char D[] = "0123456789abcdef";
    const unsigned char *p = (const unsigned char *)bytes;
    size_t i;

    for (i = 0; i < n; i++) {
        out[i * 2]     = D[p[i] >> 4];
        out[i * 2 + 1] = D[p[i] & 0x0Fu];
    }
    out[n * 2] = '\0';
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int account_unhex(void *out, size_t n, const char *hex)
{
    unsigned char *p = (unsigned char *)out;
    size_t         i;

    if (!hex || strlen(hex) != n * 2) return 0;
    for (i = 0; i < n; i++) {
        int hi = hex_digit(hex[i * 2]);
        int lo = hex_digit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return 0;
        p[i] = (unsigned char)((hi << 4) | lo);
    }
    return 1;
}

/* ---- the credential ---------------------------------------- */

/* sha256(salt || token). The salt is not defending entropy — a 256-bit
 * token has plenty — it is stopping one precomputed table from covering
 * every server in the world at once. */
static void derive(const uint8_t salt[ACCOUNT_SALT_BYTES],
                   const uint8_t token[ACCOUNT_TOKEN_BYTES],
                   uint8_t out[ACCOUNT_HASH_BYTES])
{
    Sha256 s;
    sha256_init(&s);
    sha256_update(&s, salt, ACCOUNT_SALT_BYTES);
    sha256_update(&s, token, ACCOUNT_TOKEN_BYTES);
    sha256_final(&s, out);
}

/* ---- lookup ------------------------------------------------- */

Account *account_find(AccountStore *s, uint32_t id)
{
    int i;
    if (id == 0u) return NULL;
    for (i = 0; i < s->count; i++)
        if (s->a[i].id == id) return &s->a[i];
    return NULL;
}

Account *account_for_player(AccountStore *s, uint32_t player_id)
{
    int i;
    if (player_id == 0u) return NULL;
    for (i = 0; i < s->count; i++)
        if (s->a[i].player_id == player_id) return &s->a[i];
    return NULL;
}

/* ---- minting ------------------------------------------------ */

static void clean_name(char *dst, size_t cap, const char *src)
{
    size_t n = 0;

    if (cap == 0) return;
    /* Printable ASCII, no whitespace: the file is parsed by fields and
     * a name with a space in it would become two of them. This is not a
     * display concern — nothing here reaches world state, which is
     * AUTH_PLAN invariant 7 — it is a parsing one. */
    while (src && *src && n + 1 < cap) {
        unsigned char c = (unsigned char)*src++;
        if (c > 0x20u && c < 0x7Fu) dst[n++] = (char)c;
    }
    if (n == 0) {
        const char *anon = "player";
        while (*anon && n + 1 < cap) dst[n++] = *anon++;
    }
    dst[n] = '\0';
}

AccountResult account_create(AccountStore *s, uint32_t player_id,
                             const char *name,
                             uint8_t out_token[ACCOUNT_TOKEN_BYTES])
{
    Account *a;

    if (s->count >= ACCOUNT_MAX) return ACCOUNT_FULL;
    if (!account_random(out_token, ACCOUNT_TOKEN_BYTES)) return ACCOUNT_FULL;

    a = &s->a[s->count++];
    memset(a, 0, sizeof(*a));
    a->id        = s->next_id++;
    a->player_id = player_id;
    clean_name(a->name, sizeof(a->name), name);
    a->created_unix = (uint64_t)time(NULL);

    if (!account_random(a->salt, ACCOUNT_SALT_BYTES)) {
        s->count--;
        return ACCOUNT_FULL;
    }
    derive(a->salt, out_token, a->hash);
    return ACCOUNT_OK;
}

AccountResult account_verify(AccountStore *s, uint32_t id,
                             const uint8_t token[ACCOUNT_TOKEN_BYTES],
                             uint64_t now_ms, uint32_t *out_player)
{
    Account *a = account_find(s, id);
    uint8_t  want[ACCOUNT_HASH_BYTES];

    if (out_player) *out_player = 0u;

    /* An unknown id is refused without touching a hash, which. */
    if (!a) return ACCOUNT_UNKNOWN;

    if (a->locked_until_ms > now_ms) return ACCOUNT_LOCKED;

    derive(a->salt, token, want);
    if (!ct_equal(want, a->hash, ACCOUNT_HASH_BYTES)) {
        if (++a->fails >= ACCOUNT_MAX_FAILS) {
            a->locked_until_ms = now_ms + ACCOUNT_LOCK_MS;
            a->fails           = 0u;
            sim_log("account %u locked for %u s after %d failed attempts",
                    id, ACCOUNT_LOCK_MS / 1000u, ACCOUNT_MAX_FAILS);
        }
        return ACCOUNT_BAD_TOKEN;
    }

    a->fails = 0u;
    if (out_player) *out_player = a->player_id;
    return ACCOUNT_OK;
}

/* ---- the file ----------------------------------------------- */

int account_save(const AccountStore *s, const char *path)
{
    char  tmp[512];
    FILE *f;
    int   i, ok = 1;

    if (!path || strlen(path) + 5 >= sizeof(tmp)) return 0;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    f = fopen(tmp, "wb");
    if (!f) return 0;

    ok &= fprintf(f, "# saltmarch accounts v1\n") > 0;
    ok &= fprintf(f, "# id player salt hash created name\n") > 0;

    for (i = 0; i < s->count && ok; i++) {
        const Account *a = &s->a[i];
        char salt_hex[ACCOUNT_SALT_BYTES * 2 + 1];
        char hash_hex[ACCOUNT_HASH_BYTES * 2 + 1];

        account_hex(salt_hex, a->salt, ACCOUNT_SALT_BYTES);
        account_hex(hash_hex, a->hash, ACCOUNT_HASH_BYTES);

        ok &= fprintf(f, "account %u %u %s %s %llu %s\n",
                      a->id, a->player_id, salt_hex, hash_hex,
                      (unsigned long long)a->created_unix, a->name) > 0;
    }

    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); return 0; }

    /* Windows' rename refuses an existing target. Removing first opens
     * a window where neither file exists, which is why the temporary is
     * written and flushed before anything is unlinked. */
    remove(path);
    if (rename(tmp, path) != 0) { remove(tmp); return 0; }
    return 1;
}

int account_load(AccountStore *s, const char *path)
{
    FILE *f;
    char  line[512];

    account_store_init(s);
    if (!path) return 1;

    f = fopen(path, "rb");
    if (!f) return 1;     /* no file yet: an empty store, not a failure */

    while (fgets(line, sizeof(line), f)) {
        Account  *a;
        unsigned  id = 0u, player = 0u;
        char      salt_hex[128], hash_hex[128], name[ACCOUNT_NAME_LEN];
        unsigned long long created = 0ull;
        int       n;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        n = sscanf(line, "account %u %u %127s %127s %llu %23s",
                   &id, &player, salt_hex, hash_hex, &created, name);
        if (n < 5) {
            /* A line that exists and cannot be read is refused loudly.
             * Skipping it would silently drop somebody's account, which
             * on this file means silently handing their islands to the
             * next caller who asks for the id. */
            sim_log("accounts: cannot parse '%s' — refusing to load", path);
            fclose(f);
            account_store_init(s);
            return 0;
        }
        if (n < 6) name[0] = '\0';

        if (s->count >= ACCOUNT_MAX) {
            sim_log("accounts: more than %d accounts in %s", ACCOUNT_MAX, path);
            fclose(f);
            account_store_init(s);
            return 0;
        }

        a = &s->a[s->count++];
        memset(a, 0, sizeof(*a));
        a->id        = (uint32_t)id;
        a->player_id = (uint32_t)player;
        a->created_unix = (uint64_t)created;
        clean_name(a->name, sizeof(a->name), name);

        if (!account_unhex(a->salt, ACCOUNT_SALT_BYTES, salt_hex) ||
            !account_unhex(a->hash, ACCOUNT_HASH_BYTES, hash_hex)) {
            sim_log("accounts: bad hex in %s — refusing to load", path);
            fclose(f);
            account_store_init(s);
            return 0;
        }
        if (a->id >= s->next_id) s->next_id = a->id + 1u;
    }

    fclose(f);
    return 1;
}
