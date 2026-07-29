/*  test_book.c  --  the order book screen (UI_PLAN N3)
 *
 * Linked WITHOUT SDL, against libsaltmarch_ui, so this drives the real
 * layout, the real retained-row fold and the real hit-test rather than
 * a copy of them.
 *
 * The headline assertion is the one the phase exists for: an order that
 * fills or is withdrawn while the panel is open STAYS on screen, in the
 * same place, struck through and unclickable. The order book is the
 * first screen another player changes mid-read, and a row that vanishes
 * between the frame that drew it and the click that hits it is the one
 * failure it must not have.
 *
 * Also checked:
 *   - the panel fits 1920x1080 at 0, 1, 24 and 40 rows;
 *   - rows are ordered by order id, not by the snapshot's array — the
 *     array reuses cancelled slots, which is exactly what would move a
 *     row under a cursor;
 *   - a cancel click carries the FULL 32-bit order id, not the 16 bits
 *     that fit in a widget identity;
 *   - the draft composer is a pure fold: a click sequence produces one
 *     exact (side, good, quantity, limit), and the limit follows the
 *     market until it is stepped;
 *   - Post is refused with the sim's own vocabulary — no stock, no
 *     gold, not your island, and the per-player cap;
 *   - the command a Post click implies is the command the sim accepts.
 *
 * Built and run by tests/run.sh.
 */

#include "book_view.h"
#include "ui_kit.h"
#include "game.h"
#include "orderbook.h"
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

/* A synthetic book with `n` resting orders. Not built from a snapshot:
 * the point is to reach row counts and order ids a short test session
 * would not produce. */
static void synth(BookView *v, int n)
{
    int i;

    memset(v, 0, sizeof(*v));
    strcpy(v->title, "Order book");
    v->island      = 0;
    v->tick        = 1000;
    v->yours       = 1;
    v->your_gold   = 5000;
    v->cap         = ORDERBOOK_MAX_PER_PLAYER;
    v->open_count  = n;
    v->draft_quote = 12;
    v->draft_stock = 100;
    strcpy(v->draft_name, "Wood");
    v->row_count   = n;

    for (i = 0; i < n && i < BOOK_MAX_ROWS; i++) {
        BookRow *r = &v->rows[i];
        /* Ids well past 65,535: the NPC market burns through them, and
         * a widget id has only sixteen bits to hold one. */
        r->id          = 70000u + (uint32_t)i * 3u;
        r->kind        = (uint16_t)TRADE_RESOURCE;
        r->what        = (uint16_t)(i % (int)RES_GOLD);
        r->side        = (i % 2) ? ORDER_SELL : ORDER_BUY;
        r->qty         = 5 + i;
        r->limit       = 10 + i;
        r->placed_tick = 100;
        snprintf(r->name, sizeof(r->name), "Good%d", i);
    }
}

static void draft(UiState *st, int32_t side, int32_t res, int32_t qty,
                  int32_t limit)
{
    memset(st, 0, sizeof(*st));
    st->book_side  = side;
    st->book_res   = res;
    st->book_qty   = qty;
    st->book_limit = limit;
}

/* The first widget of a group, or NULL. */
static const UiWidget *find_group(const UiList *l, int group)
{
    int i;
    for (i = 0; i < l->count; i++)
        if (ui_id_group(l->items[i].id) == group) return &l->items[i];
    return NULL;
}

static const UiWidget *find_valued(const UiList *l, uint32_t id, int32_t value)
{
    int i;
    for (i = 0; i < l->count; i++)
        if (l->items[i].id == id && l->items[i].value == value)
            return &l->items[i];
    return NULL;
}

