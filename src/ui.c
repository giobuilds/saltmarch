/*  ui.c  --  HUD rendering and hit-testing  */

#include "ui.h"
#include "fonts.h"
#include <string.h>   /* strlen (unused yet, kept for later) */

/* ---- slot_rect -----------------------------------------
 * Compute the screen rectangle for HUD slot `i`.
 * The bar is pinned to the bottom of the screen.
 * -------------------------------------------------------- */
static SDL_FRect slot_rect(int screen_w, int screen_h, int i)
{
    SDL_FRect r;
    int bar_y = screen_h - HUD_HEIGHT;
    (void)screen_w;

    r.x = (float)(HUD_MARGIN_LEFT + i * (HUD_SLOT_SIZE + HUD_SLOT_PAD));
    r.y = (float)(bar_y + (HUD_HEIGHT - HUD_SLOT_SIZE) / 2);
    r.w = (float)HUD_SLOT_SIZE;
    r.h = (float)HUD_SLOT_SIZE;
    return r;
}

/* Cog button rectangle — right-anchored in the HUD bar.
 * Same size as a building slot, inset HUD_MARGIN_LEFT from the right. */
static SDL_FRect cog_rect(int screen_w, int screen_h)
{
    int bar_y = screen_h - HUD_HEIGHT;
    SDL_FRect r;
    r.w = (float)HUD_SLOT_SIZE;
    r.h = (float)HUD_SLOT_SIZE;
    r.x = (float)(screen_w - HUD_MARGIN_LEFT) - r.w;
    r.y = (float)(bar_y + (HUD_HEIGHT - HUD_SLOT_SIZE) / 2);
    return r;
}

/* ---- HUD-placeable building list -----------------------
 * Not every BuildingType gets a HUD slot (e.g. BUILDING_HOUSE_WORKER
 * is only ever reached by upgrading a placed House — see
 * game_upgrade_house, game.c). These map a HUD slot POSITION
 * (0..hud_slot_count()-1) to the BuildingType that actually occupies
 * it, filtering out anything with hud_placeable == 0, so slot
 * positions stay contiguous instead of leaving a gap for each
 * excluded type. */
static int hud_slot_count(void)
{
    int i, n = 0;
    for (i = 0; i < BUILDING_TYPE_COUNT; i++)
        if (BUILDING_DEFS[i].hud_placeable) n++;
    return n;
}

static BuildingType hud_slot_type(int slot_index)
{
    int i, n = 0;
    for (i = 0; i < BUILDING_TYPE_COUNT; i++) {
        if (!BUILDING_DEFS[i].hud_placeable) continue;
        if (n == slot_index) return (BuildingType)i;
        n++;
    }
    return BUILDING_NONE;
}

/* Demolish button rectangle — right-anchored just left of the cog. */
static SDL_FRect demolish_rect(int screen_w, int screen_h)
{
    SDL_FRect cog = cog_rect(screen_w, screen_h);
    SDL_FRect r;
    r.w = (float)HUD_SLOT_SIZE;
    r.h = (float)HUD_SLOT_SIZE;
    r.x = cog.x - (float)HUD_SLOT_PAD - r.w;
    r.y = cog.y;
    return r;
}

/* One menu button rectangle.
 * Buttons are stacked vertically, centred on the screen. */
static SDL_FRect menu_btn_rect(int screen_w, int screen_h, int i)
{
    /* Panel top-left */
    float px = (float)((screen_w - MENU_W) / 2);
    float py = (float)((screen_h - MENU_H) / 2);
    SDL_FRect r;
    r.x = px + (float)MENU_BTN_MARGIN;
    r.w = (float)(MENU_W - MENU_BTN_MARGIN * 2);
    r.h = (float)MENU_BTN_H;
    /* Title bar is 36px, then buttons with padding */
    r.y = py + 36.0f + (float)i * ((float)MENU_BTN_H + (float)MENU_BTN_PAD);
    return r;
}

/* ---- ui_hit_test --------------------------------------- */
BuildingType ui_hit_test(int screen_w, int screen_h,
                         int mouse_x, int mouse_y)
{
    int i, count = hud_slot_count();
    for (i = 0; i < count; i++) {
        SDL_FRect r = slot_rect(screen_w, screen_h, i);
        if ((float)mouse_x >= r.x && (float)mouse_x < r.x + r.w &&
            (float)mouse_y >= r.y && (float)mouse_y < r.y + r.h) {
            return hud_slot_type(i);
        }
    }
    return BUILDING_NONE;
}

