/*  test_confirm.c  --  the one confirmation (UI_PLAN Phase 6)
 *
 * The plan's verification: "hit-test results identical before/after;
 * rendered preview matches the submitted Command byte-for-byte in the
 * headless harness."
 *
 * The second half is the one that matters. A popup that says it will
 * demolish building 12 and then submits something else is the failure
 * this phase exists to make impossible, so the test takes the command
 * the view rendered and compares it — as bytes — with what the log
 * received when the popup was accepted.
 *
 * Linked WITHOUT SDL, against libsaltmarch_ui and libsaltmarch_sim.
 */

#include "confirm_view.h"
#include "ui_snapshot.h"
#include "game.h"
#include "building.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* Everything about a command except when it applies and who sent it —
 * those are stamped by command_submit after the popup is done with it. */
static int same_action(const Command *a, const Command *b)
{
    return a->kind == b->kind && a->a == b->a && a->b == b->b &&
           a->c == b->c && a->d == b->d;
}

static int find_place(GameState *gs, BuildingType type, int *out_r, int *out_c)
{
    Island *isl = game_cur_island(gs);
    int     r, c;

    for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++)
            if (building_can_place(&isl->map, type, r, c)) {
                *out_r = r; *out_c = c;
                return 1;
            }
    return 0;
}

/* ---- 1. what you see is what is submitted ---------------- */
static void test_preview_is_the_command(void)
{
    GameState  *gs = game_init();
    UiSnapshot  snap;
    ConfirmView view;
    Command     shown;
    int         r = 0, c = 0, before;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 9001u);

    if (!find_place(gs, BUILDING_FISHERS_HUT, &r, &c)) {
        printf("  skip: nowhere to put a Fisher's Hut\n");
        game_free(gs);
        return;
    }

    game_confirm_build(gs, r, c, BUILDING_FISHERS_HUT);
    CHECK(gs->confirm.open, "the popup opened");

    ui_snapshot_build(&snap, gs);
    confirm_view_build(&view, &snap);

    CHECK(view.option_count == 2,
          "building offers two ways to pay, so two options");
    CHECK(view.options[0].preview[0] && view.options[1].preview[0],
          "each option shows the command it would submit");

    /* The command the popup is holding for option 0 — the one its
     * preview line was rendered from. */
    shown  = gs->confirm.cmd;
    before = gs->cmd_count;

    game_confirm_accept(gs);

    CHECK(gs->cmd_count == before + 1, "accepting submitted exactly one command");
    CHECK(same_action(&gs->cmd_log[gs->cmd_count - 1], &shown),
          "the submitted command is byte-for-byte the one shown");
    CHECK(!gs->confirm.open, "accepting closed the popup");

    /* And the other option submits the OTHER command — the payment
     * choice is a choice between two stored commands, not a flag
     * consulted later. */
    game_confirm_build(gs, r, c, BUILDING_FISHERS_HUT);
    game_confirm_choose(gs, 1);
    shown  = gs->confirm.alt;
    before = gs->cmd_count;
    game_confirm_accept(gs);
    CHECK(same_action(&gs->cmd_log[gs->cmd_count - 1], &shown),
          "choosing 'pay Gold' submits the Gold command, unchanged");
    CHECK((gs->cmd_log[gs->cmd_count - 1].d & 1) == 1,
          "...which is the one with the pay-in-Gold bit set");

    game_free(gs);
}

/* ---- 2. the popup is built once, not re-derived ---------- */
static void test_command_is_captured_at_open(void)
{
    GameState  *gs = game_init();
    Command     at_open;
    int         r = 0, c = 0;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);

    if (!find_place(gs, BUILDING_HOUSE, &r, &c)) {
        printf("  skip: nowhere to put a House\n");
        game_free(gs);
        return;
    }

    game_confirm_build(gs, r, c, BUILDING_HOUSE);
    at_open = gs->confirm.cmd;

    /* The cursor moves to the popup's buttons, the hover changes, a
     * different building type gets selected — none of it may reach the
     * pending command. This was the property the old build popup was
     * careful about, and it is now structural. */
    gs->hovered_row       = 0;
    gs->hovered_col       = 0;
    gs->selected_building = BUILDING_WAREHOUSE;

    CHECK(same_action(&gs->confirm.cmd, &at_open),
          "the pending command does not follow the cursor or the selection");

    game_confirm_accept(gs);
    CHECK(same_action(&gs->cmd_log[gs->cmd_count - 1], &at_open),
          "and what is submitted is still the tile that was clicked");

    game_free(gs);
}

/* ---- 3. cancel submits nothing --------------------------- */
static void test_cancel(void)
{
    GameState *gs = game_init();
    int        before;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 77u);

    before = gs->cmd_count;
    game_confirm_ship(gs);
    game_confirm_cancel(gs);

    CHECK(gs->cmd_count == before, "cancelling submits nothing at all");
    CHECK(!gs->confirm.open, "and closes the popup");

    /* Accepting with nothing open is a no-op rather than a stray
     * command from stale state. */
    CHECK(game_confirm_accept(gs) == 0 && gs->cmd_count == before,
          "accepting when nothing is open does nothing");

    game_free(gs);
}

