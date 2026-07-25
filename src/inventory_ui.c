/*  inventory_ui.c  --  Painting the stores overlay (UI_PLAN Phase 4)
 *
 *  Consumes the UiList inventory_build() produced, like every other
 *  overlay since Phase 1.
 */

#include "inventory_ui.h"
#include "fonts.h"
#include "resource.h"

static SDL_FRect to_sdl(UiRect r)
{
    SDL_FRect s;
    s.x = r.x; s.y = r.y; s.w = r.w; s.h = r.h;
    return s;
}

static void fill(SDL_Renderer *r, UiRect rect,
                 Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    SDL_FRect f = to_sdl(rect);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderFillRect(r, &f);
}

static void outline(SDL_Renderer *r, UiRect rect,
                    Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    SDL_FRect f = to_sdl(rect);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderRect(r, &f);
}

static void cell(SDL_Renderer *r, UiRect col, const char *text, SDL_Color c)
{
    font_draw_text(r, FONT_SMALL, text, (int)(col.x + 8.0f),
                   (int)(col.y + 7.0f), c);
}

void inventory_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                       const UiList *list, const InventoryView *view,
                       int mouse_x, int mouse_y)
{
    const SDL_Color TITLE = { 200, 175, 110, 255 };
    const SDL_Color HEAD  = { 150, 135,  95, 255 };
    const SDL_Color TEXT  = { 220, 210, 185, 255 };
    const SDL_Color DIM   = { 130, 122, 105, 255 };
    const SDL_Color WARN  = { 225, 150, 110, 255 };
    UiRect          panel;
    int             i, headings_drawn = 0;
    char            buf[64];

    if (list->count == 0) return;
    panel = list->items[0].rect;

    {
        UiRect all = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        fill(renderer, all, 0, 0, 0, 160);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    fill(renderer, panel, 35, 28, 18, 255);
    outline(renderer, panel, 120, 100, 60, 255);

    {
        UiRect bar = { panel.x, panel.y, panel.w, INVENTORY_TITLE_H };
        fill(renderer, bar, 55, 44, 28, 255);
        SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
        SDL_RenderLine(renderer, panel.x, panel.y + INVENTORY_TITLE_H,
                       panel.x + panel.w, panel.y + INVENTORY_TITLE_H);
        {
            UiRect stripe = { panel.x, panel.y, 5.0f, INVENTORY_TITLE_H };
            fill(renderer, stripe, view->hue_r, view->hue_g, view->hue_b, 255);
        }

        font_draw_text(renderer, FONT_NORMAL, view->title,
                       (int)(panel.x + 14.0f), (int)(panel.y + 10.0f), TITLE);

        SDL_snprintf(buf, sizeof(buf), "Residents: %d", view->residents);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(panel.x + panel.w - 150.0f),
                       (int)(panel.y + 14.0f), TEXT);
    }

    for (i = 1; i < list->count; i++) {
        const UiWidget *w     = &list->items[i];
        int             hover = ui_point_in(w->rect, (float)mouse_x,
                                            (float)mouse_y);

        if (ui_id_group(w->id) == UI_GROUP_RESOURCE) {
            const InventoryRow *row = NULL;
            int                 j;

            for (j = 0; j < view->row_count; j++)
                if (view->rows[j].ident == ui_id_value(w->id)) {
                    row = &view->rows[j];
                    break;
                }
            if (!row) continue;

            if (!headings_drawn) {
                UiRect head = w->rect;
                head.y -= INVENTORY_HEAD_H;
                head.h  = INVENTORY_HEAD_H;
                cell(renderer, inventory_col_rect(head, INV_COL_NAME),
                     "Good", HEAD);
                cell(renderer, inventory_col_rect(head, INV_COL_AMOUNT),
                     "Stored", HEAD);
                cell(renderer, inventory_col_rect(head, INV_COL_CAPACITY),
                     "Capacity", HEAD);
                cell(renderer, inventory_col_rect(head, INV_COL_ELSEWHERE),
                     "At sea / escrow", HEAD);
                headings_drawn = 1;
            }

            if ((j % 2) == 0) fill(renderer, w->rect, 44, 36, 24, 255);

            cell(renderer, inventory_col_rect(w->rect, INV_COL_NAME),
                 row->name, TEXT);

            SDL_snprintf(buf, sizeof(buf), "%d", row->amount);
            cell(renderer, inventory_col_rect(w->rect, INV_COL_AMOUNT),
                 buf, row->full ? WARN : TEXT);

            /* Capacity as a bar, not just a number: "nearly full" is the
             * thing worth seeing, and a fraction makes you do arithmetic. */
            if (row->capacity > 0) {
                UiRect track = inventory_col_rect(w->rect, INV_COL_CAPACITY);
                UiRect fillr;
                float  frac = (float)row->amount / (float)row->capacity;

                if (frac > 1.0f) frac = 1.0f;
                if (frac < 0.0f) frac = 0.0f;

                track.x += 8.0f;  track.w -= 16.0f;
                track.y += 10.0f; track.h  = 10.0f;
                fill(renderer, track, 28, 24, 18, 255);

                fillr   = track;
                fillr.w = track.w * frac;
                if (row->full) fill(renderer, fillr, 200, 110, 80, 255);
                else           fill(renderer, fillr, 120, 150, 90, 255);
                outline(renderer, track, 80, 72, 55, 255);
            } else {
                cell(renderer, inventory_col_rect(w->rect, INV_COL_CAPACITY),
                     "uncapped", DIM);
            }

            if (row->ship_cargo > 0 || row->escrow > 0)
                SDL_snprintf(buf, sizeof(buf), "%d / %d",
                             row->ship_cargo, row->escrow);
            else
                SDL_snprintf(buf, sizeof(buf), "-");
            cell(renderer, inventory_col_rect(w->rect, INV_COL_ELSEWHERE),
                 buf, DIM);
            continue;
        }

        if (w->flags & UI_W_HEADER) {
            cell(renderer, w->rect, w->label, DIM);
            continue;
        }

        {
            int disabled = (w->flags & UI_W_DISABLED) != 0;
            if (disabled)   fill(renderer, w->rect, 42, 38, 30, 255);
            else if (hover) fill(renderer, w->rect, 96, 80, 46, 255);
            else            fill(renderer, w->rect, 68, 56, 34, 255);
            outline(renderer, w->rect, disabled ? 60 : 130,
                    disabled ? 54 : 110, disabled ? 44 : 66, 255);
            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 8.0f), (int)(w->rect.y + 5.0f),
                           disabled ? DIM : TEXT);
        }
    }
}

