/*  inventory_view.c  --  The goods overlay (UI_PLAN Phase 4)
 *
 *  Layout and hit-testing; inventory_ui.c paints it. See the header for
 *  why this exists beside the corner panel rather than replacing it.
 */

#include "inventory_view.h"
#include "island_bar.h"
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

void inventory_view_build(InventoryView *out, const UiSnapshot *snap,
                          int island)
{
    const UiIsland  *isl;
    ResourceCategory cat;
    int              r, s;

    memset(out, 0, sizeof(*out));

    if (island < 0 || island >= MAX_ISLANDS) {
        copy_str(out->title, sizeof(out->title), "Stores");
        return;
    }
    isl = &snap->islands[island];

    snprintf(out->title, sizeof(out->title), "Stores — %s", isl->name);
    island_hue(island, &out->hue_r, &out->hue_g, &out->hue_b);
    out->residents = isl->residents;

    /* Grouped by category like the exchange screen, for the same
     * reason: a page of related goods reads, an arbitrary slice of the
     * enum does not. Gold is included here (unlike the exchange), since
     * this is a statement of what you have, not an offer to trade. */
    for (cat = RCAT_RAW; cat < RCAT_COUNT; cat++)
    for (r = 0; r < RES_COUNT && out->row_count < INVENTORY_MAX_ROWS; r++) {
        InventoryRow *row;

        if (RESOURCE_CATEGORIES[r] != cat) continue;

        row = &out->rows[out->row_count++];
        row->ident    = (uint16_t)r;
        row->category = (uint8_t)cat;
        copy_str(row->name, sizeof(row->name), RESOURCE_NAMES[r]);
        row->amount   = isl->stock[r];
        row->escrow   = isl->escrow[r];

        /* Gold is not subject to storage: it is not stacked in a
         * warehouse. Capacity 0 means "uncapped" to the drawer. */
        row->capacity = (r == (int)RES_GOLD) ? 0 : isl->capacity;
        row->full     = (uint8_t)(row->capacity > 0 &&
                                  row->amount >= row->capacity);

        for (s = 0; s < snap->ship_count && s < MAX_SHIPS; s++)
            if (snap->ships[s].active)
                row->ship_cargo += snap->ships[s].cargo[r];
    }
}

/* ---- geometry --------------------------------------------- */

UiRect inventory_col_rect(UiRect row, InventoryCol col)
{
    static const float W[INV_COL_COUNT] = { 160.0f, 110.0f, 140.0f, 170.0f };
    UiRect r = row;
    int    i;

    if (col < 0 || col >= INV_COL_COUNT) {
        r.w = r.h = 0.0f;
        return r;
    }
    for (i = 0; i < (int)col; i++) r.x += W[i];
    r.w = W[col];
    return r;
}

static float wanted_height(const InventoryView *v)
{
    return INVENTORY_MARGIN * 2.0f + INVENTORY_TITLE_H + INVENTORY_HEAD_H +
           (float)v->row_count * (INVENTORY_ROW_H + INVENTORY_ROW_GAP) +
           INVENTORY_FOOTER_H;
}

static UiRect panel_rect(const InventoryView *v, float sw, float sh)
{
    float max_h = INVENTORY_MAX_H;
    if (max_h > sh - 80.0f) max_h = sh - 80.0f;
    return ui_panel_centered(sw, sh, INVENTORY_W, wanted_height(v), max_h);
}

static int rows_per_page(UiRect panel)
{
    float body = panel.h - (INVENTORY_MARGIN * 2.0f + INVENTORY_TITLE_H +
                            INVENTORY_HEAD_H + INVENTORY_FOOTER_H);
    int   n = ui_rows_that_fit(body, INVENTORY_ROW_H, INVENTORY_ROW_GAP);
    return n < 1 ? 1 : n;
}

void inventory_build(UiList *out, const InventoryView *view,
                     const UiState *st, float screen_w, float screen_h)
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

    body = ui_inset(panel, INVENTORY_MARGIN);
    l    = ui_layout(body, 0.0f);

    (void)ui_row(&l, INVENTORY_TITLE_H);
    (void)ui_row(&l, INVENTORY_HEAD_H);

    page = ui_paginate(view->row_count, rows_per_page(panel),
                       st ? st->inventory_page : 0);

    for (i = 0; i < page.count; i++) {
        const InventoryRow *row = &view->rows[page.first + i];
        UiRect              rr  = ui_row(&l, INVENTORY_ROW_H);

        l.cursor += INVENTORY_ROW_GAP;

        /* Rows are display only — there is nothing to click on a good,
         * so they are headers carrying their identity for the drawer. */
        ui_list_push(out, ui_id(UI_GROUP_RESOURCE, row->ident), rr,
                     row->name, row->amount, UI_W_HEADER);
    }

    {
        UiRect footer  = ui_row(&l, INVENTORY_FOOTER_H);
        UiRect prev    = { footer.x, footer.y + 4.0f, 70.0f, 30.0f };
        UiRect label_r = { footer.x + 76.0f, footer.y + 4.0f, 80.0f, 30.0f };
        UiRect next    = { footer.x + 160.0f, footer.y + 4.0f, 70.0f, 30.0f };
        UiRect close   = { footer.x + footer.w - 110.0f, footer.y + 4.0f,
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

InventoryHit inventory_hit(const UiList *list, const UiState *st,
                           float x, float y)
{
    InventoryHit    hit;
    const UiWidget *w;

    hit.kind = INVENTORY_HIT_OUTSIDE;
    hit.page = st ? st->inventory_page : 0;

    w = ui_list_hit(list, x, y);
    if (!w) return hit;

    if (ui_id_group(w->id) == UI_GROUP_ACTION) {
        switch ((UiAction)ui_id_value(w->id)) {
        case UI_ACTION_CLOSE: hit.kind = INVENTORY_HIT_CLOSE; break;
        case UI_ACTION_PREV:
        case UI_ACTION_NEXT:
            hit.kind = INVENTORY_HIT_PAGE;
            hit.page = w->value;
            break;
        default: hit.kind = INVENTORY_HIT_NONE; break;
        }
    } else {
        hit.kind = INVENTORY_HIT_NONE;
    }
    return hit;
}
