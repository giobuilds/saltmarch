/* exchange_view.c  --  The exchange surface (UI_PLAN Phase 1) */

#include "exchange_view.h"
#include "island_bar.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>

/* The action cluster, left to right. Sell versus buy is carried by. */
#define EXCHANGE_SELL_ACTIONS 3
static const int32_t ACTION_QTY[EXCHANGE_ACTIONS]   = { -1, 10, 1, 1, 10, -1 };
static const char   *ACTION_LABEL[EXCHANGE_ACTIONS] = { "All", "-10", "-1",
                                                        "+1", "+10", "Max" };

static int action_is_sell(int i) { return i < EXCHANGE_SELL_ACTIONS; }
/* The two ends, where -1 means "resolve it against live state". */
static int action_is_all(int i)  { return ACTION_QTY[i] < 0; }

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ---- building the view ------------------------------------ */

void exchange_view_market(ExchangeView *out, const UiSnapshot *snap,
                          int island)
{
    const UiIsland *isl;
    int              r;
    ResourceCategory cat;

    memset(out, 0, sizeof(*out));
    out->kind = EXCHANGE_QUOTES;

    if (island < 0 || island >= MAX_ISLANDS) {
        copy_str(out->title, sizeof(out->title), "Marketplace");
        return;
    }
    isl = &snap->islands[island];

    /* The island's name in the title: with several islands settled,
     * "which market am I looking at" is otherwise only answerable by
     * closing the screen (UI_PLAN Phase 5 does this everywhere). */
    snprintf(out->title, sizeof(out->title), "Marketplace — %s", isl->name);
    island_hue(island, &out->hue_r, &out->hue_g, &out->hue_b);

    out->your_gold  = isl->stock[RES_GOLD];
    out->their_gold = snap->counterparty_gold;
    out->capacity   = isl->capacity;

    /* Gold is deliberately not a row: it is the medium, not a good. */
    for (cat = RCAT_RAW; cat < RCAT_COUNT; cat++)
    for (r = 0; r < (int)RES_GOLD && out->row_count < EXCHANGE_MAX_ROWS; r++) {
        ExchangeRow *row;

        if (RESOURCE_CATEGORIES[r] != cat) continue;

        row = &out->rows[out->row_count++];
        row->ident    = (uint16_t)r;
        row->category = (uint8_t)RESOURCE_CATEGORIES[r];
        copy_str(row->name, sizeof(row->name), RESOURCE_NAMES[r]);
        row->yours  = isl->stock[r];
        row->theirs = snap->counterparty_stock[r];
        row->bid    = snap->bid[r];
        row->ask    = snap->ask[r];
        row->hist_count = snap->price_hist_count[r];
        memcpy(row->hist, snap->price_hist[r], sizeof(row->hist));

        /* Refusals from THEIR side of the table. The player's own
         * limits (no stock to sell, no gold to pay, no room to store)
         * are per-button and computed during the build, since they
         * differ between the buy and sell halves of the same row. */
        row->refuse = (uint8_t)REJ_OK;
        if (out->their_gold != EXCHANGE_INFINITE &&
            out->their_gold < row->bid)
            row->refuse = (uint8_t)REJ_COUNTERPARTY_NO_GOLD;
        else if (row->theirs != EXCHANGE_INFINITE && row->theirs <= 0)
            row->refuse = (uint8_t)REJ_NO_STOCK;
    }
}

