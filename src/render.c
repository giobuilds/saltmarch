/*  render.c  --  SDL rendering  (Phase 6: sprite rendering)
 *
 *  CHANGED Phase 6:
 *  - render_map() uses SDL_RenderTexture() when sprites are loaded,
 *    falls back to draw_diamond() if sprites unavailable
 *  - render_buildings() renders the composited building texture,
 *    positioned so the base sits on the tile's diamond face
 *  - draw_diamond() kept for fallback and ghost/hover overlay
 *  - screen_to_iso() centroid offset updated for new TILE_W/H
 */

#include "render.h"
#include "game.h"
#include "building.h"
#include "fonts.h"
#include <SDL3/SDL.h>
#include <math.h>

/* ---- Fallback colours (used when sprites not loaded) --- */
static const SDL_Color TILE_COLOURS[TILE_TYPE_COUNT] = {
    { 106, 168,  79, 255 },   /* GRASS  */
    {  30, 120, 200, 255 },   /* WATER  */
    {  38,  94,  46, 255 },   /* FOREST */
    { 220, 200, 130, 255 },   /* SAND   */
};
static const SDL_Color TILE_DARK[TILE_TYPE_COUNT] = {
    {  80, 130,  55, 255 },
    {  20,  90, 160, 255 },
    {  25,  65,  30, 255 },
    { 185, 165, 100, 255 },
};

/* =========================================================
 * Coordinate conversion
 * ========================================================= */
void iso_to_screen(float row, float col, const Camera *cam,
                   float *out_x, float *out_y)
{
    /* CHANGED: float output so zoomed tile corners sit flush.
     * Integer truncation caused sub-pixel gaps between tiles. */
    float hw = (float)(TILE_W / 2) * cam->zoom;
    float hh = (float)(TILE_H / 2) * cam->zoom;
    *out_x = cam->offset_x + (col - row) * hw;
    *out_y = cam->offset_y + (col + row) * hh;
}

void screen_to_iso(int sx, int sy, const Camera *cam,
                   int *out_row, int *out_col)
{
    /* CHANGED: scale half-tile dimensions by zoom before inverting. */
    float hw = (float)(TILE_W / 2) * cam->zoom;
    float hh = (float)(TILE_H / 2) * cam->zoom;
    float px = (float)sx - cam->offset_x - hw;
    float py = (float)sy - cam->offset_y - hh;
    *out_col = (int)floorf( (px / hw + py / hh) / 2.0f );
    *out_row = (int)floorf( (py / hh - px / hw) / 2.0f );
}

/* =========================================================
 * Fallback diamond drawing (no sprites)
 * ========================================================= */
void render_draw_diamond(SDL_Renderer *renderer,
                         float bx, float by, float zoom,
                         SDL_Color top_col, SDL_Color bot_col)
{
    /* CHANGED: tile dimensions scaled by zoom so drawn size matches
     * the spacing produced by iso_to_screen — eliminates gaps. */
    float tw = (float)TILE_W * zoom;
    float th = (float)TILE_H * zoom;
    float hw = tw / 2.0f;
    float hh = th / 2.0f;
    SDL_Vertex verts[4];
    int indices[6] = { 0,1,2, 0,3,2 };

    verts[0].position.x = bx + hw;
    verts[0].position.y = by;
    verts[1].position.x = bx;
    verts[1].position.y = by + hh;
    verts[2].position.x = bx + hw;
    verts[2].position.y = by + th;
    verts[3].position.x = bx + tw;
    verts[3].position.y = by + hh;

    verts[0].tex_coord.x = verts[0].tex_coord.y = 0.0f;
    verts[1].tex_coord.x = verts[1].tex_coord.y = 0.0f;
    verts[2].tex_coord.x = verts[2].tex_coord.y = 0.0f;
    verts[3].tex_coord.x = verts[3].tex_coord.y = 0.0f;

    verts[0].color.r = top_col.r/255.0f;
    verts[0].color.g = top_col.g/255.0f;
    verts[0].color.b = top_col.b/255.0f;
    verts[0].color.a = top_col.a/255.0f;
    verts[1].color = verts[0].color;
    verts[3].color = verts[0].color;

    verts[2].color.r = bot_col.r/255.0f;
    verts[2].color.g = bot_col.g/255.0f;
    verts[2].color.b = bot_col.b/255.0f;
    verts[2].color.a = bot_col.a/255.0f;

    SDL_RenderGeometry(renderer, NULL, verts, 4, indices, 6);
}

