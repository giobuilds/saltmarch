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
#include <stdio.h>    /* snprintf */
#include <stddef.h>   /* NULL     */

/* =========================================================
 * Building definition table
 * ========================================================= */
/* Every row is DESIGNATED by its enum value. This table was positional
 * until the Shipyard / Worker's House rows were found swapped relative
 * to the enum (BUILDING_DEFS[10] held the Shipyard def while type 10 is
 * BUILDING_HOUSE_WORKER) — the same silent-misalignment failure the
 * RES_COL table had. Designated rows make the compiler place each def
 * at its enum index no matter the order rows appear in, and
 * tests/test_defs.c asserts name<->enum agreement so a future row can't
 * regress this. */
/* Fields inside each row are designated too (UI_PLAN Phase 2), not just
 * the row indices. The rows were positional until this phase needed to
 * add `category`: appending a field to BuildingDef would have been safe,
 * but inserting one anywhere else would have silently shifted every
 * value after it in all thirteen rows — the same class of failure as the
 * swapped Shipyard row, one edit away. Naming the fields makes that
 * impossible and costs nothing at runtime. */
const BuildingDef BUILDING_DEFS[BUILDING_TYPE_COUNT] = {
    [BUILDING_FISHERS_HUT] = {
        .name = "Fisher's Hut",
        .category = BCAT_GATHERING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_NEEDS_COAST,
        .col_r = 210, .col_g = 180, .col_b = 100,
        .produces = RES_FISH, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 6.0f,
        .cost = { [RES_GOLD] = 60 },
        .hud_placeable = 1
    },
    [BUILDING_WAREHOUSE] = {
        .name = "Warehouse",
        .category = BCAT_INFRASTRUCTURE,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 160, .col_g = 100, .col_b = 60,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_WOOD] = 20, [RES_GOLD] = 150 },
        .hud_placeable = 1
    },
    [BUILDING_FARM] = {
        .name = "Farm",
        .category = BCAT_GATHERING,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_NEEDS_FERTILE,
        .col_r = 80, .col_g = 160, .col_b = 50,
        .produces = RES_GRAIN, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_GOLD] = 80 },
        .hud_placeable = 1
    },
    [BUILDING_LUMBERJACK] = {
        .name = "Lumberjack",
        .category = BCAT_GATHERING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_NEEDS_FOREST,
        .col_r = 120, .col_g = 80, .col_b = 40,
        .produces = RES_WOOD, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 5.0f,
        .cost = { [RES_GOLD] = 60 },
        .hud_placeable = 1
    },
    /* Phase 5: House — residents live here, generate gold when fed */
    [BUILDING_HOUSE] = {
        .name = "House",
        .category = BCAT_HOUSING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 210, .col_g = 190, .col_b = 160,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_WOOD] = 15, [RES_GOLD] = 80 },
        .hud_placeable = 1
    },
    /* Phase 2: Road — no production; PLACE_ANY_LAND is sufficient
     * to keep it off water/forest, since building_can_place already
     * requires tile->buildable, which those tile types never have.
     * Free: a real road network needs many tiles, and charging per
     * tile made drag-placing one needlessly punishing. */
    [BUILDING_ROAD] = {
        .name = "Road",
        .category = BCAT_INFRASTRUCTURE,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 110, .col_g = 105, .col_b = 100,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { 0 },
        .hud_placeable = 1
    },
    /* Phase 4: Marketplace — no passive production; it's a pure
     * gateway building. Clicking a placed, road-connected one opens
     * the manual trade screen (see trade_ui.c, game_sell_resource). */
    [BUILDING_MARKETPLACE] = {
        .name = "Marketplace",
        .category = BCAT_INFRASTRUCTURE,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 200, .col_g = 140, .col_b = 60,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_WOOD] = 30, [RES_GOLD] = 200 },
        .hud_placeable = 1
    },
    /* Production chains, Phase 1 (Beer). Hop Farm names FERTILE_HOP in
     * needs_fertility (map.h) — it was the first building to want a
     * specific crop and, until SUPPLY_CHAIN Phase 1, the reason there
     * was a whole placement flag for that one crop. Malthouse is the
     * multi-input building: both Grain and Hops must be in stock for it
     * to tick at all (all-or-nothing, see game_tick_buildings, game.c). */
    [BUILDING_HOP_FARM] = {
        .name = "Hop Farm",
        .category = BCAT_GATHERING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .needs_fertility = FERTILE_HOP,
        .col_r = 90, .col_g = 150, .col_b = 60,
        .produces = RES_HOPS, .produce_amt = 1,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_GOLD] = 80 },
        .hud_placeable = 1
    },
    [BUILDING_MALTHOUSE] = {
        .name = "Malthouse",
        .category = BCAT_PRODUCTION,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 170, .col_g = 140, .col_b = 90,
        .produces = RES_MALT, .produce_amt = 1,
        .consumes = { RES_GRAIN, RES_HOPS }, .consume_amt = { 1, 1 },
        .tick_seconds = 10.0f,
        .cost = { [RES_WOOD] = 20, [RES_GOLD] = 150 },
        .hud_placeable = 1
    },
    [BUILDING_BREWERY] = {
        .name = "Brewery",
        .category = BCAT_PRODUCTION,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 190, .col_g = 150, .col_b = 70,
        .produces = RES_BEER, .produce_amt = 1,
        .consumes = { RES_MALT, RES_COUNT }, .consume_amt = { 1, 0 },
        .tick_seconds = 8.0f,
        .cost = { [RES_WOOD] = 20, [RES_GOLD] = 150 },
        .hud_placeable = 1
    },
    /* Colonisation: a Shipyard has no production of its own — like the
     * Marketplace it is a gateway you click, here to lay down a ship.
     * PLACE_NEEDS_COAST for the obvious reason. */
    [BUILDING_SHIPYARD] = {
        .name = "Shipyard",
        .category = BCAT_MARITIME,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_NEEDS_COAST,
        .col_r = 130, .col_g = 120, .col_b = 160,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_WOOD] = 40, [RES_GOLD] = 250 },
        .hud_placeable = 1
    },
    /* Population tiers, Phase 1: Worker's House is reached only by
     * upgrading a placed BUILDING_HOUSE (game_upgrade_house, game.c),
     * never placed directly — hud_placeable = 0 keeps it off the HUD
     * bar (see ui.c's filtered slot list). cost[] is irrelevant since
     * building_place() is never called for this type; the upgrade's
     * Gold cost lives in game_upgrade_house() instead. */
    [BUILDING_HOUSE_WORKER] = {
        .name = "Worker's House",
        .category = BCAT_HOUSING,
        .tile_w = 1, .tile_h = 1,
        .placement_flags = PLACE_ANY_LAND,
        .col_r = 230, .col_g = 200, .col_b = 140,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { 0 },
        .hud_placeable = 0
    },
    /* MMO Phase 5: the inter-player airlock (see building.h). No
     * production — like Marketplace/Shipyard it is a gateway you click,
     * here to open the escrow panel. */
    [BUILDING_HARBOR] = {
        .name = "Harbor",
        .category = BCAT_MARITIME,
        .tile_w = 2, .tile_h = 2,
        .placement_flags = PLACE_NEEDS_COAST,
        .col_r = 90, .col_g = 130, .col_b = 170,
        .produces = RES_COUNT, .produce_amt = 0,
        .consumes = { RES_COUNT, RES_COUNT }, .consume_amt = { 0, 0 },
        .tick_seconds = 0.0f,
        .cost = { [RES_WOOD] = 30, [RES_GOLD] = 200 },
        .hud_placeable = 1
    },
};

