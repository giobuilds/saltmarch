/*  test_lockstep.c  --  headless verification of MMO_PLAN Phase 5's
 *                       net half: the lockstep co-op protocol.
 *
 * Host and guest sessions live in one process, each with its own
 * GameState, joined by net_pair_mem() — the full protocol (handshake,
 * world transfer, grant, command ordering, tick authorisation, hash
 * exchange, disconnect) over the in-memory transport, so the test is
 * deterministic in any environment. Real TCP swaps in the socket layer
 * only (net_host/net_join); the sandbox this repo is developed in
 * emulates loopback TCP unfaithfully, which is exactly why the
 * transport is swappable. The loop mimics both main loops: pump, tick
 * (host freely, guest up to its authorisation horizon), after-update.
 *
 * Proves the plan's claims:
 *   - joining transfers the world (seed+log) and assigns player 2;
 *   - the host grants a landless guest a starting island THROUGH THE
 *     LOG (both sides see it);
 *   - commands from both sides apply on both sides, identically;
 *   - privacy by validation holds end-to-end (a guest order against
 *     the host's island is rejected on both);
 *   - when the guest catches up to the host's tick, the worlds hash
 *     IDENTICALLY — the lockstep invariant;
 *   - disconnect degrades to single-player continuation on both sides.
 *
 * Built and run by tests/run.sh.
 */

#include "game.h"
#include "net.h"
#include "resource.h"
#include <SDL3/SDL.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* One iteration of both main loops: pump, tick, after-update. The host
 * runs `host_ticks` ticks; the guest runs as many as it is allowed. */
static void step(NetSession *hn, GameState *hg,
                 NetSession *gn, GameState *gg, int host_ticks)
{
    if (hn) net_pump(hn, hg);
    if (gn) net_pump(gn, gg);

    while (host_ticks-- > 0) sim_run_one_tick(hg);
    while (gn && net_tick_allowed(gn, gg->sim_tick_no) &&
           gg->sim_tick_no < hg->sim_tick_no)
        sim_run_one_tick(gg);

    if (hn) net_after_update(hn, hg);
    if (gn) net_after_update(gn, gg);
}

static int find_owned_island(const GameState *gs, uint32_t player)
{
    int i;
    for (i = 0; i < MAX_ISLANDS; i++)
        if (gs->islands[i].owner == player) return i;
    return -1;
}

int main(void)
{
    GameState *hg = game_init();
    GameState *gg = game_init();
    int        i, gisl;

    if (!hg || !gg) { printf("game_init failed\n"); return 1; }
    game_new_seeded(hg, 31337u);

    NetSession *gn = NULL;
    NetSession *hn = net_pair_mem(&gn);
    CHECK(hn != NULL && gn != NULL, "host+guest pair created");
    if (!hn || !gn) return 1;
    hg->net = hn;
    gg->net = gn;

    /* ---- join: world transfer, identity, the grant ---- */
    for (i = 0; i < 40; i++) step(hn, hg, gn, gg, 1);

    CHECK(gg->world_seed == hg->world_seed,
          "guest received and installed the host's world");
    CHECK(gg->local_player_id == 2u, "guest was assigned player 2");

    gisl = find_owned_island(gg, 2u);
    CHECK(gisl >= 0, "the grant gave the guest a starting island");
    if (gisl < 0) {   /* everything below indexes islands[gisl] */
        printf("\nFAILED (no granted island — aborting)\n");
        return 1;
    }
    CHECK(find_owned_island(hg, 2u) == gisl,
          "host agrees which island the guest owns (it came via the log)");
    CHECK(gg->islands[gisl].stockpile.amount[RES_GOLD] == STARTING_GOLD,
          "granted island has the standard treasury on the guest");

    /* ---- commands from both sides land on both sides ---- */
    game_set_current_island(gg, gisl);
    game_buy_resource(gg, RES_FISH, 7);         /* guest, own island   */

    game_set_current_island(hg, 0);
    game_buy_resource(hg, RES_WOOD, 3);         /* host, own island    */

    int host_fish0 = hg->islands[0].stockpile.amount[RES_FISH];
    game_set_current_island(gg, 0);
    game_buy_resource(gg, RES_FISH, 5);         /* guest, HOST's island */
    game_set_current_island(gg, gisl);

    for (i = 0; i < 40; i++) step(hn, hg, gn, gg, 1);

    CHECK(hg->islands[gisl].stockpile.amount[RES_FISH] == 7,
          "guest's purchase applied on the HOST");
    CHECK(gg->islands[gisl].stockpile.amount[RES_FISH] == 7,
          "guest's purchase applied on the GUEST (echoed stamped)");
    CHECK(hg->islands[0].stockpile.amount[RES_WOOD] >= 3,
          "host's purchase applied on the host");
    CHECK(gg->islands[0].stockpile.amount[RES_WOOD] ==
          hg->islands[0].stockpile.amount[RES_WOOD],
          "host's purchase applied identically on the guest");
    CHECK(hg->islands[0].stockpile.amount[RES_FISH] == host_fish0,
          "guest's order against the HOST's island was rejected (privacy)");

    /* ---- the lockstep invariant: equal tick => equal hash ---- */
    for (i = 0; i < 30; i++) step(hn, hg, gn, gg, 0);   /* host frozen */
    CHECK(gg->sim_tick_no == hg->sim_tick_no,
          "guest caught up to the host's tick");
    CHECK(sim_hash(gg) == sim_hash(hg),
          "worlds hash IDENTICALLY at the same tick");

    /* ---- run on, spanning hash-report boundaries ---- */
    for (i = 0; i < 120; i++) step(hn, hg, gn, gg, 1);
    for (i = 0; i < 30; i++)  step(hn, hg, gn, gg, 0);
    CHECK(gg->sim_tick_no == hg->sim_tick_no && sim_hash(gg) == sim_hash(hg),
          "still in lockstep after 120 more ticks and hash exchanges");

    /* ---- disconnect: single-player continuation ---- */
    hg->net = NULL;
    net_close(hn);                       /* host quits (sends BYE)      */
    for (i = 0; i < 5; i++) {
        if (gn && !net_pump(gn, gg)) {   /* guest notices the loss      */
            net_close(gn);
            gn = NULL;
            gg->net = NULL;
        }
    }
    CHECK(gn == NULL, "guest detected the disconnect");

    uint64_t before = gg->sim_tick_no;
    int fish_before = gg->islands[gisl].stockpile.amount[RES_FISH];
    game_buy_resource(gg, RES_FISH, 2);  /* offline: local stamping     */
    for (i = 0; i < 5; i++) sim_run_one_tick(gg);
    CHECK(gg->sim_tick_no == before + 5,
          "guest keeps ticking offline (no authorisation gate)");
    CHECK(gg->islands[gisl].stockpile.amount[RES_FISH] == fish_before + 2,
          "guest keeps playing offline as player 2 on its own island");

    game_free(hg);
    game_free(gg);

    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