/* ---- the vitals strip ------------------------------------- */

void vitals_ui_draw(SDL_Renderer *renderer, int screen_w,
                    const VitalsView *v)
{
    const float W = 300.0f, ROW = 20.0f;
    float       x = (float)screen_w - W - 12.0f;
    float       y = 44.0f;   /* below the population readout */
    int         i;

    if (v->row_count == 0) return;

    for (i = 0; i < v->row_count; i++) {
        const VitalRow *r = &v->rows[i];
        UiRect          box = { x, y, W, ROW };
        SDL_Color       fg;

        switch ((VitalSeverity)r->severity) {
        case VITAL_ALERT: fg = (SDL_Color){ 250, 140, 120, 255 }; break;
        case VITAL_WARN:  fg = (SDL_Color){ 235, 195, 110, 255 }; break;
        default:          fg = (SDL_Color){ 175, 175, 165, 255 }; break;
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        fill(renderer, box, 20, 17, 13, 190);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        /* A severity stripe down the left edge, so the strip reads as a
         * ranked list at a glance without needing colour vision. */
        {
            UiRect stripe = { box.x, box.y, 3.0f, box.h };
            fill(renderer, stripe, fg.r, fg.g, fg.b, 255);
        }

        font_draw_text(renderer, FONT_SMALL, r->text,
                       (int)(box.x + 10.0f), (int)(box.y + 3.0f), fg);
        y += ROW + 2.0f;
    }

    if (v->hidden > 0) {
        char      buf[32];
        SDL_Color dim = { 160, 150, 135, 255 };
        SDL_snprintf(buf, sizeof(buf), "+%d more", v->hidden);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(x + 10.0f), (int)(y + 3.0f), dim);
    }
}

/* ---- the island header ------------------------------------ */

void island_bar_draw(SDL_Renderer *renderer, const UiList *list,
                     const UiSnapshot *snap, int mouse_x, int mouse_y)
{
    uint8_t hr, hg, hb;
    UiRect  bar;
    int     i;

    if (list->count == 0) return;

    bar = list->items[0].rect;
    island_hue(snap->current_island, &hr, &hg, &hb);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    fill(renderer, bar, 24, 20, 15, 210);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    /* The island's colour as a rule under the name: enough to identify
     * where you are at a glance, not enough to compete with the map. */
    {
        UiRect rule = { bar.x, bar.y + bar.h - 3.0f, bar.w, 3.0f };
        fill(renderer, rule, hr, hg, hb, 255);
    }
    outline(renderer, bar, 70, 62, 48, 255);

    for (i = 1; i < list->count; i++) {
        const UiWidget *w        = &list->items[i];
        int             disabled = (w->flags & UI_W_DISABLED) != 0;
        int             hover    = ui_point_in(w->rect, (float)mouse_x,
                                               (float)mouse_y);

        if (w->flags & UI_W_HEADER) {
            /* The name, in the island's own colour. */
            SDL_Color name = { hr, hg, hb, 255 };
            font_draw_text(renderer, FONT_NORMAL, w->label,
                           (int)(w->rect.x + 12.0f),
                           (int)(w->rect.y + 5.0f), name);
            continue;
        }

        {
            SDL_Color col = disabled ? (SDL_Color){ 90, 84, 72, 255 }
                          : hover    ? (SDL_Color){ 245, 230, 195, 255 }
                                     : (SDL_Color){ 180, 168, 140, 255 };
            if (hover && !disabled) fill(renderer, w->rect, 52, 44, 32, 255);
            font_draw_text(renderer, FONT_NORMAL, w->label,
                           (int)(w->rect.x + 10.0f),
                           (int)(w->rect.y + 5.0f), col);
        }
    }
}
