/*  test_escrow.c  --  the harbour as an exchange (UI_PLAN M5)
 *
 * Decision 4's claim, put to the test: the marketplace and the harbour
 * escrow are one screen parameterised by counterparty. If that is true,
 * the same builder and the same hit-test serve both, and only the
 * action cluster and the footer differ.
 *
 * Also here: nonce-stamped offers. A panel shows a state; the command
 * carries that state's stamp back; the sim refuses if the quay moved
 * underneath — because a visitor's ship can dock and take goods between
 * the frame you read and the button you press.
 *
 * Linked WITHOUT SDL.
 */

#include "exchange_view.h"
#include "game.h"
#include "island.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* ---- 1. the same surface ---------------------------------- */
static void test_same_surface(void)
{
    GameState   *gs = game_init();
    UiSnapshot   snap;
    ExchangeView market, quay;
    UiList       mlist, qlist;
    UiState      st;
    int          i, m_inside = 1, q_inside = 1;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 5150u);
    memset(&st, 0, sizeof(st));

    gs->islands[0].escrow[RES_WOOD] = 12;
    gs->islands[0].escrow[RES_GOLD] = 40;

    ui_snapshot_build(&snap, gs);
    exchange_view_market(&market, &snap, 0);
    exchange_view_escrow(&quay,  &snap, 0);
    exchange_build(&mlist, &market, &st, 1920.0f, 1080.0f);
    exchange_build(&qlist, &quay,   &st, 1920.0f, 1080.0f);

    CHECK(market.kind == EXCHANGE_QUOTES && quay.kind == EXCHANGE_OFFER,
          "one struct, two kinds");
    CHECK(qlist.count > 0 && mlist.count > 0,
          "the same builder produces both screens");

    for (i = 0; i < mlist.count; i++) {
        UiRect r = mlist.items[i].rect;
        if (r.x < 0.0f || r.y < 0.0f ||
            r.x + r.w > 1920.0f || r.y + r.h > 1080.0f) m_inside = 0;
    }
    for (i = 0; i < qlist.count; i++) {
        UiRect r = qlist.items[i].rect;
        if (r.x < 0.0f || r.y < 0.0f ||
            r.x + r.w > 1920.0f || r.y + r.h > 1080.0f) q_inside = 0;
    }
    CHECK(m_inside && q_inside, "both fit on screen");

    /* Gold is a row on the quay and never on the marketplace: a visitor
     * pays by leaving coin behind. */
    {
        int quay_has_gold = 0, market_has_gold = 0;
        for (i = 0; i < quay.row_count; i++)
            if (quay.rows[i].ident == (uint16_t)RES_GOLD) quay_has_gold = 1;
        for (i = 0; i < market.row_count; i++)
            if (market.rows[i].ident == (uint16_t)RES_GOLD) market_has_gold = 1;
        CHECK(quay_has_gold && !market_has_gold,
              "Gold is cargo on a quay and not a good on a market");
    }

    /* The escrow contents are the counterparty's side of the row. */
    for (i = 0; i < quay.row_count; i++)
        if (quay.rows[i].ident == (uint16_t)RES_WOOD)
            CHECK(quay.rows[i].theirs == 12,
                  "what a visitor left shows as theirs");

    /* The footer carries the blockade lever, not a pager. */
    CHECK(ui_list_find(&qlist, ui_id(UI_GROUP_ACTION, UI_ACTION_DOCKING)),
          "the harbour footer has the docking lever");
    CHECK(!ui_list_find(&qlist, ui_id(UI_GROUP_ACTION, UI_ACTION_NEXT)),
          "...and not the marketplace's pager");
    CHECK(ui_list_find(&mlist, ui_id(UI_GROUP_ACTION, UI_ACTION_NEXT)),
          "while the marketplace still pages");

    game_free(gs);
}

/* ---- 2. the nonce ----------------------------------------- */
static void test_nonce(void)
{
    GameState *gs = game_init();
    uint32_t   before, after;
    int        taken;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 6060u);

    gs->islands[0].escrow[RES_WOOD] = 10;
    before = island_escrow_nonce(&gs->islands[0]);

    /* Nothing has changed: the stamped take is honoured. */
    game_escrow_take_nonce(gs, 0, RES_WOOD, 5, before);
    sim_run_one_tick(gs);
    taken = gs->islands[0].escrow[RES_WOOD];
    CHECK(taken == 5, "a take stamped with the current quay applies");

    /* Now the quay changes under the panel — a visitor's ship docking
     * is exactly this — and the stale stamp is refused. */
    after = island_escrow_nonce(&gs->islands[0]);
    CHECK(after != before, "the stamp changes when the quay does");

    game_escrow_take_nonce(gs, 0, RES_WOOD, 5, before);   /* stale */
    sim_run_one_tick(gs);
    CHECK(gs->islands[0].escrow[RES_WOOD] == 5,
          "a take stamped with a quay that has moved does not apply");

    {
        Command      c;
        RejectReason why;
        memset(&c, 0, sizeof(c));
        c.kind      = CMD_ESCROW_TAKE;
        c.a         = 0;
        c.b         = RES_WOOD;
        c.c         = 5;
        c.d         = (int32_t)before;
        c.player_id = gs->local_player_id;
        why = sim_apply_reason(gs, &c);
        CHECK(why == REJ_OFFER_CHANGED,
              "and the sim says the offer changed, in those words");
    }

    /* Unstamped commands still work: that is what replay carries. */
    game_escrow_take(gs, 0, RES_WOOD, 5);
    sim_run_one_tick(gs);
    CHECK(gs->islands[0].escrow[RES_WOOD] == 0,
          "an unstamped take still applies, so old logs replay");

    game_free(gs);
}

/* ---- 3. the reasons the harbour can give ------------------ */
static void test_reasons(void)
{
    GameState *gs = game_init();
    Command    c;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 7070u);

    memset(&c, 0, sizeof(c));
    c.kind      = CMD_ESCROW_TAKE;
    c.a         = 0;
    c.b         = RES_WOOD;
    c.c         = 5;
    c.player_id = gs->local_player_id;
    CHECK(sim_apply_reason(gs, &c) == REJ_NO_STOCK,
          "taking from an empty quay says the quay is empty");

    memset(&c, 0, sizeof(c));
    c.kind      = CMD_ESCROW_PUT;
    c.a         = 0;
    c.b         = RES_BEER;      /* a new island brews none */
    c.c         = 5;
    c.player_id = gs->local_player_id;
    CHECK(sim_apply_reason(gs, &c) == REJ_NO_STOCK,
          "staging goods you do not have says so too");

    /* Someone else's harbour is refused as ownership, not as a shrug —
     * this is the message that teaches co-op privacy. */
    memset(&c, 0, sizeof(c));
    c.kind      = CMD_ESCROW_TAKE;
    c.a         = 0;
    c.b         = RES_WOOD;
    c.c         = 1;
    c.player_id = 99u;
    CHECK(sim_apply_reason(gs, &c) == REJ_NOT_OWNER,
          "another player's harbour refuses with NOT_OWNER");

    game_free(gs);
}

int main(void)
{
    printf("== the harbour as an exchange (no SDL linked) ==\n");
    test_same_surface();
    test_nonce();
    test_reasons();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
