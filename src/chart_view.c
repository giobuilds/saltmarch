/* chart_view.c  --  The passages, and the maps of them (UI_PLAN N4) */

#include "chart_view.h"
#include "island_bar.h"
#include "island.h"
#include "orderbook.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>

/* Every destination fits with its three routes, and there is room left
 * over for passages that rotate out while the panel is open. Checked
 * rather than commented, like ui_snapshot.c's bounds. */
typedef char chart_view_bounds_check[
    (CHART_MAX_ROWS >= (MAX_ISLANDS - 1) * (1 + SEA_ROUTES_PER_PAIR)) ? 1 : -1];

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

/* ---- the retained fold ------------------------------------- */

void chart_view_reset(ChartView *v)
{
    int32_t island = v->island;
    memset(v, 0, sizeof(*v));
    v->island = island;
}

static int find_route_row(const ChartView *v, int route_id)
{
    int i;
    for (i = 0; i < v->row_count; i++)
        if (!v->rows[i].header && v->rows[i].route_id == route_id) return i;
    return -1;
}

static int find_header_row(const ChartView *v, int island)
{
    int i;
    for (i = 0; i < v->row_count; i++)
        if (v->rows[i].header && v->rows[i].island == island) return i;
    return -1;
}

/* One past the last row belonging to `island` — where a passage that has
 * just come into play joins its own destination rather than the bottom
 * of the panel. */
static int end_of_group(const ChartView *v, int island)
{
    int i, at = v->row_count;
    for (i = 0; i < v->row_count; i++)
        if (v->rows[i].island == island) at = i + 1;
    return at;
}

/* Make room at `at` and return it, or -1 if the view is full. Rows are
 * never evicted here: unlike an order book's stale rows, a retired
 * passage is one of a fixed handful and the whole list is bounded by the
 * number of islands. */
static int insert_at(ChartView *v, int at)
{
    int i;

    if (v->row_count >= CHART_MAX_ROWS) return -1;
    if (at < 0 || at > v->row_count) at = v->row_count;

    for (i = v->row_count; i > at; i--) v->rows[i] = v->rows[i - 1];
    v->row_count++;
    memset(&v->rows[at], 0, sizeof(v->rows[at]));
    return at;
}

/* The best resting price on either side of every chart, in one pass.
 * Per row would be a scan of the whole book per row per frame, and the
 * answer is the same one. */
static void chart_quotes(const UiSnapshot *snap, int32_t *ask, int32_t *bid)
{
    int i;

    memset(ask, 0, sizeof(int32_t) * UI_MAX_ROUTES);
    memset(bid, 0, sizeof(int32_t) * UI_MAX_ROUTES);

    for (i = 0; i < snap->order_count; i++) {
        const UiOrder *o = &snap->order[i];
        int            r;

        if (o->kind != (uint16_t)TRADE_ROUTE_CHART) continue;
        r = (int)o->what;
        if (r < 0 || r >= UI_MAX_ROUTES) continue;

        if (o->side == ORDER_SELL) {
            if (ask[r] == 0 || o->limit < ask[r]) ask[r] = o->limit;
        } else {
            if (o->limit > bid[r]) bid[r] = o->limit;
        }
    }
}

/* Is one of OUR expeditions out towards `to`? Scoped to this player's */
static int survey_out(const UiSnapshot *snap, int from, int to,
                      uint64_t *out_back)
{
    int i, found = 0;

    for (i = 0; i < snap->survey_count; i++) {
        if (snap->survey[i].from_island != from) continue;
        if (snap->survey[i].to_island   != to)   continue;
        /* The soonest one home, if somehow there are two: a countdown
         * should say when the next answer arrives. */
        if (!found || snap->survey[i].finish_tick < *out_back)
            *out_back = snap->survey[i].finish_tick;
        found = 1;
    }
    return found;
}

