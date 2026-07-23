/*  test_save_replay.c  --  headless verification of MMO_PLAN Phase 1d
 *
 * The v5 save is (world seed + command log), and loading reconstructs
 * the world by replaying that log — so "loading IS the F9 test". This
 * checks that round-trip:
 *
 *   1. Seeded new game, a scripted session (a house, trades, a voyage),
 *      run forward. Record hash + tick.
 *   2. Save to a temp file, load into a SECOND world.
 *   3. Assert the loaded world reached the same tick and hashes equal,
 *      and that replay_valid is 1 (F9 works after a load, unlike the old
 *      full-state format).
 *   4. Assert game_verify_determinism() passes on the loaded world.
 *   5. Assert a garbage file is rejected without disturbing the world.
 *
 * Built and run by tests/run.sh.
 */

#include "game.h"
#include "building.h"
#include "resource.h"
#include <SDL3/SDL.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

#define TMP_PATH "test_save_replay.tmp"

static void play_session(GameState *gs, Uint32 seed)
{
    Island *isl;
    int     r, c, t, placed = 0;

    game_new_seeded(gs, seed);
    isl = game_cur_island(gs);

    for (r = 0; r < MAP_ROWS && !placed; r++)
        for (c = 0; c < MAP_COLS && !placed; c++)
            if (building_can_place(&isl->map, BUILDING_HOUSE, r, c, NULL, 0)) {
                gs->selected_building = BUILDING_HOUSE;
                gs->build_confirm_row = r;
                gs->build_confirm_col = c;
                game_place_building_confirmed(gs, 0);
                placed = 1;
            }
    gs->selected_building = BUILDING_NONE;

    game_buy_resource(gs, (ResourceType)0, 8);
    game_build_ship(gs);
    game_ship_transfer(gs, 0, (ResourceType)0, 5);
    game_ship_depart(gs, 0, 1);

    for (t = 0; t < 500; t++)
        sim_run_one_tick(gs);
}

int main(void)
{
    GameState *a = game_init();
    GameState *b = game_init();
    if (!a || !b) { printf("game_init failed\n"); return 1; }

    play_session(a, 12345u);

    uint64_t h_saved   = sim_hash(a);
    uint64_t tick_saved = a->sim_tick_no;
    int      cmds_saved = a->cmd_count;

    CHECK(game_save(a, TMP_PATH), "v5 save succeeds");

    int loaded = game_load(b, TMP_PATH);
    CHECK(loaded, "load succeeds");
    CHECK(b->sim_tick_no == tick_saved, "loaded world reached the saved tick");
    CHECK(b->cmd_count == cmds_saved, "loaded the same number of commands");
    CHECK(sim_hash(b) == h_saved, "loaded world hashes identically to saved");
    CHECK(b->replay_valid == 1, "F9 is valid after a v5 load (load is replay)");
    CHECK(game_verify_determinism(b) == 1, "F9 passes on the loaded world");

    /* A garbage file must be rejected, leaving b untouched. */
    uint64_t before = sim_hash(b);
    SDL_IOStream *io = SDL_IOFromFile(TMP_PATH, "wb");
    if (io) { const char junk[8] = "NOTASAVE"; SDL_WriteIO(io, junk, sizeof(junk));
              SDL_CloseIO(io); }
    int bad = game_load(b, TMP_PATH);
    CHECK(bad == 0, "a non-save file is rejected");
    CHECK(sim_hash(b) == before, "rejected load leaves the world untouched");

    remove(TMP_PATH);
    game_free(a);
    game_free(b);

    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
