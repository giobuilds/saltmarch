/* ui.c  --  HUD painting and the menu overlay */

#include "ui.h"
#include "fonts.h"
#include "render.h"   /* render_draw_diamond for the world icon */
#include <string.h>

/* ---- small painting helpers ------------------------------ */

static SDL_FRect to_sdl(UiRect r)
{
    SDL_FRect s;
    s.x = r.x; s.y = r.y; s.w = r.w; s.h = r.h;
    return s;
}

static void fill(SDL_Renderer *r, UiRect rect,
                 Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    SDL_FRect f = to_sdl(rect);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderFillRect(r, &f);
}

static void outline(SDL_Renderer *r, UiRect rect,
                    Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    SDL_FRect f = to_sdl(rect);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderRect(r, &f);
}

/* The one place font_measure_text is still called. Measuring to size a
 * tooltip BOX is fine — it is a drawing decision about a rect nothing
 * hit-tests. Measuring to decide where a clickable thing goes is the
 * banned case (ui_kit.h), and no longer happens anywhere. */
static void draw_tooltip(SDL_Renderer *renderer, float cx, float above_y,
                         UiRect screen, const char *line1, const char *line2)
{
    SDL_Color   fg   = { 230, 215, 180, 255 };
    SDL_Color   warn = { 235, 150, 130, 255 };
    int         w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    int         have1 = font_measure_text(FONT_SMALL, line1, &w1, &h1);
    int         have2 = line2 && line2[0]
                        ? font_measure_text(FONT_SMALL, line2, &w2, &h2) : 0;
    float       pad = 8.0f;
    float       tw  = (float)(w1 > w2 ? w1 : w2) + pad * 2.0f;
    float       th  = (float)(h1 + (have2 ? h2 + 2 : 0)) + 6.0f;
    UiRect      tip;

    if (!have1) { tw = 120.0f; th = 20.0f; }

    /* Size measured here, position decided by the kit — which clamps it
     * into the window. The leftmost slot sits 20px from the edge, so a
     * tip merely centred on it hangs off the screen. */
    tip = ui_tooltip_rect(cx, above_y, tw, th, screen);

    fill(renderer, tip, 50, 42, 28, 240);
    outline(renderer, tip, 140, 120, 70, 255);

    font_draw_text(renderer, FONT_SMALL, line1,
                   (int)(tip.x + pad), (int)(tip.y + 3.0f), fg);
    if (have2)
        font_draw_text(renderer, FONT_SMALL, line2,
                       (int)(tip.x + pad), (int)(tip.y + 3.0f + (float)h1 + 2.0f),
                       warn);
}

/* The building behind a slot widget, for its swatch and footprint
 * dots. The id carries the type, so this is a lookup, not a guess. */
static const BuildingDef *def_of(const UiWidget *w)
{
    uint16_t t = ui_id_value(w->id);
    if (t >= BUILDING_TYPE_COUNT) return NULL;
    return &BUILDING_DEFS[t];
}

