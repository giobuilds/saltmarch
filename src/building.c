/*  building.c  --  Building definitions and placement logic
 *
 *  PLACEMENT VALIDATION STRATEGY
 *  ==============================
 *  building_can_place() works in three passes:
 *
 *  Pass 1 – Bounds check
 *    The entire footprint (tile_w × tile_h) must lie within
 *    the map.  A 2×2 building at (row=63, col=63) would hang
 *    off the edge — reject it.
 *
 *  Pass 2 – Per-tile checks
 *    Every tile in the footprint must be:
 *      a) buildable  (not water, not forest)
 *      b) not already occupied by another building
 *    If PLACE_NEEDS_FERTILE is set, every tile must also
 *    carry the right fertility flag.
 *
 *  Pass 3 – Adjacency checks (only if flags require it)
 *    PLACE_NEEDS_COAST  → at least one of the 4-connected
 *                         neighbours of ANY footprint tile
 *                         must be TILE_WATER.
 *    PLACE_NEEDS_FOREST → same but for TILE_FOREST.
 *    We scan all tiles in the footprint and check their
 *    four cardinal neighbours (N, S, E, W).
 * 
 * Building definition table  (Phase 4: production fields)
 *
 * Production design:
 *   Fisher's Hut  – produces FISH from nothing (the sea is free)
 *   Warehouse     – no production; it is a storage building
 *   Farm          – produces GRAIN from nothing (sun and soil)
 *   Lumberjack    – produces WOOD from nothing (the forest is free)
 *
 * In Phase 5 we will add consumption chains:
 *   e.g. Fisher's Hut will consume WOOD for boat fuel,
 *   Farm will consume tools, etc.
 * RES_COUNT is used as a sentinel meaning "no resource".
 *
 * tick_seconds controls how fast each building works:
 *   slower tick = rarer, more valuable output
 */

#include "building.h"
#include <string.h>   /* strncpy  */
#include <stddef.h>   /* NULL     */

/* =========================================================
 * Building definition table
 * ========================================================= */
