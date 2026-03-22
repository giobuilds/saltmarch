/*  sprite.c  --  Sprite loading from original spritesheets  (Phase 6)
 *
 *  APPROACH
 *  ========
 *  We load each spritesheet as a full SDL_Surface, then blit the
 *  relevant tile region into a fresh surface, apply colour-key
 *  transparency, and upload to GPU as an SDL_Texture.
 *
 *  No external image manipulation library is needed — SDL3's
 *  SDL_image handles PNG loading, and we do region extraction
 *  with SDL_BlitSurface using a source rect.
 *
 *  BUILDING COMPOSITE
 *  ==================
 *  Each building texture is composited from three pieces:
 *    1. Left wall  (64x96 from Buildings sheet, col 0)
 *    2. Right wall (64x96 from Buildings sheet, col 1)
 *    3. Roof       (144x92 from Roofing sheet, varies per building)
 *
 *  All three are blitted onto a 256x192 RGBA surface:
 *    - Left wall  scaled to 128x192, placed at x=0
 *    - Right wall scaled to 128x192, placed at x=128
 *    - Roof       scaled to 256xH,   placed so its bottom
 *                 aligns with the wall ridge (y=63 on scaled walls)
 *
 *  COLOUR KEY REMOVAL
 *  ==================
 *  Terrain/Forest: black    (0,0,0)     → transparent
 *  Water:          magenta  (255,0,255) → transparent
 *  Buildings/Roof: teal     (0,128,128) → transparent
 *
 *  SDL_SetColorKey() marks one exact colour as transparent before
 *  we upload to GPU.  We use SDL_MapRGB() so the key is in the
 *  surface's native pixel format.
 */

#include "sprite.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <string.h>   /* memset */

/* ---- Global sprite store ------------------------------- */
SpriteStore g_sprites;

/* =========================================================
 * Spritesheet constants
 * ========================================================= */

/* Terrain / Forest / Water sheets */
#define TERRAIN_TILE_W   256
#define TERRAIN_TILE_H   144
#define TERRAIN_COLS       3

/* Building walls sheet */
#define WALL_TILE_W       64
#define WALL_TILE_H       96
#define WALL_COLS         18

/* Roofing sheet */
#define ROOF_TILE_W      144
#define ROOF_TILE_H       92
#define ROOF_COLS          3

/* Composite building canvas size.
 * COMP_W  = full tile width.
 * COMP_H  = tall enough for roof (163px) + walls below ridge (129px).
 *            163 + 129 = 292, rounded up to 320 for safety.
 * WALL_H  = scaled wall height.
 * WALL_RIDGE_Y = where left/right walls meet the roof ridge. */
#define COMP_W           256
#define COMP_H           320
#define WALL_H           192
#define WALL_RIDGE_Y      63

/* =========================================================
 * Internal helpers
 * ========================================================= */

/* Load a PNG file and return its SDL_Surface, or NULL on error. */
static SDL_Surface *load_png(const char *path)
{
    SDL_Surface *s = IMG_Load(path);
    if (!s) SDL_Log("IMG_Load(%s) failed: %s", path, SDL_GetError());
    return s;
}

/* Extract one tile from a spritesheet surface by grid index.
 * Returns a new 32-bit RGBA surface, or NULL on error.
 * The caller owns the returned surface. */
static SDL_Surface *extract_tile(SDL_Surface *sheet,
                                 int tile_w, int tile_h,
                                 int cols,   int index)
{
    int col = index % cols;
    int row = index / cols;
    SDL_Rect src = {
        col * tile_w,
        row * tile_h,
        tile_w,
        tile_h
    };

    SDL_Surface *dst = SDL_CreateSurface(tile_w, tile_h,
                                         SDL_PIXELFORMAT_RGBA8888);
    if (!dst) {
        SDL_Log("SDL_CreateSurface failed: %s", SDL_GetError());
        return NULL;
    }

    if (!SDL_BlitSurface(sheet, &src, dst, NULL)) {
        SDL_Log("SDL_BlitSurface failed: %s", SDL_GetError());
        SDL_DestroySurface(dst);
        return NULL;
    }
    return dst;
}

/* Apply a colour key (make one colour transparent) and upload
 * the surface to GPU as a texture.  Destroys the surface.
 * Returns the texture, or NULL on failure. */
