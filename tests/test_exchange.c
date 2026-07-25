/*  test_exchange.c  --  the exchange screen (UI_PLAN Phase 1)
 *
 * The phase exists to retire a capacity cliff: the old trade overlay
 * derived its height from the number of goods, so at six goods it was
 * 722px tall and at ten it would have been 1130px on a 1080px screen.
 * The headline assertion here is therefore the boring-looking one —
 * at 6, 10, 25 and 40 goods, every rect still lies inside 1920x1080.
 *
 * Linked WITHOUT SDL, against libsaltmarch_ui, so this drives the real
 * layout and the real hit-test rather than a copy of them.
 *
 * Also checked:
 *   - pagination arithmetic against the real panel geometry;
 *   - the hit round trip: clicking where a button was drawn yields the
 *     resource and quantity that button meant;
 *   - IDENTITY, not position: the same click on page 2 resolves to the
 *     resource that is actually there, which is the property that stops
 *     an old recorded click replaying as a trade in the wrong good;
 *   - buttons are refused with the right reason (no stock, no gold, no
 *     room, counterparty broke);
 *   - a scripted click sequence produces the expected sequence of
 *     (kind, resource, qty) — the miniature version of the M1 harness.
 *
 * Built and run by tests/run.sh.
 */

#include "exchange_view.h"
#include "ui_kit.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* camera.h (reached through the snapshot header) already defines these
 * as ints; the overlay is laid out against the same logical screen. */
#define SCREEN_WF ((float)SCREEN_W)
#define SCREEN_HF ((float)SCREEN_H)

/* A synthetic market with `n` goods. Deliberately not built from a
 * UiSnapshot: the point is to exercise goods counts the game does not
 * have yet, which is the whole cliff. */
static void synth(ExchangeView *v, int n)
{
    int i;

    memset(v, 0, sizeof(*v));
    strcpy(v->title, "Marketplace");
    v->kind       = EXCHANGE_QUOTES;
    v->your_gold  = 1000;
    v->their_gold = 20000;
    v->capacity   = 200;
    v->row_count  = n;

    for (i = 0; i < n && i < EXCHANGE_MAX_ROWS; i++) {
        ExchangeRow *r = &v->rows[i];
        r->ident  = (uint16_t)i;
        snprintf(r->name, sizeof(r->name), "Good%d", i);
        r->yours  = 50;
        r->theirs = 100;
        r->bid    = 2 + i;
        r->ask    = 4 + i;
        r->refuse = (uint8_t)REJ_OK;
    }
}

/* Centre of a rect — where a player who aims at a button clicks. */
static float cx(UiRect r) { return r.x + r.w * 0.5f; }
static float cy(UiRect r) { return r.y + r.h * 0.5f; }

/* ---- 1. the cliff ---------------------------------------- */
static void test_fits_on_screen(void)
{
    const int SIZES[] = { 6, 10, 25, 40 };
    int       s;

    for (s = 0; s < (int)(sizeof(SIZES) / sizeof(SIZES[0])); s++) {
        ExchangeView v;
        UiState      st;
        UiList       list;
        int          i, inside = 1, in_panel = 1;
        UiRect       panel;
        char         msg[96];

        synth(&v, SIZES[s]);
        memset(&st, 0, sizeof(st));
        exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);

        panel = list.items[0].rect;

        for (i = 0; i < list.count; i++) {
            UiRect r = list.items[i].rect;
            if (r.x < 0.0f || r.y < 0.0f ||
                r.x + r.w > SCREEN_WF || r.y + r.h > SCREEN_HF)
                inside = 0;
            if (i > 0 &&
                (r.x < panel.x - 0.01f || r.y < panel.y - 0.01f ||
                 r.x + r.w > panel.x + panel.w + 0.01f ||
                 r.y + r.h > panel.y + panel.h + 0.01f))
                in_panel = 0;
        }

        snprintf(msg, sizeof(msg),
                 "%2d goods: every rect inside 1920x1080", SIZES[s]);
        CHECK(inside, msg);
        snprintf(msg, sizeof(msg),
                 "%2d goods: every widget inside the panel", SIZES[s]);
        CHECK(in_panel, msg);
        snprintf(msg, sizeof(msg),
                 "%2d goods: nothing was dropped from the list", SIZES[s]);
        CHECK(list.dropped == 0, msg);
    }
}