const BuildingDef BUILDING_DEFS[BUILDING_TYPE_COUNT] = {
    /*  name            w  h  flags                  R    G    B
     *  produces       amt  consumes[2]         amt[2]      tick   cost  hud_placeable */
    {
        "Fisher's Hut", 1, 1, PLACE_NEEDS_COAST,   210, 180, 100,
        RES_FISH,       1,   { RES_COUNT, RES_COUNT }, { 0, 0 },  6.0f,
        { [RES_GOLD] = 60 },   /* raw producer: Gold only, no bootstrap chicken-and-egg */
        1
    },
    {
        "Warehouse",    2, 2, PLACE_ANY_LAND,       160, 100,  60,
        RES_COUNT,      0,   { RES_COUNT, RES_COUNT }, { 0, 0 },  0.0f,   /* no production */
        { [RES_WOOD] = 20, [RES_GOLD] = 150 },
        1
    },
    {
        "Farm",         2, 2, PLACE_NEEDS_FERTILE,   80, 160,  50,
        RES_GRAIN,      1,   { RES_COUNT, RES_COUNT }, { 0, 0 },  8.0f,
        { [RES_GOLD] = 80 },
        1
    },
    {
        "Lumberjack",   1, 1, PLACE_NEEDS_FOREST,  120,  80,  40,
        RES_WOOD,       1,   { RES_COUNT, RES_COUNT }, { 0, 0 },  5.0f,
        { [RES_GOLD] = 60 },
        1
    },
    /* Phase 5: House — residents live here, generate gold when fed */
    {
        "House",        1, 1, PLACE_ANY_LAND,      210, 190, 160,
        RES_COUNT,      0,   { RES_COUNT, RES_COUNT }, { 0, 0 },  0.0f,   /* pop_update handles gold, not tick system */
        { [RES_WOOD] = 15, [RES_GOLD] = 80 },
        1
    },
    /* Phase 2: Road — no production; PLACE_ANY_LAND is sufficient
     * to keep it off water/forest, since building_can_place already
     * requires tile->buildable, which those tile types never have.
     * Free: a real road network needs many tiles, and charging per
     * tile made drag-placing one needlessly punishing. */
    {
        "Road",         1, 1, PLACE_ANY_LAND,      110, 105, 100,
        RES_COUNT,      0,   { RES_COUNT, RES_COUNT }, { 0, 0 },  0.0f,
        { 0 },
        1
    },
    /* Phase 4: Marketplace — no passive production; it's a pure
     * gateway building. Clicking a placed, road-connected one opens
     * the manual trade screen (see trade_ui.c, game_sell_resource). */
    {
        "Marketplace",  2, 2, PLACE_ANY_LAND,      200, 140,  60,
        RES_COUNT,      0,   { RES_COUNT, RES_COUNT }, { 0, 0 },  0.0f,
        { [RES_WOOD] = 30, [RES_GOLD] = 200 },
        1
    },
    /* Production chains, Phase 1 (Beer). Hop Farm needs FERTILE_HOP
     * specifically (map.h), finally giving that dormant fertility bit
     * a consumer. Malthouse is the multi-input building: both Grain
     * and Hops must be in stock for it to tick at all (all-or-nothing,
     * see game_tick_buildings, game.c). */
    {
        "Hop Farm",     1, 1, PLACE_NEEDS_HOP_FERTILE, 90, 150, 60,
        RES_HOPS,       1,   { RES_COUNT, RES_COUNT }, { 0, 0 },  8.0f,
        { [RES_GOLD] = 80 },
        1
    },
    {
        "Malthouse",    2, 2, PLACE_ANY_LAND,       170, 140,  90,
        RES_MALT,       1,   { RES_GRAIN, RES_HOPS },  { 1, 1 },  10.0f,
        { [RES_WOOD] = 20, [RES_GOLD] = 150 },
        1
    },
    {
        "Brewery",      2, 2, PLACE_ANY_LAND,       190, 150,  70,
        RES_BEER,       1,   { RES_MALT, RES_COUNT },  { 1, 0 },  8.0f,
        { [RES_WOOD] = 20, [RES_GOLD] = 150 },
        1
    },
    /* Population tiers, Phase 1: Worker's House is reached only by
     * upgrading a placed BUILDING_HOUSE (game_upgrade_house, game.c),
     * never placed directly — hud_placeable = 0 keeps it off the HUD
     * bar (see ui.c's filtered slot list). cost[] is irrelevant since
     * building_place() is never called for this type; the upgrade's
     * Gold cost lives in game_upgrade_house() instead. */
    {
        "Worker's House", 1, 1, PLACE_ANY_LAND,    230, 200, 140,
        RES_COUNT,      0,   { RES_COUNT, RES_COUNT }, { 0, 0 },  0.0f,
        { 0 },
        0
    },
};

/* =========================================================
 * Helper: is tile (r,c) occupied by any placed building?
 * We check every active building's footprint.
 * ========================================================= */
static int tile_is_occupied(const Building buildings[], int count,
                             int r, int c)
{
    int i, br, bc;
    for (i = 0; i < count; i++) {
        const Building    *b   = &buildings[i];
        const BuildingDef *def = &BUILDING_DEFS[b->type];
        if (!b->active) continue;
        for (br = b->row; br < b->row + def->tile_h; br++)
            for (bc = b->col; bc < b->col + def->tile_w; bc++)
                if (br == r && bc == c) return 1;
    }
    return 0;
}

/* =========================================================
 * Helper: does the footprint have an adjacent tile of type t?
 * Checks all four cardinal neighbours of every footprint tile.
 * ========================================================= */
static int footprint_has_adjacent(const Map *map,
                                  int row, int col,
                                  int fw, int fh,
                                  TileType t)
{
    /* Cardinal direction offsets: N, S, E, W */
    static const int dr[4] = { -1, 1,  0, 0 };
    static const int dc[4] = {  0, 0,  1,-1 };

    int r, c, d;
    for (r = row; r < row + fh; r++) {
        for (c = col; c < col + fw; c++) {
            for (d = 0; d < 4; d++) {
                const Tile *nb = map_get_tile(
                    (Map *)map, r + dr[d], c + dc[d]);
                if (nb && nb->type == t) return 1;
            }
        }
    }
    return 0;
}

/* =========================================================
 * building_can_place
 * ========================================================= */
