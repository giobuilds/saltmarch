/* test_snapshot.c  --  the full-state snapshot
 * (SERVER.md, "Log truncation") */

#include "game.h"
#include "building.h"
#include "island.h"
#include "resource.h"
#include "snapshot.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* A world with something in it. A snapshot of an empty island proves
 * almost nothing: the interesting state is buildings mid-production
 * cycle, houses with residents, agents part-way along a path, and a
 * ship at sea -- every one of which is a field that has to survive. */
static GameState *build_busy_world(uint32_t seed, int ticks)
{
    GameState *gs = game_init();
    Island    *isl;
    int        r, c, t, houses = 0, jobs = 0;

    if (!gs) return NULL;
    game_new_seeded(gs, seed);
    isl = game_cur_island(gs);

    /* Fund it properly: the point is a busy island, not a study of
     * bankruptcy (an island whose charter lapses goes dormant, and a
     * dormant world is a weak test). */
    isl->stockpile.amount[RES_GOLD] = 5000000;

    /* Order matters, and it is the whole reason this helper exists. */
    for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++)
            if (building_can_place(&isl->map, BUILDING_WAREHOUSE, r, c)) {
                game_place_building(gs, r, c, BUILDING_WAREHOUSE, 1);
                r = MAP_ROWS; break;
            }

    /* Workplaces as well as homes. Without somewhere to work every */
    for (r = 0; r < MAP_ROWS && jobs < 12; r += 3)
        for (c = 0; c < MAP_COLS && jobs < 12; c += 3)
            if (building_can_place(&isl->map, BUILDING_LUMBERJACK, r, c)) {
                game_place_building(gs, r, c, BUILDING_LUMBERJACK, 1);
                jobs++;
            }

    for (r = 0; r < MAP_ROWS && houses < 40; r += 2)
        for (c = 0; c < MAP_COLS && houses < 40; c += 2)
            if (building_can_place(&isl->map, BUILDING_HOUSE, r, c)) {
                game_place_building(gs, r, c, BUILDING_HOUSE, 1);
                houses++;
            }

    for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++)
            if (building_can_place(&isl->map, BUILDING_ROAD, r, c))
                game_place_building(gs, r, c, BUILDING_ROAD, 1);

    /* Goods to keep the houses fed, and a ship at sea so a voyage is
     * mid-flight when the snapshot is taken. */
    game_buy_resource(gs, RES_FISH, 400);
    game_buy_resource(gs, RES_GRAIN, 400);
    game_build_ship(gs);
    game_ship_transfer(gs, 0, RES_FISH, 20);
    game_ship_depart(gs, 0, 1);

    for (t = 0; t < ticks; t++) {
        /* Keep the charter paid so the island stays held for the whole
         * run; charter lapse is its own test's business, not this one's. */
        isl = &gs->islands[0];
        if (isl->stockpile.amount[RES_GOLD] < 100000)
            isl->stockpile.amount[RES_GOLD] = 5000000;
        if (isl->stockpile.amount[RES_FISH] < 50)
            game_buy_resource(gs, RES_FISH, 200);
        if (isl->stockpile.amount[RES_GRAIN] < 50)
            game_buy_resource(gs, RES_GRAIN, 200);
        sim_run_one_tick(gs);
    }
    return gs;
}

static void describe(const GameState *gs, const char *label)
{
    const Island *isl = &gs->islands[0];
    int i, agents = 0, residents = 0, pathing = 0;

    for (i = 0; i < isl->agent_count; i++) {
        if (!isl->agents[i].active) continue;
        agents++;
        if (isl->agents[i].path_len > 0) pathing++;
    }
    for (i = 0; i < isl->building_count; i++)
        residents += isl->pop_data[i].residents;

    printf("  (%s: tick %llu, %d buildings, %d agents (%d walking), "
           "%d residents, %d ships)\n",
           label, (unsigned long long)gs->sim_tick_no,
           isl->building_count, agents, pathing, residents, gs->ship_count);
}