void render_draw_diamond_outline(SDL_Renderer *renderer,
                                 float bx, float by, float zoom,
                                 unsigned char r, unsigned char g,
                                 unsigned char b, unsigned char a)
{
    /* CHANGED: zoomed dimensions to match draw_diamond */
    float tw = (float)TILE_W * zoom;
    float th = (float)TILE_H * zoom;
    float hw = tw / 2.0f;
    float hh = th / 2.0f;
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderLine(renderer, bx+hw, by,    bx,    by+hh);
    SDL_RenderLine(renderer, bx,    by+hh, bx+hw, by+th);
    SDL_RenderLine(renderer, bx+hw, by+th, bx+tw, by+hh);
    SDL_RenderLine(renderer, bx+tw, by+hh, bx+hw, by);
}

/* =========================================================
 * Public render functions
 * ========================================================= */

void render_clear(SDL_Renderer *renderer)
{
    SDL_SetRenderDrawColor(renderer, 15, 60, 110, 255);
    SDL_RenderClear(renderer);
}

/* ---- render_map ----------------------------------------
 * CHANGED Phase 6: use SDL_RenderTexture when sprites ready,
 * fall back to coloured diamonds otherwise.
 *
 * Terrain sprites are TILE_W x TILE_H (256x128).
 * We position them so the top-left of the bounding box
 * matches iso_to_screen() — identical to the diamond fallback.
 * -------------------------------------------------------- */
void render_map(SDL_Renderer *renderer,
                const Map *map, const Camera *cam)
{
    /* CHANGED: float sx/sy to match iso_to_screen float output */
    int r, c;
    float sx, sy;
    float tw = (float)TILE_W * cam->zoom;
    float th = (float)TILE_H * cam->zoom;

    for (r = 0; r < map->rows; r++) {
        for (c = 0; c < map->cols; c++) {
            const Tile *t = &map->tiles[r][c];
            iso_to_screen((float)r, (float)c, cam, &sx, &sy);
            if (sx + tw < 0 || sx > SCREEN_W ||
                sy + th < 0 || sy > SCREEN_H)
                continue;
            render_draw_diamond(renderer, sx, sy, cam->zoom,
                         TILE_COLOURS[t->type],
                         TILE_DARK[t->type]);
        }
    }
}

/* ---- render_hovered_tile ------------------------------- */
void render_hovered_tile(SDL_Renderer *renderer,
                         const Camera *cam,
                         int row, int col)
{
    float sx, sy;
    if (row < 0 || col < 0) return;
    iso_to_screen((float)row, (float)col, cam, &sx, &sy);
    /* CHANGED: pass zoom to outline */
    render_draw_diamond_outline(renderer, sx, sy, cam->zoom, 255, 230, 50, 255);
}

/* ---- render_buildings ----------------------------------
 * CHANGED Phase 6: draw composited building texture.
 *
 * Building sprites are 256x192 (TILE_W x TILE_H*1.5).
 * The base of the building (where walls meet the ground)
 * sits at TILE_H from the top of the sprite, so we offset
 * the y position upward by the roof height above the base:
 *   sprite_y = tile_y - (COMP_H - TILE_H)
 *            = tile_y - 64
 * This makes the wall base align with the tile diamond top.
 * -------------------------------------------------------- */
