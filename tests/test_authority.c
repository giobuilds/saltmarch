/*  test_authority.c  --  the server's word is final
 *                        (SERVER_AUTHORITY.md Phase 1)
 *
 * Lockstep asked every client to reach the same answer independently
 * and checked that they had. Server authority does not ask: the server
 * pushes the world on a cadence and whatever a client believed is
 * replaced.
 *
 * That inverts what a test should look for. Under lockstep the thing
 * to prove was that the two worlds NEVER diverge. Here they are
 * expected to — a client predicts ahead between pushes, and being
 * briefly wrong is the design working. What has to be true is that
 * divergence is always TEMPORARY: however wrong a client gets, by
 * whatever means, the next push puts it back.
 *
 * So the tests here corrupt the client on purpose. SERVER_AUTHORITY.md
 * asks for adversarial tests "from the first commit, not after CI
 * complains", on the grounds that divergence between predicted and
 * authoritative state is a timing bug by nature and those do not show
 * up on a green local run. This is that.
 *
 * Uses net_pair_mem() like test_lockstep, so the protocol runs in full
 * over a deterministic transport.
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

/* One iteration of both main loops. Unlike the lockstep harness the
 * guest is NOT held to an authorisation horizon — it runs freely,
 * which is the whole point: it is predicting. */
static void step(NetSession *hn, GameState *hg,
                 NetSession *gn, GameState *gg, int ticks)
{
    int i;

    if (hn) net_pump(hn, hg);
    if (gn) net_pump(gn, gg);

    for (i = 0; i < ticks; i++) { sim_run_one_tick(hg); net_on_tick(hn, hg); }
    for (i = 0; i < ticks; i++) { sim_run_one_tick(gg); net_on_tick(gn, gg); }

    if (hn) net_after_update(hn, hg);
    if (gn) net_after_update(gn, gg);
}

int main(void)
{
    GameState  *hg = game_init(), *gg = game_init();
    NetSession *hn = NULL, *gn = NULL;
    int         i, mine = -1;

    if (!hg || !gg) { printf("game_init failed\n"); return 1; }
    game_new_seeded(hg, 4242u);
    gg->local_player_id = 2u;

    printf("=== the server says whose world it is ===\n");

    hn = net_pair_mem(&gn);
    if (!hn || !gn) {
        printf("  FAIL: net_pair_mem\n");
        return 1;
    }
    net_set_authoritative(hn, 1);

    /* Handshake and the join transfer. */
    for (i = 0; i < 12; i++) step(hn, hg, gn, gg, 1);

    CHECK(net_server_authoritative(gn),
          "the client is told the server's word is final");
    CHECK(!net_server_authoritative(hn),
          "and the server does not think that of itself");

    CHECK(net_tick_allowed(gn, gg->sim_tick_no),
          "so the client never waits for permission to simulate");

    printf("\n=== a client that has gone wrong is put right ===\n");
    {
        uint64_t before;

        /* Let them settle so a push has certainly landed. */
        for (i = 0; i < 40; i++) step(hn, hg, gn, gg, 1);

        /* Corrupt an island the client OWNS. A foreign one would come
         * back redacted rather than corrected (SERVER_AUTHORITY.md
         * Phase 3), which is a different property and has its own
         * test — asserting equality on somebody else's books here
         * would be asserting that concealment does not work. */
        for (i = 0; i < MAX_ISLANDS; i++)
            if (gg->islands[i].owner == 2u) { mine = i; break; }
        if (mine < 0) { printf("  FAIL: the client owns nothing\n"); return 1; }

        before = sim_hash(gg);

        /* Now vandalise the client's world. Not a plausible drift — a
         * blatant one, of the kind a modified client or a memory error
         * would produce. Under lockstep this was a fatal desync needing
         * a resync handshake. Here it should simply not survive. */
        gg->islands[mine].stockpile.amount[RES_PLANKS] += 99999;
        gg->islands[mine].stockpile.amount[RES_GOLD]   += 555555;
        CHECK(sim_hash(gg) != before, "the client's world is corrupted");
        CHECK(sim_hash(gg) != sim_hash(hg),
              "and disagrees with the server");

        /* One push interval is enough. */
        for (i = 0; i < 40; i++) step(hn, hg, gn, gg, 1);

        CHECK(gg->islands[mine].stockpile.amount[RES_PLANKS] ==
              hg->islands[mine].stockpile.amount[RES_PLANKS],
              "the pushed state overwrites what the client invented");
        CHECK(gg->islands[mine].stockpile.amount[RES_GOLD] ==
              hg->islands[mine].stockpile.amount[RES_GOLD],
              "all of it, not just the field somebody thought to check");
    }

    printf("\n=== and put right again, repeatedly ===\n");
    {
        int corrected = 0;

        /* Once could be luck — a single push that happened to land
         * after the damage. Do it repeatedly at different offsets
         * within the push cadence, because a correction that only
         * works when the damage falls in the right part of the cycle
         * is a correction that works in tests and not in play. */
        for (i = 0; i < 6; i++) {
            int j;

            for (j = 0; j < i + 1; j++) step(hn, hg, gn, gg, 1);
            gg->islands[mine].stockpile.amount[RES_FISH] += 1234 + i;
            for (j = 0; j < 40; j++) step(hn, hg, gn, gg, 1);

            if (gg->islands[mine].stockpile.amount[RES_FISH] ==
                hg->islands[mine].stockpile.amount[RES_FISH]) corrected++;
        }
        CHECK(corrected == 6,
              "wherever in the cadence the damage falls, it is undone");
    }

    printf("\n=== the client is allowed to be ahead ===\n");
    {
        /* Prediction means the client may hold a tick the server has
         * not reached. That is not an error and nothing should be
         * trying to prevent it — the old lockstep gate would have. */
        int ran = 0;

        for (i = 0; i < 20; i++) {
            net_pump(hn, hg);
            net_pump(gn, gg);
            if (net_tick_allowed(gn, gg->sim_tick_no)) {
                sim_run_one_tick(gg);
                net_on_tick(gn, gg);
                ran++;
            }
            net_after_update(hn, hg);
            net_after_update(gn, gg);
        }
        CHECK(ran == 20, "the client simulated every tick it wanted to");
        CHECK(gg->sim_tick_no > hg->sim_tick_no,
              "and got ahead of the server, which is prediction working");

        /* And the next push still lands it back on the server's tick. */
        for (i = 0; i < 40; i++) step(hn, hg, gn, gg, 1);
        CHECK(gg->sim_tick_no == hg->sim_tick_no,
              "until the server tells it where it really is");
    }

    printf("\n=== commands still travel ===\n");
    {
        /* Authority is not much use if the client cannot act. A
         * command still goes up, is ordered by the server, and comes
         * back in the state it pushes. */
        int settled_before = 0, settled_after = 0, k;

        for (k = 0; k < MAX_ISLANDS; k++)
            if (gg->islands[k].owner == 2u) settled_before++;
        CHECK(settled_before > 0,
              "the joining player was granted an island through the log");

        for (k = 0; k < MAX_ISLANDS; k++)
            if (hg->islands[k].owner == 2u) settled_after++;
        CHECK(settled_after == settled_before,
              "and the server agrees about which one");
    }

    net_close(hn);
    net_close(gn);
    game_free(hg);
    game_free(gg);

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
