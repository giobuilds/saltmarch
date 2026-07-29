/*  book_view.c  --  Your side of the order book (UI_PLAN N3)
 *
 *  Layout, the retained-row fold and hit-testing. No SDL, no drawing,
 *  no GameState — see book_view.h for why the rows are retained and why
 *  this is not a third ExchangeKind.
 */

#include "book_view.h"
#include "island_bar.h"
#include "orderbook.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>

/* The live half of a view is bounded by what the sim will let one
 * player hold; the rest is room for rows that have gone stale while the
 * panel was open. Checked rather than commented, like ui_snapshot.c's
 * bounds. */
typedef char book_view_bounds_check[
    (BOOK_MAX_ROWS > ORDERBOOK_MAX_PER_PLAYER) ? 1 : -1];

/* Stepper amounts. Coarse and fine, both directions, on both fields —
 * the same four everywhere so the hand learns one shape. */
static const int32_t QTY_STEP[4]   = { -10, -1, 1, 10 };
static const int32_t LIMIT_STEP[4] = { -10, -1, 1, 10 };
static const char   *STEP_LABEL[4] = { "-10", "-1", "+1", "+10" };

#define BOOK_QTY_MAX    9999
#define BOOK_LIMIT_MAX  99999

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

static int32_t clamp_i(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* What a row is called. A resource names itself; a chart names a route
 * id and nothing better until N4, which is the phase that knows how to
 * draw a passage. Naming it "Chart 17" is not a placeholder for want of
 * effort — the id IS the route's identity, and pretending to a name
 * before the map can show which water it means would be worse. */
static void row_name(char *dst, size_t cap, uint16_t kind, uint16_t what)
{
    if (kind == (uint16_t)TRADE_RESOURCE) {
        if (what < (uint16_t)RES_COUNT)
            copy_str(dst, cap, RESOURCE_NAMES[what]);
        else
            copy_str(dst, cap, "?");
        return;
    }
    snprintf(dst, cap, "Chart %u", (unsigned)what);
}

/* ---- the retained fold ------------------------------------- */

void book_view_reset(BookView *v)
{
    int32_t island = v->island;
    memset(v, 0, sizeof(*v));
    v->island = island;
    v->cap    = ORDERBOOK_MAX_PER_PLAYER;
}

static int find_row(const BookView *v, uint32_t id)
{
    int i;
    for (i = 0; i < v->row_count; i++)
        if (v->rows[i].id == id) return i;
    return -1;
}

/* Drop the oldest row that has already gone, so a full view can still
 * take a new order. Live rows are never evicted: they are the ones the
 * player can still act on. Returns 1 if it made room. */
static int evict_one(BookView *v)
{
    int i;
    for (i = 0; i < v->row_count; i++) {
        if (!v->rows[i].gone) continue;
        memmove(&v->rows[i], &v->rows[i + 1],
                sizeof(v->rows[0]) * (size_t)(v->row_count - i - 1));
        v->row_count--;
        return 1;
    }
    return 0;
}

/* Insert in ascending id order. Ids are monotonic, so this is almost
 * always an append — but the snapshot walks the book's ARRAY, and the
 * array reuses the slots of cancelled orders, so "the order it arrives
 * in" and "the order it was placed in" are not the same list. Sorting by
 * id here is what makes the displayed order stable frame to frame. */
static void insert_row(BookView *v, const UiOrder *o)
{
    int at, i;

    if (v->row_count >= BOOK_MAX_ROWS && !evict_one(v)) return;

    for (at = 0; at < v->row_count; at++)
        if (v->rows[at].id > o->id) break;

    for (i = v->row_count; i > at; i--) v->rows[i] = v->rows[i - 1];
    v->row_count++;

    memset(&v->rows[at], 0, sizeof(v->rows[at]));
    v->rows[at].id          = o->id;
    v->rows[at].kind        = o->kind;
    v->rows[at].what        = o->what;
    v->rows[at].side        = o->side;
    v->rows[at].qty         = o->qty;
    v->rows[at].limit       = o->limit;
    v->rows[at].placed_tick = o->placed_tick;
    row_name(v->rows[at].name, sizeof(v->rows[at].name), o->kind, o->what);
}

static int id_among(const uint32_t *ids, int n, uint32_t id)
{
    int i;
    for (i = 0; i < n; i++) if (ids[i] == id) return 1;
    return 0;
}

void book_view_update(BookView *v, const UiSnapshot *snap, int island,
                      const UiState *st)
{
    /* The ids that are still live this frame, collected first and
     * compared afterwards. Marking rows by INDEX as they are matched
     * would be the obvious way and would be wrong: inserting a row —
     * or evicting one to make room — moves every row after it, so the
     * marks would end up describing their neighbours. Ids do not
     * move. */
    uint32_t        live[UI_MAX_ORDERS];
    int             live_n = 0;
    const UiIsland *isl;
    int             i, res;

    if (island < 0 || island >= MAX_ISLANDS) {
        book_view_reset(v);
        copy_str(v->title, sizeof(v->title), "Order book");
        return;
    }
    if (v->island != island) {
        v->island = island;
        book_view_reset(v);
    }

    isl     = &snap->islands[island];
    v->tick = snap->tick;
    v->cap  = ORDERBOOK_MAX_PER_PLAYER;
    snprintf(v->title, sizeof(v->title), "Order book — %s", isl->name);
    island_hue(island, &v->hue_r, &v->hue_g, &v->hue_b);

    /* You post against your own harbour's stockpile, so a foreign
     * island's book is a screen you can read and not act on. Its gold
     * is not ours to show either — and after N2 the way to say that is
     * to say nothing, not to say zero. */
    v->yours     = (uint8_t)(isl->owner != PLAYER_NONE &&
                             isl->owner == snap->local_player_id);
    v->your_gold = v->yours ? isl->stock[RES_GOLD] : 0;

    for (i = 0; i < snap->order_count && live_n < UI_MAX_ORDERS; i++) {
        const UiOrder *o = &snap->order[i];
        int            at;

        if (!o->mine || o->island != island) continue;

        live[live_n++] = o->id;

        at = find_row(v, o->id);
        if (at < 0) insert_row(v, o);
        else        v->rows[at].qty = o->qty;   /* a partial fill: the
                                                 * number moves, the row
                                                 * does not            */
    }

    v->open_count = 0;
    for (i = 0; i < v->row_count; i++) {
        v->rows[i].gone = (uint8_t)!id_among(live, live_n, v->rows[i].id);
        if (!v->rows[i].gone) v->open_count++;
    }

    /* What the draft needs the world to tell it. */
    res = clamp_i(st ? st->book_res : 0, 0, (int32_t)RES_GOLD - 1);
    v->draft_quote = (st && st->book_side == ORDER_SELL) ? snap->bid[res]
                                                         : snap->ask[res];
    v->draft_stock = v->yours ? isl->stock[res] : 0;
    copy_str(v->draft_name, sizeof(v->draft_name), RESOURCE_NAMES[res]);
}

/* ---- the draft --------------------------------------------- */

void book_draft_default(UiState *st)
{
    if (!st) return;
    st->book_page  = 0;
    st->book_side  = ORDER_BUY;
    st->book_res   = 0;              /* the first good, whatever it is */
    st->book_qty   = 10;
    st->book_limit = 0;              /* 0 => follow the market's quote */
}

int32_t book_draft_limit(const BookView *v, const UiState *st)
{
    if (st && st->book_limit > 0) return st->book_limit;
    return v ? v->draft_quote : 0;
}

/* ---- geometry ---------------------------------------------- */

static float wanted_height(const BookView *view)
{
    return BOOK_MARGIN * 2.0f + BOOK_TITLE_H + BOOK_COMPOSER_H +
           BOOK_HEAD_H +
           (float)view->row_count * (BOOK_ROW_H + BOOK_ROW_GAP) +
           BOOK_FOOTER_H;
}

static UiRect panel_rect(const BookView *view, float screen_w, float screen_h)
{
    float max_h = BOOK_MAX_H;
    if (max_h > screen_h - 80.0f) max_h = screen_h - 80.0f;
    return ui_panel_centered(screen_w, screen_h, BOOK_W,
                             wanted_height(view), max_h);
}

static int rows_per_page(UiRect panel)
{
    float body = panel.h - (BOOK_MARGIN * 2.0f + BOOK_TITLE_H +
                            BOOK_COMPOSER_H + BOOK_HEAD_H + BOOK_FOOTER_H);
    int   n    = ui_rows_that_fit(body, BOOK_ROW_H, BOOK_ROW_GAP);
    return n < 1 ? 1 : n;
}

int book_page_count(const BookView *view, float screen_h)
{
    UiRect panel = panel_rect(view, 1920.0f, screen_h);
    UiPage p     = ui_paginate(view->row_count, rows_per_page(panel), 0);
    return p.pages;
}

UiRect book_col_rect(UiRect row, BookCol col)
{
    static const float W[BK_COL_COUNT] = {
        BOOK_COL_SIDE, BOOK_COL_NAME, BOOK_COL_QTY,
        BOOK_COL_LIMIT, BOOK_COL_VALUE, BOOK_COL_AGE
    };
    UiRect r = row;
    int    i;

    if (col < 0 || col >= BK_COL_COUNT) {
        r.w = r.h = 0.0f;
        return r;
    }
    for (i = 0; i < (int)col; i++) r.x += W[i];
    r.w = W[col];
    return r;
}

uint32_t book_row_id(uint32_t order_id)
{
    return ui_id(UI_GROUP_ORDER, (uint16_t)(order_id & 0xFFFFu));
}

/* ---- the builder ------------------------------------------- */

/* One stepper cluster: four buttons around a value. `action` is the
 * field they move; the widget's value is the STEP, decoded against the
 * current draft at hit time. Same id, different payload — the pattern
 * the exchange's quantity buttons already use. */
static float push_steppers(UiList *out, UiRect line, float x, float value_w,
                           UiAction action, const int32_t *step,
                           const char *value_text, int disabled)
{
    UiRect r;
    int    i;
    float  cursor = x;
    static const float SW[4] = { 40.0f, 34.0f, 34.0f, 40.0f };

    for (i = 0; i < 4; i++) {
        if (i == 2) {
            r.x = cursor; r.y = line.y; r.w = value_w; r.h = line.h;
            ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), r,
                         value_text, 0, UI_W_HEADER);
            cursor += value_w + BOOK_BTN_GAP;
        }
        r.x = cursor; r.y = line.y; r.w = SW[i]; r.h = line.h;
        ui_list_push(out, ui_id(UI_GROUP_ACTION, (uint16_t)action), r,
                     STEP_LABEL[i], step[i], 0);
        if (disabled) ui_list_disable_last(out, REJ_NOT_OWNER);
        cursor += SW[i] + BOOK_BTN_GAP;
    }
    return cursor;
}

