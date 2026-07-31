/*  test_intent.c  --  the recorded input stream (UI_PLAN M1)
 *
 * The format decision this phase had to get right once: an intent
 * carries the sim tick its frame's snapshot was taken at. Without it a
 * replay rebuilds a different screen than the player saw and hit-tests
 * a click against prices, pages and row counts that never existed.
 *
 * So the tests here are mostly about that: the tick is recorded, it
 * survives the save format, and the harness that consumes it can tell
 * the difference between a click that still works and one whose widget
 * has moved.
 *
 * Linked WITHOUT SDL.
 */

#include "replay.h"
#include "intent.h"
#include "game.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

#define TMP "test_intent.tmp"

static void test_recording(void)
{
    GameState *gs = game_init();
    Intent     in;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 11u);

    CHECK(gs->intent_count == 0, "a fresh game has recorded nothing");

    memset(&in, 0, sizeof(in));
    in.tick = 42;
    in.x    = 100;
    in.y    = 200;
    in.kind = (uint8_t)INTENT_LEFT_CLICK;
    in.seq  = 7;
    CHECK(intent_record(gs, &in) == 1, "a click is recorded");
    CHECK(gs->intent_count == 1 && gs->intent_log[0].tick == 42,
          "and keeps the tick its frame was drawn from");

    game_free(gs);
}

static void test_round_trip(void)
{
    GameState *a = game_init();
    GameState *b = game_init();
    Intent     in;
    int        i;

    if (!a || !b) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(a, 22u);

    for (i = 0; i < 5; i++) {
        memset(&in, 0, sizeof(in));
        in.tick             = (uint64_t)(i * 10);
        in.x                = 300 + i;
        in.y                = 400 + i;
        in.kind             = (uint8_t)INTENT_LEFT_CLICK;
        in.ui.overlay       = (uint8_t)UI_OVERLAY_TRADE;
        in.ui.exchange_page = (uint16_t)i;
        in.seq              = (uint32_t)(i + 1);
        intent_record(a, &in);
    }

    CHECK(game_save(a, TMP), "saved a session with clicks in it");
    CHECK(game_load(b, TMP), "loaded it back");
    CHECK(b->intent_count == 5, "every click survived the round trip");
    CHECK(b->intent_log[3].tick == 30 && b->intent_log[3].x == 303 &&
          b->intent_log[3].ui.exchange_page == 3,
          "...with its tick, position and view state intact");
    CHECK(sim_hash(a) == sim_hash(b),
          "and the world is what it always was — intents are cargo");

    remove(TMP);
    game_free(a);
    game_free(b);
}

/* The harness end to end: record a session by clicking the real
 * exchange screen, then verify it. */
static void test_harness(void)
{
    GameState *gs = game_init();

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }

    replay_record_ui_session(gs, 777u);
    CHECK(gs->intent_count > 0, "the click-driven session recorded clicks");
    CHECK(gs->cmd_count > 0, "...and the clicks produced commands");

    {
        int i, all_have_ticks = 1;
        for (i = 0; i < gs->intent_count; i++)
            if (gs->intent_log[i].seq != 0 && gs->intent_log[i].tick == 0 &&
                i > 0) all_have_ticks = 0;
        CHECK(all_have_ticks, "each recorded click carries a frame tick");
    }

    /* The fixture must actually exercise the passages screen, and the
     * click there must actually have produced a command. A recorded
     * click that emitted nothing is skipped by the verifier — "no
     * expectation" compares equal to everything — so a session that
     * quietly stopped buying charts would go on passing while testing
     * nothing. That is the trap N3's first recording fell into, and it
     * is worth one assertion per screen the fixture claims to cover. */
    {
        int i, chart_clicks = 0, chart_orders = 0;
        for (i = 0; i < gs->intent_count; i++) {
            if (gs->intent_log[i].ui.overlay != (uint8_t)UI_OVERLAY_CHARTS)
                continue;
            chart_clicks++;
            if (gs->intent_log[i].seq != 0) chart_orders++;
        }
        CHECK(chart_clicks > 0, "the session visited the passages screen");
        CHECK(chart_orders > 0, "and bought a chart there, not just paged");
    }

    CHECK(replay_verify_ui(gs, 0) == 0,
          "replaying those clicks through the real UI reproduces them");

    /* Break the recording rather than the layout — the same failure
     * from the other side, and one a test can cause. A click moved
     * 400px to the left lands on a different widget or on none. */
    gs->intent_log[0].x -= 400;
    CHECK(replay_verify_ui(gs, 0) != 0,
          "a click that no longer lands where it did is caught");

    game_free(gs);
}

int main(void)
{
    printf("== recorded intents and the UI harness ==\n");
    test_recording();
    test_round_trip();
    test_harness();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
