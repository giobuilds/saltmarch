/*  test_ui_kit.c  --  the UI kit and the rejection vocabulary
 *                     (UI_PLAN Phase 0 and 0.5)
 *
 * Linked WITHOUT SDL — against libsaltmarch_ui and libsaltmarch_sim
 * only. That is the point of the exercise as much as the assertions
 * are: if a layout function ever reaches for a font metric or a
 * renderer, this test stops linking, and the plan's hard rule ("no
 * layout decision may consult text measurement") fails loudly instead
 * of quietly becoming untrue.
 *
 * What it checks:
 *   1. layout arithmetic — rows, splits, right-anchored clusters,
 *      measure-then-clamp, and how many rows actually fit;
 *   2. pagination, including the awkward cases (empty list, a page
 *      index left over from a longer list);
 *   3. widget ids carry identity and survive a round trip;
 *   4. hit-testing: topmost wins, disabled and header widgets are not
 *      clickable, abutting rects never both claim a point;
 *   5. every RejectReason has a distinct, non-empty string;
 *   6. building_place_check returns the RIGHT reason for each way a
 *      placement can fail — the assertion the old dead `char *reason`
 *      channel could never support.
 *
 * Built and run by tests/run.sh.
 */

#include "ui_kit.h"
#include "ui_snapshot.h"
#include "building.h"
#include "map.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

static int near(float a, float b) { return (a - b) < 0.01f && (b - a) < 0.01f; }

/* ---- 1. layout ------------------------------------------- */
static void test_layout(void)
{
    UiRect   panel = { 100.0f, 50.0f, 400.0f, 300.0f };
    UiLayout l     = ui_layout(panel, 6.0f);
    UiRect   a, b, c;

    a = ui_row(&l, 34.0f);
    b = ui_row(&l, 34.0f);

    CHECK(near(a.x, 100.0f) && near(a.y, 50.0f) && near(a.w, 400.0f) &&
          near(a.h, 34.0f), "first row sits at the top of the bounds");
    CHECK(near(b.y, 90.0f), "the next row clears the first plus the pad");
    CHECK(!ui_layout_overflowed(&l), "two rows do not overflow 300px");

    /* Rows past the bottom are still handed out, honestly positioned,
     * so a caller can measure its true height before clamping. */
    { int i; for (i = 0; i < 20; i++) (void)ui_row(&l, 34.0f); }
    CHECK(ui_layout_overflowed(&l), "overflow is reported once past the bottom");

    l = ui_layout(panel, 6.0f);
    (void)ui_row(&l, 40.0f);
    c = ui_layout_rest(&l);
    CHECK(near(c.y, 96.0f) && near(c.h, 254.0f),
          "the remainder is everything below the cursor");

    /* Equal columns: 3 across 400 with 10px gaps => 126.67 each. */
    a = ui_split_h(panel, 3, 0, 10.0f);
    b = ui_split_h(panel, 3, 1, 10.0f);
    c = ui_split_h(panel, 3, 2, 10.0f);
    CHECK(near(a.w, b.w) && near(b.w, c.w), "split columns are equal width");
    CHECK(near(c.x + c.w, panel.x + panel.w),
          "the last column ends exactly on the right edge");
    CHECK(near(b.x - (a.x + a.w), 10.0f), "the gap between columns is exact");

    a = ui_split_h(panel, 3, 3, 10.0f);
    CHECK(near(a.w, 0.0f) && near(a.h, 0.0f),
          "an out-of-range column is a zero rect, not garbage");

    /* Right-anchored cluster: index 0 is rightmost, and adding an
     * index must not move the ones already placed. */
    a = ui_col_from_right(panel, 60.0f, 8.0f, 0);
    b = ui_col_from_right(panel, 60.0f, 8.0f, 1);
    CHECK(near(a.x + a.w, panel.x + panel.w), "cluster index 0 hugs the right");
    CHECK(near(a.x - (b.x + b.w), 8.0f), "cluster columns are gapped, in order");

    /* Measured, then clamped. */
    a = ui_panel_centered(1920.0f, 1080.0f, 760.0f, 500.0f, 900.0f);
    CHECK(near(a.h, 500.0f) && near(a.x, 580.0f) && near(a.y, 290.0f),
          "a panel that fits keeps its wanted height and centres");
    b = ui_panel_centered(1920.0f, 1080.0f, 760.0f, 4000.0f, 900.0f);
    CHECK(near(b.h, 900.0f) && near(b.y, 90.0f),
          "a panel that does not fit is clamped, still centred");

    CHECK(ui_rows_that_fit(300.0f, 34.0f, 6.0f) == 7,
          "row-fit accounts for the gaps between rows");
    CHECK(ui_rows_that_fit(10.0f, 34.0f, 6.0f) == 0,
          "no rows fit in less than one row");
    CHECK(ui_rows_that_fit(-50.0f, 34.0f, 6.0f) == 0,
          "a negative height fits nothing rather than wrapping");
}

