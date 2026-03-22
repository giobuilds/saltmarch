#ifndef SPRITE_H
#define SPRITE_H

/* =========================================================
 * sprite.h  --  Sprite loading and tile texture handles
 *               (Phase 6)
 *
 * All game textures are loaded once at startup from the
 * original spritesheet files in assets/tiles/.
 *
 * SPRITESHEET LAYOUT
 * ==================
 * Terrain sheets (Forest, Terrain1-3, Water):
 *   File size:  768 x 864 px
 *   Grid:       3 cols x 6 rows
 *   Tile size:  256 x 144 px each
 *   Background: black (0,0,0) for terrain/forest
 *               magenta (255,0,255) for water
 *
 * Building walls sheet (Isometric_Buildings_1):
 *   File size:  1152 x 768 px
 *   Grid:       18 cols x 8 rows
 *   Tile size:  64 x 96 px each
 *   Background: teal (0,128,128)
 *
 * Roofing sheet (Isometric_Town_Roofing):
 *   File size:  432 x 368 px
 *   Grid:       3 cols x 4 rows
 *   Tile size:  144 x 92 px each
 *   Background: teal (0,128,128)
 *
 * TILE SELECTIONS (chosen by Giovanni)
 * =====================================
 * TILE_GRASS   → Terrain_1  index 13  (row 4, col 1)
 * TILE_SAND    → Terrain_2  index 17  (row 5, col 2)
 * TILE_WATER   → Water      index 0   (row 0, col 0)
 * TILE_FOREST  → Forest     index 2   (row 0, col 2)
 *
 * Buildings all share walls 0 (left) and 1 (right),
 * differentiated by roof:
 *   House       → roof index 0
 *   Farm        → roof index 1
 *   Warehouse   → roof index 2
 *   Fisher's Hut→ roof index 8
 *   Lumberjack  → roof index 3
 *
 * RENDER DIMENSIONS
 * =================
 * Terrain sprites are 256x144. We render them at TILE_W x TILE_H
 * (256 x 128) — the bottom 16px of each sprite is side-face that
 * gets covered by the row in front, so slight clipping is correct.
 *
 * Building sprites are composited: left wall + right wall + roof.
 * They render taller than one tile (256 x 192) so they overlap
 * the row above — this is correct isometric painter's order.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "building.h"   /* BuildingType */
#include "map.h"        /* TileType     */

/* ---- Sprite store -------------------------------------- */
typedef struct {
    /* One texture per terrain tile type */
    SDL_Texture *terrain[TILE_TYPE_COUNT];

    /* One composited texture per building type.
     * Each is 256x192: walls + roof blended together. */
    SDL_Texture *building[BUILDING_TYPE_COUNT];

    /* 1 after sprites_load() succeeds, 0 otherwise.
     * render.c checks this before using any texture —
     * if 0 it falls back to coloured diamonds so the
     * game is still playable without the asset files. */
    int ready;
} SpriteStore;

/* Global sprite store — defined in sprite.c */
extern SpriteStore g_sprites;

/* ---- Public API ---------------------------------------- */

/* Load all textures from the spritesheets in assets_dir.
 * assets_dir should be the path to assets/tiles/ relative
 * to the working directory, e.g. "assets/tiles".
 * Returns 1 on success, 0 on failure (game continues). */
int  sprites_load(SDL_Renderer *renderer, const char *assets_dir);

/* Free all textures and reset g_sprites. */
void sprites_free(void);

#endif /* SPRITE_H */
