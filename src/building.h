#ifndef BUILDING_H
#define BUILDING_H

/* building.h -- building types, their static definitions and placement.
 *
 * Split class/instance: BUILDING_DEFS[type] holds what is true of every
 * building of a kind (footprint, placement rules, colour, production);
 * Building holds one placed instance. A new type is a new enum value
 * plus a row in the table -- placement and rendering are generic over
 * it. */

#include "map.h"      /* Tile, TileType, Fertility, MAP_* */
#include "resource.h"
#include "faction.h"  /* Phase 3: elastic gold-equivalent pricing */
#include "command.h"  /* RejectReason (UI_PLAN Phase 0.5) */
#include <stddef.h>   /* size_t */

/* ---- How many buildings can be placed at once ---------- */
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

    /* MMO Phase 5: the harbor is the inter-player airlock. A FOREIGN */
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

    /* ---- SUPPLY_CHAIN Phase 5: the southern islands ---- */
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

    /* ---- SUPPLY_CHAIN Phase 8: the Academy and Scholars ---- */
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
 * Springs and the Gramophone Works takes Planks + Brass + Shellac. */
#define MAX_BUILDING_INPUTS 3

/* ---- Static definition of one building type ------------ */
/* ---- Building categories (UI_PLAN Phase 2) ---------------
 * What KIND of thing this is, for grouping in the interface: the HUD's
 * category tabs (Phase 3) and any list long enough to want sections. */
/* Widened from five to seven in SUPPLY_CHAIN Phase 2, before. */
typedef enum {
    BCAT_NONE = 0,
    BCAT_FARMING,        /* grown or caught: fields, pastures, boats  */
    BCAT_EXTRACTION,     /* dug or felled: mines, pits, the forest    */
    BCAT_WORKSHOP,       /* one artisan's worth of processing         */
    BCAT_FACTORY,        /* heavy industry: furnaces, machine shops   */
    /* Split out of Workshops in SUPPLY_CHAIN Phase 7, which. */
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
     * top of placement_flags (SUPPLY_CHAIN Phase 1): */
    uint32_t      needs_fertility;
    uint8_t       needs_deposit;
    uint8_t       needs_adjacent_deposit;

    /* Colour for the placeholder rectangle (R, G, B) */
    unsigned char col_r, col_g, col_b;

    /* CHANGED Phase 4: production fields. */
    ResourceType  produces;
    int           produce_amt;
    ResourceType  consumes[MAX_BUILDING_INPUTS];
    int           consume_amt[MAX_BUILDING_INPUTS];
    float         tick_seconds;

    /* One-time cost deducted from the stockpile when this
     * building is placed, indexed like Stockpile.amount[]. */
    int           cost[RES_COUNT];

    /* 1 if this type gets a HUD slot the player can select and place
     * directly; 0 if it's only ever reached some other way (currently
     * just BUILDING_HOUSE_WORKER, via upgrading a BUILDING_HOUSE). */
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

    /* Phase 1b: integer tick accumulator. Counts whole sim ticks. */
    uint32_t     timer;

    /* Phase 3: derived, not meaningfully persisted — recomputed */
    int          connected;

    /* Phase 5: derived like `connected` above — zeroed and retallied */
    int          worker_count;
} Building;

/* ---- Placement validation ----------------------------- */

/* Why this building cannot go here — REJ_OK if it can (UI_PLAN Phase */
RejectReason building_place_check(const Map *map,
                                  BuildingType type,
                                  int row, int col);

/* The same check against an arbitrary def rather than a table row.
 * building_place_check() is a one-line wrapper over this. */
RejectReason building_place_check_def(const Map *map,
                                      const BuildingDef *def,
                                      int row, int col);

/* The boolean form, for the many call sites that only branch on it. */
int building_can_place(const Map *map,
                       BuildingType type,
                       int row, int col);

/* How many workers `def` can hold at once, or 0 for a building that
 * employs nobody (LIFE_PLAN Phase 1). */
int building_worker_cap(const BuildingDef *def);

/* How far `workers` advance this building's production clock in one sim
 * tick — 1 per worker would be linear; this is deliberately more
 * (LIFE_PLAN Phase 2). */
int building_work_advance(const BuildingDef *def, int workers);

/* The first input slot `def` cannot pay for out of `s`, or -1 when. */
int building_missing_input(const BuildingDef *def, const Stockpile *s);

/* Returns 1 if `s` holds enough of every resource. */
int building_can_afford(const Stockpile *s, BuildingType type);

/* Total Gold cost to place `type` paying entirely in Gold:. */
int building_gold_equivalent_cost(BuildingType type, const Faction *f);

/* Place a building into the buildings array.
 * Returns the index of the new building, or -1 if the array
 * is full or placement is invalid. */
int building_place(Building buildings[], int *count,
                   const Map *map,
                   BuildingType type, int row, int col);

#endif /* BUILDING_H */
