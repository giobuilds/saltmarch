/* test_scrub.c  --  the time-travel scrubber
 * (MMO_PLAN later phases) */

#include "game.h"
#include "scrub_view.h"
#include "building.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

int main(void)
{
    GameState *gs = game_init();
    uint64_t   mark_tick;
    uint64_t   mark_hash, live_hash;
    int        i, cmds_before;

    printf("== the time-travel scrubber ==\n");
    if (!gs) { printf("  FAIL: game_init\n"); return 1; }
    game_new_seeded(gs, 20260725u);

    /* Play a little, remember a moment, play some more. */
    game_buy_resource(gs, RES_FISH, 5);
    for (i = 0; i < 50; i++) sim_run_one_tick(gs);

    mark_tick = gs->sim_tick_no;
    mark_hash = sim_hash(gs);

    game_build_ship(gs);
    for (i = 0; i < 100; i++) sim_run_one_tick(gs);
    live_hash   = sim_hash(gs);
    cmds_before = gs->cmd_count;

    CHECK(live_hash != mark_hash, "the world moved on");

    /* Travel back. */
    game_scrub_begin(gs);
    CHECK(game_scrubbing(gs), "the scrubber is on");
    CHECK(game_scrub_max(gs) == mark_tick + 100,
          "and knows how far forward the world actually is");

    game_scrub_to(gs, mark_tick);
    CHECK(gs->sim_tick_no == mark_tick, "we are at the tick we asked for");
    CHECK(sim_hash(gs) == mark_hash,
          "and the world is EXACTLY what it was then, not an approximation");
    CHECK(gs->cmd_count == cmds_before,
          "the log survived the trip — every later command is still there");

    /* Nothing may be ordered in the past. */
    {
        int before = gs->cmd_count;
        game_buy_resource(gs, RES_FISH, 1);
        CHECK(gs->cmd_count == before,
              "a command submitted while scrubbing never reaches the log");
    }

    /* Forward again, then home. */
    game_scrub_to(gs, mark_tick + 50);
    CHECK(gs->sim_tick_no == mark_tick + 50, "scrubbing forward works too");

    game_scrub_end(gs);
    CHECK(!game_scrubbing(gs), "the scrubber is off");
    CHECK(gs->sim_tick_no == mark_tick + 100 && sim_hash(gs) == live_hash,
          "and the world is back exactly where it was left");

    /* Asking for a future tick clamps to now rather than inventing one. */
    game_scrub_begin(gs);
    game_scrub_to(gs, mark_tick + 100000);
    CHECK(gs->sim_tick_no == mark_tick + 100,
          "the future is not scrubbable");
    game_scrub_end(gs);

    /* Submissions work again once we are back. */
    {
        int before = gs->cmd_count;
        game_buy_resource(gs, RES_FISH, 1);
        CHECK(gs->cmd_count == before + 1,
              "and the world takes orders again");
    }

    /* ---- the widget ------------------------------------- */
    {
        UiList   list;
        ScrubHit hit;
        int      j, inside = 1;

        scrub_build(&list, 500, 0, 1000, 1920.0f, 1080.0f);
        for (j = 0; j < list.count; j++) {
            UiRect r = list.items[j].rect;
            if (r.x < 0.0f || r.y < 0.0f ||
                r.x + r.w > 1920.0f || r.y + r.h > 1080.0f) inside = 0;
        }
        CHECK(inside, "the bar fits on screen");

        /* Clicking the middle of the track asks for the middle tick. */
        {
            const UiWidget *track = ui_list_find(&list,
                                        ui_id(UI_GROUP_ACTION, UI_ACTION_PREV));
            CHECK(track != NULL, "the track is there to click");
            if (track) {
                hit = scrub_hit(&list, 0, 1000,
                                track->rect.x + track->rect.w * 0.5f,
                                track->rect.y + track->rect.h * 0.5f);
                CHECK(hit.kind == SCRUB_HIT_SEEK &&
                      hit.tick > 450 && hit.tick < 550,
                      "clicking halfway asks for halfway, in ticks not pixels");

                hit = scrub_hit(&list, 0, 1000, track->rect.x + 1.0f,
                                track->rect.y + track->rect.h * 0.5f);
                CHECK(hit.kind == SCRUB_HIT_SEEK && hit.tick < 20,
                      "and the left end is the beginning");
            }
        }

        {
            const UiWidget *live = ui_list_find(&list,
                                       ui_id(UI_GROUP_ACTION, UI_ACTION_ACCEPT));
            CHECK(live != NULL, "there is a way back to now");
            if (live) {
                hit = scrub_hit(&list, 0, 1000, live->rect.x + 4.0f,
                                live->rect.y + 4.0f);
                CHECK(hit.kind == SCRUB_HIT_LIVE, "and it says so");
            }
        }

        /* A world with no history yet does not divide by zero. */
        scrub_build(&list, 0, 0, 0, 1920.0f, 1080.0f);
        CHECK(list.count > 0, "a world at tick zero still draws a bar");
    }

    game_free(gs);

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