/* ---- 2. pagination --------------------------------------- */
static void test_pagination(void)
{
    ExchangeView v;
    UiState      st;
    UiList       list;
    int          pages6, pages40, rows_p0, rows_p1, i;

    memset(&st, 0, sizeof(st));

    synth(&v, 6);
    pages6 = exchange_page_count(&v, SCREEN_HF);
    CHECK(pages6 == 1, "6 goods — today's count — still fit on one page");

    synth(&v, 40);
    pages40 = exchange_page_count(&v, SCREEN_HF);
    CHECK(pages40 > 1, "40 goods paginate instead of overflowing");

    exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    rows_p0 = 0;
    for (i = 0; i < list.count; i++)
        if (ui_id_group(list.items[i].id) == UI_GROUP_RESOURCE) rows_p0++;

    st.exchange_page = 1;
    exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    rows_p1 = 0;
    for (i = 0; i < list.count; i++)
        if (ui_id_group(list.items[i].id) == UI_GROUP_RESOURCE) rows_p1++;

    CHECK(rows_p0 > 0 && rows_p1 > 0, "both pages carry rows");
    CHECK(rows_p0 * pages40 >= 40, "the pages between them cover every good");

    /* The pager is greyed at the ends rather than vanishing, so the
     * Close button's neighbours never move under the cursor. */
    st.exchange_page = 0;
    exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        const UiWidget *prev = ui_list_find(&list,
                                    ui_id(UI_GROUP_ACTION, UI_ACTION_PREV));
        const UiWidget *next = ui_list_find(&list,
                                    ui_id(UI_GROUP_ACTION, UI_ACTION_NEXT));
        CHECK(prev && (prev->flags & UI_W_DISABLED),
              "Prev is present but disabled on the first page");
        CHECK(next && !(next->flags & UI_W_DISABLED),
              "Next is live when there is a page to go to");
    }
}

/* ---- 3. hit round trip and identity ---------------------- */
static void test_hits(void)
{
    ExchangeView v;
    UiState      st;
    UiList       list;
    ExchangeHit  hit;
    int          i, first_of_page1 = -1;

    synth(&v, 40);
    memset(&st, 0, sizeof(st));
    exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);

    /* Every button reports the identity and quantity it was built with. */
    {
        int ok = 1;
        for (i = 0; i < list.count; i++) {
            const UiWidget *w = &list.items[i];
            int  g = ui_id_group(w->id);
            if (g != UI_GROUP_SELL && g != UI_GROUP_BUY) continue;
            if (w->flags & UI_W_DISABLED) continue;

            hit = exchange_hit(&list, &st, cx(w->rect), cy(w->rect));
            if (hit.res != (int)ui_id_value(w->id) || hit.qty != w->value)
                ok = 0;
            if (g == UI_GROUP_SELL && hit.kind != EXCHANGE_HIT_SELL) ok = 0;
            if (g == UI_GROUP_BUY  && hit.kind != EXCHANGE_HIT_BUY)  ok = 0;
        }
        CHECK(ok, "every button hit-tests back to its own resource and qty");
    }

    /* Clicking outside the panel is distinguishable from clicking the
     * panel background — one dismisses, the other is absorbed. */
    hit = exchange_hit(&list, &st, 5.0f, 5.0f);
    CHECK(hit.kind == EXCHANGE_HIT_OUTSIDE, "a click outside says so");
    hit = exchange_hit(&list, &st, cx(list.items[0].rect),
                       list.items[0].rect.y + 4.0f);
    CHECK(hit.kind == EXCHANGE_HIT_NONE, "a click on the panel is absorbed");

    /* IDENTITY, NOT POSITION. Take the screen position of the first
     * row's sell button on page 0, then turn to page 1 and click the
     * same pixel: it must resolve to the good that is now there, and
     * that good must not be the one that was there before. */
    {
        UiRect first_row_btn = { 0.0f, 0.0f, 0.0f, 0.0f };
        int    res_p0 = -1;

        for (i = 0; i < list.count; i++)
            if (ui_id_group(list.items[i].id) == UI_GROUP_SELL) {
                first_row_btn = list.items[i].rect;
                res_p0        = (int)ui_id_value(list.items[i].id);
                break;
            }

        st.exchange_page = 1;
        exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
        for (i = 0; i < list.count; i++)
            if (ui_id_group(list.items[i].id) == UI_GROUP_RESOURCE) {
                first_of_page1 = (int)ui_id_value(list.items[i].id);
                break;
            }

        hit = exchange_hit(&list, &st, cx(first_row_btn), cy(first_row_btn));
        CHECK(hit.kind == EXCHANGE_HIT_SELL && hit.res == first_of_page1,
              "the same pixel on page 2 sells the good that is there now");
        CHECK(hit.res != res_p0,
              "...which is not the good that occupied that row on page 1");
    }
}

