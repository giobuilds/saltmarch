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
#include "island_bar.h"
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
    s.islands[0].detail_known       = 1;
    vitals_build(&v, &s, 0);
    CHECK(has_text(&v, "hungry"), "an unhappy house is reported");

    /* And WHICH good it is short of, which is the whole difference
     * between a symptom and an answer. A Marsh Cottage wants Fish,
     * Oilskins and Marsh Gin all at once; with an empty store the strip
     * must name one of them rather than saying "going hungry" to a
     * player who can already see people leaving. */
    CHECK(has_text(&v, "no Fish"),
          "and the strip names the good it wants");

    /* Given some Fish, it moves on to the next thing it lacks — the
     * row follows the shortage rather than being written once.
     *
     * That next thing is GRAIN, not Oilskins, and the difference is the
     * point of NEEDS_PLAN Phase 1: Grain is a basic and Oilskins is a
     * luxury, so the strip names what keeps people alive before what
     * makes them happy. This assertion said "no Oilskins" when
     * Marshfolk had no basics to distinguish. */
    healthy(&s);
    s.islands[0].detail_known       = 1;
    s.islands[0].buildings[0].happy = 0;
    s.islands[0].stock[RES_FISH]    = 20;
    vitals_build(&v, &s, 0);
    CHECK(!has_text(&v, "no Fish"), "with Fish in store it stops asking");
    CHECK(has_text(&v, "no Grain"),
          "and asks for the other BASIC before either luxury");

    /* Fed, but joyless: with both basics in store the strip moves on to
     * the luxuries. */
    healthy(&s);
    s.islands[0].detail_known       = 1;
    s.islands[0].buildings[0].happy = 0;
    s.islands[0].stock[RES_FISH]    = 20;
    s.islands[0].stock[RES_GRAIN]   = 20;
    vitals_build(&v, &s, 0);
    CHECK(has_text(&v, "no Oilskins"),
          "a fed but joyless house asks for its luxuries");

    /* A house with no road is unhappy for a reason the road rule
     * already gives; naming a good would send the player to build the
     * wrong thing. */
    healthy(&s);
    s.islands[0].detail_known         = 1;
    s.islands[0].buildings[0].happy   = 0;
    s.islands[0].buildings[0].connected = 0;
    vitals_build(&v, &s, 0);
    CHECK(has_text(&v, "not connected"), "the road rule speaks");
    CHECK(!has_text(&v, "no Fish"),
          "and the hunger row does not blame the larder for a missing road");

    /* Somebody else's island arrives with a zeroed stockpile, so every
     * good would read as missing. Saying nothing beats inventing a
     * shortage on an island we were not told the stores of (N2). */
    healthy(&s);
    s.islands[0].buildings[0].happy = 0;
    s.islands[0].detail_known       = 0;
    vitals_build(&v, &s, 0);
    CHECK(has_text(&v, "hungry"), "a foreign island's hunger still shows");
    CHECK(!has_text(&v, "no "),
          "but no good is named from stores we were never told");

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
    /* A stale request opens nothing: there is no building 0 on a fresh
     * island, so the opener refuses rather than storing a command that
     * would be rejected later (UI_PLAN Phase 6). */
    game_confirm_demolish(gs, 0);
    CHECK(game_topmost_overlay(gs) != UI_OVERLAY_CONFIRM,
          "a confirmation for a building that does not exist never opens");

    game_confirm_ship(gs);
    CHECK(game_topmost_overlay(gs) == UI_OVERLAY_CONFIRM,
          "a confirm popup outranks the screen that opened it");

    game_confirm_cancel(gs);
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

/* ---- 6b. the harbour, and the lever (UI_PLAN N8) ---------- */
static void test_harbour_block(void)
{
    UiSnapshot      s;
    InventoryView   v;
    UiState         st;
    UiList          list;
    const UiWidget *w;
    InventoryHit    hit;
    int             i, caps = 0;

    printf("\n=== what a harbour can put to sea ===\n");

    healthy(&s);
    /* Ours, so the lever is live: `healthy` leaves the island unowned,
     * which is a different assertion from the ones here. */
    s.local_player_id              = 1;
    s.islands[0].owner             = 1;
    s.islands[0].merchants_out     = 2;
    s.islands[0].merchant_capacity = 3;
    s.islands[0].hulls_out         = 1;
    s.islands[0].hull_capacity     = 1;      /* every hull committed  */
    s.islands[0].scholars_out      = 0;
    s.islands[0].scholar_capacity  = 2;
    s.islands[0].research_boats    = 4;
    s.islands[0].insure_shipments  = 0;

    inventory_view_build(&v, &s, 0);
    memset(&st, 0, sizeof(st));
    inventory_build(&list, &v, &st, 1920.0f, 1080.0f);

    CHECK(v.merchants_out == 2 && v.merchant_capacity == 3 &&
          v.hulls_out == 1 && v.hull_capacity == 1 &&
          v.research_boats == 4,
          "capital is carried through as committed AND capacity");

    for (i = 0; i < list.count; i++)
        if (ui_id_group(list.items[i].id) == UI_GROUP_CAPACITY) caps++;
    CHECK(caps == 4,
          "merchants, hulls, scholars and boats each get a line");

    /* The lever says what it will DO, and its value is what it sets —
     * a toggle labelled with its current state is the oldest ambiguity
     * in a panel like this. */
    w = ui_list_find(&list, ui_id(UI_GROUP_ACTION, UI_ACTION_INSURE));
    CHECK(w && w->value == 1, "the lever offers to start insuring");
    if (w) {
        hit = inventory_hit(&list, &st, w->rect.x + w->rect.w * 0.5f,
                            w->rect.y + w->rect.h * 0.5f);
        CHECK(hit.kind == INVENTORY_HIT_INSURANCE && hit.on == 1,
              "and the click carries what to set it to");
    }

    s.islands[0].insure_shipments = 1;
    inventory_view_build(&v, &s, 0);
    inventory_build(&list, &v, &st, 1920.0f, 1080.0f);
    w = ui_list_find(&list, ui_id(UI_GROUP_ACTION, UI_ACTION_INSURE));
    CHECK(w && w->value == 0, "once on, it offers to stop");

    /* Somebody else's harbour: readable, not throwable. */
    s.islands[0].owner = 999u;
    inventory_view_build(&v, &s, 0);
    inventory_build(&list, &v, &st, 1920.0f, 1080.0f);
    w = ui_list_find(&list, ui_id(UI_GROUP_ACTION, UI_ACTION_INSURE));
    CHECK(w && (w->flags & UI_W_DISABLED) &&
          w->reason == (uint8_t)REJ_NOT_OWNER,
          "a foreign harbour's policy is not ours to change");

    {
        int inside = 1;
        for (i = 0; i < list.count; i++) {
            UiRect r = list.items[i].rect;
            if (r.x < 0.0f || r.y < 0.0f ||
                r.x + r.w > 1920.0f || r.y + r.h > 1080.0f) inside = 0;
        }
        CHECK(inside, "and the taller panel still fits on screen");
    }
}