/* ---- 1. it fits ------------------------------------------- */
static void test_fits_on_screen(void)
{
    const int SIZES[] = { 0, 1, 24, 40 };
    int       s;

    for (s = 0; s < (int)(sizeof(SIZES) / sizeof(SIZES[0])); s++) {
        BookView v;
        UiState  st;
        UiList   list;
        int      i, inside = 1;
        char     msg[96];

        synth(&v, SIZES[s]);
        draft(&st, ORDER_BUY, RES_WOOD, 10, 0);
        book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);

        for (i = 0; i < list.count; i++) {
            UiRect r = list.items[i].rect;
            if (r.x < 0.0f || r.y < 0.0f ||
                r.x + r.w > SCREEN_WF || r.y + r.h > SCREEN_HF)
                inside = 0;
        }

        snprintf(msg, sizeof(msg),
                 "%d resting orders: every widget is on screen", SIZES[s]);
        CHECK(inside && list.dropped == 0, msg);
    }
}

/* ---- 2. the full id survives the widget ------------------- */
static void test_cancel_carries_full_id(void)
{
    BookView        v;
    UiState         st;
    UiList          list;
    const UiWidget *w;
    BookHit         hit;

    synth(&v, 3);
    draft(&st, ORDER_BUY, RES_WOOD, 10, 0);
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);

    w = find_group(&list, UI_GROUP_CANCEL);
    CHECK(w != NULL, "each resting order has a Cancel");
    if (!w) return;

    hit = book_hit(&list, &v, &st, cx(w->rect), cy(w->rect));
    CHECK(hit.kind == BOOK_HIT_CANCEL, "clicking Cancel decodes as cancel");
    CHECK(hit.order_id == v.rows[0].id,
          "the cancel carries the full 32-bit order id, not its low half");
    CHECK((uint32_t)ui_id_value(w->id) != v.rows[0].id,
          "which the widget's own identity could not have held");
}

/* ---- 3. the composer is a fold ---------------------------- */
static void test_composer_fold(void)
{
    BookView v;
    UiState  st;
    UiList   list;
    BookHit  hit;

    synth(&v, 0);
    draft(&st, ORDER_BUY, RES_WOOD, 10, 0);

    /* An unstepped limit follows the market's quote. */
    CHECK(book_draft_limit(&v, &st) == v.draft_quote,
          "a fresh draft is priced at the market's quote");

    /* +10 on the quantity. */
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        const UiWidget *w = find_valued(&list,
                                        ui_id(UI_GROUP_ACTION, UI_ACTION_QTY),
                                        10);
        CHECK(w != NULL, "the quantity has a +10");
        if (!w) return;
        hit = book_hit(&list, &v, &st, cx(w->rect), cy(w->rect));
        CHECK(hit.kind == BOOK_HIT_QTY && hit.qty == 20,
              "clicking +10 folds the quantity to 20");
        st.book_qty = hit.qty;
    }

    /* -1 on the limit: it stops following and becomes explicit. */
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        const UiWidget *w = find_valued(&list,
                                        ui_id(UI_GROUP_ACTION, UI_ACTION_LIMIT),
                                        -1);
        CHECK(w != NULL, "the limit has a -1");
        if (!w) return;
        hit = book_hit(&list, &v, &st, cx(w->rect), cy(w->rect));
        CHECK(hit.kind == BOOK_HIT_LIMIT && hit.limit == v.draft_quote - 1 &&
              !hit.follow,
              "stepping the limit takes it off the quote, one below it");
        st.book_limit = hit.follow ? 0 : hit.limit;
    }

    /* Changing the good drops back to following: a price argued for
     * Wood means nothing for Fish. */
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        const UiWidget *w = find_group(&list, UI_GROUP_RESOURCE);
        CHECK(w != NULL, "the good can be cycled");
        if (!w) return;
        hit = book_hit(&list, &v, &st, cx(w->rect), cy(w->rect));
        CHECK(hit.kind == BOOK_HIT_GOOD && hit.follow,
              "changing the good goes back to following the market");
        CHECK(hit.res == (int32_t)ui_id_value(w->id),
              "and the button names the good it selects, not its position");
    }

    /* So does changing side — the two sides quote different prices. */
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        const UiWidget *w = ui_list_find(&list,
                                         ui_id(UI_GROUP_ACTION,
                                               UI_ACTION_SIDE));
        CHECK(w != NULL, "the side can be switched");
        if (!w) return;
        hit = book_hit(&list, &v, &st, cx(w->rect), cy(w->rect));
        CHECK(hit.kind == BOOK_HIT_SIDE && hit.side == ORDER_SELL &&
              hit.follow,
              "switching to Sell goes back to following the market");
    }

    /* And a sell posts as a negative quantity, which is how the side
     * fits into CMD_PLACE_ORDER's four payload slots. The expression
     * below is the one main.c and the replay harness both use. */
    draft(&st, ORDER_SELL, RES_WOOD, 4, 0);
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        const UiWidget *w = ui_list_find(&list,
                                         ui_id(UI_GROUP_ACTION,
                                               UI_ACTION_POST));
        int32_t qty;
        CHECK(w != NULL && !(w->flags & UI_W_DISABLED),
              "a sell backed by stock is offered");
        if (!w) return;
        hit = book_hit(&list, &v, &st, cx(w->rect), cy(w->rect));
        qty = (hit.side == ORDER_SELL) ? -hit.qty : hit.qty;
        CHECK(hit.kind == BOOK_HIT_POST && qty == -4,
              "a sell is carried as a negative quantity");
    }
}

