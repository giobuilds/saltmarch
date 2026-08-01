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
#include "command.h"  /* RejectReason (UI_PLAN Phase 0.5) */
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

    /* ---- SUPPLY_CHAIN Phase 3: the northern base economy ----
     * Seven chains feeding the two base tiers. Appended rather than
     * inserted, so every existing type keeps its value and only the
     * resource enum shifts under this phase's SAVE_VERSION bump. */
    BUILDING_SAWMILL      = 13,   /* Timber   -> Planks               */
    BUILDING_SHEEP_PASTURE = 14,  /* pasture  -> Wool                 */
    BUILDING_KNITTING_HOUSE = 15, /* Wool     -> Oilskins             */
    BUILDING_POTATO_FIELD = 16,   /* potato soil -> Potatoes          */
    BUILDING_STILL        = 17,   /* Potatoes -> Marsh Gin            */
    BUILDING_CLAY_PIT     = 18,   /* a clay deposit -> Clay           */
    BUILDING_BRICKWORKS   = 19,   /* Clay     -> Bricks               */
    BUILDING_PIG_PEN      = 20,   /* pasture  -> Pigs                 */
    BUILDING_BUTCHERY     = 21,   /* Pigs     -> Sausages             */
    BUILDING_TALLOW_WORKS = 22,   /* Pigs     -> Tallow               */
    BUILDING_SOAP_BOILERY = 23,   /* Tallow   -> Soap                 */
    BUILDING_WINDMILL     = 24,   /* Grain    -> Flour                */
    BUILDING_BAKEHOUSE    = 25,   /* Flour    -> Bread                */

    /* ---- SUPPLY_CHAIN Phase 4: iron, glass and the Artisans ----
     * Four chains and the tier they feed. Appended, like Phase 3's, so
     * every existing type keeps its value; only the resource enum
     * shifts under this phase's SAVE_VERSION bump. */
    BUILDING_CHARCOAL_KILN  = 26, /* Wood            -> Charcoal      */
    BUILDING_IRON_MINE      = 27, /* an iron deposit -> Iron Ore      */
    BUILDING_COAL_MINE      = 28, /* a coal deposit  -> Coal          */
    BUILDING_BLOOMERY       = 29, /* Ore + Charcoal  -> Iron          */
    BUILDING_IRONWORKS      = 30, /* Iron            -> Steel Beams   */
    BUILDING_SAND_PIT       = 31, /* a sand deposit  -> Sand          */
    BUILDING_GLASSWORKS     = 32, /* Sand            -> Glass         */
    BUILDING_BRASS_FOUNDRY  = 33, /* Iron + Charcoal -> Brass         */
    BUILDING_WINDOW_SHOP    = 34, /* Glass + Planks  -> Windows       */
    BUILDING_SPECTACLE_SHOP = 35, /* Glass + Brass   -> Spectacles    */
    BUILDING_CATTLE_PEN     = 36, /* pasture         -> Cattle        */
    BUILDING_PEPPER_FIELD   = 37, /* pepper soil     -> Pepper        */
    BUILDING_KITCHEN        = 38, /* Cattle + Pepper -> Potted Meat   */
    BUILDING_CANNERY        = 39, /* Potted Meat     -> Preserves     */
    BUILDING_FOUNDRY        = 40, /* Iron + Coal     -> Steel         */
    BUILDING_MACHINE_SHOP   = 41, /* Steel + Planks  -> Sewing Mach.  */

    /* The first tier reached by UPGRADING rather than by building, so
     * like BUILDING_HOUSE_WORKER before Phase 3 it has no HUD slot:
     * the only way to one is game_upgrade_house on a Marsh Cottage. */
    BUILDING_HOUSE_ARTISAN  = 42,

    /* ---- SUPPLY_CHAIN Phase 5: the southern islands ----
     * The four that close Artisans' fifth need. Deferred out of Phase
     * 4 precisely because Cloth has no northern source, which is what
     * now makes a southern colony a prerequisite rather than an
     * ornament. */
    BUILDING_COTTON_FIELD   = 43, /* cotton soil     -> Cotton        */
    BUILDING_SPINNING_MILL  = 44, /* Cotton          -> Cloth         */
    BUILDING_TRAPPERS_LODGE = 45, /* beside forest   -> Pelts         */
    BUILDING_FURRIER        = 46, /* Pelts + Cloth   -> Fur Coats     */

    /* ---- SUPPLY_CHAIN Phase 6: Engineers ----
     * The Watchmaker's and the Gramophone Works are the first
     * three-input buildings in real content. */
    BUILDING_GOLD_MINE        = 47, /* a gold seam    -> Gold Ore      */
    BUILDING_WIRE_MILL        = 48, /* Iron           -> Wire          */
    BUILDING_SPRING_WORKS     = 49, /* Iron           -> Springs       */
    BUILDING_LAMP_WORKS       = 50, /* Glass + Wire   -> Lamps         */
    BUILDING_WATCHMAKERS      = 51, /* Ore+Glass+Springs -> Watches    */
    BUILDING_LAC_GROVE        = 52, /* lac soil       -> Shellac       */
    BUILDING_GRAMOPHONE_WORKS = 53, /* Planks+Brass+Shellac -> Gram.   */
    BUILDING_LOBSTER_POTS     = 54, /* coast          -> Lobster       */
    BUILDING_FINE_KITCHEN     = 55, /* Lobster+Preserves -> Banquet    */
    BUILDING_HOUSE_ENGINEER   = 56, /* upgrade of a Wright's House     */

    /* ---- SUPPLY_CHAIN Phase 7: Merchants and Investors ---- */
    BUILDING_COFFEE_GROVE     = 57, /* jungle soil    -> Coffee Beans  */
    BUILDING_ROASTERY         = 58, /* Beans          -> Coffee        */
    BUILDING_CANE_FIELD       = 59, /* cane soil      -> Cane          */
    BUILDING_SUGAR_REFINERY   = 60, /* Cane           -> Sugar         */
    BUILDING_RUM_HOUSE        = 61, /* Sugar + Planks -> Rum           */
    BUILDING_MAIZE_FIELD      = 62, /* maize soil     -> Maize         */
    BUILDING_FLATBREAD_KITCHEN= 63, /* Maize + Cattle -> Flatbread     */
    BUILDING_ALPACA_PASTURE   = 64, /* alpaca grazing -> Alpaca Wool   */
    BUILDING_FELT_WORKS       = 65, /* Cotton + Alpaca-> Felt          */
    BUILDING_HATTER           = 66, /* Felt           -> Marsh Hats    */
    BUILDING_DARNING_HOUSE    = 67, /* Alpaca Wool    -> Wool Cloaks   */
    BUILDING_PLANTAIN_GROVE   = 68, /* plantain soil  -> Plantain      */
    BUILDING_FISH_OIL_RENDERY = 69, /* Fish           -> Fish Oil      */
    BUILDING_FRY_KITCHEN      = 70, /* Plantain + Oil -> Plantain Fry  */
    BUILDING_SAIL_LOFT        = 71, /* Cloth          -> Sails         */
    BUILDING_VINEYARD         = 72, /* grape soil     -> Grapes        */
    BUILDING_SPARKLING_CELLAR = 73, /* Grapes         -> Sparkling Wine*/
    BUILDING_TOBACCO_FIELD    = 74, /* tobacco soil   -> Tobacco       */
    BUILDING_CIGAR_HOUSE      = 75, /* Tobacco+Planks -> Cigars        */
    BUILDING_COCOA_GROVE      = 76, /* cocoa soil     -> Cocoa         */
    BUILDING_CHOCOLATE_HOUSE  = 77, /* Cocoa + Sugar  -> Chocolate     */
    BUILDING_PEARL_BEDS       = 78, /* beside a pearl bed -> Pearls    */
    BUILDING_JEWELLER         = 79, /* Gold Ore+Pearls-> Jewellery     */
    BUILDING_FLOWER_FIELD     = 80, /* flower soil    -> Flowers       */
    BUILDING_PERFUMERY        = 81, /* Flowers + Gin  -> Perfume       */
    BUILDING_HOUSE_MERCHANT   = 82, /* the third line's base           */
    BUILDING_HOUSE_INVESTOR   = 83, /* upgrade of a Merchant House     */

    /* ---- SUPPLY_CHAIN Phase 8: the Academy and Scholars ----
     * The Academy is a PREREQUISITE, not an action: it converts
     * nobody by itself, and demolishing it demotes nobody. While one
     * stands, road-connected, every house on the island gains a
     * second possible future. */
    BUILDING_INK_WORKS        = 84, /* Shellac        -> Ink           */
    BUILDING_PAPER_MILL       = 85, /* Wood           -> Paper         */
    BUILDING_BINDERY          = 86, /* Ink + Paper    -> Books         */
    BUILDING_CHART_HOUSE      = 87, /* Paper + Glass  -> Charts        */
    BUILDING_ACADEMY          = 88, /* the Scholars prerequisite       */
    BUILDING_HOUSE_SCHOLAR    = 89, /* reachable from ANY house tier   */

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
    /* PLACE_NEEDS_HOP_FERTILE lived here. It existed only because
     * there was no way for a def to name the crop it wanted; there is
     * now (needs_fertility below), and a flag per crop would have
     * meant fourteen of them. */
} PlacementFlags;

