/* yard_view.c  --  The yard, and the fleet it launches (UI_PLAN N6) */

#include "yard_view.h"
#include "island_bar.h"
#include "island.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>

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

/* The next ship this one could guard, cycling through the fleet and
 * round to "nobody". Content-derived: the button carries the ship it
 * SELECTS, so a recorded click keeps its meaning even if the fleet
 * changed shape between the frame and the click. */
static int32_t next_escort(const UiSnapshot *snap, int me, int32_t current)
{
    int cand;

    /* Ascending from whoever is guarded now, and off the end into
     * "nobody" — rather than modulo arithmetic round the fleet. */
    for (cand = (int)current + 1; cand < snap->ship_count; cand++) {
        if (cand == me) continue;                  /* not itself       */
        if (!snap->ships[cand].active) continue;
        if (!snap->ships[cand].mine) continue;     /* your own only    */
        return cand;
    }
    return -1;                                     /* round to nobody  */
}

void yard_view_build(YardView *v, const UiSnapshot *snap, int island)
{
    const UiIsland *isl;
    int             i, r;

    memset(v, 0, sizeof(*v));
    v->fleet_cap = MAX_SHIPS;

    if (!snap || island < 0 || island >= MAX_ISLANDS) {
        copy_str(v->title, sizeof(v->title), "Shipyard");
        return;
    }

    isl        = &snap->islands[island];
    v->island  = island;
    v->tick    = snap->tick;
    snprintf(v->title, sizeof(v->title), "Shipyard — %s", isl->name);
    island_hue(island, &v->hue_r, &v->hue_g, &v->hue_b);

    v->yours     = (uint8_t)(isl->owner != PLAYER_NONE &&
                             isl->owner == snap->local_player_id);
    v->your_gold = v->yours ? isl->stock[RES_GOLD] : 0;

    /* The yard's offer: three hulls, their numbers side by side. The
     * whole point of the screen is that these three rows are read
     * against each other before any gold moves. */
    for (i = 0; i < SHIP_CLASS_COUNT; i++) {
        YardRow *row = &v->rows[v->row_count++];

        memset(row, 0, sizeof(*row));
        row->is_hull    = 1;
        row->klass      = i;
        row->ship       = -1;
        row->escorting  = -1;
        row->guns       = SHIP_CLASSES[i].guns;
        row->hull       = SHIP_CLASSES[i].hull;
        row->hull_max   = SHIP_CLASSES[i].hull;
        row->hold       = SHIP_CLASSES[i].hold;
        row->cost       = SHIP_CLASSES[i].gold;
        row->at_island  = island;
        row->to_island  = island;
        row->affordable = (uint8_t)(v->yours && v->your_gold >= row->cost);
        copy_str(row->name, sizeof(row->name), SHIP_CLASSES[i].name);
    }

    /* The fleet: yours, wherever it is. A ship at sea is still yours to
     * look at — its condition is exactly what you want to know while it
     * is out there and cannot be helped. */
    for (i = 0; i < snap->ship_count && v->row_count < YARD_MAX_ROWS; i++) {
        const UiShip *sh = &snap->ships[i];
        YardRow      *row;

        if (!sh->active || !sh->mine) continue;

        row = &v->rows[v->row_count++];
        memset(row, 0, sizeof(*row));
        row->is_hull     = 0;
        row->klass       = sh->klass;
        row->ship        = i;
        row->guns        = sh->guns;
        row->hull        = sh->hull;
        row->hull_max    = sh->hull_max;
        row->hold        = sh->hold;
        row->at_island   = sh->at_island;
        row->to_island   = sh->to_island;
        row->escorting   = sh->escorting;
        row->escort_next = next_escort(snap, i, sh->escorting);

        for (r = 0; r < RES_COUNT; r++)
            if (r != (int)RES_GOLD) row->cargo_units += sh->cargo[r];

        snprintf(row->name, sizeof(row->name), "%s %d",
                 (sh->klass >= 0 && sh->klass < SHIP_CLASS_COUNT)
                     ? SHIP_CLASSES[sh->klass].name : "Ship", i);
        v->fleet_size++;
    }
}

/* ---- geometry ---------------------------------------------- */

static float wanted_height(const YardView *view)
{
    return YARD_MARGIN * 2.0f + YARD_TITLE_H + YARD_HEAD_H +
           (float)view->row_count * (YARD_ROW_H + YARD_ROW_GAP) +
           YARD_FOOTER_H;
}

static UiRect panel_rect(const YardView *view, float screen_w, float screen_h)
{
    float max_h = YARD_MAX_H;
    if (max_h > screen_h - 80.0f) max_h = screen_h - 80.0f;
    return ui_panel_centered(screen_w, screen_h, YARD_W,
                             wanted_height(view), max_h);
}

static int rows_per_page(UiRect panel)
{
    float body = panel.h - (YARD_MARGIN * 2.0f + YARD_TITLE_H +
                            YARD_HEAD_H + YARD_FOOTER_H);
    int   n    = ui_rows_that_fit(body, YARD_ROW_H, YARD_ROW_GAP);
    return n < 1 ? 1 : n;
}

int yard_page_count(const YardView *view, float screen_h)
{
    UiRect panel = panel_rect(view, 1920.0f, screen_h);
    UiPage p     = ui_paginate(view->row_count, rows_per_page(panel), 0);
    return p.pages;
}

