/*  test_people.c  --  the people overlay (LIFE_PLAN Phase 9)
 *
 * The screen is read-only, so there is no command to assert against.
 * What there is to get wrong is what it SHOWS:
 *
 *   - all six factors are listed, including the two nothing backs;
 *   - the cast is bounded by the snapshot's, never by the panel;
 *   - an island you were never told about shows no numbers at all;
 *   - the only click that does anything closes it.
 *
 * Linked against the sim and the UI library: no SDL.
 */

#include "people_view.h"
#include "game.h"
#include "island.h"
#include "calendar.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }     \
        else         { printf("  ok:   %s\n", (msg)); }                 \
    } while (0)

static UiSnapshot SNAP;
static PeopleView VIEW;
static UiList     LIST;

static void build(int island)
{
    people_view_build(&VIEW, &SNAP, island);
    people_build(&LIST, &VIEW, (float)SCREEN_W, (float)SCREEN_H);
}

/* How many widgets of a group the list holds. */
static int count_group(int group)
{
    int i, n = 0;
    for (i = 0; i < LIST.count; i++)
        if (ui_id_group(LIST.items[i].id) == group) n++;
    return n;
}

/* A settled island with `people` in `houses`, and a cast of `cast`. */
static void a_village(int houses, int per_house, int cast)
{
    UiIsland *isl;
    int       i;

    memset(&SNAP, 0, sizeof(SNAP));
    isl = &SNAP.islands[0];

    memcpy(isl->name, "Saltford", sizeof("Saltford"));
    isl->settled      = 1;
    isl->detail_known = 1;

    for (i = 0; i < houses; i++) {
        isl->buildings[i].active    = 1;
        isl->buildings[i].type      = BUILDING_HOUSE;
        isl->buildings[i].residents = (uint8_t)per_house;
        isl->buildings[i].happiness = HAPPINESS_MAX;
    }
    isl->building_count = houses;
    isl->residents      = houses * per_house;

    for (i = 0; i < cast && i < UI_CAST_MAX; i++) {
        UiResident *r = &isl->cast[i];
        r->id           = (uint32_t)(i + 1);
        r->age_years    = 30 + i;
        r->home_idx     = i % (houses > 0 ? houses : 1);
        r->work_idx     = (i % 3 == 0) ? -1 : 0;
        r->tenure_months = 24;
        r->productivity = PRODUCTIVITY_BASE;
        r->happiness    = HAPPINESS_MAX;
        r->married      = (uint8_t)(i % 2);
        memcpy(r->name, "Bess Cobbleworth", sizeof("Bess Cobbleworth"));
        memcpy(r->workplace, "Fisher's Hut", sizeof("Fisher's Hut"));
    }
    isl->cast_count = cast < UI_CAST_MAX ? cast : UI_CAST_MAX;
}

/* ---- 1. every factor is listed, real or not ---------------- */
static void test_all_six_factors_are_listed(void)
{
    int i;

    printf("\n=== six factors on screen, not four ===\n");

    a_village(3, 6, 4);
    build(0);

    /* Six factors plus the island's own score, which shares the group
     * one past the last of them. */
    CHECK(count_group(UI_GROUP_FACTOR) == WB_FACTOR_COUNT + 1,
          "one row per factor, and one for the total");

    for (i = 0; i < WB_FACTOR_COUNT; i++) {
        const UiWidget *w = ui_list_find(&LIST,
                                         ui_id(UI_GROUP_FACTOR, (uint16_t)i));
        if (!w) {
            printf("  FAIL: factor %d has no row\n", i);
            failures++;
            continue;
        }
        CHECK(strcmp(w->label, wellbeing_factor_name(i)) == 0,
              wellbeing_factor_name(i));
        CHECK(w->value >= 0 && w->value <= 1000,
              "and its value is a per mille of the factor");
    }

    /* The two nothing backs are listed with the rest — an omitted row
     * would read as a factor the design forgot rather than one the sim
     * has not built. */
    CHECK(ui_list_find(&LIST, ui_id(UI_GROUP_FACTOR, WB_CORRUPTION)) != NULL &&
          ui_list_find(&LIST, ui_id(UI_GROUP_FACTOR, WB_GENEROSITY)) != NULL,
          "including the two the sim does not model");
}

/* ---- 2. the score matches the projection ------------------- */
static void test_the_score_is_the_projection(void)
{
    const UiWidget *w;
    Wellbeing       direct;

    printf("\n=== and the number shown is the number computed ===\n");

    a_village(3, 6, 4);
    build(0);

    wellbeing_island(&direct, &SNAP, 0);
    w = ui_list_find(&LIST, ui_id(UI_GROUP_FACTOR, PEOPLE_FACTOR_TOTAL));

    CHECK(w != NULL, "the island's score has a row of its own");
    if (w)
        CHECK(w->value == (int32_t)(direct.score * 100.0f + 0.5f),
              "carrying wellbeing_island()'s score in hundredths");
}