/* ---- 2. pagination --------------------------------------- */
static void test_pagination(void)
{
    UiPage p;

    p = ui_paginate(6, 7, 0);
    CHECK(p.pages == 1 && p.first == 0 && p.count == 6,
          "6 goods on a 7-row page is one page (today's trade screen)");

    p = ui_paginate(25, 7, 2);
    CHECK(p.pages == 4 && p.first == 14 && p.count == 7,
          "25 goods paginate to 4 pages; page 2 starts at item 14");

    p = ui_paginate(25, 7, 3);
    CHECK(p.count == 4, "the last page holds the remainder, not a full page");

    p = ui_paginate(25, 7, 99);
    CHECK(p.page == 3 && p.count == 4,
          "an out-of-range page clamps to the last (a shrunken list)");

    p = ui_paginate(0, 7, 0);
    CHECK(p.pages == 1 && p.count == 0,
          "an empty list still has exactly one page");

    p = ui_paginate(40, 7, -1);
    CHECK(p.page == 0, "a negative page clamps to the first");
}

/* ---- 3. identity ----------------------------------------- */
static void test_ids(void)
{
    uint32_t id = ui_id(UI_GROUP_RESOURCE, 3);

    CHECK(ui_id_group(id) == UI_GROUP_RESOURCE && ui_id_value(id) == 3,
          "an id round-trips to its group and identity");
    CHECK(ui_id(UI_GROUP_NONE, 7) == UI_ID_NONE,
          "group 0 is reserved: a zeroed widget reads as no id");
    CHECK(ui_id(UI_GROUP_RESOURCE, 3) != ui_id(UI_GROUP_BUILDING, 3),
          "the same value in different groups is a different id");
}

/* ---- 4. widget list and hit-testing ---------------------- */
static void test_list(void)
{
    UiList          l;
    UiRect          panel  = { 0.0f, 0.0f, 200.0f, 100.0f };
    UiRect          button = { 10.0f, 10.0f, 50.0f, 20.0f };
    UiRect          right  = { 60.0f, 10.0f, 50.0f, 20.0f };
    const UiWidget *w;
    int             i;

    ui_list_reset(&l);
    ui_list_push(&l, ui_id(UI_GROUP_ACTION, UI_ACTION_CLOSE), panel,
                 "panel", 0, 0);
    ui_list_push(&l, ui_id(UI_GROUP_RESOURCE, 1), button, "Sell", -1, 0);
    ui_list_push(&l, ui_id(UI_GROUP_RESOURCE, 2), right, "Buy", 10, 0);

    w = ui_list_hit(&l, 20.0f, 15.0f);
    CHECK(w && ui_id_value(w->id) == 1,
          "a later widget wins the click over the panel beneath it");
    CHECK(w && w->value == -1, "the widget carries its payload");

    /* Abutting rects: the boundary belongs to exactly one of them. */
    w = ui_list_hit(&l, 60.0f, 15.0f);
    CHECK(w && ui_id_value(w->id) == 2,
          "a point on a shared edge belongs to exactly one widget");

    w = ui_list_hit(&l, 500.0f, 500.0f);
    CHECK(w == NULL, "a click outside every widget hits nothing");

    /* Disabled widgets are drawn but not clickable, and they carry the
     * reason they are disabled. Note what "not clickable" means with a
     * background panel present: the click falls through to the PANEL,
     * not to the world behind the overlay — a greyed button still
     * swallows the click, it just does not act. */
    ui_list_push(&l, ui_id(UI_GROUP_RESOURCE, 3),
                 (UiRect){ 10.0f, 40.0f, 50.0f, 20.0f }, "Buy", 1, 0);
    ui_list_disable_last(&l, REJ_CANT_AFFORD);
    w = ui_list_find(&l, ui_id(UI_GROUP_RESOURCE, 3));
    CHECK(w && (w->flags & UI_W_DISABLED) &&
          w->reason == (uint8_t)REJ_CANT_AFFORD,
          "a disabled widget remembers why");

    w = ui_list_hit(&l, 20.0f, 45.0f);
    CHECK(w && w->id == ui_id(UI_GROUP_ACTION, UI_ACTION_CLOSE),
          "a disabled widget passes the click to the panel beneath it");

    /* With nothing underneath, disabled and header widgets are simply
     * not there as far as a click is concerned. */
    {
        UiList bare;
        ui_list_reset(&bare);
        ui_list_push(&bare, ui_id(UI_GROUP_RESOURCE, 3),
                     (UiRect){ 10.0f, 40.0f, 50.0f, 20.0f }, "Buy", 1, 0);
        ui_list_disable_last(&bare, REJ_CANT_AFFORD);
        CHECK(ui_list_hit(&bare, 20.0f, 45.0f) == NULL,
              "a disabled widget is never the answer to a hit test");

        ui_list_push(&bare, ui_id(UI_GROUP_RESOURCE, 4),
                     (UiRect){ 10.0f, 70.0f, 50.0f, 20.0f }, "Fish", 0,
                     UI_W_HEADER);
        CHECK(ui_list_hit(&bare, 20.0f, 75.0f) == NULL,
              "a header is a label, not a target");
    }

    /* Overflow is counted, never silently truncated. */
    ui_list_reset(&l);
    for (i = 0; i < UI_MAX_WIDGETS + 5; i++)
        ui_list_push(&l, ui_id(UI_GROUP_RESOURCE, (uint16_t)i), button,
                     "x", 0, 0);
    CHECK(l.count == UI_MAX_WIDGETS && l.dropped == 5,
          "a full list refuses pushes and counts them");

    /* Labels are copied, not borrowed: a builder's stack buffer is
     * gone by the time the drawer reads the list. */
    ui_list_reset(&l);
    {
        char scratch[UI_LABEL_LEN];
        memcpy(scratch, "Warehouse", 10);
        ui_list_push(&l, ui_id(UI_GROUP_BUILDING, 1), button, scratch, 0, 0);
        memset(scratch, 'X', sizeof(scratch));
    }
    CHECK(strcmp(l.items[0].label, "Warehouse") == 0,
          "labels are copied into the list, not borrowed");
}

