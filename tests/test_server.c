/*  test_server.c  --  the persistent server (MMO_PLAN Phase 6)
 *
 * saltmarch_host is a clock, a socket and a checkpoint file wrapped
 * around the same sim and the same net.c the co-op host uses. This test
 * exercises everything in that sentence EXCEPT the socket, over the
 * in-memory transport, so it is deterministic in any environment (the
 * sandbox this repo is developed in emulates loopback TCP unfaithfully
 * — which is why the transport is swappable in the first place).
 *
 * What a dedicated server must do that a co-op host need not:
 *
 *   - serve MORE THAN ONE player, each with their own identity and
 *     their own granted island, and show each other's actions;
 *   - keep running when a player leaves (a co-op host, by design, does
 *     the opposite: it ends the session and continues single-player);
 *   - let a player come back to the island they left (--as N);
 *   - survive its own restart: the checkpoint it writes is a world that
 *     reloads to the same hash and keeps ticking from there;
 *   - never occupy a player identity itself.
 *
 * Built and run by tests/run.sh.
 */

#include "game.h"
#include "net.h"
#include "resource.h"
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

#define TMP_WORLD "test_server_world.tmp"

/* The server's loop and both clients' loops, one iteration: pump, tick
 * (server freely, clients up to their authorisation horizon),
 * after-update. Mirrors saltmarch_host.c's while(!g_stop) body and
 * main.c's SDL_AppIterate, minus the wall clock. */
static void step(NetSession *sn, GameState *sg,
                 NetSession *an, GameState *ag,
                 NetSession *bn, GameState *bg, int server_ticks)
{
    if (sn) net_pump(sn, sg);
    if (an) net_pump(an, ag);
    if (bn) net_pump(bn, bg);

    /* net_on_tick per COMPLETED tick, exactly as both real loops do it
     * (client.c's pump and the server's clock): it is where the desync
     * hash is taken and where command budgets refill. */
    while (server_ticks-- > 0) { sim_run_one_tick(sg); net_on_tick(sn, sg); }

    while (an && net_tick_allowed(an, ag->sim_tick_no) &&
           ag->sim_tick_no < sg->sim_tick_no)
        { sim_run_one_tick(ag); net_on_tick(an, ag); }
    while (bn && net_tick_allowed(bn, bg->sim_tick_no) &&
           bg->sim_tick_no < sg->sim_tick_no)
        { sim_run_one_tick(bg); net_on_tick(bn, bg); }

    if (sn) net_after_update(sn, sg);
    if (an) net_after_update(an, ag);
    if (bn) net_after_update(bn, bg);
}

static int find_owned_island(const GameState *gs, uint32_t player)
{
    int i;
    for (i = 0; i < MAX_ISLANDS; i++)
        if (gs->islands[i].owner == player) return i;
    return -1;
}

static int count_owned_islands(const GameState *gs, uint32_t player)
{
    int i, n = 0;
    for (i = 0; i < MAX_ISLANDS; i++)
        if (gs->islands[i].owner == player) n++;
    return n;
}

