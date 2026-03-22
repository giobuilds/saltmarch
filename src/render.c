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
#include "sprite.h"    /* CHANGED Phase 6 */
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
void iso_to_screen(int row, int col, const Camera *cam,
                   int *out_x, int *out_y)
{
    *out_x = (int)cam->offset_x + (col - row) * (TILE_W / 2);
    *out_y = (int)cam->offset_y + (col + row) * (TILE_H / 2);
}

void screen_to_iso(int sx, int sy, const Camera *cam,
                   int *out_row, int *out_col)
{
    float hw = (float)(TILE_W / 2);
    float hh = (float)(TILE_H / 2);
    float px = (float)sx - cam->offset_x - hw;
    float py = (float)sy - cam->offset_y - hh;
    *out_col = (int)floorf( (px / hw + py / hh) / 2.0f );
    *out_row = (int)floorf( (py / hh - px / hw) / 2.0f );
}

/* =========================================================
 * Fallback diamond drawing (no sprites)
 * ========================================================= */
static void draw_diamond(SDL_Renderer *renderer,
                         int bx, int by,
                         SDL_Color top_col, SDL_Color bot_col)
{
    int half_w = TILE_W / 2;
    int half_h = TILE_H / 2;
    SDL_Vertex verts[4];
    int indices[6] = { 0,1,2, 0,3,2 };

    verts[0].position.x = (float)(bx + half_w);
    verts[0].position.y = (float)(by);
    verts[1].position.x = (float)(bx);
    verts[1].position.y = (float)(by + half_h);
    verts[2].position.x = (float)(bx + half_w);
    verts[2].position.y = (float)(by + TILE_H);
    verts[3].position.x = (float)(bx + TILE_W);
    verts[3].position.y = (float)(by + half_h);

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

static void draw_diamond_outline(SDL_Renderer *renderer,
                                 int bx, int by,
                                 unsigned char r, unsigned char g,
                                 unsigned char b, unsigned char a)
{
    int hw = TILE_W / 2;
    int hh = TILE_H / 2;
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderLine(renderer,
        (float)(bx+hw),    (float)(by),
        (float)(bx),       (float)(by+hh));
    SDL_RenderLine(renderer,
        (float)(bx),       (float)(by+hh),
        (float)(bx+hw),    (float)(by+TILE_H));
    SDL_RenderLine(renderer,
        (float)(bx+hw),    (float)(by+TILE_H),
        (float)(bx+TILE_W),(float)(by+hh));
    SDL_RenderLine(renderer,
        (float)(bx+TILE_W),(float)(by+hh),
        (float)(bx+hw),    (float)(by));
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
    int r, c, sx, sy;

    for (r = 0; r < map->rows; r++) {
        for (c = 0; c < map->cols; c++) {
            const Tile *t = &map->tiles[r][c];
            iso_to_screen(r, c, cam, &sx, &sy);

            if (sx + TILE_W < 0 || sx > SCREEN_W ||
                sy + TILE_H < 0 || sy > SCREEN_H)
                continue;

            if (g_sprites.ready) {
                /* CHANGED Phase 6: sprite path */
                SDL_Texture *tex = g_sprites.terrain[t->type];
                SDL_FRect dst = {
                    (float)sx, (float)sy,
                    (float)TILE_W, (float)TILE_H
                };
                SDL_RenderTexture(renderer, tex, NULL, &dst);
            } else {
                /* Fallback: coloured diamond */
                draw_diamond(renderer, sx, sy,
                             TILE_COLOURS[t->type],
                             TILE_DARK[t->type]);
            }
        }
    }
}

/* ---- render_hovered_tile ------------------------------- */
void render_hovered_tile(SDL_Renderer *renderer,
                         const Camera *cam,
                         int row, int col)
{
    int sx, sy;
    if (row < 0 || col < 0) return;
    iso_to_screen(row, col, cam, &sx, &sy);
    draw_diamond_outline(renderer, sx, sy, 255, 230, 50, 255);
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
    int i, r, c, sx, sy;

    for (i = 0; i < count; i++) {
        const Building    *b   = &buildings[i];
        const BuildingDef *def = &BUILDING_DEFS[b->type];

        if (!b->active) continue;

        /* For multi-tile buildings, use top-left tile as anchor */
        iso_to_screen(b->row, b->col, cam, &sx, &sy);

        if (g_sprites.ready) {
            /* CHANGED Phase 6: sprite path.
             * Building sprite height = COMP_H = 192 = TILE_H * 1.5
             * Position it so the base sits on the tile surface. */
            SDL_Texture *tex = g_sprites.building[b->type];
            /* Building sprite is COMP_W x COMP_H (256x320).
             * Layout inside the sprite (from top):
             *   0..roof_h        = roof  (≈163px)
             *   roof_h-RIDGE..   = walls (WALL_H=192px)
             *   wall_y+WALL_H    = base of building
             *
             * We need the base of the building to align with
             * the tile diamond top (sy).
             * roof_h ≈ 163, WALL_RIDGE_Y = 63 → wall_y = 100
             * total sprite height = COMP_H = 320
             * base_in_sprite = wall_y + WALL_H = 100 + 192 = 292
             * So: sprite_y = sy - base_in_sprite
             *              = sy - 292
             *
             * For multi-tile footprints: the top-left tile anchor
             * is already at the correct isometric position. */
            int bld_w = TILE_W;     /* always render at one tile wide */
            int bld_h = 320;        /* COMP_H */
            
            /* CHANGED: building base anchors to the BOTTOM of the
             * tile diamond (sy + TILE_H), not the top (sy).
             * iso_to_screen() returns the bounding-box top-left,
             * so the diamond bottom is at sy + TILE_H.
             * base_in_sprite = where the wall base sits in the
             * 320px tall composite = wall_y(100) + WALL_H(192) = 292.
             * sprite_top = (sy + TILE_H) - base_in_sprite
             *            = sy + 128 - 292  →  y_off = 128 - 292 = -164 */

            int x_off = 0;
            int y_off = TILE_H - 292;  /* = -164 */

            SDL_FRect dst = {
                (float)(sx + x_off),
                (float)(sy + y_off),
                (float)bld_w,
                (float)bld_h
            };
            SDL_RenderTexture(renderer, tex, NULL, &dst);
        } else {
            /* Fallback: coloured diamonds per footprint tile */
            SDL_Color top, bot;
            top.r = def->col_r; top.g = def->col_g;
            top.b = def->col_b; top.a = 255;
            bot.r = (unsigned char)(def->col_r * 0.7f);
            bot.g = (unsigned char)(def->col_g * 0.7f);
            bot.b = (unsigned char)(def->col_b * 0.7f);
            bot.a = 255;
            for (r = b->row; r < b->row + def->tile_h; r++) {
                for (c = b->col; c < b->col + def->tile_w; c++) {
                    iso_to_screen(r, c, cam, &sx, &sy);
                    draw_diamond(renderer, sx, sy, top, bot);
                    draw_diamond_outline(renderer, sx, sy,
                                         255, 255, 255, 60);
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
    const BuildingDef *def = &BUILDING_DEFS[type];
    int r, c, sx, sy;
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
            iso_to_screen(r, c, cam, &sx, &sy);
            draw_diamond(renderer, sx, sy, top, bot);
        }
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

/* ---- render_resources ---------------------------------- */
void render_resources(SDL_Renderer *renderer, const Stockpile *s)
{
    static const SDL_Color RES_COL[RES_COUNT] = {
        { 139,  90,  43, 255 },
        {  50, 180, 230, 255 },
        { 240, 210,  50, 255 },
        { 255, 195,   0, 255 },
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