/* ---- 3. the cast is bounded by the snapshot ---------------- */
static void test_the_cast_is_bounded(void)
{
    printf("\n=== a cast row each, and never more ===\n");

    a_village(3, 6, 4);
    build(0);
    CHECK(count_group(UI_GROUP_RESIDENT) == 4,
          "four in the cast, four rows");
    CHECK(VIEW.row_count == 4, "and the view agrees");

    a_village(3, 6, UI_CAST_MAX);
    build(0);
    CHECK(count_group(UI_GROUP_RESIDENT) == UI_CAST_MAX,
          "a full cast fills the panel without a pager");
    CHECK(LIST.dropped == 0, "and nothing is dropped for want of room");

    /* The sentence lives on the view, not in the widget: it is longer
     * than UI_LABEL_LEN. */
    CHECK(strlen(VIEW.rows[0].line) > 10,
          "each row carries a line a player can read");

    a_village(3, 6, 0);
    build(0);
    CHECK(count_group(UI_GROUP_RESIDENT) == 0,
          "an island with nobody named shows no rows");
}

/* ---- 3b. and empty ground is not scored -------------------- */
static void test_empty_ground_is_not_scored(void)
{
    printf("\n=== and empty ground reports no number ===\n");

    a_village(0, 0, 0);
    build(0);
    CHECK(!VIEW.scored,
          "a settled island with nobody on it is not scored");
    CHECK(VIEW.island_wb.score == 0.0f,
          "so the drawer has nothing to paint a bar from");

    a_village(2, 6, 3);
    build(0);
    CHECK(VIEW.scored, "one with people on it is");
}

/* ---- 4. absence is not a zero ------------------------------ */
static void test_unknown_shows_nothing(void)
{
    printf("\n=== and an island you were never told about ===\n");

    a_village(3, 6, 6);
    SNAP.islands[0].detail_known = 0;
    build(0);

    CHECK(VIEW.row_count == 0, "names no residents");
    CHECK(VIEW.island_wb.score == 0.0f,
          "and carries no score — a scored zero would be a confident lie");
    CHECK(count_group(UI_GROUP_FACTOR) == WB_FACTOR_COUNT + 1,
          "the rows are still laid out, for the drawer to leave blank");
}

/* ---- 5. geometry ------------------------------------------- */
static void test_it_fits_on_the_screen(void)
{
    UiRect panel;
    int    i, inside = 1, onscreen = 1;

    printf("\n=== all of it inside its own panel ===\n");

    a_village(4, 8, UI_CAST_MAX);
    build(0);

    panel = LIST.items[0].rect;

    for (i = 1; i < LIST.count; i++) {
        UiRect r = LIST.items[i].rect;
        if (r.x < panel.x || r.y < panel.y ||
            r.x + r.w > panel.x + panel.w + 0.01f ||
            r.y + r.h > panel.y + panel.h + 0.01f)
            inside = 0;
        if (r.x < 0.0f || r.y < 0.0f ||
            r.x + r.w > (float)SCREEN_W || r.y + r.h > (float)SCREEN_H)
            onscreen = 0;
    }

    CHECK(inside, "no widget escapes the panel, even with a full cast");
    CHECK(onscreen, "and none of it leaves the screen");

    /* The cast is capped at ten, so the tallest the panel can ever be is
     * the height it wants — there is no page to turn. */
    CHECK(panel.h <= PEOPLE_MAX_H + 0.01f,
          "the panel stays within its own maximum");

    /* Layout may never consult a font metric, so the sentence column is
     * budgeted rather than measured (UI_PLAN Phase 0). This asserts the
     * budget: room for PEOPLE_LINE_LEN characters of 11pt prose at a
     * pessimistic 5.6px each, before the sparkline starts. */
    {
        const UiWidget *row = ui_list_find(&LIST,
                                           ui_id(UI_GROUP_RESIDENT, 0));
        CHECK(row != NULL, "a cast row exists to measure");
        if (row) {
            float text_w = row->rect.w - PEOPLE_ROW_BAR_W
                         - PEOPLE_ROW_BAR_PAD - 6.0f;
            CHECK(text_w >= (float)PEOPLE_LINE_LEN * 5.6f,
                  "and the sentence has room before the sparkline");
        }
    }

    /* The factor bars have to clear the panel too, with their
     * percentage after them. */
    CHECK(PEOPLE_BAR_X + PEOPLE_BAR_W + 50.0f <=
          PEOPLE_W - PEOPLE_MARGIN * 2.0f,
          "and a factor's track and its percentage both fit");
}