void chart_view_update(ChartView *v, const UiSnapshot *snap, const Sea *sea,
                       int island)
{
    uint8_t         live[UI_MAX_ROUTES];
    int32_t         ask[UI_MAX_ROUTES], bid[UI_MAX_ROUTES];
    const UiIsland *isl;
    int             d, i;

    if (!sea || island < 0 || island >= MAX_ISLANDS) {
        chart_view_reset(v);
        copy_str(v->title, sizeof(v->title), "Passages");
        return;
    }
    if (v->island != island) {
        v->island = island;
        chart_view_reset(v);
    }

    isl     = &snap->islands[island];
    v->tick = snap->tick;
    snprintf(v->title, sizeof(v->title), "Passages — %s", isl->name);
    island_hue(island, &v->hue_r, &v->hue_g, &v->hue_b);

    /* You post against your own harbour, so a foreign island's passages
     * are a screen you can read and not act on. Its stores are not ours
     * to show either, and after N2 the way to say that is to say
     * nothing rather than zero. */
    v->yours        = (uint8_t)(isl->owner != PLAYER_NONE &&
                                isl->owner == snap->local_player_id);
    v->your_gold    = v->yours ? isl->stock[RES_GOLD]   : 0;
    v->blank_charts = v->yours ? isl->stock[RES_CHARTS] : 0;

    /* What is free to send. Both are capacities the snapshot already
     * resolved (UI_PLAN N1), so no screen reproduces the rule that
     * decides how many scholars a Scholars' House keeps. */
    v->scholars_free = v->yours
                     ? isl->scholar_capacity - isl->scholars_out : 0;
    v->boats_free    = v->yours ? isl->research_boats : 0;
    if (v->scholars_free < 0) v->scholars_free = 0;

    memset(live, 0, sizeof(live));
    chart_quotes(snap, ask, bid);

    for (d = 0; d < sea->island_count && d < MAX_ISLANDS; d++) {
        int pair = sea_pair_index(sea, island, d);
        int variant, at;

        if (d == island || pair < 0) continue;

        at = find_header_row(v, d);
        if (at < 0) {
            at = insert_at(v, end_of_group(v, d));
            if (at < 0) continue;
            v->rows[at].header   = 1;
            v->rows[at].island   = d;
            v->rows[at].route_id = -1;
            copy_str(v->rows[at].name, sizeof(v->rows[at].name),
                     snap->islands[d].name);
        }
        {
            uint64_t back = 0;
            v->rows[at].surveying   = (uint8_t)survey_out(snap, island, d,
                                                          &back);
            v->rows[at].survey_back = back;
        }

        for (variant = 0; variant < SEA_ROUTES_PER_PAIR; variant++) {
            const Route *rt   = sea_route_variant(sea, island, d, variant);
            const Route *lane = sea_route_variant(sea, island, d,
                                                  SEA_ROUTE_PUBLIC);
            ChartRow    *row;
            int          rid;

            if (!rt) continue;
            rid = sea_route_id(sea, rt);
            if (rid < 0 || rid >= UI_MAX_ROUTES) continue;

            live[rid] = 1;

            i = find_route_row(v, rid);
            if (i < 0) {
                i = insert_at(v, end_of_group(v, d));
                if (i < 0) continue;
                v->rows[i].island   = d;
                v->rows[i].route_id = rid;
                copy_str(v->rows[i].name, sizeof(v->rows[i].name), rt->name);
            }

            row = &v->rows[i];
            row->variant    = variant;
            row->is_private = (uint8_t)(rt->is_private ? 1 : 0);
            row->known      = snap->route_known[rid];
            row->charts     = snap->chart_held[rid];
            row->ticks      = rt->total_ticks;
            row->saves      = (lane && lane->total_ticks > rt->total_ticks)
                            ? (int32_t)(lane->total_ticks - rt->total_ticks)
                            : 0;
            row->ask        = ask[rid];
            row->bid        = bid[rid];

            /* When this water goes out of use. The lane never does. */
            row->expires_tick = 0u;
            if (rt->is_private && variant >= 1)
                row->expires_tick =
                    sea_pair_next_rotation(sea->island_count, pair,
                                           snap->tick) +
                    (uint64_t)(variant - 1) * SEA_ROUTE_LIFETIME_TICKS;
        }

        /* Whether an expedition to this island could sail, said in the
         * sim's own vocabulary (UI_PLAN N7). */
        {
            ChartRow *head = &v->rows[find_header_row(v, d)];
            int       unknown = 0, k;

            for (k = 0; k < v->row_count; k++)
                if (!v->rows[k].header && v->rows[k].island == d &&
                    v->rows[k].is_private && !v->rows[k].gone &&
                    !v->rows[k].known) unknown = 1;

            /* IN THE SIM'S ORDER, not in the order a player might find */
            if (!v->yours)                  head->survey_reason = REJ_NOT_OWNER;
            else if (v->scholars_free <= 0) head->survey_reason = REJ_NO_CREW;
            else if (v->boats_free <= 0)    head->survey_reason = REJ_NO_BOAT;
            else if (v->blank_charts <= 0)  head->survey_reason = REJ_NO_STOCK;
            else if (!unknown)              head->survey_reason = REJ_NOTHING_TO_FIND;
            else                            head->survey_reason = REJ_OK;
        }
    }

    /* What has left play since the last frame. Marked in place and never
     * removed while the panel is open: the passage that replaced it was
     * appended below, so nothing the cursor was travelling toward has
     * moved. */
    for (i = 0; i < v->row_count; i++) {
        ChartRow *row = &v->rows[i];
        if (row->header) continue;
        if (!live[row->route_id]) {
            row->gone   = 1;
            row->charts = snap->chart_held[row->route_id];  /* voided: 0 */
        }
    }
}

