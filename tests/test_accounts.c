/*  test_accounts.c  --  who a connection is entitled to be
 *                       (AUTH_PLAN Phase 1)
 *
 * The hole this closes, in one sentence from SERVER.md: "--as N is an
 * honour system: anyone who knows an id can claim it while its owner is
 * away — and ids are small integers from 1, so they are enumerated
 * rather than guessed."
 *
 * So the assertions that matter are adversarial, not happy-path:
 *
 *   - a wrong token is refused, and refused the same way a wrong ACCOUNT
 *     is, so the wire cannot be used to enumerate which ids exist;
 *   - repeated guesses lock the account rather than continuing to
 *     answer — the per-connection limit is one attempt, which on its own
 *     is no limit at all, because a refused login costs one reconnect;
 *   - a peer that fails authentication is dropped BEFORE any world is
 *     sent (invariant 6): a peer refused afterwards would already hold
 *     every island's stockpile;
 *   - `--as` is ignored on an authenticating server: identity comes from
 *     the credential, never from what the client asked to be, which is
 *     the whole fix;
 *   - and nothing in a credential reaches the snapshot, the hash or the
 *     command log (invariants 1 and 2).
 *
 * SHA-256 is checked against NIST's published vectors, because the
 * argument for implementing it here rather than taking a dependency is
 * only honest if it is bit-for-bit correct.
 *
 * Built and run by tests/run.sh.
 */

#include "account.h"
#include "sha256.h"
#include "net.h"
#include "game.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

static const char *hexof(const uint8_t *d, size_t n)
{
    static char buf[130];
    account_hex(buf, d, n);
    return buf;
}