void exchange_view_escrow(ExchangeView *out, const UiSnapshot *snap,
                          int island)
{
    const UiIsland  *isl;
    ResourceCategory cat;
    int              r;

    memset(out, 0, sizeof(*out));
    out->kind = EXCHANGE_OFFER;

    if (island < 0 || island >= MAX_ISLANDS) {
        copy_str(out->title, sizeof(out->title), "Harbour");
        return;
    }
    isl = &snap->islands[island];

    snprintf(out->title, sizeof(out->title), "Harbour — %s", isl->name);
    island_hue(island, &out->hue_r, &out->hue_g, &out->hue_b);

    out->your_gold       = isl->stock[RES_GOLD];
    out->their_gold      = EXCHANGE_INFINITE;   /* a quay has no purse */
    out->capacity        = isl->capacity;
    out->nonce           = isl->escrow_nonce;
    out->docking_allowed = isl->docking_allowed;

    /* Every good, Gold included: a visitor pays for cargo by leaving
     * coin on the quay, so Gold is a line item here in a way it never
     * is on the marketplace screen. */
    for (cat = RCAT_RAW; cat < RCAT_COUNT; cat++)
    for (r = 0; r < RES_COUNT && out->row_count < EXCHANGE_MAX_ROWS; r++) {
        ExchangeRow *row;

        if (RESOURCE_CATEGORIES[r] != cat) continue;

        row = &out->rows[out->row_count++];
        row->ident    = (uint16_t)r;
        row->category = (uint8_t)RESOURCE_CATEGORIES[r];
        copy_str(row->name, sizeof(row->name), RESOURCE_NAMES[r]);
        row->yours    = isl->stock[r];
        row->theirs   = isl->escrow[r];      /* what is on the quay */
        row->bid      = 0;
        row->ask      = 0;
        row->refuse   = (uint8_t)(row->theirs > 0 ? REJ_OK : REJ_NO_STOCK);
        row->hist_count = 0;
    }
}

/* ---- geometry --------------------------------------------- */

/* The height this view would like, before any clamping. Measuring
 * first and clamping second is the whole trick: the old screen derived
 * its height from the goods count and simply grew off the display. */
static float wanted_height(const ExchangeView *view)
{
    return EXCHANGE_MARGIN * 2.0f + EXCHANGE_TITLE_H + EXCHANGE_HEAD_H +
           (float)view->row_count * (EXCHANGE_ROW_H + EXCHANGE_ROW_GAP) +
           EXCHANGE_FOOTER_H;
}

static UiRect panel_rect(const ExchangeView *view,
                         float screen_w, float screen_h)
{
    float max_h = EXCHANGE_MAX_H;
    /* Never taller than the screen minus a margin, whatever the
     * constant says — a small window is a clamp, not a crash. */
    if (max_h > screen_h - 80.0f) max_h = screen_h - 80.0f;
    return ui_panel_centered(screen_w, screen_h, EXCHANGE_W,
                             wanted_height(view), max_h);
}

/* Rows available inside a panel of this height. */
static int rows_per_page(UiRect panel)
{
    float body = panel.h - (EXCHANGE_MARGIN * 2.0f + EXCHANGE_TITLE_H +
                            EXCHANGE_HEAD_H + EXCHANGE_FOOTER_H);
    int   n    = ui_rows_that_fit(body, EXCHANGE_ROW_H, EXCHANGE_ROW_GAP);
    return n < 1 ? 1 : n;   /* always offer one row, even if cramped */
}

int exchange_page_count(const ExchangeView *view, float screen_h)
{
    UiRect panel = panel_rect(view, 1920.0f, screen_h);
    UiPage p     = ui_paginate(view->row_count, rows_per_page(panel), 0);
    return p.pages;
}

uint32_t exchange_row_id(uint16_t ident)
{
    return ui_id(UI_GROUP_RESOURCE, ident);
}

UiRect exchange_col_rect(UiRect row, ExchangeCol col)
{
    static const float W[EX_COL_COUNT] = {
        EXCHANGE_COL_NAME, EXCHANGE_COL_YOURS, EXCHANGE_COL_THEIRS,
        EXCHANGE_COL_BID,  EXCHANGE_COL_ASK,   EXCHANGE_COL_TREND
    };
    UiRect r = row;
    int    i;

    if (col < 0 || col >= EX_COL_COUNT) {
        r.w = r.h = 0.0f;
        return r;
    }
    for (i = 0; i < (int)col; i++) r.x += W[i];
    r.w = W[col];
    return r;
}

/* ---- the builder ------------------------------------------ */

