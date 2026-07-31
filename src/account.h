#ifndef ACCOUNT_H
#define ACCOUNT_H

/* =========================================================
 * account.h  --  Who a connection is entitled to be
 *                (AUTH_PLAN Phase 1)
 *
 * `host_assign_id()` grants an identity to anyone who asks for it: send
 * `--as 3` and you are player 3, provided player 3 owns an island and
 * is not currently connected. Ids are small integers from 1, so they
 * are enumerated rather than guessed, and the only guard is "not
 * currently connected" — which, in a world that ticks while its players
 * sleep, is most of the time. This file is what makes `--as` a LOOKUP
 * rather than an assertion.
 *
 * THE THREE LAYERS, and the line this file sits above (AUTH_PLAN):
 *
 *     account       handle + credential      SERVER-SIDE ONLY
 *         | owns                             never hashed, never sent
 *     player_id     owns islands and ships   WORLD STATE
 *         | presented as                     hashed, replayed, snapshotted
 *     display name  what the feed shows      COSMETIC
 *
 * Nothing here may cross into the layer below. `MSG_WORLD` hands the
 * snapshot to every joining client, so a credential stored there would
 * be handed to everyone on join; `sim_hash` covers world state, and
 * hashing a secret would desync every client that cannot know it. So
 * accounts live in a sidecar the host reads and never transmits, and
 * the sim does not know this file exists.
 *
 * TOKENS, NOT PASSWORDS, and therefore no cryptographic dependency. A
 * 256-bit machine-generated token has no entropy problem, so it needs
 * no KDF — a hash at rest and a constant-time comparison are enough.
 * Passwords would need a real KDF and a wire nobody can read, which is
 * AUTH_PLAN Phase 3 and brings TLS with it. See sha256.h.
 *
 * OFF BY DEFAULT. A host with no account store behaves exactly as it
 * did: co-op between friends must not regress into a login screen. The
 * dedicated server turns it on by having a store.
 * ========================================================= */

#include <stddef.h>
#include <stdint.h>

#define ACCOUNT_TOKEN_BYTES 32
#define ACCOUNT_SALT_BYTES  16
#define ACCOUNT_HASH_BYTES  32
#define ACCOUNT_NAME_LEN    24
#define ACCOUNT_MAX         64

/* Failed attempts before an account stops answering, and for how long.
 *
 * Per ACCOUNT rather than only per connection, because a refused login
 * costs the attacker one reconnect: the per-connection limit is "one
 * attempt", which on its own is no limit at all. Five is generous for a
 * client pasting a token it already has and punishing for a program
 * working through the space. */
#define ACCOUNT_MAX_FAILS   5
#define ACCOUNT_LOCK_MS     60000u

typedef struct {
    uint32_t id;                          /* account id, from 1        */
    uint32_t player_id;                   /* the world identity it owns */
    char     name[ACCOUNT_NAME_LEN];      /* display name; cosmetic     */
    uint8_t  salt[ACCOUNT_SALT_BYTES];
    uint8_t  hash[ACCOUNT_HASH_BYTES];    /* sha256(salt || token)      */
    uint64_t created_unix;

    /* Rate limiting. In memory only and deliberately not persisted: a
     * lockout is a live defence against a program hammering one
     * account, not a record to keep. Restarting the server clears it,
     * which is a real limitation and the right trade — the alternative
     * is a write to disk on every failed guess, which is a disk-filling
     * attack wearing a helpful hat. */
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
 * without a clock of its own.
 *
 * On success `*out_player` is the world identity to use — the whole
 * point of the exercise: identity comes from the credential, never from
 * what the client asked to be. */
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