/* The draft strip. Every button here is a fold over the draft rather
 * than a command: nothing is submitted until Post. */
static void build_composer(UiList *out, const BookView *view,
                           const UiState *st, UiRect strip)
{
    const int32_t side  = st ? st->book_side : 0;
    const int32_t res   = st ? st->book_res  : 0;
    const int32_t qty   = st ? st->book_qty  : 0;
    const int32_t limit = book_draft_limit(view, st);
    const int     ro    = !view->yours;      /* read-only: not your port */
    char          buf[UI_LABEL_LEN];
    UiRect        line, r;
    float         x;
    int           prev_res, next_res;

    /* ---- line one: side, good, quantity ---- */
    line = strip;
    line.h = 30.0f;

    r = line; r.w = 80.0f;
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_SIDE), r,
                 side == ORDER_SELL ? "Sell" : "Buy",
                 side == ORDER_SELL ? ORDER_BUY : ORDER_SELL, 0);
    if (ro) ui_list_disable_last(out, REJ_NOT_OWNER);
    x = r.x + r.w + BOOK_BTN_GAP;

    /* The good is chosen by cycling. The two buttons carry the resource
     * they SELECT as their identity, so neither one is "the button at
     * x=120" — a recorded click still means Beer when a good is added
     * above it. Gold is not a row: you cannot post to trade currency
     * for itself. */
    prev_res = (res + (int32_t)RES_GOLD - 1) % (int32_t)RES_GOLD;
    next_res = (res + 1) % (int32_t)RES_GOLD;

    r.x = x; r.w = 26.0f;
    ui_list_push(out, ui_id(UI_GROUP_RESOURCE, (uint16_t)prev_res), r,
                 "<", prev_res, 0);
    if (ro) ui_list_disable_last(out, REJ_NOT_OWNER);
    x += r.w + BOOK_BTN_GAP;

    r.x = x; r.w = 130.0f;
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), r,
                 view->draft_name, res, UI_W_HEADER);
    x += r.w + BOOK_BTN_GAP;

    r.x = x; r.w = 26.0f;
    ui_list_push(out, ui_id(UI_GROUP_RESOURCE, (uint16_t)next_res), r,
                 ">", next_res, 0);
    if (ro) ui_list_disable_last(out, REJ_NOT_OWNER);
    x += r.w + 18.0f;

    /* The captions live in the value labels rather than as separate
     * decoration, so every word on this strip is a widget in the list
     * the drawer walks — nothing is positioned twice. */
    snprintf(buf, sizeof(buf), "Qty %d", qty);
    push_steppers(out, line, x, 76.0f, UI_ACTION_QTY, QTY_STEP, buf, ro);

    /* ---- line two: limit, and Post ---- */
    line.y += 34.0f;

    if (limit > 0) snprintf(buf, sizeof(buf), "Limit %d ea", limit);
    else           copy_str(buf, sizeof(buf), "Limit: no quote");
    x = push_steppers(out, line, line.x, 110.0f, UI_ACTION_LIMIT, LIMIT_STEP,
                      buf, ro);

    /* Back to the quote. Worth a button of its own because a limit that
     * has drifted away from the market is otherwise only recoverable by
     * counting clicks back. */
    r = line; r.x = x; r.w = 74.0f;
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_MARKET), r,
                 "Market", 0, 0);
    if (ro)                       ui_list_disable_last(out, REJ_NOT_OWNER);
    else if (!st || !st->book_limit) ui_list_disable_last(out, REJ_UNAVAILABLE);
    x += r.w + 14.0f;

    /* What this order will reserve if it is posted — the number that
     * decides whether the player can afford it, spelled out rather than
     * left as a multiplication. */
    r = line; r.x = x; r.w = 170.0f;
    if (limit > 0 && qty > 0)
        snprintf(buf, sizeof(buf), "%s %d Gold",
                 side == ORDER_SELL ? "for" : "costs", qty * limit);
    else
        buf[0] = '\0';
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), r, buf,
                 qty * limit, UI_W_HEADER);

    r = line;
    r.x = strip.x + strip.w - 96.0f;
    r.w = 96.0f;
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_POST), r,
                 side == ORDER_SELL ? "Post sell" : "Post buy",
                 qty, 0);
    /* Why it cannot be posted, in the sim's own vocabulary — the same
     * refusal the command would come back with, said before it is
     * sent. */
    if (ro)
        ui_list_disable_last(out, REJ_NOT_OWNER);
    else if (qty <= 0 || limit <= 0)
        ui_list_disable_last(out, REJ_UNAVAILABLE);
    else if (view->open_count >= view->cap)
        ui_list_disable_last(out, REJ_UNAVAILABLE);
    else if (side == ORDER_SELL && view->draft_stock < qty)
        ui_list_disable_last(out, REJ_NO_STOCK);
    else if (side == ORDER_BUY && view->your_gold < qty * limit)
        ui_list_disable_last(out, REJ_CANT_AFFORD);
}

