/* test_sea_view.c  --  the sea as a place (UI_PLAN N5) */

#include "sea_view.h"
#include "game.h"
#include "knowledge.h"
#include "orderbook.h"
#include "pirate.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

#define SCREEN_WF ((float)SCREEN_W)
#define SCREEN_HF ((float)SCREEN_H)

static UiSnapshot SNAP;
static SeaView    VIEW;

static void run_ticks(GameState *gs, int n)
{
    int i;
    for (i = 0; i < n; i++) sim_run_one_tick(gs);
}

static void rebuild(GameState *gs, int island)
{
    ui_snapshot_build(&SNAP, gs);
    sea_view_build(&VIEW, &SNAP, &gs->sea, island, SCREEN_WF, SCREEN_HF);
}

static int on_screen(SeaScreen p)
{
    return p.x >= 0.0f && p.x <= SCREEN_WF && p.y >= 0.0f && p.y <= SCREEN_HF;
}

/* ---- 1. everything lands on the screen --------------------- */
static void test_projection(void)
{
    GameState *gs = game_init();
    int        i, j, bad = 0;
    SeaPos     corners[4];
    int        c;

    printf("\n=== the projection keeps the sea on screen ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    run_ticks(gs, 120);
    rebuild(gs, 0);

    for (i = 0; i < VIEW.path_count; i++)
        for (j = 0; j < VIEW.path[i].point_count; j++)
            if (!on_screen(VIEW.path[i].pt[j])) bad++;
    CHECK(bad == 0, "every point of every path is inside 1920x1080");

    bad = 0;
    for (i = 0; i < VIEW.mark_count; i++)
        if (!on_screen(VIEW.mark[i].at)) bad++;
    CHECK(bad == 0, "and every named waypoint with it");

    /* The margin exists for exactly this: an island generated at the
     * very edge of the sea still has room for its whole diamond. */
    corners[0].x = 0;          corners[0].y = 0;
    corners[1].x = SEA_WIDTH;  corners[1].y = 0;
    corners[2].x = 0;          corners[2].y = SEA_HEIGHT;
    corners[3].x = SEA_WIDTH;  corners[3].y = SEA_HEIGHT;
    bad = 0;
    for (c = 0; c < 4; c++) {
        SeaScreen s;
        sea_to_screen(corners[c], SCREEN_WF, SCREEN_HF, &s.x, &s.y);
        if (s.x < 60.0f || s.x > SCREEN_WF - 60.0f ||
            s.y < 60.0f || s.y > SCREEN_HF - 60.0f) bad++;
    }
    CHECK(bad == 0, "the corners of the sea keep a margin to draw into");

    game_free(gs);
}

/* ---- 2. a route is a path, not a line ---------------------- */
static void test_paths_thread_waypoints(void)
{
    GameState *gs = game_init();
    int        i, with_waypoints = 0, wrong = 0;

    printf("\n=== a route is drawn as the water it is ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    rebuild(gs, 0);

    CHECK(VIEW.path_count > 0, "there are routes out of the harbour");

    for (i = 0; i < VIEW.path_count; i++) {
        const SeaPath *p    = &VIEW.path[i];
        const Route   *real = &gs->sea.route[p->route_id];

        /* Harbour + its waypoints + harbour. A straight line between
         * two nodes would be two points however many waypoints the
         * route actually threads, which is what this file replaced. */
        if (p->point_count != real->waypoint_count + 2) wrong++;
        if (real->waypoint_count > 0) with_waypoints++;
    }

    CHECK(with_waypoints > 0, "and some of them go round by somewhere");
    CHECK(wrong == 0,
          "each is a harbour, its waypoints in order, and a harbour");

    /* The path ends where the route ends, not where the pair is
     * indexed: a route is a piece of water, not a direction of travel. */
    {
        int ends_ok = 1;
        for (i = 0; i < VIEW.path_count; i++) {
            const SeaPath *p = &VIEW.path[i];
            SeaScreen      a, b;
            sea_to_screen(gs->sea.island[p->from_island], SCREEN_WF, SCREEN_HF,
                          &a.x, &a.y);
            sea_to_screen(gs->sea.island[p->to_island], SCREEN_WF, SCREEN_HF,
                          &b.x, &b.y);
            if (fabsf(p->pt[0].x - a.x) > 0.01f ||
                fabsf(p->pt[p->point_count - 1].x - b.x) > 0.01f) ends_ok = 0;
        }
        CHECK(ends_ok, "and it starts and ends at the two harbours");
    }

    game_free(gs);
}

/* ---- 3. water you have never seen is not plotted ----------- */
static void test_unknown_is_not_plotted(void)
{
    GameState *gs = game_init();
    int        i, rid = -1, before, after;

    printf("\n=== a passage you have not learned ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    rebuild(gs, 0);
    before = VIEW.path_count;

    /* Every path drawn is one this player knows. */
    {
        int unknown_drawn = 0;
        for (i = 0; i < VIEW.path_count; i++)
            if (!SNAP.route_known[VIEW.path[i].route_id]) unknown_drawn++;
        CHECK(unknown_drawn == 0, "nothing unlearned is on the map");
    }

    /* The lanes are, though — public is public. */
    {
        int lanes = 0;
        for (i = 0; i < VIEW.path_count; i++)
            if (!VIEW.path[i].is_private) lanes++;
        CHECK(lanes == gs->sea.island_count - 1,
              "the patrolled lane to every destination is drawn");
    }

    /* Learn one, and it appears — which is what a chart buys. */
    for (i = 1; i < gs->sea.route_count && rid < 0; i++) {
        const Route *r = &gs->sea.route[i];
        if (!r->is_private) continue;
        if (r->from_island != 0 && r->to_island != 0) continue;
        if (sea_route_variant(&gs->sea, r->from_island, r->to_island, 1) != r &&
            sea_route_variant(&gs->sea, r->from_island, r->to_island, 2) != r)
            continue;              /* not one of the two in play */
        rid = i;
    }
    CHECK(rid >= 0, "a private passage out of here exists to learn");
    if (rid < 0) { game_free(gs); return; }

    knowledge_add_charts(&gs->knowledge, gs->local_player_id, rid, 1);
    rebuild(gs, 0);
    after = VIEW.path_count;

    CHECK(after == before + 1, "buying the map is what puts it on the map");
    for (i = 0; i < VIEW.path_count; i++)
        if (VIEW.path[i].route_id == rid)
            CHECK(VIEW.path[i].held,
                  "and it is drawn as water you can actually sail");

    game_free(gs);
}

/* ---- 4. a shipment moves along its route ------------------- */
static void test_cargo_moves(void)
{
    GameState *gs = game_init();
    Command    c;
    int        i, seen = 0, backwards = 0;
    float      last = -1.0f;
    SeaScreen  first_pos, last_pos;

    printf("\n=== a cargo is somewhere, and it gets there ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);

    /* Cross the market's ask so a booking exists and sails. */
    run_ticks(gs, 120);
    ui_snapshot_build(&SNAP, gs);
    memset(&c, 0, sizeof(c));
    c.kind      = CMD_PLACE_ORDER;
    c.a         = 0;
    c.b         = TRADE_PACK(TRADE_RESOURCE, (uint16_t)RES_FISH);
    c.c         = 2;
    c.d         = SNAP.ask[RES_FISH] > 0 ? SNAP.ask[RES_FISH] + 5 : 50;
    c.player_id = gs->local_player_id;
    CHECK(sim_apply_reason(gs, &c) == REJ_OK, "a buy crossing the ask");

    run_ticks(gs, 5);
    rebuild(gs, 0);

    for (i = 0; i < VIEW.cargo_count; i++)
        if (VIEW.cargo[i].mine) { seen = 1; first_pos = VIEW.cargo[i].at; }
    CHECK(seen, "and a shipment of ours is at sea");
    if (!seen) { game_free(gs); return; }

    /* Walk it in, sampling. The marker may not run backwards: a cargo
     * whose position jitters is one whose position is being guessed. */
    last_pos = first_pos;
    for (i = 0; i < 60; i++) {
        int j, found = 0;
        run_ticks(gs, 3);
        rebuild(gs, 0);
        for (j = 0; j < VIEW.cargo_count; j++) {
            const UiBooking *b = NULL;
            int              k;
            if (!VIEW.cargo[j].mine) continue;
            for (k = 0; k < SNAP.booking_count; k++)
                if (SNAP.booking[k].mine && !SNAP.booking[k].delivered)
                    b = &SNAP.booking[k];
            if (!b) continue;
            {
                float t = sea_cargo_progress(&SNAP, b, &gs->sea);
                if (t < last - 0.001f) backwards++;
                last = t;
            }
            last_pos = VIEW.cargo[j].at;
            found = 1;
        }
        if (!found) break;         /* it landed */
    }

    CHECK(backwards == 0, "the marker never runs backwards");
    CHECK(last_pos.x != first_pos.x || last_pos.y != first_pos.y,
          "and it is somewhere else than where it started");

    game_free(gs);
}

/* ---- 5. the lairs on the way ------------------------------- */
static void test_lairs(void)
{
    GameState *gs = game_init();
    int        i, marked = 0, active = 0;

    printf("\n=== the fleets, where they lie ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    run_ticks(gs, 20);
    rebuild(gs, 0);

    for (i = 0; i < gs->pirates.count; i++)
        if (gs->pirates.fleet[i].active) active++;
    for (i = 0; i < VIEW.mark_count; i++)
        if (VIEW.mark[i].lair) marked++;

    CHECK(VIEW.mark_count == gs->sea.waypoint_count,
          "every waypoint on the sea is named on the map");
    CHECK(marked > 0 && marked <= active,
          "and the ones a fleet lies at are marked as such");

    for (i = 0; i < VIEW.mark_count; i++)
        if (VIEW.mark[i].lair) {
            CHECK(VIEW.mark[i].guns > 0,
                  "a marked lair says what is sitting in it");
            break;
        }

    game_free(gs);
}

int main(void)
{
    printf("=== sea_view (UI_PLAN N5) ===\n");

    test_projection();
    test_paths_thread_waypoints();
    test_unknown_is_not_plotted();
    test_cargo_moves();
    test_lairs();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