/* ---- 1. the hash is the hash ------------------------------- */
static void test_sha256_vectors(void)
{
    uint8_t d[SHA256_DIGEST_BYTES];

    printf("\n=== SHA-256, against NIST's own vectors ===\n");

    sha256("", 0, d);
    CHECK(strcmp(hexof(d, 32),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
        "the empty message");

    sha256("abc", 3, d);
    CHECK(strcmp(hexof(d, 32),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0,
        "\"abc\"");

    sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d);
    CHECK(strcmp(hexof(d, 32),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0,
        "the 448-bit message that spans two blocks");

    {
        /* The million-'a' vector, which is the one that catches a broken
         * length counter or a mishandled final block. */
        Sha256 s;
        char  *a = (char *)malloc(1000);
        int    i;

        if (!a) { printf("  FAIL: out of memory\n"); failures++; return; }
        memset(a, 'a', 1000);
        sha256_init(&s);
        for (i = 0; i < 1000; i++) sha256_update(&s, a, 1000);
        sha256_final(&s, d);
        free(a);

        CHECK(strcmp(hexof(d, 32),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0,
            "one million 'a', fed in a thousand pieces");
    }

    /* The comparison a credential goes through. */
    {
        uint8_t x[32], y[32];
        memset(x, 0xA5, sizeof(x));
        memcpy(y, x, sizeof(y));
        CHECK(ct_equal(x, y, 32), "equal bytes compare equal");
        y[31] ^= 0x01u;
        CHECK(!ct_equal(x, y, 32), "and a difference in the LAST byte is seen");
        y[31] ^= 0x01u;
        y[0]  ^= 0x80u;
        CHECK(!ct_equal(x, y, 32), "as is one in the first");
    }
}

/* ---- 2. the store ------------------------------------------ */
static void test_store(void)
{
    AccountStore s;
    uint8_t      token[ACCOUNT_TOKEN_BYTES], other[ACCOUNT_TOKEN_BYTES];
    uint32_t     player = 0u;
    Account     *a;
    int          i;

    printf("\n=== an account, and the token it is not ===\n");

    account_store_init(&s);
    CHECK(account_create(&s, 7u, "keeper", token) == ACCOUNT_OK,
          "an account is minted for a player id");
    a = account_for_player(&s, 7u);
    CHECK(a != NULL && a->player_id == 7u, "and it owns that player");

    /* The token is random. Two in a row being equal would mean the
     * CSPRNG is not one. */
    CHECK(account_create(&s, 8u, NULL, other) == ACCOUNT_OK,
          "a second account is minted");
    CHECK(memcmp(token, other, ACCOUNT_TOKEN_BYTES) != 0,
          "with a different token — the bytes are not a counter");

    {
        int nonzero = 0;
        for (i = 0; i < ACCOUNT_TOKEN_BYTES; i++) if (token[i]) nonzero++;
        CHECK(nonzero > 4, "and the token is not mostly zeroes");
    }

    /* The credential is not in the store in the clear. */
    CHECK(memcmp(a->hash, token, ACCOUNT_TOKEN_BYTES) != 0,
          "what is stored is a hash, not the token");

    CHECK(account_verify(&s, a->id, token, 0u, &player) == ACCOUNT_OK &&
          player == 7u,
          "the right token names the player it owns");

    {
        uint8_t wrong[ACCOUNT_TOKEN_BYTES];
        memcpy(wrong, token, sizeof(wrong));
        wrong[17] ^= 0x40u;
        player = 99u;
        CHECK(account_verify(&s, a->id, wrong, 0u, &player) == ACCOUNT_BAD_TOKEN,
              "one wrong bit is a wrong token");
        CHECK(player == 0u, "and no identity comes back with a refusal");
    }

    CHECK(account_verify(&s, 4242u, token, 0u, &player) == ACCOUNT_UNKNOWN,
          "an account that does not exist is refused too");
}

/* ---- 3. guessing costs something --------------------------- */
static void test_lockout(void)
{
    AccountStore s;
    uint8_t      token[ACCOUNT_TOKEN_BYTES], wrong[ACCOUNT_TOKEN_BYTES];
    uint32_t     player = 0u;
    Account     *a;
    int          i, locked = 0;

    printf("\n=== a program working through the space ===\n");

    account_store_init(&s);
    account_create(&s, 3u, NULL, token);
    a = account_for_player(&s, 3u);
    memcpy(wrong, token, sizeof(wrong));
    wrong[0] ^= 0xFFu;

    for (i = 0; i < ACCOUNT_MAX_FAILS + 2; i++) {
        AccountResult r = account_verify(&s, a->id, wrong, 1000u, &player);
        if (r == ACCOUNT_LOCKED) locked = 1;
    }
    CHECK(locked, "repeated guesses stop being answered");

    /* And the lock is not a permanent ban on the rightful owner. */
    CHECK(account_verify(&s, a->id, token, 1000u, &player) == ACCOUNT_LOCKED,
          "even the right token waits while it is locked");
    CHECK(account_verify(&s, a->id, token, 1000u + ACCOUNT_LOCK_MS + 1u,
                         &player) == ACCOUNT_OK && player == 3u,
          "and afterwards the owner gets in again");
}

/* ---- 4. the file ------------------------------------------- */
static void test_file_round_trip(void)
{
    AccountStore s, t;
    uint8_t      token[ACCOUNT_TOKEN_BYTES];
    uint32_t     player = 0u;
    const char  *path = "build/test_accounts.tmp";
    Account     *a;

    printf("\n=== the sidecar on disk ===\n");

    remove(path);
    account_store_init(&s);
    account_create(&s, 5u, "someone", token);
    a = account_for_player(&s, 5u);
    CHECK(account_save(&s, path), "the store writes");

    CHECK(account_load(&t, path), "and reads back");
    CHECK(t.count == 1, "with the account still in it");
    CHECK(account_verify(&t, a->id, token, 0u, &player) == ACCOUNT_OK &&
          player == 5u,
          "and the token still opens it after a round trip");

    /* A file that cannot be understood must stop the server rather than
     * authenticate nobody: an empty store would hand every island to
     * the next caller who asked for its id. */
    {
        FILE *f = fopen(path, "ab");
        if (f) { fprintf(f, "account this is not an account\n"); fclose(f); }
        CHECK(!account_load(&t, path),
              "a line it cannot parse is refused, not skipped");
    }

    /* A missing file is a first run, not a failure. */
    remove(path);
    CHECK(account_load(&t, path) && t.count == 0,
          "and a missing file is simply an empty store");
}

/* ---- 5. the credential never reaches the world ------------- */
static void test_nothing_leaks_into_the_world(void)
{
    AccountStore   s;
    uint8_t        token[ACCOUNT_TOKEN_BYTES];
    GameState     *gs = game_init();
    unsigned char *buf = NULL;
    size_t         len = 0, i;
    int            found = 0;

    printf("\n=== nothing above the line reaches the world ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);

    account_store_init(&s);
    account_create(&s, gs->local_player_id, NULL, token);

    /* AUTH_PLAN invariants 1 and 2: no credential in a snapshot, and
     * the sim learns nothing. MSG_WORLD hands a snapshot to every
     * joiner, so a token in there would be handed to everyone. */
    CHECK(snapshot_encode(gs, &buf, &len), "the world snapshots");
    if (buf) {
        for (i = 0; i + ACCOUNT_TOKEN_BYTES <= len; i++)
            if (memcmp(buf + i, token, ACCOUNT_TOKEN_BYTES) == 0) found = 1;
        free(buf);
    }
    CHECK(!found, "and the token is nowhere in its bytes");

    /* Nor in the command log, which is replayed and shared. */
    found = 0;
    for (i = 0; i < (size_t)gs->cmd_count; i++) {
        const unsigned char *c = (const unsigned char *)&gs->cmd_log[i];
        size_t j;
        for (j = 0; j + 4 <= sizeof(Command); j++)
            if (memcmp(c + j, token, 4) == 0) found = 1;
    }
    CHECK(!found, "nor in the command log");

    game_free(gs);
}

/* ---- 6. the handshake, over the in-memory transport --------- */
static void test_handshake(void)
{
    AccountStore s;
    uint8_t      token[ACCOUNT_TOKEN_BYTES];
    GameState   *host_gs = game_init(), *guest_gs = game_init();
    NetSession  *host, *guest;
    Account     *a;
    int          i;

    printf("\n=== the handshake ===\n");
    if (!host_gs || !guest_gs) { printf("  FAIL: game_init\n"); failures++;
                                 return; }
    game_new_seeded(host_gs, 4242u);
    host_gs->local_player_id  = PLAYER_NONE;
    /* game_init makes a fresh client player 1; here the guest must
     * learn its identity from the handshake or not at all, so the
     * default is cleared to make the assertion mean something. */
    guest_gs->local_player_id = PLAYER_NONE;

    account_store_init(&s);
    account_create(&s, 1u, NULL, token);      /* player 1 owns island 0 */
    a = account_for_player(&s, 1u);

    /* Registration closed: an unauthenticated peer must get nothing —
     * not an identity, and above all not a world. */
    s.registration_open = 0;
    host = net_pair_mem(&guest);
    if (!host || !guest) { printf("  FAIL: mem pair\n"); failures++; return; }
    net_set_accounts(host, &s);

    for (i = 0; i < 8; i++) {
        net_pump(host, host_gs);
        net_pump(guest, guest_gs);
    }
    CHECK(net_peer_count(host) == 0,
          "a peer with no credential is dropped, not admitted");
    CHECK(guest_gs->local_player_id != 1u,
          "and never learns an identity");
    CHECK(guest_gs->sim_tick_no == 0 && guest_gs->cmd_count == 0,
          "and is sent no world at all");

    net_close(host);
    net_close(guest);

    /* And the other half: the right credential gets in, as the player
     * the ACCOUNT owns — with the guest asking to be somebody else
     * entirely, which an authenticating server ignores. */
    {
        NetCredential cred;
        GameState    *g2 = game_init();

        if (!g2) { printf("  FAIL: game_init\n"); failures++; return; }
        g2->local_player_id = PLAYER_NONE;

        cred.account_id = a->id;
        memcpy(cred.token, token, ACCOUNT_TOKEN_BYTES);

        host = net_host_mem();
        if (!host) { printf("  FAIL: mem host\n"); failures++; return; }
        net_set_accounts(host, &s);
        guest = net_join_mem_as(host, 6u /* please make me player 6 */,
                                &cred);

        for (i = 0; i < 8; i++) {
            net_pump(host, host_gs);
            net_pump(guest, g2);
        }

        CHECK(net_peer_count(host) == 1, "a valid credential is admitted");
        CHECK(g2->local_player_id == 1u,
              "as the player the ACCOUNT owns, not the one it asked for");

        net_close(host);
        net_close(guest);
        game_free(g2);
    }

    game_free(host_gs);
    game_free(guest_gs);
}

/* ---- 7. --as stops being an assertion ---------------------- */
static void test_identity_comes_from_the_credential(void)
{
    AccountStore s;
    uint8_t      token[ACCOUNT_TOKEN_BYTES];
    uint32_t     player = 0u;
    Account     *a;

    printf("\n=== the whole fix, in one assertion ===\n");

    account_store_init(&s);
    account_create(&s, 2u, NULL, token);
    a = account_for_player(&s, 2u);

    /* Whatever a client ASKS to be, what it GETS is what its credential
     * owns. The verify call is the only thing that decides, and it
     * takes no requested id at all — which is the point: there is no
     * argument through which a client could ask for another player. */
    CHECK(account_verify(&s, a->id, token, 0u, &player) == ACCOUNT_OK,
          "the credential verifies");
    CHECK(player == 2u,
          "and names player 2 because that is what the account owns");
    CHECK(account_for_player(&s, 3u) == NULL,
          "there is no account owning player 3 to be claimed at all");
}

int main(void)
{
    printf("== accounts (AUTH_PLAN Phase 1) ==\n");

    test_sha256_vectors();
    test_store();
    test_lockout();
    test_file_round_trip();
    test_nothing_leaks_into_the_world();
    test_handshake();
    test_identity_comes_from_the_credential();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
