#ifndef MAP_H
#define MAP_H

/* map.h  --  Tile map data structures  (Phase 2) */

#include <stdint.h>   /* uint32_t for seed */

/* Tile pixel dimensions (unchanged) */
#define TILE_W 64
#define TILE_H 32

/* Map size in tiles */
#define MAP_COLS 64
#define MAP_ROWS 64

/* ---- Fertility flags -----------------------------------
 * A tile can support multiple crop types simultaneously.
 * Stored as a bitmask so we can write e.g.: */
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
    /* SUPPLY_CHAIN Phase 7. Southern, and its own bit rather than
     * reusing FERTILE_PASTURE: every grass tile anywhere is pasture,
     * so an alpaca on that bit would graze the northern moors too, and
     * Marsh Hats would stop needing the south. */
    FERTILE_ALPACA   = 1 << 15,  /* Alpaca Pasture -> Alpaca Wool */
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

/* ---- Mineral deposits ---------------------------------- */
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

/* ---- Island terrain profiles ---------------------------- */
typedef enum {
    PROFILE_TEMPERATE = 0,
    PROFILE_HIGHLAND  = 1,
    PROFILE_WOODLAND  = 2,
    PROFILE_ATOLL     = 3,
    /* SUPPLY_CHAIN Phase 5: the southern climates. Appended, so every
     * existing profile keeps its value and an island's stored profile
     * still means what it meant. */
    PROFILE_PLANTATION = 4,   /* open ground: cotton, cane, tobacco   */
    PROFILE_JUNGLE     = 5,   /* forest and shade: cocoa, coffee, lac */
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
 * map.c). Every field in every tile is fully initialised. */
void map_init(Map *map, uint32_t seed, MapProfile profile);

/* Bounds-checked accessor.  Returns NULL if out of range. */
Tile *map_get_tile(Map *map, int row, int col);

#endif /* MAP_H */