void exchange_build(UiList *out, const ExchangeView *view,
                    const UiState *st, float screen_w, float screen_h)
{
    UiRect   panel = panel_rect(view, screen_w, screen_h);
    UiRect   body;
    UiLayout l;
    UiPage   page;
    int      i, a;
    char     label[UI_LABEL_LEN];

    ui_list_reset(out);

    /* The panel itself is a widget so that a click landing on it is
     * absorbed rather than falling through to the world (and, in the
     * caller, distinguishable from a click outside that dismisses). */
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), panel,
                 view->title, 0, 0);

    body = ui_inset(panel, EXCHANGE_MARGIN);
    l    = ui_layout(body, 0.0f);

    (void)ui_row(&l, EXCHANGE_TITLE_H);          /* title, drawn from  */
                                                 /* the view's title   */
    (void)ui_row(&l, EXCHANGE_HEAD_H);           /* column headings    */

    page = ui_paginate(view->row_count, rows_per_page(panel),
                       st ? st->exchange_page : 0);

    for (i = 0; i < page.count; i++) {
        const ExchangeRow *row = &view->rows[page.first + i];
        UiRect             rr  = ui_row(&l, EXCHANGE_ROW_H);

        l.cursor += EXCHANGE_ROW_GAP;

        /* The row itself: a header (never clickable) carrying the
         * resource identity, so the drawer knows where to put the
         * numbers without recomputing the layout. */
        ui_list_push(out, exchange_row_id(row->ident), rr, row->name,
                     (int32_t)row->ident, UI_W_HEADER);

        /* The one place the two kinds diverge, as decision 4 allows: */
        if (view->kind == EXCHANGE_OFFER) {
            UiRect take = ui_col_from_right(rr, EXCHANGE_BTN_W,
                                            EXCHANGE_BTN_GAP, 1);
            UiRect put  = ui_col_from_right(rr, EXCHANGE_BTN_W,
                                            EXCHANGE_BTN_GAP, 0);
            take.y += (EXCHANGE_ROW_H - 26.0f) * 0.5f;  take.h = 26.0f;
            put.y  += (EXCHANGE_ROW_H - 26.0f) * 0.5f;  put.h  = 26.0f;

            ui_list_push(out, ui_id(UI_GROUP_SELL, row->ident), take,
                         "Take", -1, 0);
            if (row->theirs <= 0)
                ui_list_disable_last(out, REJ_NO_STOCK);

            ui_list_push(out, ui_id(UI_GROUP_BUY, row->ident), put,
                         "Stage 10", 10, 0);
            if (row->yours < 10)
                ui_list_disable_last(out, REJ_NO_STOCK);
            continue;
        }

        for (a = 0; a < EXCHANGE_ACTIONS; a++) {
            UiRect   br  = ui_col_from_right(rr, EXCHANGE_BTN_W,
                                             EXCHANGE_BTN_GAP,
                                             EXCHANGE_ACTIONS - 1 - a);
            int      sell = action_is_sell(a);
            int32_t  qty  = ACTION_QTY[a];
            uint32_t id   = ui_id(sell ? UI_GROUP_SELL : UI_GROUP_BUY,
                                  row->ident);
            RejectReason why = REJ_OK;

            /* Vertically centre the button in the row rather than
             * filling it: 34px of clickable height reads as a slab. */
            br.y += (EXCHANGE_ROW_H - 26.0f) * 0.5f;
            br.h  = 26.0f;

            copy_str(label, sizeof(label), ACTION_LABEL[a]);
            ui_list_push(out, id, br, label, qty, 0);

            /* Why this particular button cannot be pressed. The player
             * gets the same vocabulary the sim rejects with, so a
             * greyed button and a refused command say the same thing. */
            if (sell) {
                if (row->yours <= 0)                    why = REJ_NO_STOCK;
                else if (!action_is_all(a) && row->yours < qty)
                                                        why = REJ_NO_STOCK;
                else if (row->refuse == (uint8_t)REJ_COUNTERPARTY_NO_GOLD)
                                                        why = REJ_COUNTERPARTY_NO_GOLD;
            } else {
                int32_t headroom = view->capacity - row->yours;
                int32_t want     = action_is_all(a) ? 1 : qty;

                if (row->theirs != EXCHANGE_INFINITE && row->theirs <= 0)
                                                        why = REJ_NO_STOCK;
                else if (headroom < want)               why = REJ_NO_STORAGE;
                else if (view->your_gold < row->ask * want)
                                                        why = REJ_CANT_AFFORD;
            }
            if (why != REJ_OK) ui_list_disable_last(out, why);
        }
    }

    /* ---- footer ------------------------------------------
     * The second permitted divergence between the two kinds: a quotes
     * screen pages, an offer screen carries the blockade lever. */
    if (view->kind == EXCHANGE_OFFER) {
        UiRect footer = ui_row(&l, EXCHANGE_FOOTER_H);
        UiRect dock   = { footer.x, footer.y + 4.0f, 210.0f, 30.0f };
        UiRect close  = { footer.x + footer.w - 110.0f, footer.y + 4.0f,
                          110.0f, 30.0f };

        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_DOCKING), dock,
                     view->docking_allowed ? "Harbour: open to ships"
                                           : "Harbour: closed (blockade)",
                     view->docking_allowed, 0);
        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_CLOSE), close,
                     "Close", 0, 0);
        return;
    }

    {
        UiRect footer = ui_row(&l, EXCHANGE_FOOTER_H);
        UiRect prev   = { footer.x, footer.y + 4.0f, 70.0f, 30.0f };
        UiRect next   = { footer.x + 160.0f, footer.y + 4.0f, 70.0f, 30.0f };
        UiRect label_r= { footer.x + 76.0f, footer.y + 4.0f, 80.0f, 30.0f };
        UiRect close  = { footer.x + footer.w * 0.5f - 55.0f,
                          footer.y + 4.0f, 110.0f, 30.0f };

        /* The pager is drawn even on a single page, greyed: a control
         * that appears only when needed teaches the player nothing
         * about where they are, and moves the Close button's neighbours
         * around underneath the cursor. */
        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_PREV), prev,
                     "< Prev", page.page - 1, 0);
        if (page.page <= 0) ui_list_disable_last(out, REJ_UNAVAILABLE);

        snprintf(label, sizeof(label), "%d / %d", page.page + 1, page.pages);
        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), label_r,
                     label, page.pages, UI_W_HEADER);

        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NEXT), next,
                     "Next >", page.page + 1, 0);
        if (page.page >= page.pages - 1)
            ui_list_disable_last(out, REJ_UNAVAILABLE);

        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_CLOSE), close,
                     "Close", 0, 0);
    }
}