int main(void)
{
    GameState     *a, *b;
    unsigned char *buf = NULL;
    size_t         len = 0;
    uint64_t       hash_at_snapshot, peeked = 0;
    int            i;

    printf("=== snapshot: capture and restore ===\n");

    a = build_busy_world(20260726u, 600);
    if (!a) { printf("game_init failed\n"); return 1; }
    describe(a, "captured");

    /* The world must actually be busy, or everything below is a test of
     * an empty island agreeing with another empty island. */
    {
        const Island *isl = &a->islands[0];
        int agents = 0, walking = 0, k;
        for (k = 0; k < isl->agent_count; k++) {
            if (!isl->agents[k].active) continue;
            agents++;
            if (isl->agents[k].path_len > 0) walking++;
        }
        CHECK(isl->building_count > 10, "the captured world has buildings");
        CHECK(agents > 0, "the captured world has live agents");
        CHECK(walking > 0,
              "some of them are mid-walk (path[] is actually exercised)");
        CHECK(a->ship_count > 0, "the captured world has a ship");
    }

    hash_at_snapshot = sim_hash(a);

    CHECK(snapshot_encode(a, &buf, &len), "the world encodes");
    if (!buf) { printf("\nFAILED\n"); return 1; }
    printf("  (snapshot is %zu bytes; the same world as a log would be "
           "%d commands)\n", len, a->cmd_count);

    CHECK(snapshot_peek_tick(buf, len, &peeked) && peeked == a->sim_tick_no,
          "the tick can be read without decoding the whole buffer");

    /* ---- restore ---- */
    b = game_init();
    if (!b) { printf("game_init failed\n"); return 1; }
    CHECK(snapshot_decode(b, buf, len), "the snapshot decodes into a world");
    CHECK(b->sim_tick_no == a->sim_tick_no, "restored at the captured tick");
    CHECK(sim_hash(b) == hash_at_snapshot,
          "the restored world hashes identically at the instant of loading");

    /* ---- THE assertion: it stays identical ---- */
    for (i = 0; i < 600; i++) {
        /* Both worlds get the same outside interference, or the
         * comparison is between different experiments. */
        a->islands[0].stockpile.amount[RES_GOLD] =
            b->islands[0].stockpile.amount[RES_GOLD];
        sim_run_one_tick(a);
        sim_run_one_tick(b);
    }
    describe(a, "original, 600 ticks on");
    describe(b, "restored, 600 ticks on");

    CHECK(a->sim_tick_no == b->sim_tick_no, "both ran the same distance");
    CHECK(sim_hash(a) == sim_hash(b),
          "restored world still hashes identically 600 ticks later");

    printf("\n=== snapshot: a bad buffer is refused, not half-applied ===\n");

    {
        GameState *c = game_init();
        uint64_t   before;
        unsigned char *tampered = (unsigned char *)malloc(len);

        if (!c || !tampered) { printf("out of memory\n"); return 1; }
        game_new_seeded(c, 999u);
        before = sim_hash(c);

        CHECK(!snapshot_decode(c, buf, 32),
              "a truncated snapshot is refused");
        CHECK(sim_hash(c) == before,
              "...and the world it was offered to is untouched");

        /* Flip a byte deep in the payload, past the header. The hash the
         * snapshot carries is what catches this; without it the world
         * would load happily and desync from everyone else's later. */
        memcpy(tampered, buf, len);
        tampered[len / 2] ^= 0xFFu;
        CHECK(!snapshot_decode(c, tampered, len),
              "a corrupted snapshot is caught by its own hash");
        CHECK(sim_hash(c) == before,
              "...and that world is untouched too");

        free(tampered);
        game_free(c);
    }

    free(buf);
    game_free(b);

    printf("\n=== checkpoint: state on disk, and what it costs ===\n");

    {
        const char *path = "test_snapshot_ckpt.smlog";
        GameState  *c;
        uint64_t    tick_at_save = a->sim_tick_no, h = sim_hash(a);

        CHECK(game_save_checkpoint(a, path), "the world checkpoints to disk");

        c = game_init();
        if (!c) { printf("game_init failed\n"); return 1; }
        CHECK(game_load(c, path), "the checkpoint loads");
        CHECK(c->sim_tick_no == tick_at_save && sim_hash(c) == h,
              "...to the same tick and the same hash");

        /* The cost side of the trade, asserted rather than assumed:
         * below the checkpoint there is no history, so the scrubber
         * has a floor and F9 stands down. */
        CHECK(game_scrub_min(c) == tick_at_save,
              "the loaded world knows history below it is gone");
        CHECK(c->replay_valid == 0,
              "...and does not claim it can be re-derived from a log");

        for (i = 0; i < 300; i++) {
            a->islands[0].stockpile.amount[RES_GOLD] =
                c->islands[0].stockpile.amount[RES_GOLD];
            sim_run_one_tick(a);
            sim_run_one_tick(c);
        }
        CHECK(sim_hash(a) == sim_hash(c),
              "a world resumed from a checkpoint runs on identically");

        /* Starting a fresh world must forget the floor. It is not just
         * stale — it belongs to a DIFFERENT world, and a scrubber
         * rebuilding from another world's snapshot would show that
         * world's past as this one's. */
        game_new_seeded(c, 5150u);
        CHECK(game_scrub_min(c) == 0 && c->floor_snap == NULL,
              "a new game forgets the checkpoint's history floor");
        CHECK(c->replay_valid == 1,
              "...and can be re-derived from its log again");

        remove(path);
        game_free(c);
    }

    printf("\n=== truncation: the world does not notice ===\n");

    {
        GameState *d = build_busy_world(4242u, 400);
        GameState *e = build_busy_world(4242u, 400);
        int        before;
        uint64_t   floor_tick;

        if (!d || !e) { printf("game_init failed\n"); return 1; }
        CHECK(sim_hash(d) == sim_hash(e),
              "two worlds built the same way agree to start with");

        before = e->cmd_count;
        game_truncate_log(e);
        floor_tick = e->sim_tick_no;

        CHECK(e->cmd_count < before,
              "truncation actually discarded the applied history");
        CHECK(game_scrub_min(e) == floor_tick,
              "...and left a floor where it cut");
        CHECK(sim_hash(d) == sim_hash(e),
              "the world itself is unchanged by losing its history");

        for (i = 0; i < 400; i++) {
            d->islands[0].stockpile.amount[RES_GOLD] =
                e->islands[0].stockpile.amount[RES_GOLD];
            sim_run_one_tick(d);
            sim_run_one_tick(e);
        }
        CHECK(sim_hash(d) == sim_hash(e),
              "a truncated world keeps running identically to one that "
              "kept everything");

        /* The scrubber still works ABOVE the floor, and refuses to
         * invent a past below it. Rebuilding from a seed whose history
         * had been discarded would not fail -- it would confidently
         * show a world in which none of it ever happened. */
        {
            uint64_t live = e->sim_tick_no;

            game_scrub_begin(e);
            game_scrub_to(e, floor_tick + 100);
            CHECK(e->sim_tick_no == floor_tick + 100,
                  "the scrubber reaches a tick inside the retained window");

            game_scrub_to(e, floor_tick > 50 ? floor_tick - 50 : 0);
            CHECK(e->sim_tick_no == floor_tick,
                  "...and clamps at the floor instead of fabricating a past");

            game_scrub_end(e);
            CHECK(e->sim_tick_no == live,
                  "and coming back to now lands where it left");
            CHECK(sim_hash(d) == sim_hash(e),
                  "...on the same world it left");
        }

        game_free(d);
        game_free(e);
    }

    game_free(a);

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
