/*  world_ui.c  --  The archipelago overview overlay  */

#include "world_ui.h"
#include "render.h"
#include "fonts.h"
#include "map.h"
#include <SDL3/SDL.h>

/* Fixed node positions as a fraction of the screen, so the layout
 * scales with resolution without any procedural placement. Home sits
 * left-of-centre; the rest fan out around it. */
static const struct { float fx, fy; } NODE_POS[MAX_ISLANDS] = {
    { 0.28f, 0.50f },   /* Home     */
    { 0.55f, 0.30f },   /* Highland */
    { 0.72f, 0.62f },   /* Woodland */
    { 0.44f, 0.76f },   /* Atoll    */
};

/* Tint per profile, so an island reads as "the wooded one" at a
 * glance rather than needing its label. Mirrors the tile colours
 * those terrains actually use in the map view. */
static SDL_Color profile_colour(MapProfile p, int settled)
{
    SDL_Color c;
    switch (p) {
    case PROFILE_HIGHLAND: c.r = 120; c.g = 175; c.b =  75; break;
    case PROFILE_WOODLAND: c.r =  48; c.g = 105; c.b =  50; break;
    case PROFILE_ATOLL:    c.r = 214; c.g = 198; c.b = 128; break;
    case PROFILE_TEMPERATE:
    default:               c.r = 106; c.g = 168; c.b =  79; break;
    }
    c.a = 255;
    /* An uncolonised island is drawn washed-out: visible and
     * inspectable, but obviously not yours yet. */
    if (!settled) {
        c.r = (unsigned char)(c.r / 2 + 30);
        c.g = (unsigned char)(c.g / 2 + 30);
        c.b = (unsigned char)(c.b / 2 + 30);
    }
    return c;
}

/* Top-left corner of island `i`'s diamond — render_draw_diamond()
 * takes a bounding-box corner, not a centre. */
static void node_origin(int screen_w, int screen_h, int i,
                        float *out_x, float *out_y)
{
    float w = (float)TILE_W * WORLD_NODE_ZOOM;
    float h = (float)TILE_H * WORLD_NODE_ZOOM;
    *out_x = NODE_POS[i].fx * (float)screen_w - w / 2.0f;
    *out_y = NODE_POS[i].fy * (float)screen_h - h / 2.0f;
}

static SDL_FRect node_bounds(int screen_w, int screen_h, int i)
{
    SDL_FRect r;
    node_origin(screen_w, screen_h, i, &r.x, &r.y);
    r.w = (float)TILE_W * WORLD_NODE_ZOOM;
    r.h = (float)TILE_H * WORLD_NODE_ZOOM;
    return r;
}

static SDL_FRect close_btn_rect(int screen_w, int screen_h)
{
    SDL_FRect r;
    r.w = 120.0f;
    r.h = 36.0f;
    r.x = (float)screen_w / 2.0f - r.w / 2.0f;
    r.y = (float)screen_h - 90.0f;
    return r;
}

static int point_in(SDL_FRect r, int x, int y)
{
    return (float)x >= r.x && (float)x < r.x + r.w &&
           (float)y >= r.y && (float)y < r.y + r.h;
}

void world_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const Island islands[], int island_count, int current,
                   int mouse_x, int mouse_y)
{
    SDL_FRect sea = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
    int i;

    /* Opaque sea, not the usual translucent dim: this overlay
     * replaces the world rather than annotating it. */
    SDL_SetRenderDrawColor(renderer, 18, 52, 88, 255);
    SDL_RenderFillRect(renderer, &sea);

    {
        SDL_Color title_col = { 210, 225, 240, 255 };
        font_draw_text(renderer, FONT_NORMAL, "Archipelago",
                       40, WORLD_TITLE_Y, title_col);
        font_draw_text(renderer, FONT_SMALL,
                       "Click an island to view it.  Right-click to close.",
                       40, WORLD_TITLE_Y + 22, title_col);
    }

    for (i = 0; i < island_count; i++) {
        const Island *isl = &islands[i];
        SDL_FRect     nb  = node_bounds(screen_w, screen_h, i);
        int           hov = point_in(nb, mouse_x, mouse_y);
        SDL_Color     top = profile_colour(isl->profile, isl->settled);
        SDL_Color     bot;
        char          buf[64];
        SDL_Color     label = { 225, 235, 245, 255 };

        bot.r = (unsigned char)(top.r * 0.65f);
        bot.g = (unsigned char)(top.g * 0.65f);
        bot.b = (unsigned char)(top.b * 0.65f);
        bot.a = 255;

        render_draw_diamond(renderer, nb.x, nb.y, WORLD_NODE_ZOOM, top, bot);

        /* Gold ring on the island you're viewing, a lighter one on
         * hover — the same selected/hovered language the HUD uses. */
        if (i == current)
            render_draw_diamond_outline(renderer, nb.x, nb.y,
                                        WORLD_NODE_ZOOM, 255, 210, 50, 255);
        else if (hov)
            render_draw_diamond_outline(renderer, nb.x, nb.y,
                                        WORLD_NODE_ZOOM, 200, 215, 235, 255);
        else
            render_draw_diamond_outline(renderer, nb.x, nb.y,
                                        WORLD_NODE_ZOOM, 90, 120, 150, 255);

        SDL_snprintf(buf, sizeof(buf), "%s", isl->name);
        font_draw_text(renderer, FONT_NORMAL, buf,
                       (int)(nb.x + 8.0f), (int)(nb.y + nb.h + 6.0f), label);

        if (!isl->settled) {
            SDL_Color dim = { 170, 185, 200, 255 };
            font_draw_text(renderer, FONT_SMALL, "Uncharted",
                           (int)(nb.x + 8.0f), (int)(nb.y + nb.h + 26.0f), dim);
        } else {
            SDL_snprintf(buf, sizeof(buf), "Pop %d",
                        pop_total(isl->pop_data, isl->building_count));
            font_draw_text(renderer, FONT_SMALL, buf,
                           (int)(nb.x + 8.0f), (int)(nb.y + nb.h + 26.0f), label);
        }
    }

    {
        SDL_FRect cr   = close_btn_rect(screen_w, screen_h);
        int       hovr = point_in(cr, mouse_x, mouse_y);
        SDL_Color lbl  = { 220, 200, 160, 255 };

        SDL_SetRenderDrawColor(renderer,
            hovr ? 70 : 45, hovr ? 58 : 38, hovr ? 38 : 26, 255);
        SDL_RenderFillRect(renderer, &cr);
        SDL_SetRenderDrawColor(renderer,
            hovr ? 200 : 110, hovr ? 175 : 92, hovr ? 100 : 58, 255);
        SDL_RenderRect(renderer, &cr);
        font_draw_text(renderer, FONT_NORMAL, "Close",
                       (int)(cr.x + 34.0f), (int)(cr.y + 8.0f), lbl);
    }
}

WorldHit world_ui_hit_test(int screen_w, int screen_h, int island_count,
                           int mouse_x, int mouse_y, int *out_island)
{
    int i;

    if (point_in(close_btn_rect(screen_w, screen_h), mouse_x, mouse_y))
        return WORLD_HIT_CLOSE;

    for (i = 0; i < island_count; i++) {
        if (point_in(node_bounds(screen_w, screen_h, i), mouse_x, mouse_y)) {
            if (out_island) *out_island = i;
            return WORLD_HIT_ISLAND;
        }
    }

    return WORLD_HIT_NONE;
}
