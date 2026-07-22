/*  world_ui.c  --  The archipelago overview overlay  */

#include "world_ui.h"
#include "render.h"
#include "fonts.h"
#include "map.h"
#include "resource.h"
#include <SDL3/SDL.h>

/* Fixed node positions as a fraction of the screen, so the layout
 * scales with resolution without any procedural placement. Home sits
 * left-of-centre; the rest fan out around it. */
static const struct { float fx, fy; } NODE_POS[MAX_ISLANDS] = {
    { 0.28f, 0.50f },   /* Saltford  */
    { 0.55f, 0.30f },   /* Brinehold */
    { 0.72f, 0.62f },   /* Tidefast  */
    { 0.44f, 0.76f },   /* Marrowbay */
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

/* Where a ship's marker sits: on its island's node when docked, or
 * lerped along the lane between two nodes while at sea. Ships at the
 * same island are fanned out slightly so a fleet is countable. */
static void ship_marker_pos(int screen_w, int screen_h,
                            const Ship *sh, int idx, float *ox, float *oy)
{
    float ax, ay, bx, by, t;
    float half_w = (float)TILE_W * WORLD_NODE_ZOOM / 2.0f;
    float half_h = (float)TILE_H * WORLD_NODE_ZOOM / 2.0f;

    if (sh->at_island >= 0) {
        node_origin(screen_w, screen_h, sh->at_island, &ax, &ay);
        *ox = ax + half_w + (float)((idx % 4) - 1) * 12.0f;
        *oy = ay + half_h + 26.0f;
        return;
    }

    node_origin(screen_w, screen_h, sh->from_island, &ax, &ay);
    node_origin(screen_w, screen_h, sh->to_island,   &bx, &by);
    t   = sh->progress;
    *ox = (ax + half_w) + ((bx + half_w) - (ax + half_w)) * t;
    *oy = (ay + half_h) + ((by + half_h) - (ay + half_h)) * t;
}

static SDL_FRect ship_marker_rect(int screen_w, int screen_h,
                                  const Ship *sh, int idx)
{
    SDL_FRect r;
    float x, y;
    ship_marker_pos(screen_w, screen_h, sh, idx, &x, &y);
    r.w = 14.0f; r.h = 14.0f;
    r.x = x - r.w / 2.0f;
    r.y = y - r.h / 2.0f;
    return r;
}

/* Selected-ship panel, right-hand side. */
static SDL_FRect panel_rect(int screen_w, int screen_h)
{
    SDL_FRect r;
    r.w = (float)WORLD_PANEL_W;
    r.h = (float)(RES_COUNT * WORLD_ROW_H + 210);
    r.x = (float)screen_w - r.w - 40.0f;
    r.y = 90.0f;
    (void)screen_h;
    return r;
}

/* Load (i=0) / Unload (i=1) button for resource row `res`. */
static SDL_FRect cargo_btn_rect(int screen_w, int screen_h, int res, int i)
{
    SDL_FRect p = panel_rect(screen_w, screen_h);
    SDL_FRect r;
    r.w = 52.0f; r.h = 20.0f;
    r.x = p.x + p.w - 10.0f - (2.0f - (float)i) * (r.w + 6.0f);
    r.y = p.y + 56.0f + (float)res * (float)WORLD_ROW_H;
    return r;
}

/* Route controls sit under the cargo manifest: outbound good,
 * return good, then the on/off toggle. */
static SDL_FRect route_btn_rect(int screen_w, int screen_h, int which)
{
    SDL_FRect p = panel_rect(screen_w, screen_h);
    SDL_FRect r;
    r.w = p.w - 20.0f; r.h = 24.0f;
    r.x = p.x + 10.0f;
    r.y = p.y + 62.0f + (float)RES_COUNT * (float)WORLD_ROW_H
        + (float)which * 28.0f;
    return r;
}

static SDL_FRect colonise_btn_rect(int screen_w, int screen_h)
{
    SDL_FRect p = panel_rect(screen_w, screen_h);
    SDL_FRect r;
    r.w = p.w - 20.0f; r.h = 30.0f;
    r.x = p.x + 10.0f;
    r.y = p.y + p.h - 40.0f;
    return r;
}

void world_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const Island islands[], int island_count, int current,
                   const Ship ships[], int ship_count, int selected_ship,
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

    /* Sea lanes for voyages in progress, drawn under the markers. */
    {
        int si;
        SDL_SetRenderDrawColor(renderer, 70, 110, 150, 255);
        for (si = 0; si < ship_count; si++) {
            float ax, ay, bx, by;
            float hw = (float)TILE_W * WORLD_NODE_ZOOM / 2.0f;
            float hh = (float)TILE_H * WORLD_NODE_ZOOM / 2.0f;
            if (!ships[si].active) continue;
            /* Draw the lane for a voyage in progress, and also for an
             * idle ship's standing route, so an established trade
             * link stays visible between runs. */
            if (ships[si].at_island >= 0 && !ships[si].route_active) continue;
            if (ships[si].at_island >= 0) {
                node_origin(screen_w, screen_h, ships[si].route_a, &ax, &ay);
                node_origin(screen_w, screen_h, ships[si].route_b, &bx, &by);
            } else {
                node_origin(screen_w, screen_h, ships[si].from_island, &ax, &ay);
                node_origin(screen_w, screen_h, ships[si].to_island,   &bx, &by);
            }
            SDL_RenderLine(renderer, ax + hw, ay + hh, bx + hw, by + hh);
        }
    }

    /* Ship markers — the same small filled square render_agents()
     * uses for people, since there is no sprite system. */
    {
        int si;
        for (si = 0; si < ship_count; si++) {
            SDL_FRect mr;
            if (!ships[si].active) continue;
            mr = ship_marker_rect(screen_w, screen_h, &ships[si], si);

            if (si == selected_ship)
                SDL_SetRenderDrawColor(renderer, 255, 225, 120, 255);
            else
                SDL_SetRenderDrawColor(renderer, 225, 235, 245, 255);
            SDL_RenderFillRect(renderer, &mr);
            SDL_SetRenderDrawColor(renderer, 30, 50, 70, 255);
            SDL_RenderRect(renderer, &mr);
        }
    }

    /* Selected-ship panel: cargo manifest and the actions that need
     * a ship to be docked somewhere. */
    if (selected_ship >= 0 && selected_ship < ship_count &&
        ships[selected_ship].active) {
        const Ship *sh = &ships[selected_ship];
        SDL_FRect   p  = panel_rect(screen_w, screen_h);
        SDL_Color   txt = { 225, 235, 245, 255 };
        SDL_Color   dim = { 150, 170, 190, 255 };
        char        buf[96];
        int         res;

        SDL_SetRenderDrawColor(renderer, 24, 42, 64, 235);
        SDL_RenderFillRect(renderer, &p);
        SDL_SetRenderDrawColor(renderer, 90, 130, 170, 255);
        SDL_RenderRect(renderer, &p);

        SDL_snprintf(buf, sizeof(buf), "Ship %d", selected_ship);
        font_draw_text(renderer, FONT_NORMAL, buf,
                       (int)(p.x + 10.0f), (int)(p.y + 8.0f), txt);

        if (sh->at_island >= 0)
            SDL_snprintf(buf, sizeof(buf), "Docked at %s",
                        islands[sh->at_island].name);
        else
            SDL_snprintf(buf, sizeof(buf), "At sea: %s -> %s (%d%%)",
                        islands[sh->from_island].name,
                        islands[sh->to_island].name,
                        (int)(sh->progress * 100.0f));
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(p.x + 10.0f), (int)(p.y + 30.0f), dim);

        for (res = 0; res < RES_COUNT; res++) {
            SDL_FRect lr = cargo_btn_rect(screen_w, screen_h, res, 0);
            SDL_FRect ur = cargo_btn_rect(screen_w, screen_h, res, 1);
            int docked = (sh->at_island == current);

            SDL_snprintf(buf, sizeof(buf), "%s %d",
                        RESOURCE_NAMES[res], sh->cargo[res]);
            font_draw_text(renderer, FONT_SMALL, buf,
                           (int)(p.x + 10.0f), (int)(lr.y + 3.0f),
                           sh->cargo[res] ? txt : dim);

            /* Loading only means anything at the island you are
             * looking at, since that is whose stockpile it moves. */
            SDL_SetRenderDrawColor(renderer, docked ? 45 : 30,
                                   docked ? 70 : 45, docked ? 95 : 60, 255);
            SDL_RenderFillRect(renderer, &lr);
            SDL_RenderFillRect(renderer, &ur);
            SDL_SetRenderDrawColor(renderer, docked ? 110 : 60,
                                   docked ? 150 : 80, docked ? 190 : 105, 255);
            SDL_RenderRect(renderer, &lr);
            SDL_RenderRect(renderer, &ur);
            font_draw_text(renderer, FONT_SMALL, "Load",
                           (int)(lr.x + 9.0f), (int)(lr.y + 3.0f),
                           docked ? txt : dim);
            font_draw_text(renderer, FONT_SMALL, "Unload",
                           (int)(ur.x + 4.0f), (int)(ur.y + 3.0f),
                           docked ? txt : dim);
        }

        /* --- Trade route ------------------------------------ */
        {
            static const char *NONE = "(nothing)";
            SDL_FRect ro = route_btn_rect(screen_w, screen_h, 0);
            SDL_FRect rb = route_btn_rect(screen_w, screen_h, 1);
            SDL_FRect rt = route_btn_rect(screen_w, screen_h, 2);
            /* A route repeats the ship's last voyage, so it needs one
             * to repeat: sail somewhere manually, then switch it on. */
            int can_route = (sh->from_island != sh->to_island);
            SDL_Color on  = { 190, 235, 200, 255 };

            SDL_snprintf(buf, sizeof(buf), "Carry out: %s",
                        sh->route_res_ab == RES_COUNT ? NONE
                                                      : RESOURCE_NAMES[sh->route_res_ab]);
            SDL_SetRenderDrawColor(renderer, 38, 58, 80, 255);
            SDL_RenderFillRect(renderer, &ro);
            SDL_SetRenderDrawColor(renderer, 95, 130, 165, 255);
            SDL_RenderRect(renderer, &ro);
            font_draw_text(renderer, FONT_SMALL, buf,
                           (int)(ro.x + 8.0f), (int)(ro.y + 4.0f), txt);

            SDL_snprintf(buf, sizeof(buf), "Bring back: %s",
                        sh->route_res_ba == RES_COUNT ? NONE
                                                      : RESOURCE_NAMES[sh->route_res_ba]);
            SDL_SetRenderDrawColor(renderer, 38, 58, 80, 255);
            SDL_RenderFillRect(renderer, &rb);
            SDL_SetRenderDrawColor(renderer, 95, 130, 165, 255);
            SDL_RenderRect(renderer, &rb);
            font_draw_text(renderer, FONT_SMALL, buf,
                           (int)(rb.x + 8.0f), (int)(rb.y + 4.0f), txt);

            if (sh->route_active)
                SDL_snprintf(buf, sizeof(buf), "Route RUNNING: %s <-> %s  (stop)",
                            islands[sh->route_a].name, islands[sh->route_b].name);
            else if (can_route)
                SDL_snprintf(buf, sizeof(buf), "Start route: %s <-> %s",
                            islands[sh->from_island].name, islands[sh->to_island].name);
            else
                SDL_snprintf(buf, sizeof(buf), "Sail somewhere to set a route");

            SDL_SetRenderDrawColor(renderer,
                sh->route_active ? 40 : (can_route ? 38 : 28),
                sh->route_active ? 95 : (can_route ? 58 : 40),
                sh->route_active ? 55 : (can_route ? 80 : 52), 255);
            SDL_RenderFillRect(renderer, &rt);
            SDL_SetRenderDrawColor(renderer,
                sh->route_active ? 120 : 95, sh->route_active ? 200 : 130,
                sh->route_active ? 130 : 165, 255);
            SDL_RenderRect(renderer, &rt);
            font_draw_text(renderer, FONT_SMALL, buf,
                           (int)(rt.x + 8.0f), (int)(rt.y + 4.0f),
                           sh->route_active ? on : (can_route ? txt : dim));
        }

        {
            SDL_FRect cb = colonise_btn_rect(screen_w, screen_h);
            int can = sh->at_island >= 0
                   && !islands[sh->at_island].settled
                   && sh->cargo[RES_GOLD] >= COLONY_FOUNDING_GOLD;
            SDL_Color lbl = can ? txt : dim;

            SDL_SetRenderDrawColor(renderer, can ? 40 : 28,
                                   can ? 95 : 48, can ? 55 : 34, 255);
            SDL_RenderFillRect(renderer, &cb);
            SDL_SetRenderDrawColor(renderer, can ? 110 : 60,
                                   can ? 190 : 90, can ? 120 : 66, 255);
            SDL_RenderRect(renderer, &cb);
            SDL_snprintf(buf, sizeof(buf), "Found Colony (%d Gold)",
                        COLONY_FOUNDING_GOLD);
            font_draw_text(renderer, FONT_SMALL, buf,
                           (int)(cb.x + 10.0f), (int)(cb.y + 8.0f), lbl);
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
                           const Ship ships[], int ship_count,
                           int selected_ship, int mouse_x, int mouse_y,
                           int *out_island, int *out_ship, ResourceType *out_res)
{
    int i;

    if (point_in(close_btn_rect(screen_w, screen_h), mouse_x, mouse_y))
        return WORLD_HIT_CLOSE;

    /* Panel widgets first: they overlay the sea, so a click there must
     * not fall through to whatever island node sits behind them. */
    if (selected_ship >= 0 && selected_ship < ship_count &&
        ships[selected_ship].active) {
        int res;
        if (point_in(colonise_btn_rect(screen_w, screen_h), mouse_x, mouse_y))
            return WORLD_HIT_COLONISE;
        if (point_in(route_btn_rect(screen_w, screen_h, 0), mouse_x, mouse_y))
            return WORLD_HIT_ROUTE_OUT;
        if (point_in(route_btn_rect(screen_w, screen_h, 1), mouse_x, mouse_y))
            return WORLD_HIT_ROUTE_BACK;
        if (point_in(route_btn_rect(screen_w, screen_h, 2), mouse_x, mouse_y))
            return WORLD_HIT_ROUTE_TOGGLE;
        for (res = 0; res < RES_COUNT; res++) {
            if (point_in(cargo_btn_rect(screen_w, screen_h, res, 0),
                        mouse_x, mouse_y)) {
                if (out_res) *out_res = (ResourceType)res;
                return WORLD_HIT_LOAD;
            }
            if (point_in(cargo_btn_rect(screen_w, screen_h, res, 1),
                        mouse_x, mouse_y)) {
                if (out_res) *out_res = (ResourceType)res;
                return WORLD_HIT_UNLOAD;
            }
        }
        if (point_in(panel_rect(screen_w, screen_h), mouse_x, mouse_y))
            return WORLD_HIT_NONE;   /* swallow clicks on panel chrome */
    }

    /* Ships before islands: a docked marker sits on its node. */
    for (i = 0; i < ship_count; i++) {
        if (!ships[i].active) continue;
        if (point_in(ship_marker_rect(screen_w, screen_h, &ships[i], i),
                    mouse_x, mouse_y)) {
            if (out_ship) *out_ship = i;
            return WORLD_HIT_SHIP;
        }
    }

    for (i = 0; i < island_count; i++) {
        if (point_in(node_bounds(screen_w, screen_h, i), mouse_x, mouse_y)) {
            if (out_island) *out_island = i;
            return WORLD_HIT_ISLAND;
        }
    }

    return WORLD_HIT_NONE;
}
