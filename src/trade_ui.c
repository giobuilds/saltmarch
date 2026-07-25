/*  trade_ui.c  --  Painting the exchange screen (UI_PLAN Phase 1)
 *
 *  Consumes the UiList that exchange_build() produced. Nothing here
 *  decides where anything goes; if a rect looks wrong, the bug is in
 *  exchange_view.c and there is a headless test that can prove it.
 */

#include "trade_ui.h"
#include "fonts.h"
#include "resource.h"

/* SDL wants its own rect type; the kit deliberately does not know about
 * it (that is what keeps layout linkable without SDL). */
static SDL_FRect to_sdl(UiRect r)
{
    SDL_FRect s;
    s.x = r.x; s.y = r.y; s.w = r.w; s.h = r.h;
    return s;
}

/* A stable per-resource hue for the swatch column. Derived from the
 * enum rather than stored in a table: a parallel colour table indexed
 * by ResourceType is exactly the shape that went stale when Hops, Malt
 * and Beer were added (see the RES_COL note in building.c). */
static SDL_Color resource_swatch(int res)
{
    static const SDL_Color C[] = {
        { 150, 110,  60, 255 },   /* Wood  */
        {  90, 150, 190, 255 },   /* Fish  */
        { 210, 185,  90, 255 },   /* Grain */
        { 120, 170,  90, 255 },   /* Hops  */
        { 190, 150,  80, 255 },   /* Malt  */
        { 200, 140,  60, 255 },   /* Beer  */
        { 225, 200, 110, 255 }    /* Gold  */
    };
    int n = (int)(sizeof(C) / sizeof(C[0]));
    if (res < 0 || res >= n) return C[0];
    return C[res];
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

/* Right-aligned-ish numeric cell: the kit forbids measuring text, so
 * numbers are drawn from a fixed inset inside their column rather than
 * measured and right-aligned. Consistent, and honest about the rule. */
static void draw_cell(SDL_Renderer *r, UiRect col, const char *text,
                      SDL_Color colour)
{
    font_draw_text(r, FONT_SMALL, text,
                   (int)(col.x + 8.0f), (int)(col.y + 9.0f), colour);
}

void trade_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const UiList *list, const ExchangeView *view,
                   int mouse_x, int mouse_y)
{
    const SDL_Color TITLE  = { 200, 175, 110, 255 };
    const SDL_Color HEAD   = { 150, 135,  95, 255 };
    const SDL_Color TEXT   = { 220, 210, 185, 255 };
    const SDL_Color DIM    = { 120, 112,  98, 255 };
    const SDL_Color WARN   = { 220, 130, 120, 255 };
    UiRect          panel;
    int             i;
    char            buf[64];

    if (list->count == 0) return;

    /* Widget 0 is the panel — exchange_build pushes it first so a click
     * inside is absorbed rather than falling through to the world. */
    panel = list->items[0].rect;

    /* Dim the world behind. */
    {
        UiRect all = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        fill(renderer, all, 0, 0, 0, 160);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    fill(renderer, panel, 35, 28, 18, 255);
    outline(renderer, panel, 120, 100, 60, 255);

    /* Title bar and the two running totals a trader actually needs:
     * their own Gold, and whether the counterparty can still pay. */
    {
        UiRect bar = { panel.x, panel.y, panel.w, EXCHANGE_TITLE_H };
        fill(renderer, bar, 55, 44, 28, 255);
        SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
        SDL_RenderLine(renderer, panel.x, panel.y + EXCHANGE_TITLE_H,
                       panel.x + panel.w, panel.y + EXCHANGE_TITLE_H);

        font_draw_text(renderer, FONT_NORMAL, view->title,
                       (int)(panel.x + 14.0f), (int)(panel.y + 10.0f), TITLE);

        SDL_snprintf(buf, sizeof(buf), "Your Gold: %d", view->your_gold);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(panel.x + panel.w - 320.0f),
                       (int)(panel.y + 14.0f), TEXT);

        if (view->their_gold != EXCHANGE_INFINITE) {
            SDL_Color col = view->their_gold > 0 ? DIM : WARN;
            SDL_snprintf(buf, sizeof(buf), "Their Gold: %d",
                         view->their_gold);
            font_draw_text(renderer, FONT_SMALL, buf,
                           (int)(panel.x + panel.w - 160.0f),
                           (int)(panel.y + 14.0f), col);
        }
    }

    /* Column headings, positioned off the first row's geometry so they
     * cannot drift from the cells beneath them. */
    for (i = 0; i < list->count; i++) {
        const UiWidget *w = &list->items[i];
        UiRect          head;

        if (ui_id_group(w->id) != UI_GROUP_RESOURCE) continue;

        head    = w->rect;
        head.y -= EXCHANGE_HEAD_H;
        head.h  = EXCHANGE_HEAD_H;

        draw_cell(renderer, exchange_col_rect(head, EX_COL_NAME),   "Good",  HEAD);
        draw_cell(renderer, exchange_col_rect(head, EX_COL_YOURS),  "Yours", HEAD);
        draw_cell(renderer, exchange_col_rect(head, EX_COL_THEIRS), "Theirs",HEAD);
        draw_cell(renderer, exchange_col_rect(head, EX_COL_BID),    "Bid",   HEAD);
        draw_cell(renderer, exchange_col_rect(head, EX_COL_ASK),    "Ask",   HEAD);
        break;   /* headings once, above the first row */
    }

    /* Rows and buttons, in list order. */
    for (i = 0; i < list->count; i++) {
        const UiWidget *w     = &list->items[i];
        int             group = ui_id_group(w->id);
        int             hover = ui_point_in(w->rect, (float)mouse_x,
                                            (float)mouse_y);

        if (i == 0) continue;   /* the panel itself, already drawn */

        if (group == UI_GROUP_RESOURCE) {
            const ExchangeRow *row = NULL;
            int                j;
            UiRect             swatch;

            for (j = 0; j < view->row_count; j++)
                if (view->rows[j].ident == ui_id_value(w->id)) {
                    row = &view->rows[j];
                    break;
                }
            if (!row) continue;

            /* Zebra striping keeps a long paginated list readable. */
            if ((j % 2) == 0) fill(renderer, w->rect, 44, 36, 24, 255);

            swatch    = exchange_col_rect(w->rect, EX_COL_NAME);
            swatch.x += 2.0f;
            swatch.y += 10.0f;
            swatch.w  = 12.0f;
            swatch.h  = 12.0f;
            {
                SDL_Color c = resource_swatch((int)row->ident);
                fill(renderer, swatch, c.r, c.g, c.b, 255);
            }

            {
                UiRect name = exchange_col_rect(w->rect, EX_COL_NAME);
                name.x += 12.0f;
                draw_cell(renderer, name, row->name, TEXT);
            }

            SDL_snprintf(buf, sizeof(buf), "%d", row->yours);
            draw_cell(renderer, exchange_col_rect(w->rect, EX_COL_YOURS),
                      buf, TEXT);

            if (row->theirs == EXCHANGE_INFINITE)
                SDL_snprintf(buf, sizeof(buf), "--");
            else
                SDL_snprintf(buf, sizeof(buf), "%d", row->theirs);
            draw_cell(renderer, exchange_col_rect(w->rect, EX_COL_THEIRS),
                      buf, row->refuse == (uint8_t)REJ_OK ? DIM : WARN);

            SDL_snprintf(buf, sizeof(buf), "%d", row->bid);
            draw_cell(renderer, exchange_col_rect(w->rect, EX_COL_BID),
                      buf, TEXT);

            SDL_snprintf(buf, sizeof(buf), "%d", row->ask);
            draw_cell(renderer, exchange_col_rect(w->rect, EX_COL_ASK),
                      buf, TEXT);
            continue;
        }

        /* Buttons: sell, buy, pager, close. A disabled one is drawn
         * flat and dark — greyed, never hidden, so the screen does not
         * reshuffle as stock and Gold move (and so the player can hover
         * it to learn why it is off, once M1's tooltips land). */
        {
            int disabled = (w->flags & UI_W_DISABLED) != 0;
            int header   = (w->flags & UI_W_HEADER) != 0;

            if (header) {
                draw_cell(renderer, w->rect, w->label, DIM);
                continue;
            }

            if (disabled)      fill(renderer, w->rect, 42, 38, 30, 255);
            else if (hover)    fill(renderer, w->rect, 96, 80, 46, 255);
            else               fill(renderer, w->rect, 68, 56, 34, 255);

            outline(renderer, w->rect, disabled ? 60 : 130,
                    disabled ? 54 : 110, disabled ? 44 : 66, 255);

            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 8.0f),
                           (int)(w->rect.y + 5.0f),
                           disabled ? DIM : TEXT);

            /* Hovering a refused button says why, at the button. Same
             * vocabulary the sim rejects with (UI_PLAN decision 3). */
            if (disabled && hover && w->reason != (uint8_t)REJ_OK)
                font_draw_text(renderer, FONT_SMALL,
                               ui_reject_text((RejectReason)w->reason),
                               (int)(w->rect.x - 180.0f),
                               (int)(w->rect.y + 5.0f), WARN);
        }
    }
}
