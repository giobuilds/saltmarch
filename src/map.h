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
 *   t->fertility = FERTILE_GRAIN | FERTILE_PASTURE;
 *
 * SUPPLY_CHAIN Phase 1 widened this from two bits to one bit per crop
 * the plan's chains start from. Two bits was enough only because there
 * was one crop-specific building; with a field on BuildingDef
 * (needs_fertility) any building can name the crop it wants, and
 * PLACE_NEEDS_HOP_FERTILE — a placement flag that existed purely
 * because there was no way to say "this crop specifically" — is gone.
 *
 * Bits are declared for crops no profile grows yet: the southern ones
 * (cotton, cane, cocoa, coffee, tobacco, maize, plantain, lac) arrive
 * with the southern island profiles in Phase 5. Declaring them now
 * costs nothing — fertility is regenerated from the map seed and never
 * saved — and keeps the enum in one piece rather than split across two
 * phases. */
typedef enum {
    FERTILE_NONE     = 0,
    FERTILE_GRAIN    = 1 << 0,   /* wheat fields, bakeries       */
    FERTILE_HOP      = 1 << 1,   /* beer production chain        */
    FERTILE_POTATO   = 1 << 2,   /* Still -> Marsh Gin           */
    FERTILE_GRAPES   = 1 << 3,   /* Sparkling Cellar             */
    FERTILE_FLOWERS  = 1 << 4,   /* Perfumery                    */
    FERTILE_PASTURE  = 1 << 5,   /* grazing: sheep, cattle, pigs */
    /* SUPPLY_CHAIN Phase 4: the Kitchen's second input. Northern, and
     * appended here rather than beside the other temperate crops
     * because inserting a bit would renumber the southern ones and
     * change what an existing map's fertility mask means. */
    FERTILE_PEPPER   = 1 << 14,  /* Cattle + Pepper -> Kitchen   */
    /* Southern crops — no northern profile grows these (Phase 5). */
    FERTILE_COTTON   = 1 << 6,
    FERTILE_CANE     = 1 << 7,
    FERTILE_COCOA    = 1 << 8,
    FERTILE_COFFEE   = 1 << 9,
    FERTILE_TOBACCO  = 1 << 10,
    FERTILE_MAIZE    = 1 << 11,
    FERTILE_PLANTAIN = 1 << 12,
    FERTILE_LAC      = 1 << 13
} Fertility;

/* ---- Mineral deposits ----------------------------------
 * What can be dug out of a tile. A tile has at most one, so this is an
 * enum rather than a bitmask: two minerals under one tile would mean
 * two mines on the same square, which the footprint rules forbid
 * anyway.
 *
 * Unlike fertility (a property of soil that several buildings may read
 * without competing) a deposit is the thing a mine consumes the site
 * of, which is why scarcity of these is what makes an island worth
 * having. Scattered by map.c's deposit pass from the island seed. */
typedef enum {
    DEPOSIT_NONE = 0,
    DEPOSIT_IRON,
    DEPOSIT_COAL,
    DEPOSIT_CLAY,
    DEPOSIT_SAND,
    DEPOSIT_GOLD_ORE,
    DEPOSIT_PEARLS,
    DEPOSIT_COUNT
} Deposit;

/* Display name for a deposit ("Iron"). Never NULL — DEPOSIT_NONE
 * reads as "" so a caller can print it unconditionally. */
const char *deposit_name(Deposit d);

/* The hover label ("Iron deposit"). Same contract: never NULL, "" for
 * DEPOSIT_NONE. Separate from deposit_name() because a list of goods
 * wants the noun and a label on the ground wants the phrase. */
const char *deposit_label(Deposit d);

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
    /* A bitmask of Fertility bits, not one Fertility value — the type
     * is uint32_t so that OR-ing several crops together stays a plain
     * integer operation rather than an out-of-range enum value. */
    uint32_t  fertility;      /* which crops grow here (bitmask)     */
    uint8_t   deposit;        /* one Deposit value, or DEPOSIT_NONE  */
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
