/*  chart_ui.c  --  Painting the passages (UI_PLAN N4)
 *
 *  Consumes the UiList that chart_build() produced. Nothing here decides
 *  where anything goes; see chart_ui.h.
 */

#include "chart_ui.h"
#include "fonts.h"
#include "island_bar.h"
#include "survey.h"
#include "simclock.h"

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
                   (int)(col.x + 8.0f), (int)(col.y + 7.0f), colour);
}

/* Ticks in the units a player counts in. Crossings are tens of seconds
 * and a passage's life is tens of minutes, so one function has to span
 * both without ever printing "1800s". */
static void dur_text(char *buf, size_t n, uint64_t ticks)
{
    uint64_t secs = ticks / (uint64_t)SIM_TICKS_PER_SEC;

    if (secs < 60)   SDL_snprintf(buf, n, "%us", (unsigned)secs);
    else if (secs < 3600)
                     SDL_snprintf(buf, n, "%um", (unsigned)(secs / 60));
    else             SDL_snprintf(buf, n, "%uh %um",
                                  (unsigned)(secs / 3600),
                                  (unsigned)((secs % 3600) / 60));
}

/* One passage's six cells.
 *
 * WHICH CELLS ARE MARKS IS THE WHOLE DESIGN OF THIS FUNCTION. A passage
 * you have not learned hides its name, its crossing and what it saves,
 * because those are what a chart tells you — but its price and its
 * expiry are drawn as plain numbers, because the market's resting offer
 * and the rotation schedule are public facts about water you have never
 * seen. Drawing all six as unknown would be tidier and would be a lie in
 * the other direction: it would say the market has nothing on the
 * counter when it has your map sitting there priced.
 */
static void draw_row(SDL_Renderer *r, const ChartRow *row, UiRect rr,
                     SDL_Color text, SDL_Color dim, SDL_Color warn,
                     SDL_Color good, uint64_t now)
{
    char buf[64];

    draw_cell(r, chart_col_rect(rr, CH_COL_NAME),
              row->known ? row->name : ui_unknown_label(),
              row->gone ? dim : (row->known ? text : dim));

    if (row->known) dur_text(buf, sizeof(buf), row->ticks);
    else            SDL_snprintf(buf, sizeof(buf), "%s", UI_UNKNOWN_MARK);
    draw_cell(r, chart_col_rect(rr, CH_COL_CROSS), buf, dim);

    if (!row->is_private) {
        draw_cell(r, chart_col_rect(rr, CH_COL_SAVES), "patrolled", good);
    } else if (!row->known) {
        draw_cell(r, chart_col_rect(rr, CH_COL_SAVES), UI_UNKNOWN_MARK, dim);
    } else {
        char saved[32];
        dur_text(saved, sizeof(saved), (uint64_t)row->saves);
        SDL_snprintf(buf, sizeof(buf), "-%s", saved);
        draw_cell(r, chart_col_rect(rr, CH_COL_SAVES), buf,
                  row->saves > 0 ? good : dim);
    }

    if (row->is_private) {
        SDL_snprintf(buf, sizeof(buf), "%d", row->charts);
        draw_cell(r, chart_col_rect(rr, CH_COL_HELD), buf,
                  row->charts > 0 ? text : dim);
    }

    /* The clock this screen exists for. A chart that quietly stops
     * working is worse than no chart, so the water says how long it has
     * left — and says plainly when it has none. */
    if (row->gone)
        draw_cell(r, chart_col_rect(rr, CH_COL_EXPIRY), "out of use", warn);
    else if (!row->is_private)
        draw_cell(r, chart_col_rect(rr, CH_COL_EXPIRY), "always", dim);
    else if (row->expires_tick > now) {
        dur_text(buf, sizeof(buf), row->expires_tick - now);
        draw_cell(r, chart_col_rect(rr, CH_COL_EXPIRY), buf,
                  row->expires_tick - now <
                      (uint64_t)SIM_TICKS_PER_SEC * 120u ? warn : dim);
    }

    if (row->is_private) {
        if (row->ask > 0) SDL_snprintf(buf, sizeof(buf), "%d", row->ask);
        else              SDL_snprintf(buf, sizeof(buf), "none");
        draw_cell(r, chart_col_rect(rr, CH_COL_PRICE), buf,
                  row->ask > 0 ? text : dim);
    }

    if (row->gone) {
        float y = rr.y + rr.h * 0.5f;
        SDL_SetRenderDrawColor(r, 150, 120, 100, 255);
        SDL_RenderLine(r, rr.x + 4.0f, y,
                       rr.x + CHART_COL_NAME + CHART_COL_CROSS +
                           CHART_COL_SAVES + CHART_COL_HELD +
                           CHART_COL_EXPIRY + CHART_COL_PRICE, y);
    }
}