static SDL_Texture *surface_to_texture(SDL_Renderer *renderer,
                                       SDL_Surface  *surf,
                                       Uint8 key_r,
                                       Uint8 key_g,
                                       Uint8 key_b)
{
    Uint32 key = SDL_MapRGB(SDL_GetPixelFormatDetails(surf->format), NULL, key_r, key_g, key_b);
    SDL_SetSurfaceColorKey(surf, 1, key);

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);

    if (!tex) SDL_Log("CreateTextureFromSurface failed: %s", SDL_GetError());
    return tex;
}

/* Scale a surface to new dimensions using SDL_BlitSurfaceScaled.
 * Returns a new surface; caller owns it. */
static SDL_Surface *scale_surface(SDL_Surface *src, int new_w, int new_h)
{
    SDL_Surface *dst = SDL_CreateSurface(new_w, new_h,
                                         SDL_PIXELFORMAT_RGBA8888);
    if (!dst) return NULL;
    SDL_Rect dst_rect = { 0, 0, new_w, new_h };
    SDL_BlitSurfaceScaled(src, NULL, dst, &dst_rect,
                          SDL_SCALEMODE_LINEAR);
    return dst;
}

/* =========================================================
 * Terrain tile loader
 * Loads one tile from a terrain sheet, applies colour key,
 * returns SDL_Texture at TILE_W x TILE_H render size.
 * ========================================================= */
static SDL_Texture *load_terrain_tile(SDL_Renderer *renderer,
                                      SDL_Surface  *sheet,
                                      int           index,
                                      Uint8 key_r, Uint8 key_g, Uint8 key_b)
{
    SDL_Surface *tile = extract_tile(sheet,
                                     TERRAIN_TILE_W, TERRAIN_TILE_H,
                                     TERRAIN_COLS, index);
    if (!tile) return NULL;
    return surface_to_texture(renderer, tile, key_r, key_g, key_b);
}

/* =========================================================
 * Building composite builder
 *
 * Composes left wall + right wall + roof onto a COMP_W x COMP_H
 * canvas and returns it as a GPU texture.
 * ========================================================= */