/* ---- 4. Post is refused in the sim's vocabulary ----------- */
static void test_post_refusals(void)
{
    BookView        v;
    UiState         st;
    UiList          list;
    const UiWidget *post;
    uint32_t        post_id = ui_id(UI_GROUP_ACTION, UI_ACTION_POST);

    /* Not your island: readable, not actionable. */
    synth(&v, 2);
    v.yours = 0;
    draft(&st, ORDER_BUY, RES_WOOD, 10, 0);
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    post = ui_list_find(&list, post_id);
    CHECK(post && (post->flags & UI_W_DISABLED) &&
          post->reason == (uint8_t)REJ_NOT_OWNER,
          "a foreign harbour's book says 'not your island', not nothing");

    /* No gold for the buy. */
    synth(&v, 0);
    v.your_gold = 5;
    draft(&st, ORDER_BUY, RES_WOOD, 10, 0);
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    post = ui_list_find(&list, post_id);
    CHECK(post && (post->flags & UI_W_DISABLED) &&
          post->reason == (uint8_t)REJ_CANT_AFFORD,
          "a buy you cannot pay for is refused as 'can't afford it'");

    /* No goods for the sell. */
    synth(&v, 0);
    v.draft_stock = 2;
    draft(&st, ORDER_SELL, RES_WOOD, 10, 0);
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    post = ui_list_find(&list, post_id);
    CHECK(post && (post->flags & UI_W_DISABLED) &&
          post->reason == (uint8_t)REJ_NO_STOCK,
          "a sell you cannot cover is refused as 'nothing in stock'");

    /* The per-player cap, which the sim enforces and the screen should
     * not let a player walk into blind. */
    synth(&v, ORDERBOOK_MAX_PER_PLAYER);
    draft(&st, ORDER_BUY, RES_WOOD, 1, 1);
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    post = ui_list_find(&list, post_id);
    CHECK(post && (post->flags & UI_W_DISABLED),
          "at the per-player cap, Post is off rather than silently refused");
}

