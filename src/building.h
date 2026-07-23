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
#include "faction.h"  /* Phase 3: elastic gold-equivalent pricing */
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
    BUILDING_MARKETPLACE =  6,   /* Phase 4: manual trade screen */

    /* Production chains, Phase 1 (Beer): a 3-stage chain proving out
     * multi-input production (see BuildingDef.consumes[] below) and
     * finally giving FERTILE_HOP (map.h) a consumer. */
    BUILDING_HOP_FARM    =  7,   /* raw producer: Hops, needs FERTILE_HOP */
    BUILDING_MALTHOUSE   =  8,   /* Grain + Hops -> Malt (multi-input) */
    BUILDING_BREWERY     =  9,   /* Malt -> Beer */

    /* Population tiers, Phase 1: reached only by upgrading an existing
     * BUILDING_HOUSE (game_upgrade_house, game.c) — never placed
     * directly from the HUD (see hud_placeable below). */
    BUILDING_HOUSE_WORKER = 10,

    /* Colonisation: the only source of ships, and therefore the only
     * way to reach another island. Coastal by necessity. */
    BUILDING_SHIPYARD     = 11,

    /* MMO Phase 5: the harbor is the inter-player airlock. A FOREIGN
     * player's ship may transfer goods at this island only if an active
     * Harbor stands here (and docking is allowed) — and only into/out of
     * the island's escrow, never its stockpile. Coastal like the
     * Shipyard. Clicking a placed one opens the escrow panel. */
    BUILDING_HARBOR       = 12,

    BUILDING_TYPE_COUNT
} BuildingType;

/* ---- Placement rule flags (bitmask) --------------------
 * Stored in BuildingDef.placement_flags.
 * The placement validator checks these against the map. */
typedef enum {
    PLACE_ANY_LAND        = 0,        /* no extra constraint      */
    PLACE_NEEDS_COAST     = 1 << 0,   /* adjacent water required  */
    PLACE_NEEDS_FOREST    = 1 << 1,   /* adjacent forest required */
    PLACE_NEEDS_FERTILE   = 1 << 2,   /* any fertility bit set    */
    /* Specifically FERTILE_HOP, not just "any fertility" — kept
     * separate from PLACE_NEEDS_FERTILE above so Farm's existing
     * (loose) behavior is untouched by adding this. */
    PLACE_NEEDS_HOP_FERTILE = 1 << 3,
} PlacementFlags;

/* Production inputs a building can consume per tick. 2 covers every
 * chain currently planned (nothing needs 3 simultaneous raw inputs);
 * raise if a future chain genuinely needs more. */
#define MAX_BUILDING_INPUTS 2

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
     *                 (RES_COUNT means "produces nothing"). Stays a
     *                 single output — no planned chain needs 2.
     * produce_amt   – units produced per tick
     * consumes      – up to MAX_BUILDING_INPUTS inputs required per
     *                 tick (RES_COUNT in a slot means "unused" — same
     *                 sentinel convention as produces, just per-slot).
     *                 All-or-nothing: a tick only fires if every
     *                 non-RES_COUNT slot has enough stock.
     * consume_amt   – units consumed per tick, parallel to consumes[]
     * tick_seconds  – real-time seconds between production ticks */
    ResourceType  produces;
    int           produce_amt;
    ResourceType  consumes[MAX_BUILDING_INPUTS];
    int           consume_amt[MAX_BUILDING_INPUTS];
    float         tick_seconds;

    /* One-time cost deducted from the stockpile when this
     * building is placed, indexed like Stockpile.amount[].
     * Unlike produces/consumes (a per-tick flow), this is a
     * lump sum paid once at building_place() time. Irrelevant for a
     * building that's never placed directly (hud_placeable == 0) —
     * see BUILDING_HOUSE_WORKER, reached only via game_upgrade_house(). */
    int           cost[RES_COUNT];

    /* 1 if this type gets a HUD slot the player can select and place
     * directly; 0 if it's only ever reached some other way (currently
     * just BUILDING_HOUSE_WORKER, via upgrading a BUILDING_HOUSE).
     * ui.c's HUD loop filters on this rather than assuming every
     * BuildingType maps 1:1 to a placeable slot. */
    int           hud_placeable;
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

    /* Phase 1b: integer tick accumulator. Counts whole sim ticks since
     * this building last produced; when timer reaches the building's
     * period (tick_seconds * SIM_TICKS_PER_SEC) a tick fires and it
     * resets to 0. Integer, not float, so the F9 desync hash never reads
     * an accumulating float. */
    uint32_t     timer;

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

    /* Phase 5: derived like `connected` above — zeroed and retallied
     * every frame by agents_update() (agent.c) from currently
     * AGENT_WORKING agents. game_tick_buildings() requires
     * worker_count >= 1 (for any building with tick_seconds > 0) on
     * top of `connected`: the "physically present" labor-supply gate. */
    int          worker_count;
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

/* Total Gold cost to place `type` paying entirely in Gold: the
 * building's own Gold cost plus every other resource's cost[] amount
 * converted at the faction's current ask (Phase 3 — the elastic market
 * price, so the gold-pay option tracks what those goods actually cost
 * right now). For a Gold-only building type (nothing to convert) this
 * equals its existing Gold cost exactly. Used by the build-confirmation
 * popup's "pay Gold" option. Pricing only — it does not move faction
 * state; paying Gold for a building is a sink, like paying in resources. */
int building_gold_equivalent_cost(BuildingType type, const Faction *f);

/* Place a building into the buildings array.
 * Returns the index of the new building, or -1 if the array
 * is full or placement is invalid. */
int building_place(Building buildings[], int *count,
                   const Map *map,
                   BuildingType type, int row, int col);

#endif /* BUILDING_H */
