/*  tier_upgrade_ui.c  --  Population tier upgrade confirmation  */

#include "tier_upgrade_ui.h"
#include "fonts.h"

static SDL_FRect panel_rect(int screen_w, int screen_h)
{
    SDL_FRect r;
    r.w = (float)TU_W;
    r.h = (float)TU_H;
    r.x = (float)((screen_w - TU_W) / 2);
    r.y = (float)((screen_h - TU_H) / 2);
    return r;
}

/* Buttons side by side, centred: 0 = Upgrade, 1 = Cancel. */
static SDL_FRect btn_rect(int screen_w, int screen_h, int i)
{
    SDL_FRect panel   = panel_rect(screen_w, screen_h);
    float     total_w = 2.0f * (float)TU_BTN_W + (float)TU_BTN_GAP;
    float     start_x = panel.x + (panel.w - total_w) / 2.0f;
    SDL_FRect r;
    r.w = (float)TU_BTN_W;
    r.h = (float)TU_BTN_H;
    r.y = panel.y + panel.h - (float)TU_MARGIN - r.h;
    r.x = start_x + (float)i * ((float)TU_BTN_W + (float)TU_BTN_GAP);
    return r;
}

static int point_in(SDL_FRect r, int x, int y)
{
    return (float)x >= r.x && (float)x < r.x + r.w &&
           (float)y >= r.y && (float)y < r.y + r.h;
}

void tier_upgrade_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                         int gold_cost, int can_afford,
                         int mouse_x, int mouse_y)
{
    SDL_FRect panel     = panel_rect(screen_w, screen_h);
    SDL_FRect dim       = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
    SDL_FRect title_bar = { panel.x, panel.y, panel.w, (float)TU_TITLE_H };
    char      msg[96];

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

    /* --- Title bar ------------------------------------------ */
    SDL_SetRenderDrawColor(renderer, 55, 44, 28, 255);
    SDL_RenderFillRect(renderer, &title_bar);
    SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
    SDL_RenderLine(renderer, panel.x, panel.y + (float)TU_TITLE_H,
                   panel.x + panel.w, panel.y + (float)TU_TITLE_H);
    {
        SDL_Color title_col = { 140, 220, 150, 255 };
        font_draw_text(renderer, FONT_NORMAL, "Upgrade to Worker's House?",
                       (int)(panel.x + 12.0f), (int)(panel.y + 8.0f), title_col);
    }

    /* --- Message --------------------------------------------- */
    SDL_snprintf(msg, sizeof(msg), "Cost: %d Gold. Workers also need Beer.",
                gold_cost);
    {
        SDL_Color msg_col = { 220, 200, 160, 255 };
        font_draw_text(renderer, FONT_NORMAL, msg,
                       (int)(panel.x + 12.0f),
                       (int)(panel.y + (float)TU_TITLE_H + 16.0f), msg_col);
    }

    /* --- Buttons: Upgrade (affirmative green), Cancel --------- */
    {
        SDL_FRect ok_r     = btn_rect(screen_w, screen_h, 0);
        SDL_FRect cancel_r = btn_rect(screen_w, screen_h, 1);
        int       ok_hovr  = point_in(ok_r, mouse_x, mouse_y) && can_afford;
        int       can_hovr = point_in(cancel_r, mouse_x, mouse_y);
        SDL_Color ok_col     = { 200, 255, 210, 255 };
        SDL_Color cancel_col = { 220, 200, 160, 255 };

        if (!can_afford) {
            ok_col.r = 140; ok_col.g = 130; ok_col.b = 120;
        }

        SDL_SetRenderDrawColor(renderer,
            !can_afford ? 45 : (ok_hovr ? 60 : 40),
            !can_afford ? 40 : (ok_hovr ? 130 : 90),
            !can_afford ? 35 : (ok_hovr ?  70 : 50), 255);
        SDL_RenderFillRect(renderer, &ok_r);
        SDL_SetRenderDrawColor(renderer,
            !can_afford ? 90 : (ok_hovr ? 120 : 80),
            !can_afford ? 85 : (ok_hovr ? 210 : 160),
            !can_afford ? 75 : (ok_hovr ? 130 : 95), 255);
        SDL_RenderRect(renderer, &ok_r);
        font_draw_text(renderer, FONT_NORMAL, "Upgrade",
                       (int)(ok_r.x + 12.0f), (int)(ok_r.y + 8.0f), ok_col);

        SDL_SetRenderDrawColor(renderer,
            can_hovr ? 70 : 50, can_hovr ? 58 : 42, can_hovr ? 38 : 26, 255);
        SDL_RenderFillRect(renderer, &cancel_r);
        SDL_SetRenderDrawColor(renderer,
            can_hovr ? 200 : 100, can_hovr ? 175 : 85, can_hovr ? 100 : 50, 255);
        SDL_RenderRect(renderer, &cancel_r);
        font_draw_text(renderer, FONT_NORMAL, "Cancel",
                       (int)(cancel_r.x + 18.0f), (int)(cancel_r.y + 8.0f), cancel_col);
    }
}

TierUpgradeHit tier_upgrade_ui_hit_test(int screen_w, int screen_h,
                                       int mouse_x, int mouse_y)
{
    if (point_in(btn_rect(screen_w, screen_h, 0), mouse_x, mouse_y))
        return TU_HIT_OK;
    if (point_in(btn_rect(screen_w, screen_h, 1), mouse_x, mouse_y))
        return TU_HIT_CANCEL;
    return TU_HIT_NONE;
}