/* ---- geometry ---------------------------------------------- */

static float wanted_height(const ChartView *view)
{
    return CHART_MARGIN * 2.0f + CHART_TITLE_H + CHART_HEAD_H +
           (float)view->row_count * (CHART_ROW_H + CHART_ROW_GAP) +
           CHART_FOOTER_H;
}

static UiRect panel_rect(const ChartView *view, float screen_w, float screen_h)
{
    float max_h = CHART_MAX_H;
    if (max_h > screen_h - 80.0f) max_h = screen_h - 80.0f;
    return ui_panel_centered(screen_w, screen_h, CHART_W,
                             wanted_height(view), max_h);
}

static int rows_per_page(UiRect panel)
{
    float body = panel.h - (CHART_MARGIN * 2.0f + CHART_TITLE_H +
                            CHART_HEAD_H + CHART_FOOTER_H);
    int   n    = ui_rows_that_fit(body, CHART_ROW_H, CHART_ROW_GAP);
    return n < 1 ? 1 : n;
}

int chart_page_count(const ChartView *view, float screen_h)
{
    UiRect panel = panel_rect(view, 1920.0f, screen_h);
    UiPage p     = ui_paginate(view->row_count, rows_per_page(panel), 0);
    return p.pages;
}

UiRect chart_col_rect(UiRect row, ChartCol col)
{
    static const float W[CH_COL_COUNT] = {
        CHART_COL_NAME, CHART_COL_CROSS, CHART_COL_SAVES,
        CHART_COL_HELD, CHART_COL_EXPIRY, CHART_COL_PRICE
    };
    UiRect r = row;
    int    i;

    if (col < 0 || col >= CH_COL_COUNT) {
        r.w = r.h = 0.0f;
        return r;
    }
    for (i = 0; i < (int)col; i++) r.x += W[i];
    r.w = W[col];
    return r;
}

/* ---- the builder ------------------------------------------- */

/* Buy and Sell for one passage. Both carry the route as their identity */
static void push_actions(UiList *out, const ChartView *view,
                         const ChartRow *row, UiRect rr)
{
    UiRect btn;
    int    ro = !view->yours;

    /* Nothing to buy or sell for the patrolled lane: everyone has it,
     * and a map of it would be a map of the way in. */
    if (!row->is_private) return;

    btn = ui_col_from_right(rr, CHART_BTN_W, CHART_BTN_GAP, 0);
    btn.y += (CHART_ROW_H - 24.0f) * 0.5f;
    btn.h  = 24.0f;
    ui_list_push(out, ui_id(UI_GROUP_CHART_SELL, (uint16_t)row->route_id),
                 btn, "Sell", row->bid > 0 ? row->bid : row->ask, 0);
    if (ro)                 ui_list_disable_last(out, REJ_NOT_OWNER);
    else if (row->charts <= 0) ui_list_disable_last(out, REJ_NO_STOCK);
    else if (row->bid <= 0 && row->ask <= 0)
        ui_list_disable_last(out, REJ_UNAVAILABLE);

    btn = ui_col_from_right(rr, CHART_BTN_W, CHART_BTN_GAP, 1);
    btn.y += (CHART_ROW_H - 24.0f) * 0.5f;
    btn.h  = 24.0f;
    ui_list_push(out, ui_id(UI_GROUP_CHART_BUY, (uint16_t)row->route_id),
                 btn, "Buy", row->ask, 0);
    /* No resting offer is not a prohibition, it is the market not having
     * that map on the counter this week — the faction rotates which
     * passages it sells. Saying which of the two it is, in the sim's own
     * vocabulary, is the whole of decision 3. */
    if (ro)                    ui_list_disable_last(out, REJ_NOT_OWNER);
    else if (row->ask <= 0)    ui_list_disable_last(out, REJ_NO_STOCK);
    else if (view->your_gold < row->ask * CHART_LOT)
        ui_list_disable_last(out, REJ_CANT_AFFORD);
}

