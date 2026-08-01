/*  test_headless.c  --  the headless twin (MMO_PLAN Phase 6)
 *
 * Every other test in this directory links the client's object files
 * alongside the sim and pulls in SDL. This one deliberately does not:
 * tests/run.sh builds it against libsaltmarch_sim ALONE, with no SDL
 * flags at all. If a client dependency ever creeps back into the sim,
 * this test stops linking — which is the failure mode we want, since a
 * server that cannot link the sim is Phase 6 undone.
 *
 * What it asserts beyond the link itself:
 *
 *   1. A world can be built, ticked, hashed and freed with no client.
 *   2. Same seed + same commands => same hash, in two separate worlds
 *      (the property the server's checkpoint/replay model rests on).
 *   3. The scripted fixture session round-trips through save/load, and
 *      writes byte-identical files on repeat runs (the checkpoint
 *      format has no uninitialised padding in it).
 *   4. sim_log_set_enabled(0) silences the sim — a server ticking ten
 *      times a second must be able to stop narrating.
 */

#include "game.h"
#include "replay.h"
#include "resource.h"
#include "simlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

#define TMP_A "test_headless_a.tmp"
#define TMP_B "test_headless_b.tmp"

/* Read a whole file into memory; returns NULL (and 0 in *n) on failure. */
static unsigned char *slurp(const char *path, size_t *n)
{
    FILE          *f = fopen(path, "rb");
    unsigned char *buf;
    long           size;

    *n = 0;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (unsigned char *)malloc((size_t)size);
    if (!buf || fread(buf, (size_t)size, 1, f) != 1) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *n = (size_t)size;
    return buf;
}

int main(void)
{
    GameState     *a, *b;
    uint64_t       ha, hb;
    unsigned char *fa, *fb;
    size_t         na, nb;
    int            quiet_worked;

    printf("== headless sim (no SDL linked) ==\n");

    a = game_init();
    b = game_init();
    if (!a || !b) { printf("  FAIL: game_init\n"); return 1; }

    /* 1 + 2: two independent worlds, same seed, same scripted session. */
    {
        int ea = replay_record_demo_session(a, 4242u);
        int eb = replay_record_demo_session(b, 4242u);

        /* THE FIXTURE MUST EXERCISE THE ECONOMY, and this assertion is
         * the only thing that keeps it honest.
         *
         * It has silently covered nothing twice: once paying for its
         * house in goods the island did not have, and once placing a
         * house on an island with NO WAREHOUSE — connectivity is seeded
         * from warehouses, so pop_update took its "no road" branch for
         * the fixture's whole life and three phases of needs work moved
         * the recorded hash not at all. A hash agreeing with itself
         * about a world where nothing happens is not a determinism
         * gate; it is a very slow way of comparing two zeroes. */
        CHECK(ea && eb,
              "the fixture fed somebody — house connected, food eaten, "
              "happiness off the floor");
    }

    CHECK(a->sim_tick_no == 2000 && b->sim_tick_no == 2000,
          "the fixture session ran its ticks with no client attached");
    CHECK(a->cmd_count > 0, "the session went through the command funnel");

    ha = sim_hash(a);
    hb = sim_hash(b);
    CHECK(ha == hb, "same seed and same commands produce the same hash");

    CHECK(game_verify_determinism(a),
          "the world equals a replay of its own log");

    /* 3: the checkpoint format round-trips and is reproducible. */
    CHECK(game_save(a, TMP_A), "saved the world as (seed, log)");
    CHECK(game_save(b, TMP_B), "saved the twin");

    fa = slurp(TMP_A, &na);
    fb = slurp(TMP_B, &nb);
    CHECK(fa && fb && na == nb && na > 0 && memcmp(fa, fb, na) == 0,
          "two recordings of the same session are byte-identical");
    free(fa);
    free(fb);

    game_free(b);
    b = game_init();
    if (!b) { printf("  FAIL: game_init\n"); return 1; }

    CHECK(game_load(b, TMP_A), "loaded it back into a fresh world");
    CHECK(b->sim_tick_no == a->sim_tick_no, "replayed to the saved tick");
    CHECK(sim_hash(b) == ha, "the loaded world hashes identically");

    /* Ticking on past the load stays in step: this is exactly what a
     * server does after restoring a checkpoint. */
    {
        int i;
        for (i = 0; i < 100; i++) { sim_run_one_tick(a); sim_run_one_tick(b); }
    }
    CHECK(sim_hash(a) == sim_hash(b),
          "100 further ticks either side of a checkpoint stay identical");

    /* 4: the sim can be told to shut up. */
    quiet_worked = sim_log_set_enabled(0);
    sim_log("this line must not appear in the test output");
    sim_log_set_enabled(quiet_worked);
    CHECK(quiet_worked == 1,
          "sim_log_set_enabled reported the previous (on) state");

    game_free(a);
    game_free(b);
    remove(TMP_A);
    remove(TMP_B);

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
