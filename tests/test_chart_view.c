/*  test_chart_view.c  --  the passages screen (UI_PLAN N4)
 *
 * Linked WITHOUT SDL, against libsaltmarch_ui, so this drives the real
 * layout, the real fold and the real hit-test rather than a copy of them.
 *
 * Two assertions carry the phase.
 *
 * The first is the expiry clock. A chart that quietly stops working is
 * worse than no chart, so the screen counts down to the tick a passage
 * goes out of use — and this runs the world to that tick and checks the
 * sim rotates ON it, not a minute either side. The countdown and the
 * event now come from one function in sea.c; before N4 the schedule
 * existed only inside game.c's update loop, where a screen could only
 * have copied it.
 *
 * The second is retention across a rotation. When a passage retires, the
 * pair's variant-1 slot starts naming DIFFERENT WATER — so a screen
 * rebuilt from the sea alone would swap the row under the cursor and a
 * click meant for one map would buy another. The retired row stays where
 * it was, marked, and its replacement is appended below.
 *
 * Also checked:
 *   - a passage you have not learned is a row like any other, with marks
 *     where its name and numbers would be (UI_PLAN N2) — the layout does
 *     not change shape with what you know;
 *   - what it saves is measured against the pair's own patrolled lane;
 *   - a Buy click emits a CMD_PLACE_ORDER naming a ROUTE, at the price
 *     the row was displaying — the route picker N3 could not be;
 *   - the panel fits 1920x1080 and pagination reaches every row;
 *   - a foreign harbour is a screen you can read and not act on.
 *
 * Built and run by tests/run.sh.
 */

#include "chart_view.h"
#include "ui_kit.h"
#include "game.h"
#include "knowledge.h"
#include "orderbook.h"
#include "resource.h"
#include "sea.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

#define SCREEN_WF ((float)SCREEN_W)
#define SCREEN_HF ((float)SCREEN_H)

static float cx(UiRect r) { return r.x + r.w * 0.5f; }
static float cy(UiRect r) { return r.y + r.h * 0.5f; }

/* The snapshot is a hundred kilobytes or so; one static copy rather than
 * one per test function, because a 1 MB default stack is a real platform
 * (Windows) and this test would be the thing that found out. */
static UiSnapshot SNAP;
static ChartView  VIEW;

static void run_ticks(GameState *gs, int n)
{
    int i;
    for (i = 0; i < n; i++) sim_run_one_tick(gs);
}

static void refresh(GameState *gs, ChartView *v, int island)
{
    ui_snapshot_build(&SNAP, gs);
    chart_view_update(v, &SNAP, &gs->sea, island);
}

static int row_of_route(const ChartView *v, int route_id)
{
    int i;
    for (i = 0; i < v->row_count; i++)
        if (!v->rows[i].header && v->rows[i].route_id == route_id) return i;
    return -1;
}

