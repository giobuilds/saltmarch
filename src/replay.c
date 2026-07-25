/*  replay.c  --  Headless record/replay (MMO_PLAN Phase 1d / Phase 6)
 *
 *  Moved out of main.c when the sim became a library: the harness that
 *  proves determinism must not need a window, an SDL init, or a client.
 *  Results go to stdout (a tool's output), narration to stderr through
 *  sim_log — so `saltmarch_replay ... | grep hash=` behaves.
 */

#include "replay.h"
#include "building.h"
#include "island.h"
#include "map.h"
#include "resource.h"
#include "simlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void replay_record_demo_session(GameState *gs, uint32_t seed)
{
    Island *isl;
    int     r, c, t, placed = 0;

    game_new_seeded(gs, seed);
    isl = game_cur_island(gs);

    for (r = 0; r < MAP_ROWS && !placed; r++)
        for (c = 0; c < MAP_COLS && !placed; c++)
            if (building_can_place(&isl->map, BUILDING_HOUSE, r, c)) {
                /* The explicit form: what to build, where, how paid.
                 * The emitted Command is byte-identical to what the
                 * confirm popup submits, which is what keeps recorded
                 * fixtures comparable across the UI_PLAN Phase 6
                 * rework. */
                game_place_building(gs, r, c, BUILDING_HOUSE, 0);
                placed = 1;
            }
    game_buy_resource(gs, (ResourceType)0, 8);
    game_build_ship(gs);
    game_ship_transfer(gs, 0, (ResourceType)0, 5);
    game_ship_depart(gs, 0, 1);

    for (t = 0; t < 500; t++)
        sim_run_one_tick(gs);
}

const char *replay_cli_usage(void)
{
    return "--record FILE [--seed N] | --replay FILE [--expect-hash HEX]";
}

/* Both front ends parse the same argv, so the scan lives in one place;
 * replay_cli_requested is just "did the scan find a mode". */
typedef struct {
    const char *replay_file;
    const char *record_file;
    const char *expect;
    uint32_t    seed;
} CliArgs;

static CliArgs cli_parse(int argc, char *argv[])
{
    CliArgs a;
    int     i;

    a.replay_file = NULL;
    a.record_file = NULL;
    a.expect      = NULL;
    a.seed        = 1u;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc)
            a.replay_file = argv[++i];
        else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc)
            a.record_file = argv[++i];
        else if (strcmp(argv[i], "--expect-hash") == 0 && i + 1 < argc)
            a.expect = argv[++i];
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            a.seed = (uint32_t)strtoul(argv[++i], NULL, 10);
    }
    return a;
}

int replay_cli_requested(int argc, char *argv[])
{
    CliArgs a = cli_parse(argc, argv);
    return (a.replay_file || a.record_file) ? 1 : 0;
}

int replay_cli_run(int argc, char *argv[])
{
    CliArgs    a  = cli_parse(argc, argv);
    GameState *gs;
    int        rc = 0;

    if (!a.replay_file && !a.record_file) {
        fprintf(stderr, "usage: %s\n", replay_cli_usage());
        return 1;
    }

    gs = game_init();
    if (!gs) {
        fprintf(stderr, "replay: game_init failed\n");
        return 1;
    }

    if (a.record_file) {
        replay_record_demo_session(gs, a.seed);
        if (!game_save(gs, a.record_file)) {
            rc = 1;
        } else {
            printf("record: %s seed=%u tick=%llu hash=%016llx\n",
                   a.record_file, a.seed,
                   (unsigned long long)gs->sim_tick_no,
                   (unsigned long long)sim_hash(gs));
        }
    } else if (!game_load(gs, a.replay_file)) {
        rc = 1;
    } else {
        uint64_t h = sim_hash(gs);
        printf("replay: %s tick=%llu hash=%016llx\n", a.replay_file,
               (unsigned long long)gs->sim_tick_no, (unsigned long long)h);

        /* Self-check: rebuild the world a SECOND time from seed+log and
         * confirm it lands on the same hash. This makes plain
         * `--replay <file>` a determinism gate needing no expected hash
         * — the form CI runs on every platform. */
        if (!game_verify_determinism(gs)) {
            printf("replay SELF-CHECK FAILED: world is nondeterministic\n");
            rc = 1;
        }

        /* Optional pin to a known hash (e.g. a committed fixture's
         * cross-platform value). */
        if (rc == 0 && a.expect) {
            uint64_t want = (uint64_t)strtoull(a.expect, NULL, 16);
            if (want != h) {
                printf("replay MISMATCH: expected %016llx got %016llx\n",
                       (unsigned long long)want, (unsigned long long)h);
                rc = 1;
            } else {
                printf("replay OK: hash matches\n");
            }
        }
    }

    game_free(gs);
    fflush(stdout);
    return rc;
}
