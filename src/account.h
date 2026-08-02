#ifndef ACCOUNT_H
#define ACCOUNT_H

/* account.h  --  Who a connection is entitled to be
 * (AUTH_PLAN Phase 1) */

#include <stddef.h>
#include <stdint.h>

#define ACCOUNT_TOKEN_BYTES 32
#define ACCOUNT_SALT_BYTES  16
#define ACCOUNT_HASH_BYTES  32
#define ACCOUNT_NAME_LEN    24
#define ACCOUNT_MAX         64

/* Failed attempts before an account stops answering, and for how long. */
#define ACCOUNT_MAX_FAILS   5
#define ACCOUNT_LOCK_MS     60000u

typedef struct {
    uint32_t id;                          /* account id, from 1        */
    uint32_t player_id;                   /* the world identity it owns */
    char     name[ACCOUNT_NAME_LEN];      /* display name; cosmetic     */
    uint8_t  salt[ACCOUNT_SALT_BYTES];
    uint8_t  hash[ACCOUNT_HASH_BYTES];    /* sha256(salt || token)      */
    uint64_t created_unix;

    /* Rate limiting. In memory only and deliberately not persisted:. */
    uint32_t fails;
    uint64_t locked_until_ms;
} Account;

/* Tagged, because net.h forward-declares it: the transport holds a
 * pointer to a store it never opens, and an anonymous typedef cannot
 * be named from a header that must not include this one. */
typedef struct AccountStore {
    Account  a[ACCOUNT_MAX];
    int      count;
    uint32_t next_id;

    /* Trust on first use: a client with no account is given one and
     * told its token (AUTH_PLAN's default, which keeps a friends server
     * as easy to run as it is now). A public server turns this off and
     * accounts are minted by an admin. */
    int      registration_open;
} AccountStore;

typedef enum {
    ACCOUNT_OK = 0,
    ACCOUNT_UNKNOWN,        /* no such account id                     */
    ACCOUNT_BAD_TOKEN,
    ACCOUNT_LOCKED,         /* too many failures, too recently        */
    ACCOUNT_FULL            /* the store cannot hold another          */
} AccountResult;

void account_store_init(AccountStore *s);

/* Read the sidecar. A missing file is an empty store and a SUCCESS —
 * that is a server being run for the first time, not an error. Returns
 * 0 only when a file exists and could not be understood, which must
 * stop the server rather than silently authenticating nobody. */
int account_load(AccountStore *s, const char *path);

/* Write it, via a temporary file and a rename, so a crash mid-write
 * cannot leave a half-parsed account file — the one file whose
 * corruption locks every player out of their own islands. */
int account_save(const AccountStore *s, const char *path);

Account *account_find(AccountStore *s, uint32_t id);

/* Which account owns `player_id`, or NULL. */
Account *account_for_player(AccountStore *s, uint32_t player_id);

/* Mint an account for `player_id` and write its token into `out_token`.
 * The token is returned ONCE, here: only its hash is kept, so a lost
 * token is reset rather than recovered. `name` may be NULL. */
AccountResult account_create(AccountStore *s, uint32_t player_id,
                             const char *name,
                             uint8_t out_token[ACCOUNT_TOKEN_BYTES]);

/* Is this connection entitled to this account? `now_ms` is a monotonic
 * clock for the lockout; the caller owns it, so this stays testable
 * without a clock of its own. */
AccountResult account_verify(AccountStore *s, uint32_t id,
                             const uint8_t token[ACCOUNT_TOKEN_BYTES],
                             uint64_t now_ms, uint32_t *out_player);

/* Cryptographically strong bytes, or 0. There is no fallback to rand():
 * a predictable token is worse than no authentication, because it looks
 * like authentication. */
int account_random(void *buf, size_t n);

/* Hex, for printing a token to an admin exactly once. `out` must hold
 * 2*n + 1 bytes. */
void account_hex(char *out, const void *bytes, size_t n);

/* And back, for a token pasted on a command line or read from a config.
 * Returns 1 if `hex` was exactly 2*n hex digits. */
int account_unhex(void *out, size_t n, const char *hex);

#endif /* ACCOUNT_H */