/* ---- 5. rows are retained, and ordered by id -------------- */
static void test_retention(void)
{
    GameState  *gs = game_init();
    UiSnapshot  snap;
    BookView    v;
    UiState     st;
    UiList      list;
    Command     c;
    uint32_t    first_id = 0, second_id = 0;
    int         i, first_at = -1;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    memset(&v, 0, sizeof(v));
    book_view_reset(&v);
    draft(&st, ORDER_BUY, RES_WOOD, 1, 0);

    /* Two resting buys, well under any quote so neither one crosses. */
    for (i = 0; i < 2; i++) {
        memset(&c, 0, sizeof(c));
        c.kind      = CMD_PLACE_ORDER;
        c.a         = 0;
        c.b         = TRADE_PACK(TRADE_RESOURCE, (uint16_t)RES_WOOD);
        c.c         = 2;
        c.d         = 1;
        c.player_id = gs->local_player_id;
        CHECK(sim_apply_reason(gs, &c) == REJ_OK, "a resting buy is accepted");
    }

    ui_snapshot_build(&snap, gs);
    book_view_update(&v, &snap, 0, &st);
    CHECK(v.row_count == 2 && v.open_count == 2,
          "both orders appear as rows");

    for (i = 0; i < snap.order_count; i++)
        if (snap.order[i].mine) {
            if (!first_id)       first_id  = snap.order[i].id;
            else if (!second_id) second_id = snap.order[i].id;
        }
    CHECK(v.rows[0].id < v.rows[1].id,
          "rows are ordered by order id, oldest first");

    /* Withdraw the FIRST one. Its slot in the book is now free for
     * reuse and it is gone from the snapshot — the moment where a
     * naive rebuild would slide row two up under the cursor. */
    for (i = 0; i < v.row_count; i++)
        if (v.rows[i].id == first_id) first_at = i;

    memset(&c, 0, sizeof(c));
    c.kind      = CMD_CANCEL_ORDER;
    c.a         = (int32_t)first_id;
    c.player_id = gs->local_player_id;
    CHECK(sim_apply_reason(gs, &c) == REJ_OK, "and can be withdrawn");

    ui_snapshot_build(&snap, gs);
    book_view_update(&v, &snap, 0, &st);

    CHECK(v.row_count == 2, "the withdrawn order still has its row");
    CHECK(v.rows[first_at].id == first_id && v.rows[first_at].gone,
          "in the same place, marked gone");
    CHECK(v.open_count == 1, "and it no longer counts against the cap");

    /* Its Cancel is dead, and says why in the sim's own words. */
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        const UiWidget *w = NULL;
        for (i = 0; i < list.count; i++)
            if (ui_id_group(list.items[i].id) == UI_GROUP_CANCEL &&
                (uint32_t)list.items[i].value == first_id)
                w = &list.items[i];

        CHECK(w && (w->flags & UI_W_DISABLED) &&
              w->reason == (uint8_t)REJ_ORDER_GONE,
              "its Cancel is disabled, reading 'that order is gone'");
        if (w) {
            BookHit hit = book_hit(&list, &v, &st, cx(w->rect), cy(w->rect));
            CHECK(hit.kind != BOOK_HIT_CANCEL,
                  "and a click on it submits nothing");
        }
    }

    /* The sim agrees about the vocabulary: cancelling something that is
     * no longer there is not a generic refusal. */
    memset(&c, 0, sizeof(c));
    c.kind      = CMD_CANCEL_ORDER;
    c.a         = (int32_t)first_id;
    c.player_id = gs->local_player_id;
    CHECK(sim_apply_reason(gs, &c) == REJ_ORDER_GONE,
          "the sim says 'that order is gone' in those words");

    /* Closing the panel is what forgets them. */
    book_view_reset(&v);
    book_view_update(&v, &snap, 0, &st);
    CHECK(v.row_count == 1 && !v.rows[0].gone,
          "reopening the panel shows only what is actually resting");

    game_free(gs);
}

/* ---- 6. the view is bounded, and live rows survive -------- */

/* A snapshot carrying `n` of the local player's orders at island 0,
 * with ids starting at `base`. Synthetic because the interesting case
 * is a view that has run for longer than one test session. */
static void snap_orders(UiSnapshot *snap, uint32_t base, int n)
{
    int i;

    memset(snap, 0, sizeof(*snap));
    snap->tick             = 500;
    snap->local_player_id  = 1;
    snap->islands[0].owner = 1;
    snap->islands[0].settled = 1;
    strcpy(snap->islands[0].name, "Test");
    snap->order_count = n;

    for (i = 0; i < n; i++) {
        UiOrder *o = &snap->order[i];
        o->id          = base + (uint32_t)i;
        o->owner       = 1;
        o->island      = 0;
        o->kind        = (uint16_t)TRADE_RESOURCE;
        o->what        = (uint16_t)RES_WOOD;
        o->side        = ORDER_BUY;
        o->qty         = 1;
        o->limit       = 2;
        o->placed_tick = 100;
        o->mine        = 1;
    }
}

