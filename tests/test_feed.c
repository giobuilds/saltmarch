/*  test_feed.c  --  headless verification of MMO_PLAN Phase 4
 *
 * The shared feed's client half, exercised without any window or sync
 * script:
 *
 *   - feed_track_departures publishes exactly one line per departure
 *     (handshake first), including a route auto-depart, and never
 *     re-publishes a voyage it has already seen.
 *   - feed_reload parses a hand-built feed_in.jsonl: resolves peer
 *     names from handshakes, clamps hostile long names, skips our own
 *     echoed lines, counts malformed/garbage lines instead of crashing,
 *     ignores a partial trailing line, and caps the ghost list.
 *   - ghost_progress maps wall time onto 0..1 and returns -1 outside
 *     the voyage window (stale ghosts fade out).
 *   - The cosmetic boundary holds: none of it touches sim_hash.
 *
 * Built and run by tests/run.sh.
 */

#include "game.h"
#include "feed.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

#define IN_PATH "test_feed_in.tmp"

static int count_lines(const char *path)
{
    FILE *fp = fopen(path, "r");
    char  buf[1024];
    int   n = 0;
    if (!fp) return -1;
    while (fgets(buf, sizeof(buf), fp)) n++;
    fclose(fp);
    return n;
}

int main(void)
{
    remove(FEED_OUT_PATH);

    /* ---- publishing: departures become lines ---- */
    GameState *gs = game_init();
    if (!gs) { printf("game_init failed\n"); return 1; }
    game_new_seeded(gs, 99u);

    Feed fd;
    feed_init(&fd, "tester");
    CHECK(fd.my_id != 0, "feed id derived from name");

    game_build_ship(gs);
    game_ship_depart(gs, 0, 2);
    sim_run_one_tick(gs);

    uint64_t h_before = sim_hash(gs);
    feed_track_departures(&fd, gs->ships, gs->ship_count, 1000000ULL);
    CHECK(count_lines(FEED_OUT_PATH) == 2,
          "one departure publishes handshake + one voyage line");

    /* Same voyage seen again next frame: no duplicate line. */
    feed_track_departures(&fd, gs->ships, gs->ship_count, 1000500ULL);
    CHECK(count_lines(FEED_OUT_PATH) == 2, "a seen voyage is not re-published");
    CHECK(sim_hash(gs) == h_before, "publishing never touches sim state");

    /* ---- parsing: a hand-built inbound feed ---- */
    {
        FILE *fp = fopen(IN_PATH, "w");
        int   i;
        fprintf(fp, "{\"hello\":42,\"name\":\"Nadia\"}\n");
        /* peer voyage, name resolvable */
        fprintf(fp, "{\"player\":42,\"ship\":0,\"from\":0,\"to\":1,"
                    "\"departure_tick\":7,\"cargo\":[0,0,0,0,0,0,0],"
                    "\"departure_unix_ms\":5000}\n");
        /* hostile long name on a second peer */
        fprintf(fp, "{\"hello\":77,\"name\":\""
                    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"}\n");
        fprintf(fp, "{\"player\":77,\"ship\":1,\"from\":2,\"to\":3,"
                    "\"departure_tick\":0,\"cargo\":[1,2,3,4,5,6,7],"
                    "\"departure_unix_ms\":6000}\n");
        /* our own line echoed back by the merge: must be skipped */
        fprintf(fp, "{\"player\":%u,\"ship\":0,\"from\":0,\"to\":2,"
                    "\"departure_tick\":1,\"cargo\":[0,0,0,0,0,0,0],"
                    "\"departure_unix_ms\":1000000}\n", fd.my_id);
        /* garbage lines: counted, not fatal */
        fprintf(fp, "this is not json at all\n");
        fprintf(fp, "{\"player\":9,\"from\":9,\"to\":9,"
                    "\"departure_unix_ms\":1}\n");   /* bad island range */
        /* flood beyond the ghost cap */
        for (i = 0; i < FEED_MAX_GHOSTS + 10; i++)
            fprintf(fp, "{\"player\":500,\"ship\":%d,\"from\":1,\"to\":2,"
                        "\"departure_tick\":0,\"cargo\":[0,0,0,0,0,0,0],"
                        "\"departure_unix_ms\":7000}\n", i);
        /* partial trailing line (a writer mid-append): must be ignored */
        fprintf(fp, "{\"player\":42,\"ship\":3,\"from\":0,\"to\":3");
        fclose(fp);
    }

    int n = feed_reload(&fd, IN_PATH);
    CHECK(n == FEED_MAX_GHOSTS, "ghost list caps at FEED_MAX_GHOSTS");
    CHECK(strcmp(fd.ghosts[0].name, "Nadia") == 0,
          "peer name resolved from its handshake");
    CHECK(strlen(fd.ghosts[1].name) == FEED_NAME_LEN - 1,
          "hostile long name clamped");
    CHECK(fd.malformed_count == 2, "garbage lines counted, not fatal");
    {
        int i, own = 0;
        for (i = 0; i < fd.ghost_count; i++)
            if (fd.ghosts[i].player_id == fd.my_id) own = 1;
        CHECK(!own, "our own echoed lines are not ghosts");
    }
    CHECK(sim_hash(gs) == h_before, "polling never touches sim state");

    /* Missing file: zero ghosts, graceful. */
    CHECK(feed_reload(&fd, "no_such_file.jsonl") == 0 && fd.ghost_count == 0,
          "missing inbound feed degrades to zero ghosts");

    /* ---- ghost_progress windowing ---- */
    {
        GhostVoyage g = {0};
        uint64_t dur_ms = (uint64_t)(SHIP_VOYAGE_SECONDS * 1000.0f);
        g.departure_unix_ms = 10000;
        CHECK(ghost_progress(&g, 9000) < 0.0f,
              "before departure: not drawn (clock skew tolerated)");
        CHECK(ghost_progress(&g, 10000) == 0.0f, "at departure: progress 0");
        float mid = ghost_progress(&g, 10000 + dur_ms / 2);
        CHECK(mid > 0.45f && mid < 0.55f, "mid-voyage: progress ~0.5");
        CHECK(ghost_progress(&g, 10000 + dur_ms) < 0.0f,
              "after arrival: stale ghost fades out");
    }

    remove(IN_PATH);
    remove(FEED_OUT_PATH);
    game_free(gs);

    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
