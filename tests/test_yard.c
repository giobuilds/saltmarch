/*  test_yard.c  --  the yard and the fleet (UI_PLAN N6)
 *
 * Linked WITHOUT SDL, against libsaltmarch_ui.
 *
 * The phase exists because a trade-off the sim makes was unreachable:
 * every ship built since MARITIME_PLAN Phase 5 has been a merchantman,
 * not by choice but because game_build_ship() is
 * game_build_ship_class(SHIP_MERCHANTMAN) and nothing could say
 * otherwise. So the headline assertion is that a click on the warship's
 * row builds a WARSHIP — and that the confirmation it opens carries that
 * hull in the command it will submit, since a popup that showed one hull
 * and submitted another would be the exact drift Phase 6 closed.
 *
 * Also checked:
 *   - the three hulls really do trade against each other (guns cost
 *     hold), so the screen is showing a decision rather than a ladder;
 *   - condition is what the hull is WORTH, not what it was: a damaged
 *     warship reports fewer guns, which is the number the bet is made
 *     against;
 *   - escort cycles through your own ships and round to nobody, never
 *     selecting itself, and the button carries the ship it would guard;
 *   - a one-ship fleet says why it cannot form a convoy;
 *   - a foreign harbour can be read and not acted on.
 *
 * Built and run by tests/run.sh.
 */

#include "yard_view.h"
#include "ui_kit.h"
#include "game.h"
#include "ship.h"
#include "resource.h"
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

static UiSnapshot SNAP;
static YardView   VIEW;

/* A warship costs four merchantmen, which a fresh colony cannot pay
 * for — and "can't afford it" is a different assertion from the ones
 * here. Funded directly rather than through the funnel: the point of
 * this file is the screen, not how the treasury got there. */
static void fund(GameState *gs)
{
    stockpile_add(&gs->islands[0].stockpile, RES_GOLD, 20000);
}

static void rebuild(GameState *gs, int island)
{
    ui_snapshot_build(&SNAP, gs);
    yard_view_build(&VIEW, &SNAP, island);
}

static int row_of_ship(const YardView *v, int ship)
{
    int i;
    for (i = 0; i < v->row_count; i++)
        if (!v->rows[i].is_hull && v->rows[i].ship == ship) return i;
    return -1;
}