/* ---- 6. the one click that does anything ------------------- */
static void test_only_close_closes(void)
{
    const UiWidget *close;
    PeopleHit       hit;

    printf("\n=== and the only click that does anything ===\n");

    a_village(3, 6, 5);
    build(0);

    close = ui_list_find(&LIST, ui_id(UI_GROUP_ACTION, UI_ACTION_CLOSE));
    CHECK(close != NULL, "there is a Close button");
    if (!close) return;

    hit = people_hit(&LIST, close->rect.x + close->rect.w * 0.5f,
                     close->rect.y + close->rect.h * 0.5f);
    CHECK(hit.kind == PEOPLE_HIT_CLOSE, "clicking it closes the screen");

    hit = people_hit(&LIST, 4.0f, 4.0f);
    CHECK(hit.kind == PEOPLE_HIT_OUTSIDE,
          "clicking off the panel closes it too");

    /* A factor row and a cast row are headers: the panel absorbs the
     * click rather than either of them acting on it. */
    {
        const UiWidget *row = ui_list_find(&LIST,
                                           ui_id(UI_GROUP_RESIDENT, 0));
        CHECK(row != NULL, "a cast row is on screen");
        if (row) {
            hit = people_hit(&LIST, row->rect.x + 20.0f,
                             row->rect.y + row->rect.h * 0.5f);
            CHECK(hit.kind == PEOPLE_HIT_NONE,
                  "and clicking a resident does nothing — this screen "
                  "reads, it does not act");
        }
    }
}

/* ---- 7. on a real island ----------------------------------- */
static void test_on_a_real_island(void)
{
    GameState *gs = game_init();
    Island    *isl;
    int        t, wr, wc, laid = 0;

    printf("\n=== on a village that actually grew ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 12345u);
    isl = game_cur_island(gs);

    for (wr = 0; wr + 5 < MAP_ROWS && !laid; wr++)
        for (wc = 0; wc + 6 < MAP_COLS && !laid; wc++) {
            int k, ok = 1;
            if (!building_can_place(&isl->map, BUILDING_WAREHOUSE, wr, wc))
                continue;
            for (k = 0; k < 4; k++) {
                if (!building_can_place(&isl->map, BUILDING_ROAD, wr+2, wc+k))
                    ok = 0;
                if (!building_can_place(&isl->map, BUILDING_HOUSE, wr+3, wc+k))
                    ok = 0;
            }
            if (!ok) continue;
            game_place_building(gs, wr, wc, BUILDING_WAREHOUSE, 1);
            for (k = 0; k < 4; k++) {
                game_place_building(gs, wr+2, wc+k, BUILDING_ROAD,  1);
                game_place_building(gs, wr+3, wc+k, BUILDING_HOUSE, 1);
            }
            laid = 1;
        }
    if (!laid) { printf("  FAIL: nowhere to build\n"); failures++;
                 game_free(gs); return; }

    for (t = 0; t < 30 * 12 * (int)CALENDAR_MONTH_TICKS; t++) {
        isl->stockpile.amount[RES_FISH]      = 900;
        isl->stockpile.amount[RES_GRAIN]     = 900;
        isl->stockpile.amount[RES_OILSKINS]  = 900;
        isl->stockpile.amount[RES_MARSH_GIN] = 900;
        sim_run_one_tick(gs);
    }

    ui_snapshot_build(&SNAP, gs);
    build(gs->current_island);

    CHECK(VIEW.row_count > 0, "the screen names somebody");
    CHECK(VIEW.residents > UI_CAST_MAX,
          "on an island holding more than it can name");
    CHECK(VIEW.island_wb.score > 0.0f &&
          VIEW.island_wb.score <= WB_SCALE_MAX,
          "and scores it somewhere on the ladder");
    CHECK(LIST.dropped == 0, "with every widget laid out");

    printf("        %s: %d people, %d waiting, score %.2f\n",
           VIEW.title, VIEW.residents, VIEW.reserve,
           (double)VIEW.island_wb.score);
    if (VIEW.row_count > 0)
        printf("        \"%s\"  %.2f\n", VIEW.rows[0].line,
               (double)VIEW.rows[0].wb.score);

    game_free(gs);
}

int main(void)
{
    printf("== the people overlay (LIFE_PLAN Phase 9) ==\n");

    test_all_six_factors_are_listed();
    test_the_score_is_the_projection();
    test_the_cast_is_bounded();
    test_empty_ground_is_not_scored();
    test_unknown_shows_nothing();
    test_it_fits_on_the_screen();
    test_only_close_closes();
    test_on_a_real_island();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
