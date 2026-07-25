/*  test_vitals.c  --  the alert strip, the stores overlay and the
 *                     overlay arbiter (UI_PLAN Phase 4)
 *
 * Linked WITHOUT SDL, against libsaltmarch_ui and libsaltmarch_sim.
 *
 * The plan's named verification is the last group here: a synthetic
 * snapshot with a stalled tick accumulator must produce a health row.
 * That one matters more than it looks — it is the difference between a
 * player noticing a stalled simulation in seconds and noticing it at
 * the next desync.
 */

#include "vitals.h"
#include "inventory_view.h"
#include "game.h"
#include "building.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* A settled island with nothing wrong with it. */
static void healthy(UiSnapshot *s)
{
    UiIsland *isl;

    memset(s, 0, sizeof(*s));
    s->health.feed_age_s    = -1;
    s->health.net_connected = -1;

    isl = &s->islands[0];
    isl->settled  = 1;
    isl->capacity = 200;
    isl->stock[0] = 10;

    /* One connected, staffed house so "no houses" stays quiet. */
    isl->building_count           = 1;
    isl->buildings[0].type        = BUILDING_HOUSE;
    isl->buildings[0].active      = 1;
    isl->buildings[0].connected   = 1;
    isl->buildings[0].residents   = 5;
    isl->buildings[0].happy       = 1;
    isl->buildings[0].worker_count= 1;
}

static int has_text(const VitalsView *v, const char *needle)
{
    int i;
    for (i = 0; i < v->row_count; i++)
        if (strstr(v->rows[i].text, needle)) return 1;
    return 0;
}

/* ---- 1. quiet when all is well --------------------------- */
static void test_quiet(void)
{
    UiSnapshot s;
    VitalsView v;

    healthy(&s);
    vitals_build(&v, &s, 0);
    CHECK(v.row_count == 0, "a healthy island raises nothing");

    /* An unsettled island is not a broken one — looking at a colony
     * you have not founded must not fill the strip with complaints. */
    memset(&s, 0, sizeof(s));
    s.health.feed_age_s = -1;
    s.health.net_connected = -1;
    vitals_build(&v, &s, 0);
    CHECK(v.row_count == 0, "an unsettled island raises nothing either");
}

/* ---- 2. the island rules --------------------------------- */
static void test_island_rules(void)
{
    UiSnapshot s;
    VitalsView v;

    healthy(&s);
    s.islands[0].building_count      = 3;
    s.islands[0].buildings[1].type   = BUILDING_FARM;
    s.islands[0].buildings[1].active = 1;
    s.islands[0].buildings[1].connected = 0;   /* no road */
    s.islands[0].buildings[1].worker_count = 1;
    s.islands[0].buildings[2].type   = BUILDING_LUMBERJACK;
    s.islands[0].buildings[2].active = 1;
    s.islands[0].buildings[2].connected = 1;
    s.islands[0].buildings[2].worker_count = 0;  /* nobody working */
    vitals_build(&v, &s, 0);

    CHECK(has_text(&v, "not connected"), "a disconnected building is reported");
    CHECK(has_text(&v, "without workers"), "an unstaffed producer is reported");

    /* A road with no road connection is not a fault, and neither is a
     * Marketplace with no worker — it has none by design. */
    healthy(&s);
    s.islands[0].building_count      = 3;
    s.islands[0].buildings[1].type   = BUILDING_ROAD;
    s.islands[0].buildings[1].active = 1;
    s.islands[0].buildings[2].type   = BUILDING_MARKETPLACE;
    s.islands[0].buildings[2].active = 1;
    s.islands[0].buildings[2].connected = 1;
    vitals_build(&v, &s, 0);
    CHECK(!has_text(&v, "not connected"),
          "roads and warehouses are exempt from the road rule");
    CHECK(!has_text(&v, "without workers"),
          "a building that never had workers is not idle");

    healthy(&s);
    s.islands[0].buildings[0].happy = 0;
    vitals_build(&v, &s, 0);
    CHECK(has_text(&v, "hungry"), "an unhappy house is reported");

    healthy(&s);
    s.islands[0].stock[0] = s.islands[0].capacity;
    vitals_build(&v, &s, 0);
    CHECK(has_text(&v, "full"), "a full store is reported");

    healthy(&s);
    s.islands[0].building_count = 0;
    vitals_build(&v, &s, 0);
    CHECK(has_text(&v, "No houses"), "an island with no housing is reported");
}

/* ---- 3. ranking and the cap ------------------------------ */
static void test_ranking_and_cap(void)
{
    UiSnapshot s;
    VitalsView v;
    int        i, ordered = 1;

    /* Everything wrong at once: the day the strip must not overflow. */
    healthy(&s);
    s.health.replay_state  = 2;      /* desync   */
    s.health.backlog_ticks = 40;     /* stalled  */
    s.health.feed_age_s    = 3600;   /* stale    */
    s.health.net_connected = 0;
    s.islands[0].buildings[0].happy = 0;
    s.islands[0].building_count      = 3;
    s.islands[0].buildings[1].type   = BUILDING_FARM;
    s.islands[0].buildings[1].active = 1;
    s.islands[0].buildings[2].type   = BUILDING_LUMBERJACK;
    s.islands[0].buildings[2].active = 1;
    for (i = 0; i < 6; i++) s.islands[0].stock[i] = s.islands[0].capacity;

    vitals_build(&v, &s, 0);

    CHECK(v.row_count <= VITALS_MAX_ROWS,
          "the strip never exceeds its cap, however bad things get");
    for (i = 1; i < v.row_count; i++)
        if (v.rows[i].severity > v.rows[i - 1].severity) ordered = 0;
    CHECK(ordered, "rows are ranked most severe first");
    CHECK(v.rows[0].severity == VITAL_ALERT,
          "the sim's own health outranks anything happening in the world");
    CHECK(has_text(&v, "DESYNC"), "a desync is stated in as many words");
}

