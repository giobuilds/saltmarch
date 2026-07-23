/*  build_confirm_ui.c  --  Build-confirmation popup  (fix pass)  */

#include "build_confirm_ui.h"
#include "fonts.h"
#include <SDL3/SDL.h>

static SDL_FRect panel_rect(int screen_w, int screen_h)
{
    SDL_FRect r;
    r.w = (float)BC_W;
    r.h = (float)BC_H;
    r.x = (float)((screen_w - BC_W) / 2);
    r.y = (float)((screen_h - BC_H) / 2);
    return r;
}

/* Payment option row: 0 = pay resources, 1 = pay Gold. */
static SDL_FRect option_rect(int screen_w, int screen_h, int i)
{
    SDL_FRect panel = panel_rect(screen_w, screen_h);
    SDL_FRect r;
    r.x = panel.x + (float)BC_MARGIN;
    r.w = panel.w - (float)BC_MARGIN * 2.0f;
    r.h = (float)BC_OPT_H;
    r.y = panel.y + (float)BC_TITLE_H + (float)BC_MARGIN
        + (float)i * ((float)BC_OPT_H + (float)BC_OPT_PAD);
    return r;
}

/* Bottom buttons: 0 = Ok, 1 = Cancel, side by side. */
static SDL_FRect bottom_btn_rect(int screen_w, int screen_h, int i)
{
    SDL_FRect panel = panel_rect(screen_w, screen_h);
    float      total_w = 2.0f * (float)BC_BTN_W + (float)BC_BTN_GAP;
    float      start_x = panel.x + (panel.w - total_w) / 2.0f;
    SDL_FRect  r;
    r.w = (float)BC_BTN_W;
    r.h = (float)BC_BTN_H;
    r.y = panel.y + panel.h - (float)BC_MARGIN - r.h;
    r.x = start_x + (float)i * ((float)BC_BTN_W + (float)BC_BTN_GAP);
    return r;
}

static int point_in(SDL_FRect r, int x, int y)
{
    return (float)x >= r.x && (float)x < r.x + r.w &&
           (float)y >= r.y && (float)y < r.y + r.h;
}

/* Builds a "N Name, N Name" string from `cost[]`, e.g. "20 Wood, 150
 * Gold". Every building's def->cost[] (including its own Gold cost)
 * is shown for the resources option; the Gold option instead shows
 * building_gold_equivalent_cost() as a single flat amount. */
static void format_resource_cost(const int cost[RES_COUNT],
                                 char *buf, size_t bufsz)
{
    int  i;
    int  first = 1;
    char item[32];

    buf[0] = '\0';
    for (i = 0; i < RES_COUNT; i++) {
        if (cost[i] <= 0) continue;
        SDL_snprintf(item, sizeof(item), "%s%d %s",
                    first ? "" : ", ", cost[i], RESOURCE_NAMES[i]);
        SDL_strlcat(buf, item, bufsz);
        first = 0;
    }
    if (first) SDL_strlcat(buf, "Free", bufsz);
}