/* Production inputs a building can consume per tick. Raised from 2 in
 * SUPPLY_CHAIN Phase 2: the Watchmaker's takes Gold Ore + Glass +
 * Springs and the Gramophone Works takes Planks + Brass + Shellac.
 *
 * NOTE for anyone adding a def: RES_WOOD is 0, so a `consumes` slot
 * left out of an initialiser reads as Wood rather than as "unused".
 * Write all MAX_BUILDING_INPUTS slots explicitly; tests/test_defs.c
 * asserts every unused one is RES_COUNT. */
#define MAX_BUILDING_INPUTS 3

/* ---- Static definition of one building type ------------ */
/* ---- Building categories (UI_PLAN Phase 2) ---------------
 * What KIND of thing this is, for grouping in the interface: the HUD's
 * category tabs (Phase 3) and any list long enough to want sections.
 *
 * Deliberately about function rather than era or cost, because that is
 * the question a player asks the HUD ("where do I get planks?"), and
 * because it stays answerable as buildings are added.
 *
 * BCAT_NONE is 0 so a row that forgets to declare one is not silently
 * filed under a real category — tests/test_defs.c asserts no def is
 * left at NONE. */
/* Widened from five to seven in SUPPLY_CHAIN Phase 2, before the
 * content that needs it arrives. Gathering split into what you grow
 * and what you dig, Production into what a workshop makes and what a
 * factory does — at roughly 21 slots per tab and ~60 buildings coming,
 * five categories would have overflowed and the split would have had
 * to happen mid-content, renumbering tabs a player had learned. */