static void test_bounded_retention(void)
{
    UiSnapshot snap;
    BookView   v;
    UiState    st;
    int        i, live = 0, kept_all_live = 1;

    memset(&v, 0, sizeof(v));
    book_view_reset(&v);
    draft(&st, ORDER_BUY, RES_WOOD, 1, 0);

    /* Fill it: BOOK_MAX_ROWS orders, then let them all go. */
    snap_orders(&snap, 1000u, BOOK_MAX_ROWS);
    book_view_update(&v, &snap, 0, &st);
    CHECK(v.row_count == BOOK_MAX_ROWS, "the view fills to its bound");

    snap_orders(&snap, 1000u, 0);
    book_view_update(&v, &snap, 0, &st);
    CHECK(v.row_count == BOOK_MAX_ROWS && v.open_count == 0,
          "all of them go stale, none of them vanishes");

    /* Now a fresh batch arrives with nowhere to go. The stale rows are
     * what gets dropped; the live ones are the rows a player can still
     * act on and are never evicted. */
    snap_orders(&snap, 9000u, 10);
    book_view_update(&v, &snap, 0, &st);

    CHECK(v.row_count <= BOOK_MAX_ROWS, "the view stays inside its bound");
    CHECK(v.open_count == 10, "every new order got a row");

    for (i = 0; i < v.row_count; i++)
        if (!v.rows[i].gone) live++;
    CHECK(live == 10, "and no live row was evicted to make space");

    for (i = 1; i < v.row_count; i++)
        if (v.rows[i].id < v.rows[i - 1].id) kept_all_live = 0;
    CHECK(kept_all_live, "rows are still in id order after eviction");
}

/* ---- 7. a Post click becomes the command the sim takes ---- */
static void test_post_round_trip(void)
{
    GameState      *gs = game_init();
    UiSnapshot      snap;
    BookView        v;
    UiState         st;
    UiList          list;
    const UiWidget *post;
    BookHit         hit;
    Command         c;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 5150u);

    memset(&v, 0, sizeof(v));
    book_view_reset(&v);
    /* A buy: a new island starts with gold and no goods, so a sell is
     * the one thing it cannot do. */
    draft(&st, ORDER_BUY, RES_WOOD, 3, 0);

    ui_snapshot_build(&snap, gs);
    book_view_update(&v, &snap, 0, &st);
    book_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);

    CHECK(v.yours, "your own island's book is actionable");
    CHECK(v.draft_quote == snap.ask[RES_WOOD],
          "a buy is priced off the market's ask");

    post = ui_list_find(&list, ui_id(UI_GROUP_ACTION, UI_ACTION_POST));
    CHECK(post && !(post->flags & UI_W_DISABLED),
          "a buy you can pay for is offered");
    if (!post) { game_free(gs); return; }

    hit = book_hit(&list, &v, &st, cx(post->rect), cy(post->rect));
    CHECK(hit.kind == BOOK_HIT_POST, "clicking Post decodes as a post");

    /* Exactly what main.c builds from that hit. */
    memset(&c, 0, sizeof(c));
    c.kind      = CMD_PLACE_ORDER;
    c.a         = 0;
    c.b         = TRADE_PACK(TRADE_RESOURCE, (uint16_t)hit.res);
    c.c         = (hit.side == ORDER_SELL) ? -hit.qty : hit.qty;
    c.d         = hit.limit;
    c.player_id = gs->local_player_id;

    CHECK(c.c == 3 && c.d == snap.ask[RES_WOOD],
          "carried as a positive quantity at the price the screen showed");
    CHECK(sim_apply_reason(gs, &c) == REJ_OK,
          "and the sim accepts the command the screen implied");

    /* Which comes straight back as a row. */
    ui_snapshot_build(&snap, gs);
    book_view_update(&v, &snap, 0, &st);
    CHECK(v.open_count >= 1, "the posted order appears in the book");

    game_free(gs);
}

int main(void)
{
    printf("== order book (no SDL linked) ==\n");
    test_fits_on_screen();
    test_cancel_carries_full_id();
    test_composer_fold();
    test_post_refusals();
    test_retention();
    test_bounded_retention();
    test_post_round_trip();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