static SDL_Texture *build_composite(SDL_Renderer  *renderer,
                                    SDL_Surface   *wall_sheet,
                                    SDL_Surface   *roof_sheet,
                                    int            roof_index)
{
    int wall_w, roof_w, roof_h, roof_y;
    SDL_Surface *canvas = NULL;
    SDL_Texture *tex    = NULL;
    SDL_Rect dst_l, dst_r, dst_roof;

    /* ── Step 1: Extract raw tiles ─────────────────────── */
    SDL_Surface *raw_l    = extract_tile(wall_sheet, WALL_TILE_W,
                                         WALL_TILE_H, WALL_COLS, 0);
    SDL_Surface *raw_r    = extract_tile(wall_sheet, WALL_TILE_W,
                                         WALL_TILE_H, WALL_COLS, 1);
    SDL_Surface *raw_roof = extract_tile(roof_sheet, ROOF_TILE_W,
                                         ROOF_TILE_H, ROOF_COLS, roof_index);
    if (!raw_l || !raw_r || !raw_roof) goto cleanup_raw;

    /* ── Step 2: Convert all to RGBA ───────────────────── */
    {
        SDL_Surface *tmp;
        tmp = SDL_ConvertSurface(raw_l,    SDL_PIXELFORMAT_RGBA8888);
        SDL_DestroySurface(raw_l);    raw_l    = tmp;
        tmp = SDL_ConvertSurface(raw_r,    SDL_PIXELFORMAT_RGBA8888);
        SDL_DestroySurface(raw_r);    raw_r    = tmp;
        tmp = SDL_ConvertSurface(raw_roof, SDL_PIXELFORMAT_RGBA8888);
        SDL_DestroySurface(raw_roof); raw_roof = tmp;
        if (!raw_l || !raw_r || !raw_roof) goto cleanup_raw;
    }

    /* ── Step 3: Remove teal background BEFORE scaling ────
     * We set alpha=0 on exact teal pixels while the image is
     * still at original resolution (no interpolation yet).
     * Then scale with NEAREST to avoid re-introducing teal
     * at sub-pixel boundaries. */
    {
        SDL_Surface *surfs[3] = { raw_l, raw_r, raw_roof };
        int s;
        for (s = 0; s < 3; s++) {
            Uint32 *px  = (Uint32 *)surfs[s]->pixels;
            int     n   = surfs[s]->w * surfs[s]->h;
            int     i;
            /* Lock needed for pixel access */
            SDL_LockSurface(surfs[s]);
            for (i = 0; i < n; i++) {
                Uint8 r2, g2, b2, a2;
                SDL_GetRGBA(px[i],
                    SDL_GetPixelFormatDetails(surfs[s]->format),
                    NULL, &r2, &g2, &b2, &a2);
                /* Tolerance-based teal removal */
                if (r2 < 20 && g2 > 100 && g2 < 160 &&
                    b2 > 100 && b2 < 160) {
                    px[i] = SDL_MapRGBA(
                        SDL_GetPixelFormatDetails(surfs[s]->format),
                        NULL, 0, 0, 0, 0);
                }
            }
            SDL_UnlockSurface(surfs[s]);
        }
    }

    /* ── Step 4: Scale walls and roof ─────────────────── */
    wall_w = COMP_W / 2;                         /* 128 */
    roof_w = COMP_W;                             /* 256 */
    roof_h = (int)((float)ROOF_TILE_H * ((float)roof_w / ROOF_TILE_W));

    {
        SDL_Surface *wall_l = scale_surface(raw_l,    wall_w, WALL_H);
        SDL_Surface *wall_r = scale_surface(raw_r,    wall_w, WALL_H);
        SDL_Surface *roof   = scale_surface(raw_roof, roof_w, roof_h);
        SDL_DestroySurface(raw_l);
        SDL_DestroySurface(raw_r);
        SDL_DestroySurface(raw_roof);
        raw_l = raw_r = raw_roof = NULL;
        if (!wall_l || !wall_r || !roof) {
            if (wall_l) SDL_DestroySurface(wall_l);
            if (wall_r) SDL_DestroySurface(wall_r);
            if (roof)   SDL_DestroySurface(roof);
            return NULL;
        }

        /* ── Step 5: Composite onto RGBA canvas ───────── */
        canvas = SDL_CreateSurface(COMP_W, COMP_H,
                                   SDL_PIXELFORMAT_RGBA8888);
        if (!canvas) {
            SDL_DestroySurface(wall_l);
            SDL_DestroySurface(wall_r);
            SDL_DestroySurface(roof);
            return NULL;
        }
        SDL_ClearSurface(canvas, 0.0f, 0.0f, 0.0f, 0.0f);

        /* Walls start at y = roof_h - WALL_RIDGE_Y so the ridge
         * aligns with the bottom of the roof. */
        int wall_y = roof_h - WALL_RIDGE_Y;
        dst_l    = (SDL_Rect){ 0,      wall_y, wall_w, WALL_H };
        dst_r    = (SDL_Rect){ wall_w, wall_y, wall_w, WALL_H };
        SDL_BlitSurface(wall_l, NULL, canvas, &dst_l);
        SDL_BlitSurface(wall_r, NULL, canvas, &dst_r);

        /* Roof sits at y=0 — fills the space above the walls */
        roof_y   = 0;
        dst_roof = (SDL_Rect){ 0, roof_y, roof_w, roof_h };
        SDL_BlitSurface(roof, NULL, canvas, &dst_roof);

        SDL_DestroySurface(wall_l);
        SDL_DestroySurface(wall_r);
        SDL_DestroySurface(roof);
    }

    tex = SDL_CreateTextureFromSurface(renderer, canvas);
    SDL_DestroySurface(canvas);
    if (!tex) SDL_Log("Building composite upload failed: %s", SDL_GetError());
    return tex;

cleanup_raw:
    if (raw_l)    SDL_DestroySurface(raw_l);
    if (raw_r)    SDL_DestroySurface(raw_r);
    if (raw_roof) SDL_DestroySurface(raw_roof);
    return NULL;
}

/* =========================================================
 * sprites_load  --  public entry point
 * ========================================================= */
