/*  yard_ui.c  --  Painting the yard and the fleet (UI_PLAN N6)
 *
 *  Consumes the UiList that yard_build() produced. Nothing here decides
 *  where anything goes; see yard_ui.h.
 */

#include "yard_ui.h"
#include "fonts.h"
#include "island_bar.h"
#include "resource.h"

static SDL_FRect to_sdl(UiRect r)
{
    SDL_FRect s;
    s.x = r.x; s.y = r.y; s.w = r.w; s.h = r.h;
    return s;
}

static void fill(SDL_Renderer *r, UiRect rect, Uint8 cr, Uint8 cg,
                 Uint8 cb, Uint8 ca)
{
    SDL_FRect f = to_sdl(rect);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderFillRect(r, &f);
}

static void outline(SDL_Renderer *r, UiRect rect, Uint8 cr, Uint8 cg,
                    Uint8 cb, Uint8 ca)
{
    SDL_FRect f = to_sdl(rect);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderRect(r, &f);
}

static void draw_cell(SDL_Renderer *r, UiRect col, const char *text,
                      SDL_Color colour)
{
    font_draw_text(r, FONT_SMALL, text,
                   (int)(col.x + 8.0f), (int)(col.y + 8.0f), colour);
}

/* Condition, as a bar and a number. A hull only ever goes down, so what
 * a fleet's worth of them looks like matters more than any single
 * figure — and below a third, sending it anywhere is a decision. */
static void draw_condition(SDL_Renderer *r, UiRect col, const YardRow *row)
{
    UiRect    track = { col.x + 8.0f, col.y + 18.0f, col.w - 20.0f, 6.0f };
    UiRect    fillr = track;
    SDL_Color txt   = { 220, 210, 185, 255 };
    SDL_Color warn  = { 220, 130, 120, 255 };
    float     frac  = (row->hull_max > 0)
                    ? (float)row->hull / (float)row->hull_max : 1.0f;
    char      buf[32];

    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    SDL_snprintf(buf, sizeof(buf), "%d / %d", row->hull, row->hull_max);
    font_draw_text(r, FONT_SMALL, buf, (int)(col.x + 8.0f),
                   (int)(col.y + 2.0f), frac < 0.34f ? warn : txt);

    fillr.w = track.w * frac;
    fill(r, track, 40, 34, 26, 255);
    if (frac < 0.34f) fill(r, fillr, 200, 110, 100, 255);
    else              fill(r, fillr, 130, 170, 130, 255);
}