typedef enum {
    BCAT_NONE = 0,
    BCAT_FARMING,        /* grown or caught: fields, pastures, boats  */
    BCAT_EXTRACTION,     /* dug or felled: mines, pits, the forest    */
    BCAT_WORKSHOP,       /* one artisan's worth of processing         */
    BCAT_FACTORY,        /* heavy industry: furnaces, machine shops   */
    /* Split out of Workshops in SUPPLY_CHAIN Phase 7, which would
     * otherwise have put thirty buildings on a bar that shows
     * twenty-one. The line is what a building is FOR, not how big it
     * is: a Refinery turns one raw good into bulk stock that other
     * buildings consume; a Luxury workshop makes a finished thing a
     * household asks for and nothing else uses. */
    BCAT_REFINERY,       /* raw good -> bulk stock for other buildings */
    BCAT_LUXURY,         /* the finished things the upper tiers want   */
    BCAT_HOUSING,        /* where residents live                      */
    BCAT_INFRASTRUCTURE, /* roads, storage, the market                */
    BCAT_MARITIME,       /* everything about ships and other players  */
    BCAT_COUNT
} BuildingCategory;

/* Display name for a category ("Gathering"). Never NULL. */
const char *building_category_name(BuildingCategory c);

typedef struct {
    const char   *name;
    BuildingCategory category;     /* UI grouping; see above    */
    int           tile_w;          /* footprint width  in tiles */
    int           tile_h;          /* footprint height in tiles */
    PlacementFlags placement_flags;

    /* Terrain this type needs under EVERY tile of its footprint, on
     * top of placement_flags (SUPPLY_CHAIN Phase 1):
     *
     * needs_fertility – a mask of Fertility bits; the tile must have
     *                   all of them. 0 means "no crop requirement".
     *                   Distinct from PLACE_NEEDS_FERTILE, which asks
     *                   only that the soil grow *something*.
     * needs_deposit   – a Deposit every tile must hold. DEPOSIT_NONE
     *                   means "no mineral requirement".
     * needs_adjacent_deposit
     *                 – a Deposit at least one tile ALONGSIDE the
     *                   footprint must hold. For the deposit you work
     *                   rather than stand on: pearl beds lie in
     *                   shallow water, where nothing can be built, so
     *                   the station stands on the shore beside them.
     *                   Same shape as PLACE_NEEDS_COAST, which is the
     *                   Fisher's Hut standing on land next to the sea
     *                   it fishes.
     *
     * Fields rather than flags because the answer is which crop, not
     * whether: a Potato Field and a Hop Farm differ by one constant
     * here and by nothing in the validator. */
    uint32_t      needs_fertility;
    uint8_t       needs_deposit;
    uint8_t       needs_adjacent_deposit;

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

/* Why this building cannot go here — REJ_OK if it can (UI_PLAN Phase
 * 0.5). This replaces the old (char *reason, size_t) out-parameter,
 * which every caller in the codebase passed NULL for: the message was
 * written, never read, and the strings could not be tested. A returned
 * enum is checkable headlessly, survives into the sim's rejection
 * vocabulary (command.h), and leaves the wording to the UI.
 *
 * Checks the map only — bounds, terrain, adjacency. Affordability
 * (building_can_afford) and occupancy (building_place) are separate
 * questions asked by separate callers. */
RejectReason building_place_check(const Map *map,
                                  BuildingType type,
                                  int row, int col);

/* The same check against an arbitrary def rather than a table row.
 * building_place_check() is a one-line wrapper over this.
 *
 * Split out in SUPPLY_CHAIN Phase 1 so the terrain rules can be tested
 * against a def written by the test, instead of only against whatever
 * buildings happen to exist. Phase 1 adds terrain and no content, so
 * without this seam the crop and deposit rules would have had to wait
 * for a building to want them before anything could prove they work —
 * and the def table is about to quadruple, which is exactly when you
 * want the rules pinned down independently of it. */
RejectReason building_place_check_def(const Map *map,
                                      const BuildingDef *def,
                                      int row, int col);

/* The boolean form, for the many call sites that only branch on it.
 * Deliberately kept rather than making everyone write
 * `== REJ_OK`: REJ_OK is 0, so a mechanical conversion of
 * `if (building_can_place(...))` to the enum would have inverted every
 * one of those conditions silently. */
int building_can_place(const Map *map,
                       BuildingType type,
                       int row, int col);

/* How many workers `def` can hold at once, or 0 for a building that
 * employs nobody (LIFE_PLAN Phase 1).
 *
 * Until this existed a building's labour was a light switch:
 * agents_assign_jobs() let exactly one agent claim each workplace, so
 * Building.worker_count was 0 or 1 and production was a step function
 * of labour — nothing at zero, full rate at one, and identical at six.
 * The gap between 0 and 1 was infinite and everything above it flat.
 *
 * DERIVED FROM THE CATEGORY, NOT STORED PER DEF. The categories already
 * say how big a thing is — BCAT_WORKSHOP is documented as "one artisan's
 * worth of processing" and BCAT_FACTORY as heavy industry — so a table
 * here states the rule once instead of scattering ninety numbers no one
 * can compare across the def rows. Footprint deliberately does NOT enter
 * it: only 1x1 and 2x2 exist, and a fishing crew is bigger than the hut
 * it lands its catch at.
 *
 * A producing def whose category names no crew falls back to 1, which is
 * exactly today's behaviour rather than a building that suddenly employs
 * nobody. tests/test_defs.c asserts none actually take that path.
 *
 * Takes a def rather than a type for the same reason
 * building_place_check_def() does: a test can drive the rule with a def
 * it wrote itself, instead of only through whatever buildings happen to
 * exist. */
int building_worker_cap(const BuildingDef *def);

/* The first input slot `def` cannot pay for out of `s`, or -1 when it
 * can run. All-or-nothing: a building ticks only when every
 * non-RES_COUNT slot has enough, so nothing half-consumes one input
 * while short of another.
 *
 * Lives here rather than inside island_tick_buildings() so a test can
 * drive it with a def of its own — MAX_BUILDING_INPUTS went to 3 in
 * SUPPLY_CHAIN Phase 2 and no building uses the third slot until
 * Phase 6, which would otherwise leave the widening unproven. */
int building_missing_input(const BuildingDef *def, const Stockpile *s);

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