int ui_cog_hit_test(int screen_w, int screen_h,
                    int mouse_x, int mouse_y)
{
    SDL_FRect r = cog_rect(screen_w, screen_h);
    return ((float)mouse_x >= r.x && (float)mouse_x < r.x + r.w &&
            (float)mouse_y >= r.y && (float)mouse_y < r.y + r.h);
}

int ui_demolish_hit_test(int screen_w, int screen_h,
                         int mouse_x, int mouse_y)
{
    SDL_FRect r = demolish_rect(screen_w, screen_h);
    return ((float)mouse_x >= r.x && (float)mouse_x < r.x + r.w &&
            (float)mouse_y >= r.y && (float)mouse_y < r.y + r.h);
}

/* ---- ui_draw ------------------------------------------- */
void ui_draw(SDL_Renderer *renderer,
             int screen_w, int screen_h,
             BuildingType selected,
             int mouse_x, int mouse_y, int menu_open,
             int demolish_active)
{
    int i;
    float bar_y = (float)(screen_h - HUD_HEIGHT);

    /* --- Background bar --------------------------------- */
    SDL_SetRenderDrawColor(renderer, 30, 25, 20, 220);
    SDL_FRect bar = { 0.0f, bar_y, (float)screen_w, (float)HUD_HEIGHT };
    SDL_RenderFillRect(renderer, &bar);

    /* Thin top border line */
    SDL_SetRenderDrawColor(renderer, 90, 75, 55, 255);
    SDL_RenderLine(renderer,
        0.0f,           bar_y,
        (float)screen_w,bar_y);

    /* --- Building slots --------------------------------- */
    for (i = 0; i < hud_slot_count(); i++) {
        BuildingType       t    = hud_slot_type(i);
        const BuildingDef *def  = &BUILDING_DEFS[t];
        SDL_FRect          r    = slot_rect(screen_w, screen_h, i);
        int                hovr = ui_hit_test(screen_w, screen_h,
                                              mouse_x, mouse_y) == t;
        int                sel  = (selected == t);

        SDL_SetRenderDrawColor(renderer,
            (hovr || sel) ? 60 : 40,
            (hovr || sel) ? 50 : 33,
            (hovr || sel) ? 35 : 22, 255);
        SDL_RenderFillRect(renderer, &r);
 
        SDL_FRect swatch = { r.x+8.0f, r.y+8.0f, r.w-16.0f, r.h-24.0f };
        SDL_SetRenderDrawColor(renderer,
            def->col_r, def->col_g, def->col_b, 255);
        SDL_RenderFillRect(renderer, &swatch);

        /* Slot border: gold if selected, dim grey otherwise */
        if (sel)
            SDL_SetRenderDrawColor(renderer, 255, 210, 50, 255);
        else if (hovr)
            SDL_SetRenderDrawColor(renderer, 160, 140, 90, 255);
        else
            SDL_SetRenderDrawColor(renderer, 70, 60, 40, 255);
        SDL_RenderRect(renderer, &r);

        /* Building size annotation (small dot grid in corner) */
        /* e.g. 2x2 gets four dots, 1x1 gets one */
        {
            float dw = (float)def->tile_w;
            float dh = (float)def->tile_h;
            float dot_area_x = r.x + r.w - 4.0f - dw * 5.0f;
            float dot_area_y = r.y + r.h - 4.0f - dh * 5.0f;
            int dr, dc;
            SDL_SetRenderDrawColor(renderer, 200, 180, 120, 200);
            for (dr = 0; dr < def->tile_h; dr++) {
                for (dc = 0; dc < def->tile_w; dc++) {
                    SDL_FRect dot = {
                        dot_area_x + (float)dc * 5.0f,
                        dot_area_y + (float)dr * 5.0f,
                        3.0f, 3.0f
                    };
                    SDL_RenderFillRect(renderer, &dot);
                }
            }
        }
    

            /* Tooltip: building name, shown above the slot on hover */
        if (hovr) {
            int   name_w = 0, name_h = 0;
            int   have_size = font_measure_text(FONT_SMALL, def->name,
                                                &name_w, &name_h);
            float pad_x  = 8.0f;
            float tip_w  = have_size ? (float)name_w + pad_x * 2.0f : r.w;
            float tip_h  = have_size ? (float)name_h + 6.0f : 18.0f;
            SDL_FRect tip = {
                r.x + r.w / 2.0f - tip_w / 2.0f,
                r.y - tip_h - 4.0f,
                tip_w, tip_h
            };
            SDL_SetRenderDrawColor(renderer, 50, 42, 28, 240);
            SDL_RenderFillRect(renderer, &tip);
            SDL_SetRenderDrawColor(renderer, 140, 120, 70, 255);
            SDL_RenderRect(renderer, &tip);

            if (have_size) {
                SDL_Color name_col = { 230, 215, 180, 255 };
                font_draw_text(renderer, FONT_SMALL, def->name,
                               (int)(tip.x + pad_x), (int)(tip.y + 3.0f),
                               name_col);
            }
        }
    }
 
    /* --- Demolish button --------------------------------- */
    {
        SDL_FRect r    = demolish_rect(screen_w, screen_h);
        int       hovr = ui_demolish_hit_test(screen_w, screen_h, mouse_x, mouse_y);

        SDL_SetRenderDrawColor(renderer,
            (hovr || demolish_active) ? 90 : 45,
            (hovr || demolish_active) ? 30 : 22,
            (hovr || demolish_active) ? 28 : 18, 255);
        SDL_RenderFillRect(renderer, &r);

        /* Border: bright red while active, dim otherwise */
        if (demolish_active)
            SDL_SetRenderDrawColor(renderer, 255, 90, 70, 255);
        else if (hovr)
            SDL_SetRenderDrawColor(renderer, 200, 110, 90, 255);
        else
            SDL_SetRenderDrawColor(renderer, 90, 50, 42, 255);
        SDL_RenderRect(renderer, &r);

        /* Icon: a simple X made of two crossing lines, matching the
         * cog's "abstract geometric shape" style rather than a real
         * pictogram (no sprite system exists — see render.c). */
        SDL_SetRenderDrawColor(renderer, 230, 90, 75, 255);
        SDL_RenderLine(renderer, r.x+16.0f, r.y+16.0f, r.x+r.w-16.0f, r.y+r.h-16.0f);
        SDL_RenderLine(renderer, r.x+r.w-16.0f, r.y+16.0f, r.x+16.0f, r.y+r.h-16.0f);
    }

    /* --- Cog button ------------------------------------- */
    {
        SDL_FRect r    = cog_rect(screen_w, screen_h);
        int       hovr = ui_cog_hit_test(screen_w, screen_h, mouse_x, mouse_y);
 
        /* Background */
        SDL_SetRenderDrawColor(renderer,
            (hovr || menu_open) ? 70 : 40,
            (hovr || menu_open) ? 58 : 33,
            (hovr || menu_open) ? 38 : 22, 255);
        SDL_RenderFillRect(renderer, &r);
 
        /* Border: gold when menu is open, dim otherwise */
        if (menu_open)
            SDL_SetRenderDrawColor(renderer, 255, 210, 50, 255);
        else if (hovr)
            SDL_SetRenderDrawColor(renderer, 160, 140, 90, 255);
        else
            SDL_SetRenderDrawColor(renderer, 70, 60, 40, 255);
        SDL_RenderRect(renderer, &r);
 
        /* Cog icon — drawn as concentric rectangles for now.
         * Outer ring */
        SDL_FRect outer = { r.x+10.0f, r.y+10.0f, r.w-20.0f, r.h-20.0f };
        SDL_SetRenderDrawColor(renderer, 190, 170, 110, 255);
        SDL_RenderRect(renderer, &outer);
 
        /* Inner filled square (the hub) */
        SDL_FRect inner = { r.x+22.0f, r.y+22.0f, r.w-44.0f, r.h-44.0f };
        SDL_SetRenderDrawColor(renderer, 190, 170, 110, 255);
        SDL_RenderFillRect(renderer, &inner);
 
        /* Four teeth on the cog — small rectangles at N/S/E/W */
        float cx = r.x + r.w / 2.0f;
        float cy = r.y + r.h / 2.0f;
        SDL_FRect teeth[4] = {
            { cx-4.0f, r.y+4.0f,  8.0f, 8.0f },   /* top    */
            { cx-4.0f, r.y+r.h-12.0f, 8.0f, 8.0f },/* bottom */
            { r.x+4.0f,  cy-4.0f, 8.0f, 8.0f },    /* left   */
            { r.x+r.w-12.0f, cy-4.0f, 8.0f, 8.0f } /* right  */
        };
        SDL_SetRenderDrawColor(renderer, 190, 170, 110, 255);
        SDL_RenderFillRects(renderer, teeth, 4);
    }
}
 