void yard_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                  const UiList *list, const YardView *view,
                  int mouse_x, int mouse_y)
{
    const SDL_Color TITLE = { 200, 175, 110, 255 };
    const SDL_Color HEAD  = { 150, 135,  95, 255 };
    const SDL_Color TEXT  = { 220, 210, 185, 255 };
    const SDL_Color DIM   = { 120, 112,  98, 255 };
    const SDL_Color WARN  = { 220, 130, 120, 255 };
    const SDL_Color GOOD  = { 150, 190, 150, 255 };
    UiRect          panel;
    int             i;
    char            buf[64];
    RejectReason    hover_reason = REJ_OK;

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
        UiRect bar    = { panel.x, panel.y, panel.w, YARD_TITLE_H };
        UiRect stripe = { panel.x, panel.y, 5.0f, YARD_TITLE_H };

        fill(renderer, bar, 55, 44, 28, 255);
        SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
        SDL_RenderLine(renderer, panel.x, panel.y + YARD_TITLE_H,
                       panel.x + panel.w, panel.y + YARD_TITLE_H);
        fill(renderer, stripe, view->hue_r, view->hue_g, view->hue_b, 255);

        font_draw_text(renderer, FONT_NORMAL, view->title,
                       (int)(panel.x + 14.0f), (int)(panel.y + 10.0f), TITLE);

        ui_fmt_known(buf, sizeof(buf), view->yours, "Gold: %d",
                     view->your_gold);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(panel.x + panel.w - 330.0f),
                       (int)(panel.y + 14.0f), TEXT);

        SDL_snprintf(buf, sizeof(buf), "Fleet: %d / %d",
                     view->fleet_size, view->fleet_cap);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(panel.x + panel.w - 150.0f),
                       (int)(panel.y + 14.0f),
                       view->fleet_size >= view->fleet_cap ? WARN : DIM);
    }

    /* Headings off the first row, so they cannot drift from the cells. */
    for (i = 0; i < list->count; i++) {
        const UiWidget *w = &list->items[i];
        UiRect          head;
        int             g = ui_id_group(w->id);

        if (g != UI_GROUP_HULL && g != UI_GROUP_SHIP) continue;

        head    = w->rect;
        head.y -= YARD_HEAD_H;
        head.h  = YARD_HEAD_H;

        draw_cell(renderer, yard_col_rect(head, YD_COL_NAME),  "Hull",  HEAD);
        draw_cell(renderer, yard_col_rect(head, YD_COL_GUNS),  "Guns",  HEAD);
        draw_cell(renderer, yard_col_rect(head, YD_COL_HULL),
                  g == UI_GROUP_HULL ? "Takes" : "Condition", HEAD);
        draw_cell(renderer, yard_col_rect(head, YD_COL_HOLD),  "Hold",  HEAD);
        draw_cell(renderer, yard_col_rect(head, YD_COL_WHERE),
                  g == UI_GROUP_HULL ? "Cost" : "Where", HEAD);
        break;
    }

    for (i = 0; i < list->count; i++) {
        const UiWidget *w     = &list->items[i];
        int             group = ui_id_group(w->id);
        int             hover = ui_point_in(w->rect, (float)mouse_x,
                                            (float)mouse_y);

        if (i == 0) continue;

        if (group == UI_GROUP_HULL || group == UI_GROUP_SHIP) {
            const YardRow *row = NULL;
            int            j;

            for (j = 0; j < view->row_count; j++) {
                const YardRow *cand = &view->rows[j];
                if (group == UI_GROUP_HULL && cand->is_hull &&
                    cand->klass == w->value) { row = cand; break; }
                if (group == UI_GROUP_SHIP && !cand->is_hull &&
                    cand->ship == w->value)  { row = cand; break; }
            }
            if (!row) continue;

            if ((j % 2) == 0) fill(renderer, w->rect, 44, 36, 24, 255);

            draw_cell(renderer, yard_col_rect(w->rect, YD_COL_NAME),
                      row->name, TEXT);

            /* A merchantman's "0 guns" is the point of the row, not a
             * missing number, so it is drawn rather than left blank. */
            SDL_snprintf(buf, sizeof(buf), "%d", row->guns);
            draw_cell(renderer, yard_col_rect(w->rect, YD_COL_GUNS), buf,
                      row->guns > 0 ? TEXT : DIM);

            if (row->is_hull) {
                SDL_snprintf(buf, sizeof(buf), "%d", row->hull);
                draw_cell(renderer, yard_col_rect(w->rect, YD_COL_HULL),
                          buf, TEXT);
            } else {
                draw_condition(renderer, yard_col_rect(w->rect, YD_COL_HULL),
                               row);
            }

            if (row->is_hull)
                SDL_snprintf(buf, sizeof(buf), "%d ea", row->hold);
            else
                SDL_snprintf(buf, sizeof(buf), "%d / %d", row->cargo_units,
                             row->hold * ((int)RES_COUNT - 1));
            draw_cell(renderer, yard_col_rect(w->rect, YD_COL_HOLD), buf,
                      row->hold > 0 ? TEXT : DIM);

            if (row->is_hull) {
                SDL_snprintf(buf, sizeof(buf), "%d Gold", row->cost);
                draw_cell(renderer, yard_col_rect(w->rect, YD_COL_WHERE), buf,
                          row->affordable ? TEXT : WARN);
            } else if (row->at_island >= 0) {
                draw_cell(renderer, yard_col_rect(w->rect, YD_COL_WHERE),
                          "In port", GOOD);
            } else {
                draw_cell(renderer, yard_col_rect(w->rect, YD_COL_WHERE),
                          "At sea", DIM);
            }
            continue;
        }

        {
            int disabled = (w->flags & UI_W_DISABLED) != 0;
            int muted    = (w->flags & UI_W_MUTED) != 0;
            int header   = (w->flags & UI_W_HEADER) != 0;

            if (header) {
                draw_cell(renderer, w->rect, w->label, DIM);
                continue;
            }

            if (disabled)      fill(renderer, w->rect, 42, 38, 30, 255);
            else if (muted)    fill(renderer, w->rect, 54, 46, 30, 255);
            else if (hover)    fill(renderer, w->rect, 96, 80, 46, 255);
            else               fill(renderer, w->rect, 68, 56, 34, 255);

            outline(renderer, w->rect, disabled ? 60 : 130,
                    disabled ? 54 : 110, disabled ? 44 : 66, 255);

            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 8.0f),
                           (int)(w->rect.y + 4.0f),
                           disabled ? DIM : (muted ? HEAD : TEXT));

            if (disabled && hover && w->reason != (uint8_t)REJ_OK)
                hover_reason = (RejectReason)w->reason;
        }
    }

    if (hover_reason != REJ_OK) {
        const float TIP_W = 200.0f, TIP_H = 18.0f;
        UiRect      screen = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
        UiRect      tip    = ui_tooltip_rect((float)mouse_x + 12.0f +
                                                 TIP_W * 0.5f,
                                             (float)mouse_y - 2.0f,
                                             TIP_W, TIP_H, screen);

        font_draw_text(renderer, FONT_SMALL, ui_reject_text(hover_reason),
                       (int)tip.x, (int)tip.y, WARN);
    }
}