void chart_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const UiList *list, const ChartView *view,
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
        UiRect bar    = { panel.x, panel.y, panel.w, CHART_TITLE_H };
        UiRect stripe = { panel.x, panel.y, 5.0f, CHART_TITLE_H };

        fill(renderer, bar, 55, 44, 28, 255);
        SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
        SDL_RenderLine(renderer, panel.x, panel.y + CHART_TITLE_H,
                       panel.x + panel.w, panel.y + CHART_TITLE_H);
        fill(renderer, stripe, view->hue_r, view->hue_g, view->hue_b, 255);

        font_draw_text(renderer, FONT_NORMAL, view->title,
                       (int)(panel.x + 14.0f), (int)(panel.y + 10.0f), TITLE);

        /* Blank charts are the Chart House's output and what a survey
         * spends, so the number belongs beside the passages rather than
         * three screens away in the stores. */
        ui_fmt_known(buf, sizeof(buf), view->yours, "Blank charts: %d",
                     view->blank_charts);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(panel.x + panel.w - 330.0f),
                       (int)(panel.y + 14.0f), TEXT);

        ui_fmt_known(buf, sizeof(buf), view->yours, "Gold: %d",
                     view->your_gold);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(panel.x + panel.w - 150.0f),
                       (int)(panel.y + 14.0f), DIM);
    }

    /* What an expedition costs and what it risks, once, under the title
     * rather than on every row (UI_PLAN N7). The odds come from the
     * sim's own constant: a screen that quoted its own number would be
     * a second opinion about a gamble. */
    {
        UiRect strip = { panel.x + 14.0f, panel.y + CHART_TITLE_H + 2.0f,
                         panel.w - 28.0f, CHART_HEAD_H };

        /* Its own buffer: `buf` is 64 bytes, which this sentence
         * exceeded by a word and a half, so it rendered as
         * "...1 blank chart (0" and stopped — a screen that told the
         * player a fact and then trailed off mid-parenthesis. */
        char legend[160];

        SDL_snprintf(legend, sizeof(legend),
                     "An expedition: 1 scholar (%d free), 1 boat (%d), "
                     "1 blank chart (%d).  %d%% find nothing.",
                     view->scholars_free, view->boats_free,
                     view->blank_charts, SURVEY_FAIL_PER_MILLE / 10);
        font_draw_text(renderer, FONT_SMALL, legend,
                       (int)strip.x, (int)strip.y, DIM);
    }

    /* Column headings, positioned off the first passage row so they
     * cannot drift from the cells beneath them.
     *
     * Off the first row of the LIST, not the first ROUTE row: a
     * destination heading comes before the routes under it, so
     * subtracting a header's height from the first route landed the
     * column names on top of "Brinehold". Anchoring to whichever row
     * is first — heading or passage — puts them in the strip above the
     * list, which is where the layout left room for them. */
    for (i = 0; i < list->count; i++) {
        const UiWidget *w = &list->items[i];
        UiRect          head;
        int             g = ui_id_group(w->id);

        if (g != UI_GROUP_ROUTE && g != UI_GROUP_ISLAND) continue;

        head    = w->rect;
        head.y -= CHART_HEAD_H;
        head.h  = CHART_HEAD_H;

        draw_cell(renderer, chart_col_rect(head, CH_COL_NAME),   "Passage", HEAD);
        draw_cell(renderer, chart_col_rect(head, CH_COL_CROSS),  "Crossing", HEAD);
        draw_cell(renderer, chart_col_rect(head, CH_COL_SAVES),  "Saves",   HEAD);
        draw_cell(renderer, chart_col_rect(head, CH_COL_HELD),   "Held",    HEAD);
        draw_cell(renderer, chart_col_rect(head, CH_COL_EXPIRY), "In use",  HEAD);
        draw_cell(renderer, chart_col_rect(head, CH_COL_PRICE),  "Price",   HEAD);
        break;
    }

    for (i = 0; i < list->count; i++) {
        const UiWidget *w     = &list->items[i];
        int             group = ui_id_group(w->id);
        int             hover = ui_point_in(w->rect, (float)mouse_x,
                                            (float)mouse_y);

        if (i == 0) continue;   /* the panel itself */

        if (group == UI_GROUP_ISLAND) {
            /* A destination heading, in that island's own colour — the
             * same hue the world map and the island bar use, so "the
             * blue island" means one place everywhere. */
            uint8_t hr, hg, hb;
            SDL_Color hue;

            island_hue((int)ui_id_value(w->id), &hr, &hg, &hb);
            hue.r = hr; hue.g = hg; hue.b = hb; hue.a = 255;

            fill(renderer, w->rect, 48, 40, 26, 255);
            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 8.0f), (int)(w->rect.y + 7.0f),
                           hue);
            /* An expedition already out says when it is due back. What
             * it finds is the passage below turning from a mark into a
             * name; what it does not find is the same row unchanged,
             * which is the honest shape of a gamble (UI_PLAN N7). */
            if (w->value) {
                const ChartRow *head_row = NULL;
                int             j;

                for (j = 0; j < view->row_count; j++)
                    if (view->rows[j].header &&
                        view->rows[j].island == (int32_t)ui_id_value(w->id))
                        head_row = &view->rows[j];

                if (head_row && head_row->survey_back > view->tick) {
                    char left[32];
                    dur_text(left, sizeof(left),
                             head_row->survey_back - view->tick);
                    SDL_snprintf(buf, sizeof(buf),
                                 "expedition out — back in %s", left);
                } else {
                    SDL_snprintf(buf, sizeof(buf), "expedition out");
                }
                font_draw_text(renderer, FONT_SMALL, buf,
                               (int)(w->rect.x + CHART_COL_NAME + 8.0f),
                               (int)(w->rect.y + 7.0f), HEAD);
            }
            continue;
        }

        if (group == UI_GROUP_ROUTE) {
            const ChartRow *row = NULL;
            int             j;

            for (j = 0; j < view->row_count; j++)
                if (!view->rows[j].header &&
                    view->rows[j].route_id == w->value) {
                    row = &view->rows[j];
                    break;
                }
            if (!row) continue;

            if ((j % 2) == 0) fill(renderer, w->rect, 44, 36, 24, 255);
            draw_row(renderer, row, w->rect, TEXT, DIM, WARN, GOOD,
                     view->tick);
            continue;
        }

        {
            int disabled = (w->flags & UI_W_DISABLED) != 0;
            int header   = (w->flags & UI_W_HEADER) != 0;

            if (header) {
                draw_cell(renderer, w->rect, w->label, DIM);
                continue;
            }

            if (disabled)   fill(renderer, w->rect, 42, 38, 30, 255);
            else if (hover) fill(renderer, w->rect, 96, 80, 46, 255);
            else            fill(renderer, w->rect, 68, 56, 34, 255);

            outline(renderer, w->rect, disabled ? 60 : 130,
                    disabled ? 54 : 110, disabled ? 44 : 66, 255);

            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 8.0f),
                           (int)(w->rect.y + 4.0f),
                           disabled ? DIM : TEXT);

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