int building_can_place(const Map *map,
                       BuildingType type,
                       int row, int col,
                       char *reason, size_t reason_len)
{
    const BuildingDef *def = &BUILDING_DEFS[type];
    int r, c;

    /* --- Pass 1: bounds --------------------------------- */
    if (row < 0 || col < 0 ||
        row + def->tile_h > map->rows ||
        col + def->tile_w > map->cols) {
        if (reason) strncpy(reason, "Out of bounds", reason_len);
        return 0;
    }

    /* --- Pass 2: per-tile checks ------------------------ */
    for (r = row; r < row + def->tile_h; r++) {
        for (c = col; c < col + def->tile_w; c++) {
            const Tile *t = map_get_tile((Map *)map, r, c);

            if (!t || !t->buildable) {
                if (reason) strncpy(reason, "Tile not buildable", reason_len);
                return 0;
            }

            /* Fertility check for farms */
            if (def->placement_flags & PLACE_NEEDS_FERTILE) {
                if (t->fertility == FERTILE_NONE) {
                    if (reason) strncpy(reason, "Soil not fertile", reason_len);
                    return 0;
                }
            }

            /* Hop Farm needs FERTILE_HOP specifically, not just any
             * fertility bit — separate from PLACE_NEEDS_FERTILE above
             * so Farm's existing (loose) check is untouched. */
            if (def->placement_flags & PLACE_NEEDS_HOP_FERTILE) {
                if (!(t->fertility & FERTILE_HOP)) {
                    if (reason) strncpy(reason, "Needs hop-fertile soil", reason_len);
                    return 0;
                }
            }

            /* Occupied check — pass NULL for buildings when
             * called from the hover ghost (no buildings ptr).
             * We handle that by checking count == 0. */
        }
    }

    /* --- Pass 3: adjacency ------------------------------ */
    if (def->placement_flags & PLACE_NEEDS_COAST) {
        if (!footprint_has_adjacent(map, row, col,
                                    def->tile_w, def->tile_h,
                                    TILE_WATER)) {
            if (reason) strncpy(reason, "Needs water nearby", reason_len);
            return 0;
        }
    }

    if (def->placement_flags & PLACE_NEEDS_FOREST) {
        if (!footprint_has_adjacent(map, row, col,
                                    def->tile_w, def->tile_h,
                                    TILE_FOREST)) {
            if (reason) strncpy(reason, "Needs forest nearby", reason_len);
            return 0;
        }
    }

    return 1;   /* all checks passed */
}

/* =========================================================
 * building_place
 * ========================================================= */
int building_place(Building buildings[], int *count,
                   const Map *map,
                   BuildingType type, int row, int col)
{
    int i, slot;

    /* Check for tile overlap with existing buildings */
    {
        const BuildingDef *def = &BUILDING_DEFS[type];
        int r, c;
        for (r = row; r < row + def->tile_h; r++)
            for (c = col; c < col + def->tile_w; c++)
                if (tile_is_occupied(buildings, *count, r, c))
                    return -1;
    }

    if (!building_can_place(map, type, row, col, NULL, 0))
        return -1;

    /* Reuse a demolished building's slot before appending — without
     * this, repeated build/destroy cycles on the same spot would
     * burn through MAX_BUILDINGS even though the live count stays
     * low. (The cap only applies when no free slot exists, so
     * reusing one works even with *count already at MAX_BUILDINGS.) */
    slot = -1;
    for (i = 0; i < *count; i++) {
        if (!buildings[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        if (*count >= MAX_BUILDINGS) return -1;
        slot = (*count)++;
    }

    buildings[slot].type         = type;
    buildings[slot].row          = row;
    buildings[slot].col          = col;
    buildings[slot].active       = 1;
    buildings[slot].timer        = 0.0f;   /* CHANGED Phase 4: initialise timer */
    buildings[slot].connected    = 0;
    buildings[slot].worker_count = 0;

    return slot;
}

/* =========================================================
 * building_can_afford
 * ========================================================= */
int building_can_afford(const Stockpile *s, BuildingType type)
{
    const BuildingDef *def = &BUILDING_DEFS[type];
    int i;

    for (i = 0; i < RES_COUNT; i++)
        if (s->amount[i] < def->cost[i])
            return 0;

    return 1;
}

/* =========================================================
 * building_gold_equivalent_cost
 * ========================================================= */
int building_gold_equivalent_cost(BuildingType type)
{
    const BuildingDef *def = &BUILDING_DEFS[type];
    int i, total = def->cost[RES_GOLD];

    for (i = 0; i < RES_COUNT; i++)
        if (i != RES_GOLD)
            total += def->cost[i] * BUY_PRICE[i];

    return total;
}