/* ---- decoding a click -------------------------------------- */

ExchangeHit exchange_hit(const UiList *list, const ExchangeView *view,
                         const UiState *st, float x, float y)
{
    ExchangeHit     hit;
    const UiWidget *w;

    hit.kind  = EXCHANGE_HIT_OUTSIDE;
    hit.res   = -1;
    hit.qty   = 0;
    hit.price = 0;
    hit.page  = st ? st->exchange_page : 0;
    memset(&hit.rect, 0, sizeof(hit.rect));

    w = ui_list_hit(list, x, y);
    if (!w) return hit;
    hit.rect = w->rect;

    switch (ui_id_group(w->id)) {
    case UI_GROUP_SELL:
    case UI_GROUP_BUY: {
        int      sell = ui_id_group(w->id) == UI_GROUP_SELL;
        int      res  = (int)ui_id_value(w->id);
        int      i;

        hit.kind = sell ? EXCHANGE_HIT_SELL : EXCHANGE_HIT_BUY;
        hit.res  = res;
        hit.qty  = w->value;   /* count, or -1 for "all"/"max" */

        /* The price this row was DISPLAYING, which travels with the
         * command as its limit. Read from the view rather than
         * recomputed, so it is literally the number the player saw. */
        for (i = 0; view && i < view->row_count; i++)
            if (view->rows[i].ident == (uint16_t)res) {
                hit.price = sell ? view->rows[i].bid : view->rows[i].ask;
                break;
            }
        break;
    }

    case UI_GROUP_ACTION:
        switch ((UiAction)ui_id_value(w->id)) {
        case UI_ACTION_CLOSE: hit.kind = EXCHANGE_HIT_CLOSE; break;
        case UI_ACTION_PREV:
        case UI_ACTION_NEXT:
            hit.kind = EXCHANGE_HIT_PAGE;
            hit.page = w->value;
            break;
        case UI_ACTION_DOCKING:
            hit.kind = EXCHANGE_HIT_DOCKING;
            hit.qty  = w->value ? 0 : 1;   /* the state to switch TO */
            break;
        default:
            hit.kind = EXCHANGE_HIT_NONE;   /* the panel: absorb it    */
            break;
        }
        break;

    default:
        hit.kind = EXCHANGE_HIT_NONE;
        break;
    }
    return hit;
}