/* ---- 4b. tooltip placement ------------------------------- */
/* The bug this exists for: the leftmost HUD slot sits 20px from the
 * window edge, so a tooltip merely centred on it hung off the screen. */
static void test_tooltip(void)
{
    UiRect screen = { 0.0f, 0.0f, 1920.0f, 1080.0f };
    UiRect tip;

    tip = ui_tooltip_rect(960.0f, 968.0f, 200.0f, 20.0f, screen);
    CHECK(near(tip.x, 860.0f) && near(tip.y, 944.0f),
          "with room on all sides, a tip is centred just above its anchor");

    /* The reported case: first HUD slot, centre x = 52. */
    tip = ui_tooltip_rect(52.0f, 968.0f, 200.0f, 20.0f, screen);
    CHECK(tip.x >= 0.0f, "a tip near the left edge is pushed back on screen");
    CHECK(near(tip.w, 200.0f), "...without being squashed to fit");

    tip = ui_tooltip_rect(1900.0f, 968.0f, 200.0f, 20.0f, screen);
    CHECK(tip.x + tip.w <= 1920.0f, "a tip near the right edge is pulled in");

    /* No room above: flip below rather than clip against the top. */
    tip = ui_tooltip_rect(960.0f, 10.0f, 200.0f, 20.0f, screen);
    CHECK(tip.y >= 0.0f && tip.y > 10.0f,
          "a tip with no room above flips below its anchor");

    /* Wider than the window: still starts on screen rather than at a
     * negative x, so at least the beginning of it is readable. */
    tip = ui_tooltip_rect(960.0f, 968.0f, 3000.0f, 20.0f, screen);
    CHECK(tip.x >= 0.0f, "an over-wide tip still starts inside the window");
}

/* ---- 4c. untrusted labels (UI_PLAN M4) ------------------- */
/* Peer names arrive as bytes from a file other people append to. */
static void test_clean_label(void)
{
    char out[16];

    ui_clean_label(out, sizeof(out), "Bosun Clegg");
    CHECK(strcmp(out, "Bosun Clegg") == 0, "an ordinary name is untouched");

    ui_clean_label(out, sizeof(out), "a\nb\tc");
    CHECK(strcmp(out, "a?b?c") == 0,
          "control characters cannot smuggle newlines into a log or a tip");

    ui_clean_label(out, sizeof(out), "wildly-too-long-name-here");
    CHECK(strlen(out) == sizeof(out) - 1 && out[sizeof(out) - 1] == '\0',
          "an over-long name is clamped and still terminated");

    ui_clean_label(out, sizeof(out), "\x01\x02\x7f");
    CHECK(strcmp(out, "???") == 0, "unprintables become question marks");

    ui_clean_label(out, sizeof(out), NULL);
    CHECK(out[0] == '\0', "a missing name is an empty one, not a crash");

    CHECK(ui_clean_label(NULL, 0, "x") == 0,
          "no output buffer is handled rather than written to");
}

