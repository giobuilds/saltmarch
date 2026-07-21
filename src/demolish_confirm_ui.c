/*  demolish_confirm_ui.c  --  Bulldozer confirmation popup  */

#include "demolish_confirm_ui.h"
#include "fonts.h"

static SDL_FRect panel_rect(int screen_w, int screen_h)
{
    SDL_FRect r;
    r.w = (float)DC_W;
    r.h = (float)DC_H;
    r.x = (float)((screen_w - DC_W) / 2);
    r.y = (float)((screen_h - DC_H) / 2);
    return r;
}

/* Buttons side by side, centred: 0 = Destroy, 1 = Cancel. */
static SDL_FRect btn_rect(int screen_w, int screen_h, int i)
{
    SDL_FRect panel   = panel_rect(screen_w, screen_h);
    float     total_w = 2.0f * (float)DC_BTN_W + (float)DC_BTN_GAP;
    float     start_x = panel.x + (panel.w - total_w) / 2.0f;
    SDL_FRect r;
    r.w = (float)DC_BTN_W;
    r.h = (float)DC_BTN_H;
    r.y = panel.y + panel.h - (float)DC_MARGIN - r.h;
    r.x = start_x + (float)i * ((float)DC_BTN_W + (float)DC_BTN_GAP);
    return r;
}

static int point_in(SDL_FRect r, int x, int y)
{
    return (float)x >= r.x && (float)x < r.x + r.w &&
           (float)y >= r.y && (float)y < r.y + r.h;
}

void demolish_confirm_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                              const char *building_name,
                              int mouse_x, int mouse_y)
{
    SDL_FRect panel     = panel_rect(screen_w, screen_h);
    SDL_FRect dim       = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
    SDL_FRect title_bar = { panel.x, panel.y, panel.w, (float)DC_TITLE_H };
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
    SDL_RenderLine(renderer, panel.x, panel.y + (float)DC_TITLE_H,
                   panel.x + panel.w, panel.y + (float)DC_TITLE_H);
    {
        SDL_Color title_col = { 230, 110, 95, 255 };
        font_draw_text(renderer, FONT_NORMAL, "Destroy Building?",
                       (int)(panel.x + 12.0f), (int)(panel.y + 8.0f), title_col);
    }

    /* --- Message --------------------------------------------- */
    SDL_snprintf(msg, sizeof(msg), "Destroy this %s?", building_name);
    {
        SDL_Color msg_col = { 220, 200, 160, 255 };
        font_draw_text(renderer, FONT_NORMAL, msg,
                       (int)(panel.x + 12.0f),
                       (int)(panel.y + (float)DC_TITLE_H + 16.0f), msg_col);
    }

    /* --- Buttons: Destroy (red — the irreversible one), Cancel --- */
    {
        SDL_FRect ok_r     = btn_rect(screen_w, screen_h, 0);
        SDL_FRect cancel_r = btn_rect(screen_w, screen_h, 1);
        int       ok_hovr  = point_in(ok_r, mouse_x, mouse_y);
        int       can_hovr = point_in(cancel_r, mouse_x, mouse_y);
        SDL_Color ok_col     = { 255, 210, 200, 255 };
        SDL_Color cancel_col = { 220, 200, 160, 255 };

        SDL_SetRenderDrawColor(renderer,
            ok_hovr ? 150 : 100, ok_hovr ? 35 : 24, ok_hovr ? 30 : 20, 255);
        SDL_RenderFillRect(renderer, &ok_r);
        SDL_SetRenderDrawColor(renderer,
            ok_hovr ? 230 : 140, ok_hovr ? 90 : 60, ok_hovr ? 75 : 50, 255);
        SDL_RenderRect(renderer, &ok_r);
        font_draw_text(renderer, FONT_NORMAL, "Destroy",
                       (int)(ok_r.x + 14.0f), (int)(ok_r.y + 8.0f), ok_col);

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

DemolishConfirmHit demolish_confirm_ui_hit_test(int screen_w, int screen_h,
                                                int mouse_x, int mouse_y)
{
    if (point_in(btn_rect(screen_w, screen_h, 0), mouse_x, mouse_y))
        return DC_HIT_OK;
    if (point_in(btn_rect(screen_w, screen_h, 1), mouse_x, mouse_y))
        return DC_HIT_CANCEL;
    return DC_HIT_NONE;
}
