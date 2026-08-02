/* test_fx_reject.c  --  the rejection channel (UI_PLAN M1) */

#include "fx_reject.h"
#include "game.h"
#include "building.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

static int find_place(GameState *gs, BuildingType type, int *r, int *c)
{
    Island *isl = game_cur_island(gs);
    int     rr, cc;
    for (rr = 0; rr < MAP_ROWS; rr++)
        for (cc = 0; cc < MAP_COLS; cc++)
            if (building_can_place(&isl->map, type, rr, cc)) {
                *r = rr; *c = cc;
                return 1;
            }
    return 0;
}

/* ---- 1. sequences ---------------------------------------- */
static void test_sequences(void)
{
    GameState *gs = game_init();
    uint32_t   first, second;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 100u);

    game_buy_resource(gs, RES_FISH, 1);
    first = gs->cmd_seq_last;
    game_buy_resource(gs, RES_FISH, 1);
    second = gs->cmd_seq_last;

    CHECK(first > 0 && second == first + 1,
          "every submitted command gets the next sequence number");
    CHECK(gs->cmd_log[gs->cmd_count - 1].seq == second,
          "and the number is on the command in the log");

    game_free(gs);
}

/* ---- 2. rejected flashes, accepted does not -------------- */
static void test_reject_and_accept(void)
{
    GameState *gs = game_init();
    FxReject   fx;
    int        r = 0, c = 0;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 200u);
    fx_reject_init(&fx);

    /* A placement that will succeed. */
    if (!find_place(gs, BUILDING_FISHERS_HUT, &r, &c)) {
        printf("  skip: nowhere to build\n");
        game_free(gs);
        return;
    }
    game_place_building(gs, r, c, BUILDING_FISHERS_HUT, 0);
    fx_reject_expect(&fx, gs->cmd_seq_last, fx_anchor_tile(r, c));
    sim_run_one_tick(gs);

    CHECK(fx_reject_drain(&fx, gs) == 0,
          "an accepted command raises no flash");
    CHECK(fx.pending_count == 0,
          "...but its pending entry is cleared either way");

    /* A placement in the sea: rejected, and it must say why. */
    game_place_building(gs, 0, 0, BUILDING_FARM, 0);
    fx_reject_expect(&fx, gs->cmd_seq_last, fx_anchor_tile(0, 0));
    sim_run_one_tick(gs);

    CHECK(fx_reject_drain(&fx, gs) == 1, "a rejected command raises one");
    CHECK(fx.flash_count == 1 && fx.flashes[0].text[0] != '\0',
          "the flash has something to say");
    CHECK(fx.flashes[0].reason != (uint8_t)REJ_OK,
          "and carries the sim's own reason");
    CHECK(fx.flashes[0].anchor.kind == FX_ANCHOR_TILE &&
          fx.flashes[0].anchor.row == 0 && fx.flashes[0].anchor.col == 0,
          "at the tile the click came from");

    game_free(gs);
}

/* ---- 3. silence for what is not ours --------------------- */
static void test_not_ours(void)
{
    GameState *gs = game_init();
    FxReject   fx;
    Command    c;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 300u);
    fx_reject_init(&fx);

    /* A command from another player, carrying a sequence number that
     * collides with one we are waiting for — which is normal, since
     * every client numbers from 1. */
    fx_reject_expect(&fx, 7u, fx_anchor_tile(3, 3));

    memset(&c, 0, sizeof(c));
    c.kind      = CMD_PLACE_BUILDING;
    c.a         = 0;
    c.b         = 0;      /* water: will be rejected */
    c.c         = 0;
    c.d         = (int32_t)(BUILDING_FARM * 2);
    c.seq       = 7u;
    c.player_id = 99u;    /* not us */
    c.tick      = gs->sim_tick_no;
    command_log_append(gs, &c);
    sim_run_one_tick(gs);

    CHECK(fx_reject_drain(&fx, gs) == 0,
          "another player's rejection raises nothing here");
    CHECK(fx.pending_count == 1,
          "...and does not consume our pending entry");

    /* A replayed command of our own: seq 0, because replay does not
     * go through command_submit. F9 recomputes hundreds of these. */
    memset(&c, 0, sizeof(c));
    c.kind      = CMD_PLACE_BUILDING;
    c.b         = 0;
    c.c         = 0;
    c.d         = (int32_t)(BUILDING_FARM * 2);
    c.player_id = gs->local_player_id;
    c.seq       = 0u;
    c.tick      = gs->sim_tick_no;
    command_log_append(gs, &c);
    sim_run_one_tick(gs);

    CHECK(fx_reject_drain(&fx, gs) == 0,
          "a replayed rejection is silent — no pending entry to match");

    game_free(gs);
}

/* ---- 4. latency ------------------------------------------ */
static void test_delayed_answer(void)
{
    GameState *gs = game_init();
    FxReject   fx;
    Command    c;
    int        i;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 400u);
    fx_reject_init(&fx);

    /* Stamped for a tick well in the future, as a co-op host would. */
    memset(&c, 0, sizeof(c));
    c.kind      = CMD_PLACE_BUILDING;
    c.b         = 0;
    c.c         = 0;
    c.d         = (int32_t)(BUILDING_FARM * 2);
    c.player_id = gs->local_player_id;
    c.seq       = 42u;
    c.tick      = gs->sim_tick_no + 8;
    command_log_append(gs, &c);
    fx_reject_expect(&fx, 42u, fx_anchor_tile(0, 0));

    for (i = 0; i < 6; i++) {
        sim_run_one_tick(gs);
        fx_reject_drain(&fx, gs);
    }
    CHECK(fx.flash_count == 0 && fx.pending_count == 1,
          "before the command applies, nothing is said and nothing lost");

    for (i = 0; i < 6; i++) {
        sim_run_one_tick(gs);
        fx_reject_drain(&fx, gs);
    }
    CHECK(fx.flash_count == 1,
          "the answer arrives whenever it arrives, at the right anchor");

    game_free(gs);
}

/* ---- 5. decay and capacity ------------------------------- */
static void test_decay(void)
{
    FxReject fx;
    int      i;

    fx_reject_init(&fx);

    for (i = 0; i < FX_MAX_PENDING + 4; i++)
        fx_reject_expect(&fx, (uint32_t)(i + 1), fx_anchor_tile(i, i));
    CHECK(fx.pending_count == FX_MAX_PENDING,
          "the pending ring is bounded");
    CHECK(fx.pending[fx.pending_count - 1].seq ==
          (uint32_t)(FX_MAX_PENDING + 4),
          "...and keeps the NEWEST, since that is the click just made");

    fx_reject_expect(&fx, 0u, fx_anchor_tile(1, 1));
    CHECK(fx.pending_count == FX_MAX_PENDING,
          "sequence 0 is not a command and is not recorded");
}

int main(void)
{
    printf("== the rejection channel (no SDL linked) ==\n");
    test_sequences();
    test_reject_and_accept();
    test_not_ours();
    test_delayed_answer();
    test_decay();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