const char *building_category_name(BuildingCategory c)
{
    /* Designated, like everything else indexed by an enum here. */
    static const char *const NAMES[BCAT_COUNT] = {
        [BCAT_NONE]           = "Other",
        [BCAT_GATHERING]      = "Gathering",
        [BCAT_PRODUCTION]     = "Production",
        [BCAT_HOUSING]        = "Housing",
        [BCAT_INFRASTRUCTURE] = "Infrastructure",
        [BCAT_MARITIME]       = "Maritime"
    };
    if (c < 0 || c >= BCAT_COUNT || !NAMES[c]) return "Other";
    return NAMES[c];
}

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
 * Helper: does the footprint have an adjacent tile carrying `dep`?
 * Same cardinal-neighbour sweep as footprint_has_adjacent below, but
 * asking about what is IN the tile rather than what it is — for a
 * building that works a deposit it cannot stand on (pearl beds in
 * shallow water). Kept separate rather than generalised into one
 * predicate-taking sweep: two four-line loops read better than one
 * loop plus a callback, at this size.
 * ========================================================= */
static int footprint_has_adjacent_deposit(const Map *map,
                                          int row, int col,
                                          int fw, int fh,
                                          uint8_t dep)
{
    static const int dr[4] = { -1, 1,  0, 0 };
    static const int dc[4] = {  0, 0,  1,-1 };

    int r, c, d;
    for (r = row; r < row + fh; r++)
        for (c = col; c < col + fw; c++)
            for (d = 0; d < 4; d++) {
                const Tile *nb = map_get_tile(
                    (Map *)map, r + dr[d], c + dc[d]);
                if (nb && nb->deposit == dep) return 1;
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
RejectReason building_place_check(const Map *map,
                                  BuildingType type,
                                  int row, int col)
{
    return building_place_check_def(map, &BUILDING_DEFS[type], row, col);
}

RejectReason building_place_check_def(const Map *map,
                                      const BuildingDef *def,
                                      int row, int col)
{
    int r, c;

    /* --- Pass 1: bounds --------------------------------- */
    if (row < 0 || col < 0 ||
        row + def->tile_h > map->rows ||
        col + def->tile_w > map->cols) {
        return REJ_OUT_OF_BOUNDS;
    }

    /* --- Pass 2: per-tile checks ------------------------ */
    for (r = row; r < row + def->tile_h; r++) {
        for (c = col; c < col + def->tile_w; c++) {
            const Tile *t = map_get_tile((Map *)map, r, c);

            if (!t || !t->buildable) {
                return REJ_NOT_BUILDABLE;
            }

            /* Fertility check for farms: does this soil grow anything? */
            if (def->placement_flags & PLACE_NEEDS_FERTILE) {
                if (t->fertility == FERTILE_NONE) {
                    return REJ_NEEDS_FERTILE;
                }
            }

            /* ...and does it grow THIS? Separate from the loose check
             * above so Farm's "any fertile ground" behaviour is
             * untouched by a def naming a crop. */
            if (def->needs_fertility &&
                (t->fertility & def->needs_fertility) != def->needs_fertility) {
                return REJ_NEEDS_CROP;
            }

            if (def->needs_deposit != DEPOSIT_NONE &&
                t->deposit != def->needs_deposit) {
                return REJ_NEEDS_DEPOSIT;
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
            return REJ_NEEDS_COAST;
        }
    }

    if (def->placement_flags & PLACE_NEEDS_FOREST) {
        if (!footprint_has_adjacent(map, row, col,
                                    def->tile_w, def->tile_h,
                                    TILE_FOREST)) {
            return REJ_NEEDS_FOREST;
        }
    }

    /* The deposit you work from beside rather than stand on. Same
     * refusal as the under-footprint case: "nothing to work here" is
     * the same sentence either way, and the player knows which
     * building they are holding — the argument that collapsed fourteen
     * crops into one REJ_NEEDS_CROP. */
    if (def->needs_adjacent_deposit != DEPOSIT_NONE) {
        if (!footprint_has_adjacent_deposit(map, row, col,
                                            def->tile_w, def->tile_h,
                                            def->needs_adjacent_deposit)) {
            return REJ_NEEDS_DEPOSIT;
        }
    }

    return REJ_OK;   /* all checks passed */
}

int building_can_place(const Map *map, BuildingType type, int row, int col)
{
    return building_place_check(map, type, row, col) == REJ_OK;
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

    if (!building_can_place(map, type, row, col))
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
    buildings[slot].timer        = 0;   /* integer tick accumulator (1b) */
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
int building_gold_equivalent_cost(BuildingType type, const Faction *f)
{
    const BuildingDef *def = &BUILDING_DEFS[type];
    int i, total = def->cost[RES_GOLD];

    for (i = 0; i < RES_COUNT; i++)
        if (i != RES_GOLD)
            total += def->cost[i] * faction_ask(f, (ResourceType)i);

    return total;
}
