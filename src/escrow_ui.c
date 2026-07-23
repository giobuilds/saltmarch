/*  escrow_ui.c  --  The Harbor escrow panel (MMO_PLAN Phase 5)  */

#include "escrow_ui.h"
#include "fonts.h"

static SDL_FRect panel_rect(int screen_w, int screen_h)
{
    SDL_FRect r;
    r.w = (float)ESCROW_W;
    r.h = (float)ESCROW_H;
    r.x = (float)((screen_w - ESCROW_W) / 2);
    r.y = (float)((screen_h - ESCROW_H) / 2);
    return r;
}

static SDL_FRect row_rect(int screen_w, int screen_h, int i)
{
    SDL_FRect panel = panel_rect(screen_w, screen_h);
    SDL_FRect r;
    r.x = panel.x + (float)ESCROW_MARGIN;
    r.w = panel.w - (float)ESCROW_MARGIN * 2.0f;
    r.h = (float)ESCROW_ROW_H;
    r.y = panel.y + (float)ESCROW_TITLE_H + (float)ESCROW_MARGIN
        + (float)i * (float)ESCROW_ROW_H;
    return r;
}

/* btn 0 = Take, 1 = Put — right-aligned within the row. */
static SDL_FRect row_btn_rect(int screen_w, int screen_h, int i, int btn)
{
    SDL_FRect row = row_rect(screen_w, screen_h, i);
    SDL_FRect r;
    r.w = (float)ESCROW_BTN_W;
    r.h = (float)ESCROW_BTN_H;
    r.x = row.x + row.w
        - (float)(2 - btn) * ((float)ESCROW_BTN_W + (float)ESCROW_BTN_GAP);
    r.y = row.y + (row.h - r.h) / 2.0f;
    return r;
}

static SDL_FRect docking_btn_rect(int screen_w, int screen_h)
{
    SDL_FRect panel = panel_rect(screen_w, screen_h);
    SDL_FRect r;
    r.w = 190.0f;
    r.h = 30.0f;
    r.x = panel.x + (float)ESCROW_MARGIN;
    r.y = panel.y + panel.h - (float)ESCROW_MARGIN - r.h;
    return r;
}

static SDL_FRect close_btn_rect(int screen_w, int screen_h)
{
    SDL_FRect panel = panel_rect(screen_w, screen_h);
    SDL_FRect r;
    r.w = 110.0f;
    r.h = 30.0f;
    r.x = panel.x + panel.w - (float)ESCROW_MARGIN - r.w;
    r.y = panel.y + panel.h - (float)ESCROW_MARGIN - r.h;
    return r;
}

static int point_in(SDL_FRect r, int x, int y)
{
    return (float)x >= r.x && (float)x < r.x + r.w &&
           (float)y >= r.y && (float)y < r.y + r.h;
}

static void draw_btn(SDL_Renderer *renderer, SDL_FRect r, const char *label,
                     int enabled, int hovered)
{
    SDL_Color lbl;

    SDL_SetRenderDrawColor(renderer,
        (hovered && enabled) ? 90 : 60,
        (hovered && enabled) ? 75 : 50,
        (hovered && enabled) ? 50 : 33, 255);
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer,
        (hovered && enabled) ? 200 : 100,
        (hovered && enabled) ? 175 : 85,
        (hovered && enabled) ? 100 : 50, 255);
    SDL_RenderRect(renderer, &r);

    lbl.r = enabled ? 220 : 130;
    lbl.g = enabled ? 200 : 115;
    lbl.b = enabled ? 160 : 110;
    lbl.a = 255;
    font_draw_text(renderer, FONT_SMALL, label,
                   (int)(r.x + 8.0f), (int)(r.y + 5.0f), lbl);
}

void escrow_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                    const Island *isl, int mouse_x, int mouse_y)
{
    SDL_FRect panel     = panel_rect(screen_w, screen_h);
    SDL_FRect dim       = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
    SDL_FRect title_bar = { panel.x, panel.y, panel.w, (float)ESCROW_TITLE_H };
    char      buf[96];
    int       i;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_RenderFillRect(renderer, &dim);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(renderer, 22, 33, 44, 255);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 90, 130, 170, 255);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 32, 48, 64, 255);
    SDL_RenderFillRect(renderer, &title_bar);
    {
        SDL_Color title_col = { 170, 205, 235, 255 };
        SDL_snprintf(buf, sizeof(buf), "%s — Harbor escrow", isl->name);
        font_draw_text(renderer, FONT_NORMAL, buf,
                       (int)(panel.x + 12.0f), (int)(panel.y + 8.0f),
                       title_col);
    }

    for (i = 0; i < RES_COUNT; i++) {
        SDL_FRect row  = row_rect(screen_w, screen_h, i);
        SDL_FRect take = row_btn_rect(screen_w, screen_h, i, 0);
        SDL_FRect put  = row_btn_rect(screen_w, screen_h, i, 1);
        SDL_Color txt  = { 210, 220, 230, 255 };

        SDL_snprintf(buf, sizeof(buf), "%-6s in escrow: %d",
                     RESOURCE_NAMES[i], isl->escrow[i]);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(row.x + 4.0f), (int)(row.y + 7.0f), txt);

        draw_btn(renderer, take, "Take",
                 isl->escrow[i] > 0, point_in(take, mouse_x, mouse_y));
        draw_btn(renderer, put, "Put 10",
                 isl->stockpile.amount[i] > 0, point_in(put, mouse_x, mouse_y));
    }

    draw_btn(renderer, docking_btn_rect(screen_w, screen_h),
             isl->docking_allowed ? "Docking: OPEN" : "Docking: CLOSED",
             1, point_in(docking_btn_rect(screen_w, screen_h),
                         mouse_x, mouse_y));
    draw_btn(renderer, close_btn_rect(screen_w, screen_h), "Close", 1,
             point_in(close_btn_rect(screen_w, screen_h), mouse_x, mouse_y));
}

EscrowHit escrow_ui_hit_test(int screen_w, int screen_h,
                             int mouse_x, int mouse_y,
                             ResourceType *out_res)
{
    int i;

    if (point_in(close_btn_rect(screen_w, screen_h), mouse_x, mouse_y))
        return ESCROW_HIT_CLOSE;
    if (point_in(docking_btn_rect(screen_w, screen_h), mouse_x, mouse_y))
        return ESCROW_HIT_DOCKING;

    for (i = 0; i < RES_COUNT; i++) {
        if (point_in(row_btn_rect(screen_w, screen_h, i, 0),
                     mouse_x, mouse_y)) {
            *out_res = (ResourceType)i;
            return ESCROW_HIT_TAKE;
        }
        if (point_in(row_btn_rect(screen_w, screen_h, i, 1),
                     mouse_x, mouse_y)) {
            *out_res = (ResourceType)i;
            return ESCROW_HIT_PUT;
        }
    }

    return ESCROW_HIT_NONE;
}
