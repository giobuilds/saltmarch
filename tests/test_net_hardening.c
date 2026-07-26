/*  test_net_hardening.c  --  the transport's own failure modes
 *                            (SERVER.md, "Transport hardening plan")
 *
 * test_lockstep.c and test_server.c prove the protocol does the right
 * thing when everyone behaves. This file is the other half: what
 * happens when a peer misbehaves, stalls, or quietly diverges — the
 * cases an audit found unhandled, each of which was a live defect
 * rather than a hypothetical.
 *
 * Everything here runs over net_pair_mem's in-memory transport, so it
 * is deterministic in any environment and needs no sockets. The
 * timeouts (handshake, idle, send stall) deliberately do NOT apply to
 * that transport — it has no sockets to stall and no latency to wait
 * on — so they are exercised by ci/host-smoke.sh over real TCP
 * instead, where they mean something.
 *
 * What is asserted here:
 *   - the desync detector actually fires, at boundaries reached during
 *     BURSTY tick advancement, long after the hash ring has wrapped —
 *     the two bugs that between them made it near-useless;
 *   - a guest whose send fails drops the command rather than stamping
 *     it locally, because the alternative is a world quietly forked off
 *     the authoritative log;
 *   - a client cannot flood the authoritative command log, and one that
 *     keeps trying loses its connection.
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

/* One iteration of both main loops. `host_ticks` per step is the knob
 * that matters in this file: the hash boundary check used to be "did
 * this frame happen to land exactly on a multiple of
 * NET_HASH_INTERVAL", so any value that does not divide 50 evenly made
 * the host skip most of its boundaries and the guest skip a different
 * set — which is why 3 is used below and not 1. */
static void step(NetSession *hn, GameState *hg,
                 NetSession *gn, GameState *gg, int host_ticks)
{
    if (hn) net_pump(hn, hg);
    if (gn) net_pump(gn, gg);

    while (host_ticks-- > 0) { sim_run_one_tick(hg); net_on_tick(hn, hg); }
    while (gn && net_tick_allowed(gn, gg->sim_tick_no) &&
           gg->sim_tick_no < hg->sim_tick_no)
        { sim_run_one_tick(gg); net_on_tick(gn, gg); }

    if (hn) net_after_update(hn, hg);
    if (gn) net_after_update(gn, gg);
}

/* Freeze the host and let the guest run up to its clock. sim_hash
 * covers sim_tick_no, so two worlds one tick apart hash differently
 * even when they agree about everything that matters: comparing them
 * only means something once the guest has converged. */
