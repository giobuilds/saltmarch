/* book_ui.c  --  Painting the order book (UI_PLAN N3) */

#include "book_ui.h"
#include "fonts.h"
#include "orderbook.h"
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

/* How long an order has been resting, in the units a player counts in.
 * Ticks are the sim's business; seconds are theirs. */
static void age_text(char *buf, size_t n, uint64_t now, uint64_t placed)
{
    uint64_t ticks = (now > placed) ? now - placed : 0;
    uint64_t secs  = ticks / (uint64_t)SIM_TICKS_PER_SEC;

    if (secs < 60) SDL_snprintf(buf, n, "%us", (unsigned)secs);
    else           SDL_snprintf(buf, n, "%um", (unsigned)(secs / 60));
}

void book_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                  const UiList *list, const BookView *view,
                  int mouse_x, int mouse_y)
{
    const SDL_Color TITLE = { 200, 175, 110, 255 };
    const SDL_Color HEAD  = { 150, 135,  95, 255 };
    const SDL_Color TEXT  = { 220, 210, 185, 255 };
    const SDL_Color DIM   = { 120, 112,  98, 255 };
    const SDL_Color WARN  = { 220, 130, 120, 255 };
    const SDL_Color BUY   = { 150, 190, 150, 255 };
    const SDL_Color SELL  = { 210, 165, 120, 255 };
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
        UiRect bar    = { panel.x, panel.y, panel.w, BOOK_TITLE_H };
        UiRect stripe = { panel.x, panel.y, 5.0f, BOOK_TITLE_H };

        fill(renderer, bar, 55, 44, 28, 255);
        SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
        SDL_RenderLine(renderer, panel.x, panel.y + BOOK_TITLE_H,
                       panel.x + panel.w, panel.y + BOOK_TITLE_H);
        fill(renderer, stripe, view->hue_r, view->hue_g, view->hue_b, 255);

        font_draw_text(renderer, FONT_NORMAL, view->title,
                       (int)(panel.x + 14.0f), (int)(panel.y + 10.0f), TITLE);

        /* Gold you were not told is drawn as a mark, not as zero
         * (UI_PLAN N2) — a rival's harbour has a purse, we simply do
         * not know what is in it. */
        ui_fmt_known(buf, sizeof(buf), view->yours, "Your Gold: %d",
                     view->your_gold);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(panel.x + panel.w - 330.0f),
                       (int)(panel.y + 14.0f), TEXT);

        /* How much of the per-player cap is spent. A book that quietly
         * refuses the 25th order is worse than one that counts. */
        SDL_snprintf(buf, sizeof(buf), "Resting: %d / %d",
                     view->open_count, view->cap);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(panel.x + panel.w - 150.0f),
                       (int)(panel.y + 14.0f),
                       view->open_count >= view->cap ? WARN : DIM);
    }

    /* Column headings, positioned off the first row so they cannot
     * drift from the cells beneath them. */
    for (i = 0; i < list->count; i++) {
        const UiWidget *w = &list->items[i];
        UiRect          head;

        if (ui_id_group(w->id) != UI_GROUP_ORDER) continue;

        head    = w->rect;
        head.y -= BOOK_HEAD_H;
        head.h  = BOOK_HEAD_H;

        draw_cell(renderer, book_col_rect(head, BK_COL_SIDE),  "Side",  HEAD);
        draw_cell(renderer, book_col_rect(head, BK_COL_NAME),  "Good",  HEAD);
        draw_cell(renderer, book_col_rect(head, BK_COL_QTY),   "Qty",   HEAD);
        draw_cell(renderer, book_col_rect(head, BK_COL_LIMIT), "Limit", HEAD);
        draw_cell(renderer, book_col_rect(head, BK_COL_VALUE), "Held",  HEAD);
        draw_cell(renderer, book_col_rect(head, BK_COL_AGE),   "Age",   HEAD);
        break;
    }

    for (i = 0; i < list->count; i++) {
        const UiWidget *w     = &list->items[i];
        int             group = ui_id_group(w->id);
        int             hover = ui_point_in(w->rect, (float)mouse_x,
                                            (float)mouse_y);

        if (i == 0) continue;   /* the panel itself */

        if (group == UI_GROUP_ORDER) {
            const BookRow *row = NULL;
            SDL_Color      col;
            int            j;

            for (j = 0; j < view->row_count; j++)
                if (view->rows[j].id == (uint32_t)w->value) {
                    row = &view->rows[j];
                    break;
                }
            if (!row) continue;

            if ((j % 2) == 0) fill(renderer, w->rect, 44, 36, 24, 255);

            col = row->gone ? DIM : (row->side == ORDER_SELL ? SELL : BUY);

            draw_cell(renderer, book_col_rect(w->rect, BK_COL_SIDE),
                      row->side == ORDER_SELL ? "Sell" : "Buy", col);
            draw_cell(renderer, book_col_rect(w->rect, BK_COL_NAME),
                      row->name, row->gone ? DIM : TEXT);

            SDL_snprintf(buf, sizeof(buf), "%d", row->qty);
            draw_cell(renderer, book_col_rect(w->rect, BK_COL_QTY), buf,
                      row->gone ? DIM : TEXT);

            SDL_snprintf(buf, sizeof(buf), "%d", row->limit);
            draw_cell(renderer, book_col_rect(w->rect, BK_COL_LIMIT), buf,
                      row->gone ? DIM : TEXT);

            /* What the order is holding of yours. A sell took the goods
             * out of the stockpile when it was posted and a buy took
             * the gold; that reservation is the fact players are most
             * likely to be surprised by, so it gets a column. */
            if (row->side == ORDER_SELL)
                SDL_snprintf(buf, sizeof(buf), "%d %s", row->qty, row->name);
            else
                SDL_snprintf(buf, sizeof(buf), "%d Gold",
                             row->qty * row->limit);
            draw_cell(renderer, book_col_rect(w->rect, BK_COL_VALUE), buf,
                      DIM);

            age_text(buf, sizeof(buf), view->tick, row->placed_tick);
            draw_cell(renderer, book_col_rect(w->rect, BK_COL_AGE), buf,
                      DIM);

            /* Struck through rather than removed: the row stays where
             * the eye left it, and the line says why the Cancel beside
             * it is dead (UI_PLAN N3). */
            if (row->gone) {
                float y = w->rect.y + w->rect.h * 0.5f;
                SDL_SetRenderDrawColor(renderer, 150, 120, 100, 255);
                SDL_RenderLine(renderer, w->rect.x + 4.0f, y,
                               w->rect.x + BOOK_COL_SIDE + BOOK_COL_NAME +
                                   BOOK_COL_QTY + BOOK_COL_LIMIT +
                                   BOOK_COL_VALUE + BOOK_COL_AGE, y);
            }
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