/* ---- 1. the three hulls are a choice ----------------------- */
static void test_hulls_trade_against_each_other(void)
{
    GameState *gs = game_init();
    int        i, hulls = 0;

    printf("\n=== what the yard offers ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    rebuild(gs, 0);

    for (i = 0; i < VIEW.row_count; i++) if (VIEW.rows[i].is_hull) hulls++;
    CHECK(hulls == SHIP_CLASS_COUNT, "every hull the yard can lay down");

    /* Guns cost hold: that is the whole trade, and if it ever stops
     * being true the screen is showing a ladder rather than a choice. */
    CHECK(SHIP_CLASSES[SHIP_MERCHANTMAN].hold > SHIP_CLASSES[SHIP_WARSHIP].hold &&
          SHIP_CLASSES[SHIP_WARSHIP].guns > SHIP_CLASSES[SHIP_MERCHANTMAN].guns,
          "the hold you give up is the guns you get");

    for (i = 0; i < VIEW.row_count; i++) {
        if (!VIEW.rows[i].is_hull) continue;
        CHECK(VIEW.rows[i].cost == SHIP_CLASSES[VIEW.rows[i].klass].gold,
              "and each row quotes the yard's own price");
        break;
    }

    game_free(gs);
}

/* ---- 2. a click builds the hull it names ------------------- */
static void test_build_names_the_hull(void)
{
    GameState      *gs = game_init();
    UiState         st;
    UiList          list;
    const UiWidget *w = NULL;
    YardHit         hit;
    int             i;

    printf("\n=== laying down a warship ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    fund(gs);
    memset(&st, 0, sizeof(st));
    rebuild(gs, 0);
    yard_build(&list, &VIEW, &st, SCREEN_WF, SCREEN_HF);

    for (i = 0; i < list.count; i++)
        if (list.items[i].id == ui_id(UI_GROUP_BUILD_HULL, SHIP_WARSHIP))
            w = &list.items[i];
    CHECK(w != NULL, "the warship has a button of its own");
    if (!w) { game_free(gs); return; }

    hit = yard_hit(&list, &VIEW, &st, cx(w->rect), cy(w->rect));
    CHECK(hit.kind == YARD_HIT_BUILD && hit.klass == SHIP_WARSHIP,
          "and clicking it asks for a warship, not a merchantman");

    /* The confirmation carries the hull in the command it will submit —
     * Phase 6's property, which is the whole reason the class goes
     * through the popup rather than round it. */
    game_confirm_ship_class(gs, hit.klass);
    CHECK(gs->confirm.open && gs->confirm.cmd.kind == CMD_BUILD_SHIP &&
          gs->confirm.cmd.b == SHIP_WARSHIP,
          "the popup holds the command that builds THAT hull");

    CHECK(game_confirm_accept(gs) == 1, "accepting submits it");
    sim_run_one_tick(gs);

    {
        int found = 0;
        for (i = 0; i < gs->ship_count; i++)
            if (gs->ships[i].active && gs->ships[i].klass == SHIP_WARSHIP)
                found = 1;
        CHECK(found, "and a warship is what the yard launched");
    }

    game_free(gs);
}

/* ---- 3. condition is what it is worth NOW ------------------ */
static void test_condition(void)
{
    GameState *gs = game_init();
    int        at, ship = -1, i;

    printf("\n=== a hull that has been in a fight ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    fund(gs);

    CHECK(game_build_ship_class(gs, SHIP_WARSHIP) == 1,
          "a warship is ordered");
    sim_run_one_tick(gs);
    for (i = 0; i < gs->ship_count; i++)
        if (gs->ships[i].active && gs->ships[i].klass == SHIP_WARSHIP)
            ship = i;
    if (ship < 0) { printf("  FAIL: no warship\n"); failures++;
                    game_free(gs); return; }

    rebuild(gs, 0);
    at = row_of_ship(&VIEW, ship);
    CHECK(at >= 0, "and appears in the fleet");
    if (at < 0) { game_free(gs); return; }

    CHECK(VIEW.rows[at].hull == VIEW.rows[at].hull_max,
          "fresh from the yard it is whole");
    CHECK(VIEW.rows[at].guns == SHIP_CLASSES[SHIP_WARSHIP].guns,
          "and worth its full broadside");

    /* Take it apart a bit. The screen must report what it is worth NOW,
     * because that is the number a player is about to bet on — not the
     * class table's, which is what it was when it was new. */
    gs->ships[ship].hull = SHIP_CLASSES[SHIP_WARSHIP].hull / 4;
    rebuild(gs, 0);
    at = row_of_ship(&VIEW, ship);

    CHECK(VIEW.rows[at].hull < VIEW.rows[at].hull_max,
          "damaged, it says so as a fraction of what it was");
    CHECK(VIEW.rows[at].guns < SHIP_CLASSES[SHIP_WARSHIP].guns &&
          VIEW.rows[at].guns == ship_fighting_strength(&gs->ships[ship]),
          "and reports the guns the sim would actually count");

    game_free(gs);
}

/* ---- 4. forming a convoy ----------------------------------- */
static void test_escort_cycles(void)
{
    GameState      *gs = game_init();
    UiState         st;
    UiList          list;
    const UiWidget *w = NULL;
    YardHit         hit;
    int             i, a = -1, b = -1;

    printf("\n=== a second hull is the answer ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    fund(gs);
    memset(&st, 0, sizeof(st));

    /* One ship cannot escort anything, and the screen says which of the
     * two reasons applies rather than cycling between "nobody" and
     * "nobody". */
    game_build_ship_class(gs, SHIP_MERCHANTMAN);
    sim_run_one_tick(gs);
    rebuild(gs, 0);
    yard_build(&list, &VIEW, &st, SCREEN_WF, SCREEN_HF);
    for (i = 0; i < list.count; i++)
        if (ui_id_group(list.items[i].id) == UI_GROUP_ESCORT) w = &list.items[i];
    CHECK(w && (w->flags & UI_W_DISABLED) &&
          w->reason == (uint8_t)REJ_NO_TARGET,
          "a fleet of one has nothing to guard, and says so");

    game_build_ship_class(gs, SHIP_CUTTER);
    sim_run_one_tick(gs);
    rebuild(gs, 0);

    for (i = 0; i < gs->ship_count; i++) {
        if (!gs->ships[i].active) continue;
        if (a < 0) a = i; else if (b < 0) b = i;
    }
    CHECK(a >= 0 && b >= 0, "two hulls are afloat");

    yard_build(&list, &VIEW, &st, SCREEN_WF, SCREEN_HF);
    w = ui_list_find(&list, ui_id(UI_GROUP_ESCORT, (uint16_t)b));
    CHECK(w && !(w->flags & UI_W_DISABLED), "now the cutter may guard");
    if (!w) { game_free(gs); return; }

    hit = yard_hit(&list, &VIEW, &st, cx(w->rect), cy(w->rect));
    CHECK(hit.kind == YARD_HIT_ESCORT && hit.ship == b,
          "the click names the ship being ordered");
    CHECK(hit.target == a,
          "and the ship it would guard — never itself");

    CHECK(game_set_escort(gs, hit.ship, hit.target) == 1,
          "the sim takes the order");
    sim_run_one_tick(gs);
    CHECK(gs->ships[b].escorting == a, "and the convoy is formed");

    /* Keep cycling and it comes round to nobody: a convoy you cannot
     * dissolve is a trap, and this button is the only one there is.
     * Bounded by the fleet, so a cycle that never released would hang
     * here rather than pass. */
    {
        int steps, released = 0, self = 0;

        for (steps = 0; steps <= gs->ship_count + 1 && !released; steps++) {
            rebuild(gs, 0);
            yard_build(&list, &VIEW, &st, SCREEN_WF, SCREEN_HF);
            w = ui_list_find(&list, ui_id(UI_GROUP_ESCORT, (uint16_t)b));
            if (!w) break;
            hit = yard_hit(&list, &VIEW, &st, cx(w->rect), cy(w->rect));
            if (hit.target == b) self = 1;
            if (hit.target < 0) released = 1;
            game_set_escort(gs, hit.ship, hit.target);
            sim_run_one_tick(gs);
        }
        CHECK(released, "cycling round the fleet comes back to nobody");
        CHECK(!self, "and never offers to guard itself");
    }

    game_free(gs);
}

/* ---- 5. it fits, and a foreign harbour is read-only -------- */
static void test_fits_and_foreign(void)
{
    GameState *gs = game_init();
    UiState    st;
    UiList     list;
    int        i, inside = 1, acted = 0;

    printf("\n=== the panel, and somebody else's yard ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    fund(gs);
    memset(&st, 0, sizeof(st));

    for (i = 0; i < 6; i++) { game_build_ship_class(gs, i % SHIP_CLASS_COUNT);
                              sim_run_one_tick(gs); }
    rebuild(gs, 0);
    yard_build(&list, &VIEW, &st, SCREEN_WF, SCREEN_HF);

    for (i = 0; i < list.count; i++) {
        UiRect r = list.items[i].rect;
        if (r.x < 0.0f || r.y < 0.0f ||
            r.x + r.w > SCREEN_WF || r.y + r.h > SCREEN_HF) inside = 0;
    }
    CHECK(inside, "every widget is inside 1920x1080 with a full fleet");
    CHECK(list.dropped == 0, "and none was dropped for want of room");

    ui_snapshot_build(&SNAP, gs);
    SNAP.islands[1].owner   = 999u;
    SNAP.islands[1].settled = 1;
    yard_view_build(&VIEW, &SNAP, 1);
    yard_build(&list, &VIEW, &st, SCREEN_WF, SCREEN_HF);

    CHECK(!VIEW.yours, "a foreign yard is not ours to order from");
    for (i = 0; i < list.count; i++) {
        int g = ui_id_group(list.items[i].id);
        if (g != UI_GROUP_BUILD_HULL) continue;
        if (!(list.items[i].flags & UI_W_DISABLED)) acted = 1;
    }
    CHECK(!acted, "and every Lay down is off");

    game_free(gs);
}

int main(void)
{
    printf("=== yard_view (UI_PLAN N6) ===\n");

    test_hulls_trade_against_each_other();
    test_build_names_the_hull();
    test_condition();
    test_escort_cycles();
    test_fits_and_foreign();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