static void converge(NetSession *hn, GameState *hg,
                     NetSession *gn, GameState *gg)
{
    int guard = 0;
    while (gg->sim_tick_no < hg->sim_tick_no && guard++ < 500)
        step(hn, hg, gn, gg, 0);
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
    GameState  *hg = game_init();
    GameState  *gg = game_init();
    NetSession *gn = NULL, *hn;
    int         i, gisl;

    if (!hg || !gg) { printf("game_init failed\n"); return 1; }
    game_new_seeded(hg, 90210u);

    hn = net_pair_mem(&gn);
    if (!hn || !gn) { printf("net_pair_mem failed\n"); return 1; }
    net_attach(hg, hn);
    net_attach(gg, gn);

    printf("=== net hardening: desync detection ===\n");

    /* Join, then run well past HASH_RING (16) boundaries — 50 ticks
     * each — in bursts of SEVEN. Both halves of the old bug live in
     * this stretch. The ring's write slot stopped advancing after the
     * sixteenth boundary, so only the newest was ever really kept; and
     * a boundary was recorded only if a burst happened to END on it, so
     * with a stride of 7 the host recorded one boundary in seven
     * (multiples of 350) and the guest, whose bursts are irregular
     * because it is catching up, a different sparse subset. Two sparse
     * subsets that rarely intersect is a detector that rarely fires. */
    for (i = 0; i < 400; i++) step(hn, hg, gn, gg, 7);

    gisl = find_owned_island(gg, 2u);
    CHECK(gisl >= 0, "guest joined and was granted an island");
    if (gisl < 0) { printf("\nFAILED\n"); return 1; }
    CHECK(hg->sim_tick_no > 20u * NET_HASH_INTERVAL,
          "ran past twenty hash boundaries (the ring has wrapped)");

    converge(hn, hg, gn, gg);
    CHECK(sim_hash(hg) == sim_hash(gg), "worlds agree before the damage");

    /* Fake a divergence: reach into the guest's world and change
     * something the hash covers, without telling the host. This is what
     * a real desync looks like from the protocol's point of view — the
     * guest believes it is fine and keeps reporting. */
    gg->islands[gisl].stockpile.amount[RES_FISH] += 777;
    CHECK(sim_hash(hg) != sim_hash(gg), "the guest's world now diverges");

    /* The host should notice at the NEXT boundary the guest reports —
     * within 50 ticks — and answer with a full world, which the guest
     * rebuilds by replay.
     *
     * The budget here is the assertion. 20 steps of 7 is 140 ticks:
     * two or three boundaries, generous for a detector that examines
     * every one of them and far too tight for the old one, which
     * recorded a boundary only when a burst happened to end exactly on
     * it — one in seven at this stride, and not the same one in seven
     * the guest reported. That detector did eventually fire, hundreds
     * of ticks late, which is why a loose budget here passed against
     * the bug and told us nothing. */
    for (i = 0; i < 20; i++) step(hn, hg, gn, gg, 7);
    converge(hn, hg, gn, gg);

    CHECK(sim_hash(hg) == sim_hash(gg),
          "the host detected the desync and resynced the guest by replay");
    CHECK(net_peer_count(hn) == 1,
          "a resync repairs the session rather than ending it");

    printf("\n=== net hardening: a guest never forks the world ===\n");

    /* Sever the host, leaving the guest with a live session whose sends
     * now fail. command_submit falls back to LOCAL stamping when the
     * session declines a submission — which on a guest would apply a
     * command the host has never seen and never will. Dropping the
     * click is the smaller loss; the fallback belongs to a session that
     * has actually been torn down. */
    {
        int before = gg->cmd_count;

        net_close(hn);
        hn = NULL;

        game_set_current_island(gg, gisl);
        game_set_docking(gg, gisl, 1);

        CHECK(gg->cmd_count == before,
              "a failed send drops the command instead of stamping it "
              "into the guest's own log");
    }

    game_free(hg);
    game_free(gg);

    printf("\n=== net hardening: the command log cannot be flooded ===\n");

    /* A fresh pair, because the one above is deliberately broken. */
    hg = game_init();
    gg = game_init();
    if (!hg || !gg) { printf("game_init failed\n"); return 1; }
    game_new_seeded(hg, 4711u);

    gn = NULL;
    hn = net_pair_mem(&gn);
    if (!hn || !gn) { printf("net_pair_mem failed\n"); return 1; }
    net_attach(hg, hn);
    net_attach(gg, gn);

    for (i = 0; i < 40; i++) step(hn, hg, gn, gg, 1);
    gisl = find_owned_island(gg, 2u);
    CHECK(gisl >= 0, "second guest joined and was granted an island");
    if (gisl < 0) { printf("\nFAILED\n"); return 1; }

    {
        int before = hg->cmd_count, accepted;

        /* 200 submissions with no tick in between, so nothing refills
         * the budget. Every one of these would otherwise be appended to
         * the log that is never truncated, replayed by every future
         * joiner, and written into every checkpoint — permanently, for
         * everyone, without exploiting anything cleverer than a loop.
         * 200 is over budget but under the point of no return, which is
         * what makes the next two checks different questions. */
        game_set_current_island(gg, gisl);
        for (i = 0; i < 200; i++) game_set_docking(gg, gisl, i & 1);

        net_pump(hn, hg);          /* the host parses the whole burst */
        accepted = hg->cmd_count - before;

        CHECK(accepted > 0, "a burst of submissions is not refused wholesale");
        CHECK(accepted <= 64,           /* CMD_BURST in net.c */
              "the host accepted only the peer's banked budget, not 200");
        CHECK(net_peer_count(hn) == 1,
              "one overrun is metered, not punished");

        /* Keep going. A peer that ignores the budget this persistently
         * is not a player with a fast mouse. */
        for (i = 0; i < 400; i++) game_set_docking(gg, gisl, i & 1);
        net_pump(hn, hg);

        CHECK(net_peer_count(hn) == 0,
              "a peer that keeps flooding loses the connection");
    }

    net_close(hn);
    net_close(gn);
    game_free(hg);
    game_free(gg);

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