void build_confirm_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                           BuildingType type, const Stockpile *s,
                           const Faction *fac,
                           int payment_selected, int mouse_x, int mouse_y)
{
    const BuildingDef *def = &BUILDING_DEFS[type];
    int   gold_cost = building_gold_equivalent_cost(type, fac);
    int   afford_resources = building_can_afford(s, type);
    int   afford_gold      = s->amount[RES_GOLD] >= gold_cost;
    char  cost_buf[128];
    char  line_buf[160];
    int   i;

    SDL_FRect panel     = panel_rect(screen_w, screen_h);
    SDL_FRect dim       = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
    SDL_FRect title_bar = { panel.x, panel.y, panel.w, (float)BC_TITLE_H };

    /* --- Dim the world behind the popup ------------------ */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_RenderFillRect(renderer, &dim);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    /* --- Panel background/border -------------------------- */
    SDL_SetRenderDrawColor(renderer, 35, 28, 18, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
    SDL_RenderRect(renderer, &panel);

    /* --- Title bar: building name -------------------------- */
    SDL_SetRenderDrawColor(renderer, 55, 44, 28, 255);
    SDL_RenderFillRect(renderer, &title_bar);
    SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
    SDL_RenderLine(renderer, panel.x, panel.y + (float)BC_TITLE_H,
                   panel.x + panel.w, panel.y + (float)BC_TITLE_H);
    {
        SDL_Color title_col = { 200, 175, 110, 255 };
        font_draw_text(renderer, FONT_NORMAL, def->name,
                       (int)(panel.x + 12.0f), (int)(panel.y + 8.0f), title_col);
    }

    /* --- Payment options ------------------------------------ */
    format_resource_cost(def->cost, cost_buf, sizeof(cost_buf));

    for (i = 0; i < BC_OPT_COUNT; i++) {
        SDL_FRect row     = option_rect(screen_w, screen_h, i);
        int       hovr    = point_in(row, mouse_x, mouse_y);
        int       sel     = (payment_selected == i);
        int       afford  = (i == 0) ? afford_resources : afford_gold;
        SDL_Color text_col;

        SDL_SetRenderDrawColor(renderer,
            (hovr || sel) ? 60 : 45, (hovr || sel) ? 50 : 37,
            (hovr || sel) ? 35 : 25, 255);
        SDL_RenderFillRect(renderer, &row);

        /* Selected option gets the same gold highlight border used
         * for a selected HUD slot; unaffordable options are dimmer
         * regardless of hover/selection. */
        if (sel)
            SDL_SetRenderDrawColor(renderer, 255, 210, 50, 255);
        else if (hovr)
            SDL_SetRenderDrawColor(renderer, 160, 140, 90, 255);
        else
            SDL_SetRenderDrawColor(renderer, 80, 66, 45, 255);
        SDL_RenderRect(renderer, &row);

        text_col.r = afford ? 220 : 140;
        text_col.g = afford ? 200 : 120;
        text_col.b = afford ? 160 : 120;
        text_col.a = 255;

        if (i == 0)
            SDL_snprintf(line_buf, sizeof(line_buf), "Pay Resources: %s", cost_buf);
        else
            SDL_snprintf(line_buf, sizeof(line_buf), "Pay Gold Only: %d Gold", gold_cost);

        font_draw_text(renderer, FONT_NORMAL, line_buf,
                       (int)(row.x + 10.0f), (int)(row.y + 8.0f), text_col);
        if (!afford)
            font_draw_text(renderer, FONT_SMALL, "(can't afford)",
                           (int)(row.x + 10.0f), (int)(row.y + 30.0f), text_col);
    }

    /* --- Ok / Cancel ----------------------------------------- */
    {
        SDL_FRect ok_r     = bottom_btn_rect(screen_w, screen_h, 0);
        SDL_FRect cancel_r = bottom_btn_rect(screen_w, screen_h, 1);
        int       ok_hovr  = point_in(ok_r, mouse_x, mouse_y);
        int       can_hovr = point_in(cancel_r, mouse_x, mouse_y);
        SDL_Color label_col = { 220, 200, 160, 255 };
        SDL_Color cancel_col = { 220, 175, 175, 255 };

        SDL_SetRenderDrawColor(renderer,
            ok_hovr ? 70 : 50, ok_hovr ? 58 : 42, ok_hovr ? 38 : 26, 255);
        SDL_RenderFillRect(renderer, &ok_r);
        SDL_SetRenderDrawColor(renderer,
            ok_hovr ? 200 : 100, ok_hovr ? 175 : 85, ok_hovr ? 100 : 50, 255);
        SDL_RenderRect(renderer, &ok_r);
        font_draw_text(renderer, FONT_NORMAL, "Ok",
                       (int)(ok_r.x + 34.0f), (int)(ok_r.y + 8.0f), label_col);

        SDL_SetRenderDrawColor(renderer,
            can_hovr ? 120 : 80, can_hovr ? 35 : 22, can_hovr ? 35 : 22, 255);
        SDL_RenderFillRect(renderer, &cancel_r);
        SDL_SetRenderDrawColor(renderer,
            can_hovr ? 200 : 100, can_hovr ? 100 : 50, can_hovr ? 100 : 50, 255);
        SDL_RenderRect(renderer, &cancel_r);
        font_draw_text(renderer, FONT_NORMAL, "Cancel",
                       (int)(cancel_r.x + 18.0f), (int)(cancel_r.y + 8.0f), cancel_col);
    }
}

BuildConfirmHit build_confirm_ui_hit_test(int screen_w, int screen_h,
                                          int mouse_x, int mouse_y)
{
    int i;

    if (point_in(bottom_btn_rect(screen_w, screen_h, 0), mouse_x, mouse_y))
        return BC_HIT_OK;
    if (point_in(bottom_btn_rect(screen_w, screen_h, 1), mouse_x, mouse_y))
        return BC_HIT_CANCEL;

    for (i = 0; i < BC_OPT_COUNT; i++) {
        if (point_in(option_rect(screen_w, screen_h, i), mouse_x, mouse_y))
            return (i == 0) ? BC_HIT_PAY_RESOURCES : BC_HIT_PAY_GOLD;
    }

    return BC_HIT_NONE;
}