/* =========================================================
 * Menu overlay
 * ========================================================= */
 
static const char *MENU_LABELS[MENU_BTN_COUNT] =
    { "New Game", "Load", "Save", "Quit" };

MenuHit ui_menu_hit_test(int screen_w, int screen_h,
                         int mouse_x, int mouse_y)
{
    int i;
    for (i = 0; i < MENU_BTN_COUNT; i++) {
        SDL_FRect r = menu_btn_rect(screen_w, screen_h, i);
        if ((float)mouse_x >= r.x && (float)mouse_x < r.x + r.w &&
            (float)mouse_y >= r.y && (float)mouse_y < r.y + r.h)
            return (MenuHit)(i + 1);   /* +1 because MENU_HIT_NONE = 0 */
    }
    return MENU_HIT_NONE;
}
 
void ui_menu_draw(SDL_Renderer *renderer,
                  int screen_w, int screen_h,
                  int mouse_x, int mouse_y)
{
    int i;
    float px = (float)((screen_w - MENU_W) / 2);
    float py = (float)((screen_h - MENU_H) / 2);
 
    /* --- Dim the world behind the menu ----------------- */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_FRect dim = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
    SDL_RenderFillRect(renderer, &dim);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
 
    /* --- Panel background ------------------------------ */
    SDL_FRect panel = { px, py, (float)MENU_W, (float)MENU_H };
    SDL_SetRenderDrawColor(renderer, 35, 28, 18, 255);
    SDL_RenderFillRect(renderer, &panel);
 
    /* Panel border */
    SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
    SDL_RenderRect(renderer, &panel);
 
    /* Title bar */
    SDL_FRect title_bar = { px, py, (float)MENU_W, 34.0f };
    SDL_SetRenderDrawColor(renderer, 55, 44, 28, 255);
    SDL_RenderFillRect(renderer, &title_bar);
    SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
    SDL_RenderLine(renderer, px, py+34.0f, px+(float)MENU_W, py+34.0f);
 
    /* Title text */
    {
        SDL_Color title_col = { 200, 175, 110, 255 };
        font_draw_text(renderer, FONT_NORMAL, "Menu",
                       (int)(px + 12.0f), (int)(py + 8.0f), title_col);
    }
 
    /* --- Buttons --------------------------------------- */
    for (i = 0; i < MENU_BTN_COUNT; i++) {
        SDL_FRect r    = menu_btn_rect(screen_w, screen_h, i);
        MenuHit   hov  = ui_menu_hit_test(screen_w, screen_h,
                                          mouse_x, mouse_y);
        int       hovr = (hov == (MenuHit)(i + 1));

        /* Quit is always the last button; give it a reddish
         * tint to signal danger. */
        if (i == MENU_BTN_COUNT - 1) {
            SDL_SetRenderDrawColor(renderer,
                hovr ? 120 : 80,
                hovr ? 35  : 22,
                hovr ? 35  : 22, 255);
        } else {
            SDL_SetRenderDrawColor(renderer,
                hovr ? 70 : 50,
                hovr ? 58 : 42,
                hovr ? 38 : 26, 255);
        }
        SDL_RenderFillRect(renderer, &r);
 
        /* Button border */
        SDL_SetRenderDrawColor(renderer,
            hovr ? 200 : 100,
            hovr ? 175 : 85,
            hovr ? 100 : 50, 255);
        SDL_RenderRect(renderer, &r);
 
        /* Button label */
        {
            SDL_Color label_col;
            int is_quit = (i == MENU_BTN_COUNT - 1);
            label_col.r = is_quit ? 220 : 190;
            label_col.g = is_quit ?  80 : 165;
            label_col.b = is_quit ?  80 : 100;
            label_col.a = 255;
            font_draw_text(renderer, FONT_NORMAL, MENU_LABELS[i],
                           (int)(r.x + 12.0f),
                           (int)(r.y + (r.h - 18.0f) / 2.0f),
                           label_col);
        }
    }
}
 