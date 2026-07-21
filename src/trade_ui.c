/*  trade_ui.c  --  Marketplace manual trade screen  (Phase 4)  */

#include "trade_ui.h"
#include "fonts.h"

static const char *SELLABLE_NAMES[TRADE_SELLABLE_COUNT] = { "Wood", "Fish", "Grain" };
static const char *BTN_LABELS[3] = { "+1", "+10", "Sell All" };

static SDL_FRect panel_rect(int screen_w, int screen_h)
{
    SDL_FRect r;
    r.w = (float)TRADE_W;
    r.h = (float)TRADE_H;
    r.x = (float)((screen_w - TRADE_W) / 2);
    r.y = (float)((screen_h - TRADE_H) / 2);
    return r;
}

static SDL_FRect row_rect(int screen_w, int screen_h, int i)
{
    SDL_FRect panel = panel_rect(screen_w, screen_h);
    SDL_FRect r;
    r.x = panel.x + (float)TRADE_MARGIN;
    r.w = panel.w - (float)TRADE_MARGIN * 2.0f;
    r.h = (float)TRADE_ROW_H;
    r.y = panel.y + (float)TRADE_TITLE_H + (float)TRADE_MARGIN
        + (float)i * ((float)TRADE_ROW_H + (float)TRADE_ROW_PAD);
    return r;
}

/* Button index within a row: 0 = +1, 1 = +10, 2 = Sell All.
 * Right-aligned within the row, evenly spaced. */
static SDL_FRect row_btn_rect(int screen_w, int screen_h, int row_i, int btn_i)
{
    SDL_FRect row = row_rect(screen_w, screen_h, row_i);
    float total_w = 3.0f * (float)TRADE_BTN_W + 2.0f * (float)TRADE_BTN_GAP;
    float start_x = row.x + row.w - 10.0f - total_w;
    SDL_FRect r;
    r.w = (float)TRADE_BTN_W;
    r.h = (float)TRADE_BTN_H;
    r.y = row.y + (row.h - r.h) / 2.0f;
    r.x = start_x + (float)btn_i * ((float)TRADE_BTN_W + (float)TRADE_BTN_GAP);
    return r;
}

static SDL_FRect close_btn_rect(int screen_w, int screen_h)
{
    SDL_FRect panel = panel_rect(screen_w, screen_h);
    SDL_FRect r;
    r.w = (float)TRADE_BTN_W * 1.5f;
    r.h = (float)TRADE_CLOSE_H;
    r.x = panel.x + (panel.w - r.w) / 2.0f;
    r.y = panel.y + panel.h - (float)TRADE_MARGIN - r.h;
    return r;
}

static int point_in(SDL_FRect r, int x, int y)
{
    return (float)x >= r.x && (float)x < r.x + r.w &&
           (float)y >= r.y && (float)y < r.y + r.h;
}

void trade_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const Stockpile *s, int mouse_x, int mouse_y)
{
    int i, btn;
    SDL_FRect panel = panel_rect(screen_w, screen_h);
    SDL_FRect dim   = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
    SDL_FRect title_bar = { panel.x, panel.y, panel.w, (float)TRADE_TITLE_H };

    /* --- Dim the world behind the screen ---------------- */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_RenderFillRect(renderer, &dim);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    /* --- Panel background/border ------------------------ */
    SDL_SetRenderDrawColor(renderer, 35, 28, 18, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
    SDL_RenderRect(renderer, &panel);

    /* --- Title bar ---------------------------------------- */
    SDL_SetRenderDrawColor(renderer, 55, 44, 28, 255);
    SDL_RenderFillRect(renderer, &title_bar);
    SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
    SDL_RenderLine(renderer, panel.x, panel.y + (float)TRADE_TITLE_H,
                   panel.x + panel.w, panel.y + (float)TRADE_TITLE_H);
    {
        SDL_Color title_col = { 200, 175, 110, 255 };
        font_draw_text(renderer, FONT_NORMAL, "Marketplace",
                       (int)(panel.x + 12.0f), (int)(panel.y + 8.0f), title_col);
    }

    /* --- Resource rows ------------------------------------ */
    for (i = 0; i < TRADE_SELLABLE_COUNT; i++) {
        SDL_FRect    row  = row_rect(screen_w, screen_h, i);
        ResourceType res  = (ResourceType)i;
        char         buf[64];
        SDL_Color    text_col = { 220, 200, 160, 255 };

        SDL_SetRenderDrawColor(renderer, 45, 37, 25, 255);
        SDL_RenderFillRect(renderer, &row);
        SDL_SetRenderDrawColor(renderer, 80, 66, 45, 255);
        SDL_RenderRect(renderer, &row);

        SDL_snprintf(buf, sizeof(buf), "%s: %d  (%d Gold ea.)",
                    SELLABLE_NAMES[i], s->amount[res], SELL_PRICE[res]);
        font_draw_text(renderer, FONT_NORMAL, buf,
                       (int)(row.x + 10.0f), (int)(row.y + 6.0f), text_col);

        for (btn = 0; btn < 3; btn++) {
            SDL_FRect br      = row_btn_rect(screen_w, screen_h, i, btn);
            int       hovr    = point_in(br, mouse_x, mouse_y);
            SDL_Color lbl_col = { 220, 200, 160, 255 };

            SDL_SetRenderDrawColor(renderer,
                hovr ? 90 : 60, hovr ? 75 : 50, hovr ? 50 : 33, 255);
            SDL_RenderFillRect(renderer, &br);
            SDL_SetRenderDrawColor(renderer,
                hovr ? 200 : 100, hovr ? 175 : 85, hovr ? 100 : 50, 255);
            SDL_RenderRect(renderer, &br);

            font_draw_text(renderer, FONT_SMALL, BTN_LABELS[btn],
                           (int)(br.x + 8.0f), (int)(br.y + 7.0f), lbl_col);
        }
    }

    /* --- Close button -------------------------------------- */
    {
        SDL_FRect cr   = close_btn_rect(screen_w, screen_h);
        int       hovr = point_in(cr, mouse_x, mouse_y);
        SDL_Color lbl_col = { 220, 175, 175, 255 };

        SDL_SetRenderDrawColor(renderer,
            hovr ? 120 : 80, hovr ? 35 : 22, hovr ? 35 : 22, 255);
        SDL_RenderFillRect(renderer, &cr);
        SDL_SetRenderDrawColor(renderer,
            hovr ? 200 : 100, hovr ? 100 : 50, hovr ? 100 : 50, 255);
        SDL_RenderRect(renderer, &cr);

        font_draw_text(renderer, FONT_NORMAL, "Close",
                       (int)(cr.x + 20.0f), (int)(cr.y + 6.0f), lbl_col);
    }
}

TradeHit trade_ui_hit_test(int screen_w, int screen_h,
                           int mouse_x, int mouse_y,
                           ResourceType *out_res, int *out_qty)
{
    int i, btn;

    if (point_in(close_btn_rect(screen_w, screen_h), mouse_x, mouse_y))
        return TRADE_HIT_CLOSE;

    for (i = 0; i < TRADE_SELLABLE_COUNT; i++) {
        for (btn = 0; btn < 3; btn++) {
            if (point_in(row_btn_rect(screen_w, screen_h, i, btn),
                        mouse_x, mouse_y)) {
                *out_res = (ResourceType)i;
                *out_qty = (btn == 0) ? 1 : (btn == 1) ? 10 : -1;
                return TRADE_HIT_SELL;
            }
        }
    }

    return TRADE_HIT_NONE;
}
