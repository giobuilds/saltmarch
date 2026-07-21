/*  trade_ui.c  --  Marketplace manual trade screen  (Phase 4,
 *  extended with buying)  */

#include "trade_ui.h"
#include "fonts.h"

static const char *SELLABLE_NAMES[TRADE_SELLABLE_COUNT] = { "Wood", "Fish", "Grain" };
static const char *SELL_BTN_LABELS[3] = { "+1", "+10", "All" };
static const char *BUY_BTN_LABELS[3]  = { "+1", "+10", "Max" };

static SDL_FRect panel_rect(int screen_w, int screen_h)
{
    SDL_FRect r;
    r.w = (float)TRADE_W;
    r.h = (float)TRADE_H;
    r.x = (float)((screen_w - TRADE_W) / 2);
    r.y = (float)((screen_h - TRADE_H) / 2);
    return r;
}

/* One resource's whole block: label line + sell row + buy row. */
static SDL_FRect block_rect(int screen_w, int screen_h, int i)
{
    SDL_FRect panel = panel_rect(screen_w, screen_h);
    SDL_FRect r;
    r.x = panel.x + (float)TRADE_MARGIN;
    r.w = panel.w - (float)TRADE_MARGIN * 2.0f;
    r.h = (float)TRADE_BLOCK_H;
    r.y = panel.y + (float)TRADE_TITLE_H + (float)TRADE_MARGIN
        + (float)i * ((float)TRADE_BLOCK_H + (float)TRADE_BLOCK_PAD);
    return r;
}

/* Button within resource block `resource_i`'s action row: is_buy
 * selects the Buy row (1) vs the Sell row (0); btn_i is 0/1/2 for
 * +1/+10/All-or-Max. Right-aligned within the block, evenly spaced. */
static SDL_FRect action_btn_rect(int screen_w, int screen_h,
                                 int resource_i, int is_buy, int btn_i)
{
    SDL_FRect block   = block_rect(screen_w, screen_h, resource_i);
    float     total_w = 3.0f * (float)TRADE_BTN_W + 2.0f * (float)TRADE_BTN_GAP;
    float     start_x = block.x + block.w - 10.0f - total_w;
    SDL_FRect r;
    r.w = (float)TRADE_BTN_W;
    r.h = (float)TRADE_BTN_H;
    r.x = start_x + (float)btn_i * ((float)TRADE_BTN_W + (float)TRADE_BTN_GAP);
    r.y = block.y + 28.0f
        + (float)is_buy * ((float)TRADE_BTN_H + (float)TRADE_ROW_GAP);
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

/* Shared by draw's Sell/Buy rows and the "greyed out" affordability
 * check: whether resource `res` has anything left to sell, or any
 * room+Gold left to buy. */
static int can_sell(const Stockpile *s, ResourceType res)
{
    return s->amount[res] > 0;
}

static int can_buy(const Stockpile *s, ResourceType res)
{
    int headroom = s->capacity - s->amount[res];
    return headroom > 0 && s->amount[RES_GOLD] >= BUY_PRICE[res];
}

void trade_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const Stockpile *s, int mouse_x, int mouse_y)
{
    int i, dir, btn;
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

    /* --- Resource blocks ------------------------------------ */
    for (i = 0; i < TRADE_SELLABLE_COUNT; i++) {
        SDL_FRect    block = block_rect(screen_w, screen_h, i);
        ResourceType res   = (ResourceType)i;
        char         buf[80];
        SDL_Color    text_col = { 220, 200, 160, 255 };

        SDL_SetRenderDrawColor(renderer, 45, 37, 25, 255);
        SDL_RenderFillRect(renderer, &block);
        SDL_SetRenderDrawColor(renderer, 80, 66, 45, 255);
        SDL_RenderRect(renderer, &block);

        SDL_snprintf(buf, sizeof(buf), "%s: %d  (sell %dg / buy %dg)",
                    SELLABLE_NAMES[i], s->amount[res],
                    SELL_PRICE[res], BUY_PRICE[res]);
        font_draw_text(renderer, FONT_NORMAL, buf,
                       (int)(block.x + 10.0f), (int)(block.y + 4.0f), text_col);

        /* Row-direction labels, left of the button groups */
        {
            SDL_Color dir_col = { 170, 155, 120, 255 };
            SDL_FRect sell_r  = action_btn_rect(screen_w, screen_h, i, 0, 0);
            SDL_FRect buy_r   = action_btn_rect(screen_w, screen_h, i, 1, 0);
            font_draw_text(renderer, FONT_SMALL, "Sell",
                           (int)(block.x + 10.0f), (int)(sell_r.y + 6.0f), dir_col);
            font_draw_text(renderer, FONT_SMALL, "Buy",
                           (int)(block.x + 10.0f), (int)(buy_r.y + 6.0f), dir_col);
        }

        for (dir = 0; dir < 2; dir++) {
            int afford = dir ? can_buy(s, res) : can_sell(s, res);

            for (btn = 0; btn < 3; btn++) {
                SDL_FRect br      = action_btn_rect(screen_w, screen_h, i, dir, btn);
                int       hovr    = point_in(br, mouse_x, mouse_y);
                SDL_Color lbl_col;

                SDL_SetRenderDrawColor(renderer,
                    (hovr && afford) ? 90 : 60,
                    (hovr && afford) ? 75 : 50,
                    (hovr && afford) ? 50 : 33, 255);
                SDL_RenderFillRect(renderer, &br);
                SDL_SetRenderDrawColor(renderer,
                    (hovr && afford) ? 200 : 100,
                    (hovr && afford) ? 175 : 85,
                    (hovr && afford) ? 100 : 50, 255);
                SDL_RenderRect(renderer, &br);

                lbl_col.r = afford ? 220 : 130;
                lbl_col.g = afford ? 200 : 115;
                lbl_col.b = afford ? 160 : 110;
                lbl_col.a = 255;
                font_draw_text(renderer, FONT_SMALL,
                               dir ? BUY_BTN_LABELS[btn] : SELL_BTN_LABELS[btn],
                               (int)(br.x + 8.0f), (int)(br.y + 6.0f), lbl_col);
            }
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
    int i, dir, btn;

    if (point_in(close_btn_rect(screen_w, screen_h), mouse_x, mouse_y))
        return TRADE_HIT_CLOSE;

    for (i = 0; i < TRADE_SELLABLE_COUNT; i++) {
        for (dir = 0; dir < 2; dir++) {
            for (btn = 0; btn < 3; btn++) {
                if (point_in(action_btn_rect(screen_w, screen_h, i, dir, btn),
                            mouse_x, mouse_y)) {
                    *out_res = (ResourceType)i;
                    *out_qty = (btn == 0) ? 1 : (btn == 1) ? 10 : -1;
                    return dir ? TRADE_HIT_BUY : TRADE_HIT_SELL;
                }
            }
        }
    }

    return TRADE_HIT_NONE;
}