/* ---- 1. every destination, every route --------------------- */
static void test_rows(void)
{
    GameState *gs = game_init();
    int        i, headers = 0, routes = 0, ordered = 1, last_island = -1;

    printf("\n=== the passages out of a harbour ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);

    chart_view_reset(&VIEW);
    refresh(gs, &VIEW, 0);

    for (i = 0; i < VIEW.row_count; i++) {
        const ChartRow *r = &VIEW.rows[i];
        if (r->header) headers++; else routes++;
        if (r->island < last_island) ordered = 0;
        last_island = r->island;
        if (r->island == 0) ordered = 0;      /* never a route to itself */
    }

    CHECK(headers == gs->sea.island_count - 1,
          "one heading per destination in the sea");
    CHECK(routes == headers * SEA_ROUTES_PER_PAIR,
          "and three routes under each — one lane and two passages");
    CHECK(ordered, "grouped by destination, in a fixed order");

    /* Exactly one of the three is the patrolled lane. */
    {
        int lanes = 0, privates = 0;
        for (i = 0; i < VIEW.row_count; i++) {
            if (VIEW.rows[i].header) continue;
            if (VIEW.rows[i].is_private) privates++; else lanes++;
        }
        CHECK(lanes == headers && privates == headers * 2,
              "one lane and two private passages per destination");
    }

    game_free(gs);
}

/* ---- 2. absence, and what it does NOT hide ----------------- */
static void test_absence(void)
{
    GameState *gs = game_init();
    int        at = -1, i, rid = -1;
    int        before_count;

    printf("\n=== a passage you have not learned ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);

    chart_view_reset(&VIEW);
    refresh(gs, &VIEW, 0);

    for (i = 0; i < VIEW.row_count && rid < 0; i++)
        if (!VIEW.rows[i].header && VIEW.rows[i].is_private) {
            rid = VIEW.rows[i].route_id;
            at  = i;
        }
    CHECK(rid >= 0, "the world has a private passage to look at");
    if (rid < 0) { game_free(gs); return; }

    CHECK(!VIEW.rows[at].known,
          "a fresh player has not learned it");
    CHECK(VIEW.rows[at].charts == 0, "and holds no map of it");
    before_count = VIEW.row_count;

    /* The row exists either way. That is the N2 rule: mark rather than
     * hide, so hit-testing and pagination do not grow a second shape
     * that only unknown passages ever take. */
    knowledge_add_charts(&gs->knowledge, gs->local_player_id, rid, 2);
    refresh(gs, &VIEW, 0);

    CHECK(VIEW.row_count == before_count && row_of_route(&VIEW, rid) == at,
          "learning it neither adds a row nor moves one");
    CHECK(VIEW.rows[at].known && VIEW.rows[at].charts == 2,
          "it is simply known now, with two maps in hand");

    /* The lane is known to everyone by definition — that is what public
     * means, and the screen must not mark it as a secret. */
    for (i = 0; i < VIEW.row_count; i++)
        if (!VIEW.rows[i].header && !VIEW.rows[i].is_private) {
            CHECK(VIEW.rows[i].known,
                  "the patrolled lane is known without being learned");
            break;
        }

    game_free(gs);
}

/* ---- 3. what a passage saves ------------------------------- */
static void test_saves(void)
{
    GameState *gs = game_init();
    int        i, checked = 0, all_faster = 1, all_measured = 1;

    printf("\n=== what a passage saves ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);

    chart_view_reset(&VIEW);
    refresh(gs, &VIEW, 0);

    for (i = 0; i < VIEW.row_count; i++) {
        const ChartRow *r = &VIEW.rows[i];
        const Route    *lane;

        if (r->header || !r->is_private) continue;
        lane = sea_route_variant(&gs->sea, 0, r->island, SEA_ROUTE_PUBLIC);
        if (!lane) continue;

        if (r->ticks >= lane->total_ticks) all_faster = 0;
        if (r->saves != (int32_t)(lane->total_ticks - r->ticks))
            all_measured = 0;
        checked++;
    }

    CHECK(checked > 0, "there are private passages to measure");
    CHECK(all_faster,
          "every one of them beats the lane it is an alternative to");
    CHECK(all_measured,
          "and the saving is measured against that pair's own lane");

    game_free(gs);
}

/* ---- 4. the clock agrees with the sim ---------------------- */
static void test_expiry_and_retention(void)
{
    GameState *gs = game_init();
    int        pair, at, rid = -1, i;
    uint64_t   expires;
    uint8_t    cursor_before;

    printf("\n=== the expiry clock, and the row that outlives it ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);

    chart_view_reset(&VIEW);
    refresh(gs, &VIEW, 0);

    /* The passage at the cursor — variant 1 — is the one that goes at
     * the next rotation. */
    for (i = 0; i < VIEW.row_count && rid < 0; i++)
        if (!VIEW.rows[i].header && VIEW.rows[i].variant == 1) {
            rid = VIEW.rows[i].route_id;
            at  = i;
        }
    if (rid < 0) { printf("  FAIL: no live private passage\n"); failures++;
                   game_free(gs); return; }

    pair    = sea_pair_index(&gs->sea, 0, VIEW.rows[at].island);
    expires = VIEW.rows[at].expires_tick;
    CHECK(expires > 0, "a private passage says when it goes out of use");
    CHECK(VIEW.rows[at].expires_tick <
          VIEW.rows[at + 1].expires_tick,
          "and the second one outlives the first by a full lifetime");

    /* Hold maps of it, so the rotation has something to void. */
    knowledge_add_charts(&gs->knowledge, gs->local_player_id, rid, 3);
    cursor_before = gs->sea.pair_cursor[pair];

    /* Right up to the last tick before the clock runs out. */
    while (gs->sim_tick_no + 1 < expires) sim_run_one_tick(gs);
    refresh(gs, &VIEW, 0);

    CHECK(gs->sea.pair_cursor[pair] == cursor_before,
          "the pair has not rotated a tick early");
    CHECK(!VIEW.rows[at].gone && VIEW.rows[at].charts == 3,
          "the passage is still in use, and the maps are still good");

    /* And over it. */
    run_ticks(gs, 2);
    refresh(gs, &VIEW, 0);

    CHECK(gs->sea.pair_cursor[pair] != cursor_before,
          "the pair rotates on the tick the screen counted down to");
    CHECK(VIEW.rows[at].route_id == rid && VIEW.rows[at].gone,
          "the retired passage keeps its row, marked out of use");
    CHECK(VIEW.rows[at].charts == 0,
          "and the maps of it are waste paper, which the row now says");

    /* The replacement joined its own destination rather than the bottom
     * of the panel, and nothing that was live moved. */
    {
        int dest = VIEW.rows[at].island;
        int live_here = 0, misplaced = 0;

        for (i = 0; i < VIEW.row_count; i++) {
            if (VIEW.rows[i].header || VIEW.rows[i].island != dest) continue;
            if (!VIEW.rows[i].gone) live_here++;
        }
        for (i = 1; i < VIEW.row_count; i++)
            if (VIEW.rows[i].island < VIEW.rows[i - 1].island) misplaced = 1;

        CHECK(live_here == SEA_ROUTES_PER_PAIR,
              "three passages are in play again");
        CHECK(VIEW.row_count == (gs->sea.island_count - 1) *
                                (1 + SEA_ROUTES_PER_PAIR) + 1,
              "the view grew by exactly the one that replaced it");
        CHECK(!misplaced, "and the destinations are still in order");
    }

    /* Closing the panel is what forgets it. */
    chart_view_reset(&VIEW);
    refresh(gs, &VIEW, 0);
    CHECK(row_of_route(&VIEW, rid) < 0,
          "reopening shows only the water that is actually in use");

    game_free(gs);
}

/* ---- 5. a Buy names a route, at the price shown ------------ */
static void test_buy_emits_a_chart_order(void)
{
    GameState      *gs = game_init();
    UiState         st;
    UiList          list;
    const UiWidget *buy = NULL;
    ChartHit        hit;
    Command         c;
    int             i;

    printf("\n=== buying a map of somewhere ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    memset(&st, 0, sizeof(st));

    /* The market re-quotes every hundred ticks and puts a few maps on
     * the counter, rotating which. */
    run_ticks(gs, 120);
    chart_view_reset(&VIEW);
    refresh(gs, &VIEW, 0);
    chart_build(&list, &VIEW, &st, SCREEN_WF, SCREEN_HF);

    for (i = 0; i < list.count; i++)
        if (ui_id_group(list.items[i].id) == UI_GROUP_CHART_BUY &&
            !(list.items[i].flags & UI_W_DISABLED)) {
            buy = &list.items[i];
            break;
        }

    /* If the market is offering nothing this test asserts nothing, which
     * is the trap N3's first recorded fixture fell into. Fail instead. */
    CHECK(buy != NULL, "the market has a map on the counter to click");
    if (!buy) { game_free(gs); return; }

    hit = chart_hit(&list, &VIEW, &st, cx(buy->rect), cy(buy->rect));
    CHECK(hit.kind == CHART_HIT_BUY, "the click reads as a purchase");
    CHECK(hit.route_id == (int32_t)ui_id_value(buy->id),
          "naming the route the row was for, not a row index");
    CHECK(hit.limit == buy->value && hit.limit > 0,
          "at the price the row was displaying");

    {
        int at = row_of_route(&VIEW, hit.route_id);
        CHECK(at >= 0 && VIEW.rows[at].ask == hit.limit,
              "which is the best resting offer on the book");
    }

    /* And that is a command the sim accepts, keyed by (kind, id) rather
     * than by a ResourceType — the whole reason a chart could not ride
     * the book's composer. */
    memset(&c, 0, sizeof(c));
    c.kind      = CMD_PLACE_ORDER;
    c.a         = 0;
    c.b         = TRADE_PACK(TRADE_ROUTE_CHART, (uint16_t)hit.route_id);
    c.c         = CHART_LOT;
    c.d         = hit.limit;
    c.player_id = gs->local_player_id;

    CHECK(TRADE_KIND_OF(c.b) == (uint16_t)TRADE_ROUTE_CHART &&
          TRADE_ID_OF(c.b)   == (uint16_t)hit.route_id,
          "the payload packs the passage, not a good");
    CHECK(sim_apply_reason(gs, &c) == REJ_OK,
          "and the sim takes the order");

    /* It crosses the market's ask, so a map arrives rather than an
     * order resting for ever. */
    run_ticks(gs, 400);
    CHECK(knowledge_knows(&gs->knowledge, gs->local_player_id,
                          hit.route_id, 1),
          "buying the map is how the passage becomes known");

    game_free(gs);
}

/* ---- 6. it fits, and every row is reachable ---------------- */
static void test_fits_on_screen(void)
{
    GameState *gs = game_init();
    UiState    st;
    UiList     list;
    int        i, inside = 1, pages, seen = 0;

    printf("\n=== the panel fits, and pagination reaches every row ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    memset(&st, 0, sizeof(st));

    chart_view_reset(&VIEW);
    refresh(gs, &VIEW, 0);

    pages = chart_page_count(&VIEW, SCREEN_HF);
    CHECK(pages >= 1, "there is at least one page");

    for (st.chart_page = 0; st.chart_page < pages; st.chart_page++) {
        chart_build(&list, &VIEW, &st, SCREEN_WF, SCREEN_HF);
        for (i = 0; i < list.count; i++) {
            UiRect r = list.items[i].rect;
            if (r.x < 0.0f || r.y < 0.0f ||
                r.x + r.w > SCREEN_WF || r.y + r.h > SCREEN_HF) inside = 0;
            if (ui_id_group(list.items[i].id) == UI_GROUP_ROUTE ||
                ui_id_group(list.items[i].id) == UI_GROUP_ISLAND) seen++;
        }
        CHECK(list.dropped == 0, "no widget is dropped for want of room");
    }
    st.chart_page = 0;

    CHECK(inside, "every widget is inside 1920x1080");
    CHECK(seen == VIEW.row_count,
          "and every row appears on exactly one page");

    game_free(gs);
}

/* ---- 7. a foreign harbour ---------------------------------- */
static void test_foreign_harbour(void)
{
    GameState      *gs = game_init();
    UiState         st;
    UiList          list;
    int             i, acted = 0, said_why = 0;

    printf("\n=== somebody else's harbour ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    memset(&st, 0, sizeof(st));

    run_ticks(gs, 120);
    ui_snapshot_build(&SNAP, gs);

    /* Look at the passages from a harbour that is not ours. */
    SNAP.islands[1].owner   = 999u;
    SNAP.islands[1].settled = 1;
    chart_view_reset(&VIEW);
    chart_view_update(&VIEW, &SNAP, &gs->sea, 1);
    chart_build(&list, &VIEW, &st, SCREEN_WF, SCREEN_HF);

    CHECK(!VIEW.yours, "it is not ours to post from");
    CHECK(VIEW.row_count > 0, "but the water is still drawn");

    for (i = 0; i < list.count; i++) {
        int g = ui_id_group(list.items[i].id);
        if (g != UI_GROUP_CHART_BUY && g != UI_GROUP_CHART_SELL) continue;
        if (!(list.items[i].flags & UI_W_DISABLED)) acted = 1;
        if (list.items[i].reason == (uint8_t)REJ_NOT_OWNER) said_why = 1;
    }
    CHECK(!acted, "every Buy and Sell is off");
    CHECK(said_why, "and says 'not your island' in the sim's own words");

    game_free(gs);
}

/* ---- 8. the lane is nobody's secret and nobody's cargo ----- */
static void test_lane_has_no_market(void)
{
    GameState *gs = game_init();
    UiState    st;
    UiList     list;
    int        i, lane_buttons = 0;

    printf("\n=== a map of the way in ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    memset(&st, 0, sizeof(st));

    chart_view_reset(&VIEW);
    refresh(gs, &VIEW, 0);
    chart_build(&list, &VIEW, &st, SCREEN_WF, SCREEN_HF);

    for (i = 0; i < list.count; i++) {
        int g   = ui_id_group(list.items[i].id);
        int rid = (int)ui_id_value(list.items[i].id);
        int at;

        if (g != UI_GROUP_CHART_BUY && g != UI_GROUP_CHART_SELL) continue;
        at = row_of_route(&VIEW, rid);
        if (at >= 0 && !VIEW.rows[at].is_private) lane_buttons++;
    }

    CHECK(lane_buttons == 0,
          "the patrolled lane is not for sale — everybody already has it");

    game_free(gs);
}

int main(void)
{
    printf("=== chart_view (UI_PLAN N4) ===\n");

    test_rows();
    test_absence();
    test_saves();
    test_expiry_and_retention();
    test_buy_emits_a_chart_order();
    test_fits_on_screen();
    test_foreign_harbour();
    test_lane_has_no_market();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
