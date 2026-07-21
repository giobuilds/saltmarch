#ifndef BUILDING_H
#define BUILDING_H

/* =========================================================
 * building.h  --  Building types, definitions, instances
 *
 * There are two distinct concepts here, keep them separate
 * in your mind:
 *
 *   BuildingDef  – static data about a TYPE of building.
 *                  One entry per building type, never changes.
 *                  (like a class definition)
 *
 *   Building     – a placed INSTANCE on the map.
 *                  Has a position and refers to its def.
 *                  (like an object / instance)
 * 
 *                 (Phase 4: production fields added)
 * ========================================================= */

#include "map.h"      /* Tile, TileType, Fertility, MAP_* */
#include "resource.h"
#include <stddef.h>   /* size_t */

/* ---- How many buildings can be placed at once ----------
 * Raised from 64 (Phase 2, roads): a road is a BUILDING_ROAD
 * entry like any other building, so a real road network shares
 * this same budget instead of getting its own cap. 600 is
 * generous for a 64x64 map's buildable tile count. */
#define MAX_BUILDINGS 600

/* Storage capacity added to every non-gold resource by each
 * active Warehouse (see resource.h's BASE_STORAGE_CAP for the
 * cap before any Warehouse is built). */
#define WAREHOUSE_STORAGE_BONUS 100

/* ---- Building type identifiers ------------------------- */
typedef enum {
    BUILDING_NONE       = -1,   /* sentinel: nothing selected */
    BUILDING_FISHERS_HUT = 0,
    BUILDING_WAREHOUSE   = 1,
    BUILDING_FARM        = 2,
    BUILDING_LUMBERJACK  = 3,
    BUILDING_HOUSE       =  4,   /* Phase 5 */
    BUILDING_ROAD        =  5,   /* Phase 2: roads/logistics */
    BUILDING_TYPE_COUNT
} BuildingType;

/* ---- Placement rule flags (bitmask) --------------------
 * Stored in BuildingDef.placement_flags.
 * The placement validator checks these against the map. */
typedef enum {
    PLACE_ANY_LAND     = 0,        /* no extra constraint      */
    PLACE_NEEDS_COAST  = 1 << 0,   /* adjacent water required  */
    PLACE_NEEDS_FOREST = 1 << 1,   /* adjacent forest required */
    PLACE_NEEDS_FERTILE= 1 << 2,   /* all tiles must be fertile*/
} PlacementFlags;

/* ---- Static definition of one building type ------------ */
typedef struct {
    const char   *name;
    int           tile_w;          /* footprint width  in tiles */
    int           tile_h;          /* footprint height in tiles */
    PlacementFlags placement_flags;
    /* Colour for the placeholder rectangle (R, G, B) */
    unsigned char col_r, col_g, col_b;

    /* CHANGED Phase 4: production fields.
     * produces      – which resource this building outputs
     *                 (RES_COUNT means "produces nothing")
     * produce_amt   – units produced per tick
     * consumes      – which resource this building needs as input
     *                 (RES_COUNT means "needs no input")
     * consume_amt   – units consumed per tick
     * tick_seconds  – real-time seconds between production ticks */
    ResourceType  produces;
    int           produce_amt;
    ResourceType  consumes;
    int           consume_amt;
    float         tick_seconds;

    /* One-time cost deducted from the stockpile when this
     * building is placed, indexed like Stockpile.amount[].
     * Unlike produces/consumes (a per-tick flow), this is a
     * lump sum paid once at building_place() time. */
    int           cost[RES_COUNT];
} BuildingDef;

/* The global table of all building definitions.
 * Defined in building.c, declared here for all to use.
 * Indexed by BuildingType. */
extern const BuildingDef BUILDING_DEFS[BUILDING_TYPE_COUNT];

/* ---- One placed building instance --------------------- */
typedef struct {
    BuildingType type;
    int          row;   /* top-left tile of the footprint */
    int          col;
    int          active; /* 1 = placed, 0 = empty slot     */

    /* CHANGED Phase 4: time accumulator.
     * Counts seconds since this building last produced.
     * When timer >= def->tick_seconds a tick fires and
     * timer resets to 0. */
    float        timer;

    /* Phase 3: derived, not meaningfully persisted — recomputed
     * every frame by connectivity_update() from the current road
     * network, before it's read by game_tick_buildings()/
     * pop_update(). 1 for a Warehouse or Road (neither needs a
     * route to itself), otherwise 1 iff a road path reaches an
     * active Warehouse. (It does get written byte-for-byte into
     * save files along with the rest of the struct, but that's
     * harmless: it's overwritten by the next connectivity_update()
     * before anything reads it.) */
    int          connected;
} Building;

/* ---- Placement validation ----------------------------- */

/* Returns 1 if the building of `type` can be placed with its
 * top-left corner at (row, col) on `map`.
 * Returns 0 and sets reason[0..reason_len] to a short message
 * explaining why not (useful for a future status bar).
 * Pass NULL for reason if you don't need the message. */
int building_can_place(const Map *map,
                       BuildingType type,
                       int row, int col,
                       char *reason, size_t reason_len);

/* Returns 1 if `s` holds enough of every resource in
 * BUILDING_DEFS[type].cost[] to afford placing it. Deliberately
 * separate from building_can_place() (which only knows about the
 * map) so "can't afford" and "can't place here" stay distinct
 * reasons a caller can tell apart. */
int building_can_afford(const Stockpile *s, BuildingType type);

/* Place a building into the buildings array.
 * Returns the index of the new building, or -1 if the array
 * is full or placement is invalid. */
int building_place(Building buildings[], int *count,
                   const Map *map,
                   BuildingType type, int row, int col);

#endif /* BUILDING_H */