/* ---- 4. refusals ----------------------------------------- */
static void test_refusals(void)
{
    ExchangeView v;
    UiState      st;
    UiList       list;
    int          i;

    memset(&st, 0, sizeof(st));

    /* Nothing in stock: every sell button off, buys unaffected. */
    synth(&v, 6);
    for (i = 0; i < v.row_count; i++) v.rows[i].yours = 0;
    exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        int sells_off = 1, buys_on = 0;
        for (i = 0; i < list.count; i++) {
            const UiWidget *w = &list.items[i];
            if (ui_id_group(w->id) == UI_GROUP_SELL) {
                if (!(w->flags & UI_W_DISABLED)) sells_off = 0;
                else if (w->reason != (uint8_t)REJ_NO_STOCK) sells_off = 0;
            }
            if (ui_id_group(w->id) == UI_GROUP_BUY &&
                !(w->flags & UI_W_DISABLED)) buys_on = 1;
        }
        CHECK(sells_off, "with an empty store, selling is refused: NO_STOCK");
        CHECK(buys_on, "...and buying is still offered");
    }

    /* No Gold: buys refused as CANT_AFFORD. */
    synth(&v, 6);
    v.your_gold = 0;
    exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        int right = 1, any = 0;
        for (i = 0; i < list.count; i++) {
            const UiWidget *w = &list.items[i];
            if (ui_id_group(w->id) != UI_GROUP_BUY) continue;
            any = 1;
            if (!(w->flags & UI_W_DISABLED) ||
                w->reason != (uint8_t)REJ_CANT_AFFORD) right = 0;
        }
        CHECK(any && right, "with no Gold, buying is refused: CANT_AFFORD");
    }

    /* Store full: buys refused as NO_STORAGE, sells still available. */
    synth(&v, 6);
    for (i = 0; i < v.row_count; i++) v.rows[i].yours = v.capacity;
    exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        int buys_off = 1, sells_on = 0;
        for (i = 0; i < list.count; i++) {
            const UiWidget *w = &list.items[i];
            if (ui_id_group(w->id) == UI_GROUP_BUY) {
                if (!(w->flags & UI_W_DISABLED) ||
                    w->reason != (uint8_t)REJ_NO_STORAGE) buys_off = 0;
            }
            if (ui_id_group(w->id) == UI_GROUP_SELL &&
                !(w->flags & UI_W_DISABLED)) sells_on = 1;
        }
        CHECK(buys_off, "with a full store, buying is refused: NO_STORAGE");
        CHECK(sells_on, "...and selling is the way out of it");
    }

    /* Counterparty out of Gold: selling to them is refused with THEIR
     * reason, which is the message MMO Phase 3 needed and never had. */
    synth(&v, 6);
    v.their_gold = 0;
    for (i = 0; i < v.row_count; i++)
        v.rows[i].refuse = (uint8_t)REJ_COUNTERPARTY_NO_GOLD;
    exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    {
        int right = 1, any = 0;
        for (i = 0; i < list.count; i++) {
            const UiWidget *w = &list.items[i];
            if (ui_id_group(w->id) != UI_GROUP_SELL) continue;
            any = 1;
            if (!(w->flags & UI_W_DISABLED) ||
                w->reason != (uint8_t)REJ_COUNTERPARTY_NO_GOLD) right = 0;
        }
        CHECK(any && right,
              "a broke counterparty refuses to buy, and says so");
    }
}

/* ---- 5. a scripted session ------------------------------- */
/* The miniature of UI_PLAN M1's CI harness: drive build + hit_test with
 * a click sequence and assert the emitted actions. When intents are
 * recorded into the .smlog this becomes the same loop over real data. */