/* ---- ui_draw ------------------------------------------- */
void ui_draw(SDL_Renderer *renderer, const UiList *list,
             const HudView *view, int mouse_x, int mouse_y)
{
    const UiWidget *hovered_slot = NULL;
    int             i;

    if (list->count == 0) return;

    /* Widget 0 is the bar itself. */
    {
        UiRect bar = list->items[0].rect;
        fill(renderer, bar, 30, 25, 20, 220);
        SDL_SetRenderDrawColor(renderer, 90, 75, 55, 255);
        SDL_RenderLine(renderer, bar.x, bar.y, bar.x + bar.w, bar.y);
    }

    for (i = 1; i < list->count; i++) {
        const UiWidget *w     = &list->items[i];
        int             group = ui_id_group(w->id);
        int             hovr  = ui_point_in(w->rect, (float)mouse_x,
                                            (float)mouse_y);
        int             sel   = (w->flags & UI_W_SELECTED) != 0;
        int             muted = (w->flags & UI_W_MUTED) != 0;

        /* ---- category tabs ---------------------------- */
        if (group == UI_GROUP_CATEGORY) {
            SDL_Color label = sel ? (SDL_Color){ 245, 225, 165, 255 }
                                  : (SDL_Color){ 165, 150, 115, 255 };

            fill(renderer, w->rect, sel ? 58 : 34, sel ? 48 : 28,
                 sel ? 30 : 20, 255);
            outline(renderer, w->rect, sel ? 200 : (hovr ? 140 : 70),
                    sel ? 175 : (hovr ? 120 : 60),
                    sel ? 105 : (hovr ? 75 : 40), 255);

            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 10.0f),
                           (int)(w->rect.y + 6.0f), label);
            continue;
        }

        /* ---- building slots --------------------------- */
        if (group == UI_GROUP_BUILDING) {
            const BuildingDef *def = def_of(w);
            UiRect             swatch;

            if (hovr) hovered_slot = w;

            fill(renderer, w->rect, (hovr || sel) ? 60 : 40,
                 (hovr || sel) ? 50 : 33, (hovr || sel) ? 35 : 22, 255);

            swatch = w->rect;
            swatch.x += 8.0f;  swatch.y += 8.0f;
            swatch.w -= 16.0f; swatch.h -= 24.0f;

            if (def) {
                /* Greyed, not hidden: an unaffordable building keeps
                 * its slot and its place, dimmed. Its swatch is drawn
                 * at a third intensity rather than in a flat grey, so
                 * the building is still recognisable by colour. */
                if (muted)
                    fill(renderer, swatch, (Uint8)(def->col_r / 3),
                         (Uint8)(def->col_g / 3), (Uint8)(def->col_b / 3), 255);
                else
                    fill(renderer, swatch, def->col_r, def->col_g,
                         def->col_b, 255);
            }

            if (sel)       outline(renderer, w->rect, 255, 210, 50, 255);
            else if (hovr) outline(renderer, w->rect, 160, 140, 90, 255);
            else if (muted)outline(renderer, w->rect, 55, 48, 34, 255);
            else           outline(renderer, w->rect, 70, 60, 40, 255);

            /* Footprint dots: 2x2 gets four, 1x1 gets one. */
            if (def) {
                float ax = w->rect.x + w->rect.w - 4.0f - (float)def->tile_w * 5.0f;
                float ay = w->rect.y + w->rect.h - 4.0f - (float)def->tile_h * 5.0f;
                int   dr, dc;
                SDL_SetRenderDrawColor(renderer, 200, 180, 120, muted ? 90 : 200);
                for (dr = 0; dr < def->tile_h; dr++)
                    for (dc = 0; dc < def->tile_w; dc++) {
                        SDL_FRect dot = { ax + (float)dc * 5.0f,
                                          ay + (float)dr * 5.0f, 3.0f, 3.0f };
                        SDL_RenderFillRect(renderer, &dot);
                    }
            }
            continue;
        }

        /* ---- the overflow marker ---------------------- */
        if (w->flags & UI_W_HEADER) {
            SDL_Color dim = { 150, 135, 95, 255 };
            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 16.0f),
                           (int)(w->rect.y + 24.0f), dim);
            continue;
        }

        /* ---- the right-hand cluster ------------------- */
        switch ((UiAction)ui_id_value(w->id)) {
        case UI_ACTION_WORLD: {
            SDL_Color a = { 120, 180, 110, 255 }, b = { 80, 130, 75, 255 };
            fill(renderer, w->rect, (hovr || sel) ? 40 : 25,
                 (hovr || sel) ? 62 : 38, (hovr || sel) ? 88 : 55, 255);
            outline(renderer, w->rect, sel ? 255 : (hovr ? 120 : 60),
                    sel ? 210 : (hovr ? 170 : 85),
                    sel ? 50 : (hovr ? 220 : 115), 255);
            render_draw_diamond(renderer, w->rect.x + 10.0f,
                                w->rect.y + 16.0f, 0.30f, a, b);
            render_draw_diamond(renderer, w->rect.x + 30.0f,
                                w->rect.y + 28.0f, 0.30f, a, b);
            render_draw_diamond(renderer, w->rect.x + 14.0f,
                                w->rect.y + 38.0f, 0.30f, a, b);
            break;
        }
        case UI_ACTION_DEMOLISH:
            fill(renderer, w->rect, (hovr || sel) ? 90 : 45,
                 (hovr || sel) ? 30 : 22, (hovr || sel) ? 28 : 18, 255);
            outline(renderer, w->rect, sel ? 255 : (hovr ? 200 : 90),
                    sel ? 90 : (hovr ? 110 : 50),
                    sel ? 70 : (hovr ? 90 : 42), 255);
            SDL_SetRenderDrawColor(renderer, 230, 90, 75, 255);
            SDL_RenderLine(renderer, w->rect.x + 16.0f, w->rect.y + 16.0f,
                           w->rect.x + w->rect.w - 16.0f,
                           w->rect.y + w->rect.h - 16.0f);
            SDL_RenderLine(renderer, w->rect.x + w->rect.w - 16.0f,
                           w->rect.y + 16.0f, w->rect.x + 16.0f,
                           w->rect.y + w->rect.h - 16.0f);
            break;

        case UI_ACTION_MENU: {
            UiRect out_r = w->rect, in_r = w->rect;
            float  ccx = w->rect.x + w->rect.w * 0.5f;
            float  ccy = w->rect.y + w->rect.h * 0.5f;
            SDL_FRect teeth[4];

            fill(renderer, w->rect, (hovr || sel) ? 70 : 40,
                 (hovr || sel) ? 58 : 33, (hovr || sel) ? 38 : 22, 255);
            outline(renderer, w->rect, sel ? 255 : (hovr ? 160 : 70),
                    sel ? 210 : (hovr ? 140 : 60),
                    sel ? 50 : (hovr ? 90 : 40), 255);

            out_r.x += 10.0f; out_r.y += 10.0f;
            out_r.w -= 20.0f; out_r.h -= 20.0f;
            outline(renderer, out_r, 190, 170, 110, 255);

            in_r.x += 22.0f; in_r.y += 22.0f;
            in_r.w -= 44.0f; in_r.h -= 44.0f;
            fill(renderer, in_r, 190, 170, 110, 255);

            teeth[0] = (SDL_FRect){ ccx - 4.0f, w->rect.y + 4.0f, 8.0f, 8.0f };
            teeth[1] = (SDL_FRect){ ccx - 4.0f,
                                    w->rect.y + w->rect.h - 12.0f, 8.0f, 8.0f };
            teeth[2] = (SDL_FRect){ w->rect.x + 4.0f, ccy - 4.0f, 8.0f, 8.0f };
            teeth[3] = (SDL_FRect){ w->rect.x + w->rect.w - 12.0f,
                                    ccy - 4.0f, 8.0f, 8.0f };
            SDL_SetRenderDrawColor(renderer, 190, 170, 110, 255);
            SDL_RenderFillRects(renderer, teeth, 4);
            break;
        }
        default:
            break;
        }
    }

    /* The hovered slot's tooltip goes last, so nothing paints over it.
     * It says what the building is and, when the slot is greyed, why —
     * in the sim's own rejection vocabulary. */
    if (hovered_slot) {
        const BuildingDef *def = def_of(hovered_slot);
        UiRect             bar = list->items[0].rect;
        UiRect             screen;
        char               line1[64];
        const char        *line2 = NULL;

        /* The bar spans the window, so it tells us how big the window
         * is without ui_draw needing screen dimensions passed in. */
        screen.x = 0.0f;
        screen.y = 0.0f;
        screen.w = bar.w;
        screen.h = bar.y + bar.h;

        if (def && def->cost[RES_GOLD] > 0)
            SDL_snprintf(line1, sizeof(line1), "%s  -  %d Gold",
                         hovered_slot->label, def->cost[RES_GOLD]);
        else
            SDL_snprintf(line1, sizeof(line1), "%s", hovered_slot->label);

        if (hovered_slot->reason != (uint8_t)REJ_OK)
            line2 = ui_reject_text((RejectReason)hovered_slot->reason);

        /* Anchored above the BAR, not above the slot: the tab strip sits
         * between them, and a tip anchored to the slot drew over it. */
        draw_tooltip(renderer,
                     hovered_slot->rect.x + hovered_slot->rect.w * 0.5f,
                     bar.y, screen, line1, line2);
    }
    (void)view;
}

/* Menu overlay */

/* One menu button rectangle. Buttons are stacked vertically, centred on
 * the screen. Still geometry-in-the-drawer, unlike the HUD: the menu is
 * folded into the unified confirm layer in UI_PLAN Phase 6, and porting
 * it twice would be work done to be thrown away. */
static SDL_FRect menu_btn_rect(int screen_w, int screen_h, int i)
{
    float px = (float)((screen_w - MENU_W) / 2);
    float py = (float)((screen_h - MENU_H) / 2);
    SDL_FRect r;
    r.x = px + (float)MENU_BTN_MARGIN;
    r.w = (float)(MENU_W - MENU_BTN_MARGIN * 2);
    r.h = (float)MENU_BTN_H;
    r.y = py + 36.0f + (float)i * ((float)MENU_BTN_H + (float)MENU_BTN_PAD);
    return r;
}
 
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
 