/* ---- 5. the rejection vocabulary ------------------------- */
static void test_reject_text(void)
{
    int r, r2, distinct = 1, nonempty = 1;

    for (r = REJ_OK + 1; r < REJ_COUNT; r++) {
        const char *t = ui_reject_text((RejectReason)r);
        if (!t || t[0] == '\0') nonempty = 0;
        for (r2 = REJ_OK + 1; r2 < r; r2++)
            if (strcmp(t, ui_reject_text((RejectReason)r2)) == 0) distinct = 0;
    }

    CHECK(nonempty, "every rejection reason has something to say");
    CHECK(distinct, "no two reasons say the same thing");
    CHECK(ui_reject_text(REJ_OK)[0] == '\0', "REJ_OK says nothing");
    CHECK(ui_reject_text((RejectReason)9999)[0] != '\0',
          "an unknown reason still returns a string, never NULL");
}

/* ---- 6. placement reasons -------------------------------- */
static int find_tile(const Map *map, TileType type, int *out_r, int *out_c)
{
    int r, c;
    for (r = 0; r < map->rows; r++)
        for (c = 0; c < map->cols; c++)
            if (map->tiles[r][c].type == type) {
                *out_r = r; *out_c = c;
                return 1;
            }
    return 0;
}

static void test_placement_reasons(void)
{
    Map map;
    int r = 0, c = 0;

    map_init(&map, 4242u, PROFILE_TEMPERATE);

    CHECK(building_place_check(&map, BUILDING_WAREHOUSE, -1, 0) ==
          REJ_OUT_OF_BOUNDS, "off the top edge is OUT_OF_BOUNDS");
    CHECK(building_place_check(&map, BUILDING_WAREHOUSE, map.rows - 1, 0) ==
          REJ_OUT_OF_BOUNDS,
          "a footprint hanging off the bottom is OUT_OF_BOUNDS");

    if (find_tile(&map, TILE_WATER, &r, &c))
        CHECK(building_place_check(&map, BUILDING_WAREHOUSE, r, c) ==
              REJ_NOT_BUILDABLE, "water is NOT_BUILDABLE");

    /* A grass tile with no fertility is where a Farm gets told why. */
    {
        int rr, cc, found = 0;
        for (rr = 0; rr < map.rows && !found; rr++)
            for (cc = 0; cc < map.cols && !found; cc++) {
                const Tile *t = &map.tiles[rr][cc];
                if (t->buildable && t->fertility == FERTILE_NONE) {
                    CHECK(building_place_check(&map, BUILDING_FARM, rr, cc) ==
                          REJ_NEEDS_FERTILE,
                          "a farm on infertile soil is NEEDS_FERTILE");
                    found = 1;
                }
            }
        if (!found) printf("  skip: this map has no infertile buildable tile\n");
    }

    /* Naming a crop is stricter than asking for fertile soil: a tile
     * fertile for grain but not hops must say so specifically. */
    {
        int rr, cc, found = 0;
        for (rr = 0; rr < map.rows && !found; rr++)
            for (cc = 0; cc < map.cols && !found; cc++) {
                const Tile *t = &map.tiles[rr][cc];
                if (t->buildable && t->fertility != FERTILE_NONE &&
                    !(t->fertility & FERTILE_HOP)) {
                    CHECK(building_place_check(&map, BUILDING_HOP_FARM,
                                               rr, cc) == REJ_NEEDS_CROP,
                          "a hop farm on plain fertile soil is NEEDS_CROP");
                    found = 1;
                }
            }
        if (!found) printf("  skip: this map has no non-hop fertile tile\n");
    }

    /* Somewhere on a temperate island a warehouse is legal. */
    {
        int rr, cc, found = 0;
        for (rr = 0; rr < map.rows && !found; rr++)
            for (cc = 0; cc < map.cols && !found; cc++)
                if (building_place_check(&map, BUILDING_WAREHOUSE, rr, cc) ==
                    REJ_OK) found = 1;
        CHECK(found, "a legal placement returns REJ_OK");
    }

    /* The boolean wrapper and the enum must never disagree — the whole
     * reason the wrapper exists (REJ_OK is 0, so a mechanical
     * conversion would have inverted every caller). */
    {
        int rr, cc, agree = 1;
        for (rr = 0; rr < map.rows; rr += 7)
            for (cc = 0; cc < map.cols; cc += 7) {
                int  boolean = building_can_place(&map, BUILDING_FISHERS_HUT,
                                                  rr, cc);
                int  is_ok   = building_place_check(&map, BUILDING_FISHERS_HUT,
                                                    rr, cc) == REJ_OK;
                if (boolean != is_ok) agree = 0;
            }
        CHECK(agree, "building_can_place agrees with the enum everywhere");
    }
}

int main(void)
{
    printf("== ui kit (no SDL linked) ==\n");
    test_layout();
    test_pagination();
    test_ids();
    test_list();
    test_tooltip();
    test_clean_label();
    test_reject_text();
    test_placement_reasons();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