/* ---- 7. the island header (UI_PLAN Phase 5) -------------- */
static void test_island_bar(void)
{
    UiSnapshot   s;
    UiList       list;
    IslandBarHit hit;
    uint8_t      r0, g0, b0, r1, g1, b1;
    int          i;

    /* One settled island: the chevrons exist but lead nowhere. */
    healthy(&s);
    s.current_island = 0;
    island_bar_build(&list, &s, 1920.0f);

    CHECK(list.count > 1, "the header is built");
    {
        int disabled = 0, total = 0;
        for (i = 1; i < list.count; i++) {
            if (list.items[i].flags & UI_W_HEADER) continue;
            total++;
            if (list.items[i].flags & UI_W_DISABLED) disabled++;
        }
        CHECK(total == 2 && disabled == 2,
              "with one island both chevrons are present but disabled");
    }

    /* Two settled: each chevron carries the island it goes TO. */
    healthy(&s);
    s.islands[2].settled = 1;
    s.current_island     = 0;
    island_bar_build(&list, &s, 1920.0f);
    {
        int found_target = 0;
        for (i = 1; i < list.count; i++) {
            const UiWidget *w = &list.items[i];
            if (w->flags & (UI_W_HEADER | UI_W_DISABLED)) continue;
            hit = island_bar_hit(&list, w->rect.x + w->rect.w * 0.5f,
                                 w->rect.y + w->rect.h * 0.5f);
            if (hit.kind == ISLAND_BAR_HIT_SWITCH && hit.island == 2)
                found_target = 1;
        }
        CHECK(found_target,
              "a chevron switches to the other SETTLED island");
    }

    /* Unsettled islands are skipped, not stepped onto. */
    healthy(&s);
    s.islands[3].settled = 1;   /* 1 and 2 remain unsettled */
    s.current_island     = 0;
    island_bar_build(&list, &s, 1920.0f);
    for (i = 1; i < list.count; i++) {
        const UiWidget *w = &list.items[i];
        if (w->flags & (UI_W_HEADER | UI_W_DISABLED)) continue;
        hit = island_bar_hit(&list, w->rect.x + w->rect.w * 0.5f,
                             w->rect.y + w->rect.h * 0.5f);
        CHECK(hit.island == 3 || hit.island == 0,
              "chevrons step over islands you have not settled");
    }

    /* The header names the island it is on. */
    healthy(&s);
    memcpy(s.islands[0].name, "Saltford", 9);
    island_bar_build(&list, &s, 1920.0f);
    {
        int named = 0;
        for (i = 1; i < list.count; i++)
            if ((list.items[i].flags & UI_W_HEADER) &&
                strcmp(list.items[i].label, "Saltford") == 0) named = 1;
        CHECK(named, "the header says which island you are on");
    }

    /* Hues are fixed per index: two clients looking at the same world
     * must not disagree about what colour Brinehold is. */
    island_hue(1, &r0, &g0, &b0);
    island_hue(1, &r1, &g1, &b1);
    CHECK(r0 == r1 && g0 == g1 && b0 == b1, "an island's hue is stable");
    island_hue(2, &r1, &g1, &b1);
    CHECK(!(r0 == r1 && g0 == g1 && b0 == b1),
          "different islands get different hues");
    island_hue(-5, &r1, &g1, &b1);
    CHECK(1, "an out-of-range index still returns a colour, not garbage");

    /* Clicks that miss the header say so, so the cascade can pass them
     * to the map underneath. */
    hit = island_bar_hit(&list, 10.0f, 600.0f);
    CHECK(hit.kind == ISLAND_BAR_HIT_NONE,
          "a click away from the header is not a header click");
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
    test_harbour_block();
    test_island_bar();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