void book_build(UiList *out, const BookView *view, const UiState *st,
                float screen_w, float screen_h)
{
    UiRect   panel = panel_rect(view, screen_w, screen_h);
    UiRect   body, strip;
    UiLayout l;
    UiPage   page;
    int      i;
    char     label[UI_LABEL_LEN];

    ui_list_reset(out);

    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), panel,
                 view->title, 0, 0);

    body = ui_inset(panel, BOOK_MARGIN);
    l    = ui_layout(body, 0.0f);

    (void)ui_row(&l, BOOK_TITLE_H);
    strip = ui_row(&l, BOOK_COMPOSER_H);
    build_composer(out, view, st, strip);

    (void)ui_row(&l, BOOK_HEAD_H);

    page = ui_paginate(view->row_count, rows_per_page(panel),
                       st ? st->book_page : 0);

    for (i = 0; i < page.count; i++) {
        const BookRow *row = &view->rows[page.first + i];
        UiRect         rr  = ui_row(&l, BOOK_ROW_H);
        UiRect         btn;

        l.cursor += BOOK_ROW_GAP;

        /* The row: a header carrying the order's identity, so the
         * drawer can find its columns without redoing the layout. The
         * full 32-bit id is the VALUE — see book_view.h on why it
         * cannot be the id. */
        ui_list_push(out, book_row_id(row->id), rr, row->name,
                     (int32_t)row->id, UI_W_HEADER);

        btn = ui_col_from_right(rr, BOOK_BTN_W + 6.0f, BOOK_BTN_GAP, 0);
        btn.y += (BOOK_ROW_H - 24.0f) * 0.5f;
        btn.h  = 24.0f;

        ui_list_push(out, ui_id(UI_GROUP_CANCEL,
                                (uint16_t)(row->id & 0xFFFFu)), btn,
                     row->gone ? "Gone" : "Cancel", (int32_t)row->id, 0);
        if (row->gone) ui_list_disable_last(out, REJ_ORDER_GONE);
    }

    {
        UiRect footer = ui_row(&l, BOOK_FOOTER_H);
        UiRect prev   = { footer.x, footer.y + 4.0f, 70.0f, 30.0f };
        UiRect next   = { footer.x + 160.0f, footer.y + 4.0f, 70.0f, 30.0f };
        UiRect label_r= { footer.x + 76.0f, footer.y + 4.0f, 80.0f, 30.0f };
        UiRect close  = { footer.x + footer.w - 110.0f, footer.y + 4.0f,
                          110.0f, 30.0f };

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

BookHit book_hit(const UiList *list, const BookView *view,
                 const UiState *st, float x, float y)
{
    BookHit         hit;
    const UiWidget *w;
    int32_t         limit = book_draft_limit(view, st);

    memset(&hit, 0, sizeof(hit));
    hit.kind   = BOOK_HIT_OUTSIDE;
    hit.side   = st ? st->book_side : 0;
    hit.res    = st ? st->book_res  : 0;
    hit.qty    = st ? st->book_qty  : 0;
    hit.limit  = limit;
    hit.follow = (st && st->book_limit > 0) ? 0 : 1;
    hit.page   = st ? st->book_page : 0;

    w = ui_list_hit(list, x, y);
    if (!w) return hit;
    hit.rect = w->rect;

    switch (ui_id_group(w->id)) {
    case UI_GROUP_CANCEL:
        hit.kind     = BOOK_HIT_CANCEL;
        hit.order_id = (uint32_t)w->value;
        break;

    case UI_GROUP_RESOURCE:
        /* A limit typed against Wood means nothing against Beer, so
         * changing the good drops back to following the market. */
        hit.kind   = BOOK_HIT_GOOD;
        hit.res    = (int32_t)ui_id_value(w->id);
        hit.follow = 1;
        hit.limit  = 0;
        break;

    case UI_GROUP_ACTION:
        switch ((UiAction)ui_id_value(w->id)) {
        case UI_ACTION_CLOSE: hit.kind = BOOK_HIT_CLOSE; break;
        case UI_ACTION_PREV:
        case UI_ACTION_NEXT:
            hit.kind = BOOK_HIT_PAGE;
            hit.page = w->value;
            break;
        case UI_ACTION_SIDE:
            /* The two sides quote against different prices, so a side
             * change follows the market again for the same reason a
             * good change does. */
            hit.kind   = BOOK_HIT_SIDE;
            hit.side   = w->value;
            hit.follow = 1;
            hit.limit  = 0;
            break;
        case UI_ACTION_QTY:
            hit.kind = BOOK_HIT_QTY;
            hit.qty  = clamp_i(hit.qty + w->value, 0, BOOK_QTY_MAX);
            break;
        case UI_ACTION_LIMIT:
            hit.kind   = BOOK_HIT_LIMIT;
            hit.limit  = clamp_i(limit + w->value, 1, BOOK_LIMIT_MAX);
            hit.follow = 0;
            break;
        case UI_ACTION_MARKET:
            hit.kind   = BOOK_HIT_LIMIT;
            hit.limit  = view ? view->draft_quote : 0;
            hit.follow = 1;
            break;
        case UI_ACTION_POST:
            hit.kind = BOOK_HIT_POST;
            break;
        default:
            hit.kind = BOOK_HIT_NONE;   /* the panel: absorb it        */
            break;
        }
        break;

    default:
        hit.kind = BOOK_HIT_NONE;
        break;
    }
    return hit;
}