/* ---- 4. the plan's named check: a stalled sim ------------- */
static void test_stalled_sim(void)
{
    UiSnapshot s;
    VitalsView v;

    healthy(&s);
    s.health.backlog_ticks = 1;
    vitals_build(&v, &s, 0);
    CHECK(v.row_count == 0,
          "being between ticks is normal and says nothing");

    healthy(&s);
    s.health.backlog_ticks = 8;
    vitals_build(&v, &s, 0);
    CHECK(has_text(&v, "behind"),
          "a stalled tick accumulator raises a health row");
    CHECK(v.rows[0].severity == VITAL_ALERT,
          "...at alert severity, not buried under island chatter");

    healthy(&s);
    s.health.feed_age_s = 3600;
    vitals_build(&v, &s, 0);
    CHECK(has_text(&v, "stale"), "a feed that stopped updating is reported");

    healthy(&s);
    s.health.feed_age_s = -1;
    vitals_build(&v, &s, 0);
    CHECK(!has_text(&v, "stale"),
          "having no feed at all is not a stale feed (single player)");
}

/* ---- 5. the overlay arbiter and the wheel bug ------------- */
static void test_overlay_arbiter(void)
{
    GameState *gs = game_init();

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }

    CHECK(game_topmost_overlay(gs) == UI_OVERLAY_NONE &&
          !game_overlay_open(gs),
          "a fresh game has no overlay open");

    gs->world_open = 1;
    CHECK(game_topmost_overlay(gs) == UI_OVERLAY_WORLD,
          "the world map is an overlay");
    CHECK(game_overlay_open(gs),
          "...so the camera knows not to zoom under it (the wheel bug)");

    gs->trade_open = 1;
    CHECK(game_topmost_overlay(gs) == UI_OVERLAY_TRADE,
          "the trade screen sits above the world map");

    gs->menu_open = 1;
    CHECK(game_topmost_overlay(gs) == UI_OVERLAY_MENU,
          "the menu is modal over everything");

    gs->menu_open = 0;
    gs->build_confirm_open = 1;
    CHECK(game_topmost_overlay(gs) == UI_OVERLAY_BUILD_CONFIRM,
          "a confirm popup outranks the screen that opened it");

    gs->build_confirm_open = 0;
    gs->trade_open = 0;
    gs->world_open = 0;
    gs->inventory_open = 1;
    CHECK(game_topmost_overlay(gs) == UI_OVERLAY_INVENTORY,
          "the stores overlay is in the arbiter's list too");

    /* Switching island closes everything — the arbiter must agree. */
    game_set_current_island(gs, 1);
    CHECK(!game_overlay_open(gs),
          "switching island leaves no overlay behind");

    game_free(gs);
}

/* ---- 6. the stores overlay ------------------------------- */
static void test_inventory(void)
{
    UiSnapshot    s;
    InventoryView v;
    UiState       st;
    UiList        list;
    int           i, inside = 1, gold_row = -1;

    healthy(&s);
    s.ship_count = 1;
    s.ships[0].active = 1;
    s.ships[0].cargo[0] = 7;
    s.islands[0].escrow[0] = 3;

    inventory_view_build(&v, &s, 0);
    CHECK(v.row_count == RES_COUNT,
          "every good has a row here, Gold included");

    for (i = 0; i < v.row_count; i++)
        if (v.rows[i].ident == (uint16_t)RES_GOLD) gold_row = i;
    CHECK(gold_row >= 0 && v.rows[gold_row].capacity == 0,
          "Gold is uncapped — it is not stacked in a warehouse");

    for (i = 0; i < v.row_count; i++)
        if (v.rows[i].ident == 0) {
            CHECK(v.rows[i].ship_cargo == 7 && v.rows[i].escrow == 3,
                  "goods at sea and in escrow are counted, not lost");
        }

    memset(&st, 0, sizeof(st));
    inventory_build(&list, &v, &st, 1920.0f, 1080.0f);
    for (i = 0; i < list.count; i++) {
        UiRect r = list.items[i].rect;
        if (r.x < 0.0f || r.y < 0.0f ||
            r.x + r.w > 1920.0f || r.y + r.h > 1080.0f) inside = 0;
    }
    CHECK(inside, "the overlay fits on screen");

    {
        InventoryHit hit = inventory_hit(&list, &st, 5.0f, 5.0f);
        CHECK(hit.kind == INVENTORY_HIT_OUTSIDE,
              "a click outside dismisses it");
    }
}

int main(void)
{
    printf("== vitals, stores and the overlay arbiter ==\n");
    test_quiet();
    test_island_rules();
    test_ranking_and_cap();
    test_stalled_sim();
    test_overlay_arbiter();
    test_inventory();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