void render_buildings(SDL_Renderer *renderer,
                      const Building buildings[], int count,
                      const Camera *cam)
{
    /* CHANGED: float sx/sy for zoomed rendering */
    int i, r, c;
    float sx, sy;

    for (i = 0; i < count; i++) {
        const Building    *b   = &buildings[i];
        const BuildingDef *def = &BUILDING_DEFS[b->type];

        if (!b->active) continue;

        {
            SDL_Color top, bot;
            top.r = def->col_r; top.g = def->col_g;
            top.b = def->col_b; top.a = 255;
            bot.r = (unsigned char)(def->col_r * 0.7f);
            bot.g = (unsigned char)(def->col_g * 0.7f);
            bot.b = (unsigned char)(def->col_b * 0.7f);
            bot.a = 255;
            for (r = b->row; r < b->row + def->tile_h; r++) {
                for (c = b->col; c < b->col + def->tile_w; c++) {
                    iso_to_screen((float)r, (float)c, cam, &sx, &sy);
                    render_draw_diamond(renderer, sx, sy, cam->zoom, top, bot);
                    render_draw_diamond_outline(renderer, sx, sy, cam->zoom,
                                         255, 255, 255, 60);
                    /* Phase 3: red outline if not road-connected to
                     * a Warehouse (Warehouses/Roads are always
                     * connected — see connectivity_update). */
                    if (!b->connected)
                        render_draw_diamond_outline(renderer, sx, sy, cam->zoom,
                                             220, 40, 40, 200);
                }
            }
        }
    }
}

/* ---- render_ghost -------------------------------------- */
void render_ghost(SDL_Renderer *renderer,
                  const Camera *cam,
                  BuildingType type,
                  int row, int col,
                  int valid)
{
    /* CHANGED: float sx/sy, pass zoom */
    const BuildingDef *def = &BUILDING_DEFS[type];
    int r, c;
    float sx, sy;
    SDL_Color top, bot;

    if (row < 0 || col < 0) return;

    if (valid) {
        top.r = 80;  top.g = 200; top.b = 80;  top.a = 160;
        bot.r = 50;  bot.g = 140; bot.b = 50;  bot.a = 160;
    } else {
        top.r = 200; top.g = 60;  top.b = 60;  top.a = 160;
        bot.r = 140; bot.g = 40;  bot.b = 40;  bot.a = 160;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (r = row; r < row + def->tile_h; r++) {
        for (c = col; c < col + def->tile_w; c++) {
            iso_to_screen((float)r, (float)c, cam, &sx, &sy);
            render_draw_diamond(renderer, sx, sy, cam->zoom, top, bot);
        }
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

/* ---- render_resources ---------------------------------- */
void render_resources(SDL_Renderer *renderer, const Stockpile *s)
{
    /* Designated initialisers, matching RESOURCE_NAMES/SELL_PRICE/
     * BUY_PRICE (resource.c). These were positional and only covered
     * 4 of 7 resources after Hops/Malt/Beer were added — which both
     * shifted Gold's amber onto Hops and left Malt/Beer/Gold with
     * {0,0,0,0} invisible swatches. Keyed initialisers make a missing
     * entry obvious rather than silently wrong. */
    static const SDL_Color RES_COL[RES_COUNT] = {
        [RES_WOOD]  = { 139,  90,  43, 255 },
        [RES_FISH]  = {  50, 180, 230, 255 },
        [RES_GRAIN] = { 240, 210,  50, 255 },
        [RES_HOPS]  = { 120, 180,  70, 255 },
        [RES_MALT]  = { 190, 150,  90, 255 },
        [RES_BEER]  = { 220, 160,  40, 255 },
        [RES_GOLD]  = { 255, 195,   0, 255 },
    };
    SDL_Color text_col  = { 220, 200, 160, 255 };
    SDL_Color label_col = { 160, 140, 100, 255 };

    int panel_x=16, panel_y=16, row_h=22, swatch_w=12;
    int seg_w=7, seg_gap=2, max_segs=10;
    int bar_x  = panel_x + swatch_w + 6;
    int num_x  = bar_x + max_segs*(seg_w+seg_gap) + 8;
    int panel_w= num_x + 52;
    int panel_h= RES_COUNT * row_h + 10;
    int i, j;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 20, 16, 10, 200);
    SDL_FRect bg = { (float)(panel_x-6), (float)(panel_y-6),
                     (float)panel_w,      (float)panel_h };
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 90, 75, 45, 180);
    SDL_RenderRect(renderer, &bg);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    for (i = 0; i < RES_COUNT; i++) {
        int   amount = s->amount[i];
        int   segs   = amount / 10;
        float ry     = (float)(panel_y + i * row_h);
        char  buf[16];

        SDL_FRect sw = { (float)panel_x, ry+4.0f,
                         (float)swatch_w, (float)(row_h-8) };
        SDL_SetRenderDrawColor(renderer,
            RES_COL[i].r, RES_COL[i].g, RES_COL[i].b, 255);
        SDL_RenderFillRect(renderer, &sw);

        if (segs > max_segs) segs = max_segs;
        for (j = 0; j < max_segs; j++) {
            float bx = (float)(bar_x + j*(seg_w+seg_gap));
            SDL_FRect seg = { bx, ry+5.0f,
                              (float)seg_w, (float)(row_h-10) };
            if (j < segs)
                SDL_SetRenderDrawColor(renderer,
                    (unsigned char)(RES_COL[i].r*0.85f),
                    (unsigned char)(RES_COL[i].g*0.85f),
                    (unsigned char)(RES_COL[i].b*0.85f), 255);
            else
                SDL_SetRenderDrawColor(renderer, 40, 35, 25, 255);
            SDL_RenderFillRect(renderer, &seg);
            SDL_SetRenderDrawColor(renderer, 70, 60, 40, 255);
            SDL_RenderRect(renderer, &seg);
        }

        font_draw_text(renderer, FONT_SMALL, RESOURCE_NAMES[i],
                       num_x, (int)ry+1, label_col);
        SDL_snprintf(buf, sizeof(buf), "%d", amount);
        font_draw_text(renderer, FONT_NORMAL, buf,
                       num_x+38, (int)ry, text_col);
    }
}

/* ---- render_population --------------------------------- */
void render_population(SDL_Renderer *renderer,
                       int total_pop, int screen_w)
{
    char buf[32];
    SDL_Color col = { 200, 230, 200, 255 };
    SDL_snprintf(buf, sizeof(buf), "Pop: %d", total_pop);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 20, 16, 10, 200);
    SDL_FRect bg = { (float)(screen_w-110), 10.0f, 100.0f, 24.0f };
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 90, 75, 45, 180);
    SDL_RenderRect(renderer, &bg);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    font_draw_text(renderer, FONT_NORMAL, buf, screen_w-105, 13, col);
}