static void test_scripted_clicks(void)
{
    ExchangeView v;
    UiState      st;
    UiList       list;
    ExchangeHit  hit;
    int          i, emitted = 0, correct = 1;
    int          seen_res[4], seen_qty[4];

    synth(&v, 40);
    memset(&st, 0, sizeof(st));

    /* Click: sell-1 on the first row, then Next, then buy-10 on the
     * first row of the new page, then Close. */
    exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);

    for (i = 0; i < list.count; i++) {
        const UiWidget *w = &list.items[i];
        if (ui_id_group(w->id) == UI_GROUP_SELL && w->value == 1) {
            hit = exchange_hit(&list, &st, cx(w->rect), cy(w->rect));
            seen_res[emitted] = hit.res;
            seen_qty[emitted] = hit.qty;
            emitted++;
            break;
        }
    }

    {
        const UiWidget *next = ui_list_find(&list,
                                    ui_id(UI_GROUP_ACTION, UI_ACTION_NEXT));
        hit = exchange_hit(&list, &st, cx(next->rect), cy(next->rect));
        if (hit.kind != EXCHANGE_HIT_PAGE) correct = 0;
        st.exchange_page = hit.page;          /* the fold UiState is  */
    }

    exchange_build(&list, &v, &st, SCREEN_WF, SCREEN_HF);
    for (i = 0; i < list.count; i++) {
        const UiWidget *w = &list.items[i];
        if (ui_id_group(w->id) == UI_GROUP_BUY && w->value == 10) {
            hit = exchange_hit(&list, &st, cx(w->rect), cy(w->rect));
            seen_res[emitted] = hit.res;
            seen_qty[emitted] = hit.qty;
            emitted++;
            break;
        }
    }

    {
        const UiWidget *close = ui_list_find(&list,
                                    ui_id(UI_GROUP_ACTION, UI_ACTION_CLOSE));
        hit = exchange_hit(&list, &st, cx(close->rect), cy(close->rect));
        if (hit.kind != EXCHANGE_HIT_CLOSE) correct = 0;
    }

    CHECK(emitted == 2 && correct,
          "a scripted click sequence emits exactly the expected actions");
    CHECK(seen_qty[0] == 1 && seen_qty[1] == 10,
          "each emitted action carries the quantity its button showed");
    CHECK(seen_res[0] != seen_res[1],
          "the page turn changed which good the clicks referred to");
}

/* ---- 6. category grouping (UI_PLAN Phase 2) -------------- */
static void test_grouping(void)
{
    ExchangeView v;
    UiSnapshot   snap;
    int          i, ordered = 1, seen_gold = 0;

    /* A real market view, not a synthetic one: this is the path the
     * game takes, and the assertion is about the resource table. */
    memset(&snap, 0, sizeof(snap));
    snap.islands[0].settled  = 1;
    snap.islands[0].capacity = 200;
    for (i = 0; i < RES_COUNT; i++) {
        snap.islands[0].stock[i]         = 10;
        snap.counterparty_stock[i]       = 10;
        snap.bid[i]                      = 2;
        snap.ask[i]                      = 4;
    }
    snap.counterparty_gold = 500;

    exchange_view_market(&v, &snap, 0);

    CHECK(v.row_count == (int)RES_GOLD,
          "every tradeable good gets a row, and Gold does not");

    for (i = 1; i < v.row_count; i++)
        if (v.rows[i].category < v.rows[i - 1].category) ordered = 0;
    CHECK(ordered, "rows are grouped by category, raw goods first");

    for (i = 0; i < v.row_count; i++)
        if (v.rows[i].ident == (uint16_t)RES_GOLD) seen_gold = 1;
    CHECK(!seen_gold, "Gold is the medium, never a row of its own");

    /* Within a category, enum order is preserved — a player who has
     * learned where Wood sits should not find it moved. */
    {
        int wood = -1, fish = -1;
        for (i = 0; i < v.row_count; i++) {
            if (v.rows[i].ident == (uint16_t)RES_WOOD) wood = i;
            if (v.rows[i].ident == (uint16_t)RES_FISH) fish = i;
        }
        CHECK(wood >= 0 && fish > wood,
              "inside a category, goods keep their familiar order");
    }
}

int main(void)
{
    printf("== exchange screen (no SDL linked) ==\n");
    test_fits_on_screen();
    test_pagination();
    test_hits();
    test_refusals();
    test_scripted_clicks();
    test_grouping();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