void chart_build(UiList *out, const ChartView *view, const UiState *st,
                 float screen_w, float screen_h)
{
    UiRect   panel = panel_rect(view, screen_w, screen_h);
    UiRect   body;
    UiLayout l;
    UiPage   page;
    int      i;
    char     label[UI_LABEL_LEN];

    ui_list_reset(out);

    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), panel,
                 view->title, 0, 0);

    body = ui_inset(panel, CHART_MARGIN);
    l    = ui_layout(body, 0.0f);

    (void)ui_row(&l, CHART_TITLE_H);
    (void)ui_row(&l, CHART_HEAD_H);

    page = ui_paginate(view->row_count, rows_per_page(panel),
                       st ? st->chart_page : 0);

    for (i = 0; i < page.count; i++) {
        const ChartRow *row = &view->rows[page.first + i];
        UiRect          rr  = ui_row(&l, CHART_ROW_H);

        l.cursor += CHART_ROW_GAP;

        if (row->header) {
            /* A destination, carrying the island it names. The row */
            UiRect btn;

            ui_list_push(out, ui_id(UI_GROUP_ISLAND, (uint16_t)row->island),
                         rr, row->name, row->surveying, UI_W_HEADER);

            btn = ui_col_from_right(rr, CHART_BTN_W + 60.0f, CHART_BTN_GAP, 0);
            btn.y += (CHART_ROW_H - 24.0f) * 0.5f;
            btn.h  = 24.0f;
            ui_list_push(out, ui_id(UI_GROUP_SURVEY, (uint16_t)row->island),
                         btn, "Send expedition", row->island, 0);
            if (row->survey_reason != (uint8_t)REJ_OK)
                ui_list_disable_last(out, (RejectReason)row->survey_reason);
            continue;
        }

        /* The row itself: a header carrying the route's identity, so the
         * drawer finds its columns without redoing the layout. A route
         * id fits sixteen bits, so unlike an order it needs no second
         * home in the value. */
        ui_list_push(out, ui_id(UI_GROUP_ROUTE, (uint16_t)row->route_id),
                     rr, row->name, row->route_id, UI_W_HEADER);

        push_actions(out, view, row, rr);
    }

    {
        UiRect footer = ui_row(&l, CHART_FOOTER_H);
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

ChartHit chart_hit(const UiList *list, const ChartView *view,
                   const UiState *st, float x, float y)
{
    ChartHit        hit;
    const UiWidget *w;

    (void)view;

    memset(&hit, 0, sizeof(hit));
    hit.kind     = CHART_HIT_OUTSIDE;
    hit.island   = -1;
    hit.route_id = -1;
    hit.page     = st ? st->chart_page : 0;

    w = ui_list_hit(list, x, y);
    if (!w) return hit;
    hit.rect = w->rect;

    switch (ui_id_group(w->id)) {
    case UI_GROUP_CHART_BUY:
        hit.kind     = CHART_HIT_BUY;
        hit.route_id = (int32_t)ui_id_value(w->id);
        hit.limit    = w->value;
        break;

    case UI_GROUP_CHART_SELL:
        hit.kind     = CHART_HIT_SELL;
        hit.route_id = (int32_t)ui_id_value(w->id);
        hit.limit    = w->value;
        break;

    case UI_GROUP_SURVEY:
        hit.kind   = CHART_HIT_SURVEY;
        hit.island = (int32_t)ui_id_value(w->id);
        break;

    case UI_GROUP_ACTION:
        switch ((UiAction)ui_id_value(w->id)) {
        case UI_ACTION_CLOSE: hit.kind = CHART_HIT_CLOSE; break;
        case UI_ACTION_PREV:
        case UI_ACTION_NEXT:
            hit.kind = CHART_HIT_PAGE;
            hit.page = w->value;
            break;
        default:
            hit.kind = CHART_HIT_NONE;   /* the panel: absorb it        */
            break;
        }
        break;

    default:
        hit.kind = CHART_HIT_NONE;
        break;
    }
    return hit;
}