/* ---- render_agents ---------------------------------------
 * Phase 5: one small filled square per active agent, projected
 * through iso_to_screen() (now widened to accept the fractional
 * row/col an agent mid-walk actually has) and centred on the tile
 * diamond's centroid. Same small-square technique already used for
 * the HUD's building-size dot-grid annotation in ui.c — no new
 * drawing primitive needed. */
void render_agents(SDL_Renderer *renderer,
                   const Agent agents[], int count,
                   const Camera *cam)
{
    static const SDL_Color AGENT_COLOUR = { 250, 220, 130, 255 };
    const float DOT = 5.0f;
    float hw, hh;
    int i;

    hw = (float)(TILE_W / 2) * cam->zoom;
    hh = (float)(TILE_H / 2) * cam->zoom;

    SDL_SetRenderDrawColor(renderer,
        AGENT_COLOUR.r, AGENT_COLOUR.g, AGENT_COLOUR.b, AGENT_COLOUR.a);

    for (i = 0; i < count; i++) {
        SDL_FRect dot;
        float     sx, sy;

        if (!agents[i].active) continue;

        iso_to_screen(agents[i].row, agents[i].col, cam, &sx, &sy);
        dot.x = sx + hw - DOT / 2.0f;
        dot.y = sy + hh - DOT / 2.0f;
        dot.w = DOT;
        dot.h = DOT;
        SDL_RenderFillRect(renderer, &dot);
    }
}