int sprites_load(SDL_Renderer *renderer, const char *assets_dir)
{
    char path[512];
    SDL_Surface *sheet_terrain1 = NULL;
    SDL_Surface *sheet_terrain2 = NULL;
    SDL_Surface *sheet_forest   = NULL;
    SDL_Surface *sheet_water    = NULL;
    SDL_Surface *sheet_walls    = NULL;
    SDL_Surface *sheet_roof     = NULL;
    int ok = 0;

    memset(&g_sprites, 0, sizeof(g_sprites));

    /* ---- Load sheets ----------------------------------- */
    SDL_snprintf(path, sizeof(path),
        "%s/Overworld_-_Terrain_1_-_Thick_256x144.png", assets_dir);
    sheet_terrain1 = load_png(path);

    SDL_snprintf(path, sizeof(path),
        "%s/Overworld_-_Terrain_2_-_Thick_256x144.png", assets_dir);
    sheet_terrain2 = load_png(path);

    SDL_snprintf(path, sizeof(path),
        "%s/Overworld_-_Forest_-_Thick_256x144.png", assets_dir);
    sheet_forest = load_png(path);

    SDL_snprintf(path, sizeof(path),
        "%s/Overworld_-_Water_-_Thick_256x144.png", assets_dir);
    sheet_water = load_png(path);

    SDL_snprintf(path, sizeof(path),
        "%s/Isometric_Buildings_1_-_64x96.png", assets_dir);
    sheet_walls = load_png(path);

    SDL_snprintf(path, sizeof(path),
        "%s/Isometric_Town_Roofing_-_143x92.png", assets_dir);
    sheet_roof = load_png(path);

    if (!sheet_terrain1 || !sheet_terrain2 || !sheet_forest ||
        !sheet_water    || !sheet_walls    || !sheet_roof) {
        SDL_Log("sprites_load: one or more sheets failed to load");
        goto cleanup;
    }

    /* ---- Terrain textures ----------------------------- */
    /* TILE_GRASS  → Terrain_1  index 13, black bg */
    g_sprites.terrain[TILE_GRASS] =
        load_terrain_tile(renderer, sheet_terrain1, 13, 0, 0, 0);

    /* TILE_WATER  → Water      index 0,  magenta bg */
    g_sprites.terrain[TILE_WATER] =
        load_terrain_tile(renderer, sheet_water, 0, 255, 0, 255);

    /* TILE_FOREST → Forest     index 2,  black bg */
    g_sprites.terrain[TILE_FOREST] =
        load_terrain_tile(renderer, sheet_forest, 2, 0, 0, 0);

    /* TILE_SAND   → Terrain_2  index 17, black bg */
    g_sprites.terrain[TILE_SAND] =
        load_terrain_tile(renderer, sheet_terrain2, 17, 0, 0, 0);

    /* Verify all terrain textures loaded */
    {
        int t;
        for (t = 0; t < TILE_TYPE_COUNT; t++) {
            if (!g_sprites.terrain[t]) {
                SDL_Log("sprites_load: terrain[%d] failed", t);
                goto cleanup;
            }
        }
    }

    /* ---- Building textures (composited) --------------- */
    /* roof indices: house=0, farm=1, warehouse=2,
     *               fishers_hut=8, lumberjack=3 */
    g_sprites.building[BUILDING_FISHERS_HUT] =
        build_composite(renderer, sheet_walls, sheet_roof, 8);
    g_sprites.building[BUILDING_WAREHOUSE] =
        build_composite(renderer, sheet_walls, sheet_roof, 2);
    g_sprites.building[BUILDING_FARM] =
        build_composite(renderer, sheet_walls, sheet_roof, 1);
    g_sprites.building[BUILDING_LUMBERJACK] =
        build_composite(renderer, sheet_walls, sheet_roof, 3);
    g_sprites.building[BUILDING_HOUSE] =
        build_composite(renderer, sheet_walls, sheet_roof, 0);

    /* Verify all building textures */
    {
        int b;
        for (b = 0; b < BUILDING_TYPE_COUNT; b++) {
            if (!g_sprites.building[b]) {
                SDL_Log("sprites_load: building[%d] failed", b);
                goto cleanup;
            }
        }
    }

    g_sprites.ready = 1;
    SDL_Log("Sprites loaded successfully.");
    ok = 1;

cleanup:
    if (sheet_terrain1) SDL_DestroySurface(sheet_terrain1);
    if (sheet_terrain2) SDL_DestroySurface(sheet_terrain2);
    if (sheet_forest)   SDL_DestroySurface(sheet_forest);
    if (sheet_water)    SDL_DestroySurface(sheet_water);
    if (sheet_walls)    SDL_DestroySurface(sheet_walls);
    if (sheet_roof)     SDL_DestroySurface(sheet_roof);
    return ok;
}

/* =========================================================
 * sprites_free
 * ========================================================= */
void sprites_free(void)
{
    int i;
    for (i = 0; i < TILE_TYPE_COUNT; i++) {
        if (g_sprites.terrain[i]) {
            SDL_DestroyTexture(g_sprites.terrain[i]);
            g_sprites.terrain[i] = NULL;
        }
    }
    for (i = 0; i < BUILDING_TYPE_COUNT; i++) {
        if (g_sprites.building[i]) {
            SDL_DestroyTexture(g_sprites.building[i]);
            g_sprites.building[i] = NULL;
        }
    }
    g_sprites.ready = 0;
}