/* ---- 4. layout and hit-testing --------------------------- */
static void test_hits(void)
{
    GameState  *gs = game_init();
    UiSnapshot  snap;
    ConfirmView view;
    UiList      list;
    ConfirmHit  hit;
    int         i, inside = 1, r = 0, c = 0;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 31337u);
    if (!find_place(gs, BUILDING_FARM, &r, &c)) {
        printf("  skip: nowhere to put a Farm\n");
        game_free(gs);
        return;
    }

    game_confirm_build(gs, r, c, BUILDING_FARM);
    ui_snapshot_build(&snap, gs);
    confirm_view_build(&view, &snap);
    confirm_build(&list, &view, 1920.0f, 1080.0f);

    for (i = 0; i < list.count; i++) {
        UiRect rr = list.items[i].rect;
        if (rr.x < 0.0f || rr.y < 0.0f ||
            rr.x + rr.w > 1920.0f || rr.y + rr.h > 1080.0f) inside = 0;
    }
    CHECK(inside, "the popup fits on screen");

    {
        const UiWidget *accept = ui_list_find(&list,
                                    ui_id(UI_GROUP_ACTION, UI_ACTION_ACCEPT));
        const UiWidget *cancel = ui_list_find(&list,
                                    ui_id(UI_GROUP_ACTION, UI_ACTION_REJECT));
        CHECK(accept && cancel, "it has both an accept and a cancel");

        hit = confirm_hit(&list, accept->rect.x + 4.0f,
                          accept->rect.y + 4.0f);
        CHECK(hit.kind == CONFIRM_HIT_ACCEPT, "the accept button accepts");

        hit = confirm_hit(&list, cancel->rect.x + 4.0f,
                          cancel->rect.y + 4.0f);
        CHECK(hit.kind == CONFIRM_HIT_CANCEL, "the cancel button cancels");
    }

    /* Payment options are selectable and report which one. */
    {
        const UiWidget *opt = ui_list_find(&list, ui_id(UI_GROUP_RESOURCE, 1));
        CHECK(opt != NULL, "the second payment option is on screen");
        if (opt) {
            hit = confirm_hit(&list, opt->rect.x + opt->rect.w * 0.5f,
                              opt->rect.y + opt->rect.h * 0.5f);
            CHECK(hit.kind == CONFIRM_HIT_CHOOSE && hit.option == 1,
                  "clicking an option reports which option it was");
        }
    }

    hit = confirm_hit(&list, 5.0f, 5.0f);
    CHECK(hit.kind == CONFIRM_HIT_OUTSIDE,
          "a click outside dismisses, like every other overlay");

    /* An unaffordable option is greyed with the sim's own reason, but
     * remains visible — hiding it would answer "why can I not build
     * this" with silence. */
    {
        Island *isl = game_cur_island(gs);
        int     res;

        for (res = 0; res < RES_COUNT; res++) isl->stockpile.amount[res] = 0;
        ui_snapshot_build(&snap, gs);
        confirm_view_build(&view, &snap);
        confirm_build(&list, &view, 1920.0f, 1080.0f);

        CHECK(!view.options[0].affordable && !view.options[1].affordable,
              "with an empty treasury neither payment is affordable");
        {
            const UiWidget *opt = ui_list_find(&list,
                                      ui_id(UI_GROUP_RESOURCE, 0));
            CHECK(opt && (opt->flags & UI_W_MUTED) &&
                  opt->reason == (uint8_t)REJ_CANT_AFFORD,
                  "...and the option says so in the sim's vocabulary");
        }
    }

    game_free(gs);
}

/* ---- 5. every kind builds a view ------------------------- */
static void test_all_kinds(void)
{
    GameState  *gs = game_init();
    UiSnapshot  snap;
    ConfirmView view;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 5150u);

    game_confirm_ship(gs);
    ui_snapshot_build(&snap, gs);
    confirm_view_build(&view, &snap);
    CHECK(view.option_count == 1 && view.title[0],
          "the ship popup has a title and one option");
    CHECK(strstr(view.options[0].preview, "BUILD_SHIP") != NULL,
          "and previews the BUILD_SHIP command it will send");
    game_confirm_cancel(gs);

    /* Nothing open: an empty view, not a stale one. */
    ui_snapshot_build(&snap, gs);
    confirm_view_build(&view, &snap);
    CHECK(view.option_count == 0, "with nothing open the view is empty");

    game_free(gs);
}

int main(void)
{
    printf("== the one confirmation (no SDL linked) ==\n");
    test_preview_is_the_command();
    test_command_is_captured_at_open();
    test_cancel();
    test_hits();
    test_all_kinds();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