UiRect yard_col_rect(UiRect row, YardCol col)
{
    static const float W[YD_COL_COUNT] = {
        YARD_COL_NAME, YARD_COL_GUNS, YARD_COL_HULL,
        YARD_COL_HOLD, YARD_COL_WHERE
    };
    UiRect r = row;
    int    i;

    if (col < 0 || col >= YD_COL_COUNT) {
        r.w = r.h = 0.0f;
        return r;
    }
    for (i = 0; i < (int)col; i++) r.x += W[i];
    r.w = W[col];
    return r;
}

/* ---- the builder ------------------------------------------- */

void yard_build(UiList *out, const YardView *view, const UiState *st,
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

    body = ui_inset(panel, YARD_MARGIN);
    l    = ui_layout(body, 0.0f);

    (void)ui_row(&l, YARD_TITLE_H);
    (void)ui_row(&l, YARD_HEAD_H);

    page = ui_paginate(view->row_count, rows_per_page(panel),
                       st ? st->yard_page : 0);

    for (i = 0; i < page.count; i++) {
        const YardRow *row = &view->rows[page.first + i];
        UiRect         rr  = ui_row(&l, YARD_ROW_H);
        UiRect         btn;

        l.cursor += YARD_ROW_GAP;

        if (row->is_hull) {
            ui_list_push(out, ui_id(UI_GROUP_HULL, (uint16_t)row->klass),
                         rr, row->name, row->klass, UI_W_HEADER);

            btn = ui_col_from_right(rr, YARD_BTN_W, YARD_BTN_GAP, 0);
            btn.y += (YARD_ROW_H - 24.0f) * 0.5f;
            btn.h  = 24.0f;
            ui_list_push(out, ui_id(UI_GROUP_BUILD_HULL,
                                    (uint16_t)row->klass), btn,
                         "Lay down", row->cost, 0);
            /* Greyed rather than refused when it is only the money:
             * UI_W_MUTED exists for "not right now" and the confirm
             * popup can still show what it would cost (Phase 3). */
            if (!view->yours)
                ui_list_disable_last(out, REJ_NOT_OWNER);
            else if (view->fleet_size >= view->fleet_cap)
                ui_list_disable_last(out, REJ_UNAVAILABLE);
            else if (!row->affordable)
                out->items[out->count - 1].flags |= UI_W_MUTED;
            continue;
        }

        ui_list_push(out, ui_id(UI_GROUP_SHIP, (uint16_t)row->ship), rr,
                     row->name, row->ship, UI_W_HEADER);

        /* Who this hull is guarding, as one cycling button. The value
         * is the ship it would guard NEXT — decoded at hit time, so
         * the click and the label cannot disagree. */
        btn = ui_col_from_right(rr, YARD_BTN_W + 40.0f, YARD_BTN_GAP, 0);
        btn.y += (YARD_ROW_H - 24.0f) * 0.5f;
        btn.h  = 24.0f;

        if (row->escorting >= 0)
            snprintf(label, sizeof(label), "Guarding %d", row->escorting);
        else
            copy_str(label, sizeof(label), "Guarding —");

        ui_list_push(out, ui_id(UI_GROUP_ESCORT, (uint16_t)row->ship), btn,
                     label, row->escort_next, 0);
        if (!view->yours && !row->is_hull)
            ui_list_disable_last(out, REJ_NOT_OWNER);
        else if (view->fleet_size < 2)
            /* A convoy needs a second hull. Saying so beats a button
             * that cycles for ever between "nobody" and "nobody". */
            ui_list_disable_last(out, REJ_NO_TARGET);
    }

    {
        UiRect footer = ui_row(&l, YARD_FOOTER_H);
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

YardHit yard_hit(const UiList *list, const YardView *view,
                 const UiState *st, float x, float y)
{
    YardHit         hit;
    const UiWidget *w;

    (void)view;

    memset(&hit, 0, sizeof(hit));
    hit.kind   = YARD_HIT_OUTSIDE;
    hit.klass  = -1;
    hit.ship   = -1;
    hit.target = -1;
    hit.page   = st ? st->yard_page : 0;

    w = ui_list_hit(list, x, y);
    if (!w) return hit;
    hit.rect = w->rect;

    switch (ui_id_group(w->id)) {
    case UI_GROUP_BUILD_HULL:
        hit.kind  = YARD_HIT_BUILD;
        hit.klass = (int32_t)ui_id_value(w->id);
        break;

    case UI_GROUP_ESCORT:
        hit.kind   = YARD_HIT_ESCORT;
        hit.ship   = (int32_t)ui_id_value(w->id);
        hit.target = w->value;
        break;

    case UI_GROUP_ACTION:
        switch ((UiAction)ui_id_value(w->id)) {
        case UI_ACTION_CLOSE: hit.kind = YARD_HIT_CLOSE; break;
        case UI_ACTION_PREV:
        case UI_ACTION_NEXT:
            hit.kind = YARD_HIT_PAGE;
            hit.page = w->value;
            break;
        default:
            hit.kind = YARD_HIT_NONE;
            break;
        }
        break;

    default:
        hit.kind = YARD_HIT_NONE;
        break;
    }
    return hit;
}