int main(void)
{
    GameState  *sg = game_init();     /* the server                     */
    GameState  *ag = game_init();     /* player A                       */
    GameState  *bg = game_init();     /* player B                       */
    NetSession *sn, *an, *bn;
    uint32_t    a_id, b_id;
    int         a_isl, b_isl, i;

    if (!sg || !ag || !bg) { printf("game_init failed\n"); return 1; }

    printf("== persistent server ==\n");

    /* Exactly what saltmarch_host does at startup for a fresh world. */
    game_new_seeded(sg, 20260724u);
    sg->local_player_id = PLAYER_NONE;

    sn = net_pair_mem(&an);
    if (!sn || !an) { printf("  FAIL: session pair\n"); return 1; }
    net_set_persistent(sn, 1);
    bn = net_join_mem(sn, PLAYER_NONE);
    CHECK(bn != NULL, "a second player can join the same session");
    if (!bn) return 1;

    /* The server never submits commands of its own, so it needs no
     * net_attach; the clients do. */
    net_attach(ag, an);
    net_attach(bg, bn);

    for (i = 0; i < 60; i++) step(sn, sg, an, ag, bn, bg, 1);

    /* ---- two players, two identities, two islands ---- */
    a_id = ag->local_player_id;
    b_id = bg->local_player_id;

    CHECK(ag->world_seed == sg->world_seed && bg->world_seed == sg->world_seed,
          "both players received the server's world");
    CHECK(a_id != PLAYER_NONE && b_id != PLAYER_NONE && a_id != b_id,
          "each player got a distinct identity");
    CHECK(a_id != sg->local_player_id && b_id != sg->local_player_id,
          "the server holds no player identity of its own");

    a_isl = find_owned_island(sg, a_id);
    b_isl = find_owned_island(sg, b_id);
    CHECK(a_isl >= 0 && b_isl >= 0 && a_isl != b_isl,
          "each player was granted their own starting island");
    if (a_isl < 0 || b_isl < 0) {
        printf("\nFAILED (no grant — aborting)\n");
        return 1;
    }
    CHECK(net_peer_count(sn) == 2, "the server reports two connections");

    /* ---- one player's action is visible to the other ---- */
    game_set_current_island(ag, a_isl);
    game_buy_resource(ag, RES_FISH, 6);

    for (i = 0; i < 40; i++) step(sn, sg, an, ag, bn, bg, 1);
    for (i = 0; i < 20; i++) step(sn, sg, an, ag, bn, bg, 0);

    CHECK(sg->islands[a_isl].stockpile.amount[RES_FISH] == 6,
          "player A's purchase applied on the server");
    CHECK(bg->islands[a_isl].stockpile.amount[RES_FISH] == 6,
          "and is visible in player B's world");
    CHECK(ag->sim_tick_no == sg->sim_tick_no &&
          bg->sim_tick_no == sg->sim_tick_no,
          "both players caught up to the server's tick");
    CHECK(sim_hash(ag) == sim_hash(sg) && sim_hash(bg) == sim_hash(sg),
          "all three worlds hash IDENTICALLY");

    /* ---- a player leaves; the world does not stop ---- */
    net_detach(ag);
    net_close(an);
    an = NULL;

    {
        uint64_t before = sg->sim_tick_no;
        for (i = 0; i < 40; i++) step(sn, sg, NULL, ag, bn, bg, 1);
        /* The authorisation horizon is exclusive, so a player is always
         * a tick or two behind a server that never stops; the zero-tick
         * steps are the client catching up, exactly as it does whenever
         * the server is momentarily idle. */
        for (i = 0; i < 20; i++) step(sn, sg, NULL, ag, bn, bg, 0);
        CHECK(sg->sim_tick_no == before + 40,
              "the server kept ticking through a disconnect");
        CHECK(net_peer_count(sn) == 1,
              "the server dropped only the peer that left");
        CHECK(bg->sim_tick_no == sg->sim_tick_no &&
              sim_hash(bg) == sim_hash(sg),
              "the remaining player stayed in lockstep");
    }

    /* ---- and comes back to the island they left ---- */
    an = net_join_mem(sn, a_id);
    CHECK(an != NULL, "the returning player reconnected");
    if (!an) { printf("\nFAILED\n"); return 1; }
    net_attach(ag, an);

    for (i = 0; i < 60; i++) step(sn, sg, an, ag, bn, bg, 1);
    for (i = 0; i < 20; i++) step(sn, sg, an, ag, bn, bg, 0);

    CHECK(ag->local_player_id == a_id,
          "the server honoured the resumed identity");
    CHECK(count_owned_islands(sg, a_id) == 1,
          "no second island was granted on the way back in");
    CHECK(find_owned_island(ag, a_id) == a_isl &&
          ag->islands[a_isl].stockpile.amount[RES_FISH] >= 6,
          "the returning player's island is exactly as they left it");
    CHECK(sim_hash(ag) == sim_hash(sg),
          "the rejoined world hashes with the server's");

    /* ---- the founding island is claimable, not stranded ----
     * A fresh world always makes its creator player 1; on a server there
     * is no creator, so island 0 sits owned by an id nobody holds until
     * someone asks for it with --as 1. */
    {
        GameState  *cg = game_init();
        NetSession *cn;
        if (!cg) { printf("  FAIL: game_init\n"); return 1; }

        cn = net_join_mem(sn, 1u);
        CHECK(cn != NULL, "a third player can claim the founding identity");
        net_attach(cg, cn);
        for (i = 0; i < 60; i++) {
            net_pump(cn, cg);
            step(sn, sg, an, ag, bn, bg, 1);
            net_after_update(cn, cg);
            while (net_tick_allowed(cn, cg->sim_tick_no) &&
                   cg->sim_tick_no < sg->sim_tick_no)
                sim_run_one_tick(cg);
        }
        CHECK(cg->local_player_id == 1u, "they are player 1");
        CHECK(find_owned_island(sg, 1u) == 0 &&
              count_owned_islands(sg, 1u) == 1,
              "they own the founding island and nothing else");

        net_detach(cg);
        net_close(cn);
        game_free(cg);
    }

    /* ---- restart: the checkpoint is the world ---- */
    CHECK(game_save(sg, TMP_WORLD), "the server checkpointed");
    {
        GameState *rg = game_init();
        uint64_t   h_before;
        if (!rg) { printf("  FAIL: game_init\n"); return 1; }

        h_before = sim_hash(sg);
        CHECK(game_load(rg, TMP_WORLD), "a restarted server loaded it");
        rg->local_player_id = PLAYER_NONE;
        CHECK(rg->sim_tick_no == sg->sim_tick_no && sim_hash(rg) == h_before,
              "the restarted world is the same world");

        for (i = 0; i < 50; i++) { sim_run_one_tick(rg); sim_run_one_tick(sg); }
        CHECK(sim_hash(rg) == sim_hash(sg),
              "and keeps ticking identically from there");

        game_free(rg);
    }
    remove(TMP_WORLD);

    net_detach(ag);
    net_detach(bg);
    net_close(an);
    net_close(bn);
    net_close(sn);
    game_free(sg);
    game_free(ag);
    game_free(bg);

    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
