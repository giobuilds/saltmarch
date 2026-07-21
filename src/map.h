#ifndef MAP_H
#define MAP_H

/* =========================================================
 * map.h  --  Tile map data structures  (Phase 2)
 *
 * Phase 2 changes vs Phase 1:
 *   - MAP_COLS/ROWS expanded to 64x64
 *   - Tile gains buildable and fertility fields
 *   - New Fertility bitmask enum
 *   - map_init() now takes a seed for the noise generator
 * ========================================================= */

#include <stdint.h>   /* uint32_t for seed */

/* Tile pixel dimensions (unchanged) */
#define TILE_W 64
#define TILE_H 32

/* Map size in tiles */
#define MAP_COLS 64
#define MAP_ROWS 64

/* ---- Fertility flags -----------------------------------
 * A tile can support multiple crop types simultaneously.
 * Stored as a bitmask so we can write e.g.:
 *   t->fertility = FERTILE_GRAIN | FERTILE_HOP;
 *
 * FERTILE_HOP is assigned by tile_set_metadata() (map.c) on
 * high-elevation grass but no building consumes it yet — it's
 * live world-gen data waiting on a future beer-chain building,
 * not dead code. */
typedef enum {
    FERTILE_NONE   = 0,
    FERTILE_GRAIN  = 1 << 0,   /* wheat fields, bakeries  */
    FERTILE_HOP    = 1 << 1,   /* beer production chain   */
} Fertility;

/* ---- Island terrain profiles ----------------------------
 * Which flavour of island a Map represents. Stored in Map (and in
 * Island, and in the save file) from the island refactor onward so
 * that adding the per-profile generation behaviour later needs no
 * save-format change.
 *
 * Only PROFILE_TEMPERATE — today's exact generation — has distinct
 * behaviour so far; the others are declared now and become
 * meaningful when map_init() learns to vary its thresholds by
 * profile. Their eventual roles:
 *   HIGHLAND – hop-rich, grain-poor (the reason to colonise)
 *   WOODLAND – timber-rich, little fertile ground
 *   ATOLL    – almost all coast: fish and not much else
 * ========================================================= */
typedef enum {
    PROFILE_TEMPERATE = 0,
    PROFILE_HIGHLAND  = 1,
    PROFILE_WOODLAND  = 2,
    PROFILE_ATOLL     = 3,
    PROFILE_COUNT
} MapProfile;

/* ---- Tile types ---------------------------------------- */
typedef enum {
    TILE_GRASS  = 0,
    TILE_WATER  = 1,
    TILE_FOREST = 2,
    TILE_SAND   = 3,
    TILE_TYPE_COUNT
} TileType;

/* ---- One tile in the grid ------------------------------ */
typedef struct {
    TileType  type;
    int       elevation;      /* 0-255 heightmap value kept for debug */

    /* Gameplay fields */
    int       buildable;      /* 1 if a building may be placed here  */
    Fertility fertility;      /* which crops grow here (bitmask)     */
} Tile;

/* ---- The whole map ------------------------------------- */
typedef struct {
    Tile tiles[MAP_ROWS][MAP_COLS];
    int  rows;
    int  cols;
    /* The seed REQUESTED of map_init(), not any internal working seed
     * its validate-and-reseed loop may have settled on. Generation is
     * deterministic given (requested seed, profile), so persisting the
     * request is what lets a save reproduce the map exactly. */
    uint32_t   seed;
    MapProfile profile;
} Map;

/* ---- Function declarations ---------------------------- */

/* Generate a new island using value noise seeded by `seed`, shaped by
 * `profile` (thresholds and fertility rules — see PROFILE_PARAMS in
 * map.c). Every field in every tile is fully initialised.
 *
 * Retries with a derived seed until the result satisfies the
 * profile's minimum-resource requirements, so a Highland always
 * actually has hops and the starting island is always playable. The
 * requested seed is what gets stored in map->seed. */
void map_init(Map *map, uint32_t seed, MapProfile profile);

/* Bounds-checked accessor.  Returns NULL if out of range. */
Tile *map_get_tile(Map *map, int row, int col);

#endif /* MAP_H */
