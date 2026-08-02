#ifndef RESOURCE_H
#define RESOURCE_H

/* resource.h  --  Resource types and stockpile  (Phase 4) */

/* ---- Resource types ------------------------------------
 * RES_GOLD stays last on purpose: exchange_view.c's row loop
 * (and its "Gold is excluded, conveniently the last slot" comment)
 * depends on every tradeable good being inserted before it. */
typedef enum {
    RES_WOOD  = 0,
    RES_FISH  = 1,
    RES_GRAIN = 2,
    /* Production chains, Phase 1 (Beer): Hop Farm -> Malthouse
     * (Grain + Hops) -> Brewery. */
    RES_HOPS  = 3,
    RES_MALT  = 4,
    RES_BEER  = 5,

    /* ---- SUPPLY_CHAIN Phase 3: the northern base tiers ---- */
    RES_PLANKS,     /* Timber   -> Sawmill                            */
    RES_WOOL,       /* Sheep Pasture                                  */
    RES_OILSKINS,   /* Wool     -> Knitting House   (Marshfolk want)  */
    RES_POTATOES,   /* Potato Field                                   */
    RES_MARSH_GIN,  /* Potatoes -> Still            (Marshfolk want)  */
    RES_CLAY,       /* Clay Pit  (a deposit)                          */
    RES_BRICKS,     /* Clay     -> Brickworks                         */
    RES_PIGS,       /* Pig Pen                                        */
    RES_SAUSAGES,   /* Pigs     -> Butchery         (Wrights want)    */
    RES_TALLOW,     /* Pigs     -> Tallow Works                       */
    RES_SOAP,       /* Tallow   -> Soap Boilery     (Wrights want)    */
    RES_FLOUR,      /* Grain    -> Windmill                           */
    RES_BREAD,      /* Flour    -> Bakehouse        (Wrights want)    */

    /* ---- SUPPLY_CHAIN Phase 4: iron, glass and the Artisans ---- */
    RES_CHARCOAL,        /* Wood            -> Charcoal Kiln          */
    RES_IRON_ORE,        /* Iron Mine        (a deposit)              */
    RES_COAL,            /* Coal Mine        (a deposit)              */
    RES_IRON,            /* Ore + Charcoal  -> Bloomery               */
    RES_STEEL_BEAMS,     /* Iron            -> Ironworks   (build mat)*/
    RES_SAND,            /* Sand Pit         (a deposit)              */
    RES_GLASS,           /* Sand            -> Glassworks             */
    RES_BRASS,           /* Iron + Charcoal -> Brass Foundry          */
    RES_WINDOWS,         /* Glass + Planks  -> Window Shop  (Artisans)*/
    RES_SPECTACLES,      /* Glass + Brass   -> Spectacle Shop (Artis.)*/
    RES_CATTLE,          /* Cattle Pen       (pasture)                */
    RES_PEPPER,          /* Pepper Field     (FERTILE_PEPPER)         */
    RES_POTTED_MEAT,     /* Cattle + Pepper -> Kitchen                */
    RES_PRESERVES,       /* Potted Meat     -> Cannery      (Artisans)*/
    RES_STEEL,           /* Iron + Coal     -> Foundry                */
    RES_SEWING_MACHINES, /* Steel + Planks  -> Machine Shop (Artisans)*/

    /* ---- SUPPLY_CHAIN Phase 5: the southern islands ---- */
    RES_COTTON,     /* Cotton Field    (southern only)                */
    RES_CLOTH,      /* Cotton         -> Spinning Mill                */
    RES_PELTS,      /* Trapper's Lodge (northern forest)              */
    RES_FUR_COATS,  /* Pelts + Cloth  -> Furrier       (Artisans)     */

    /* ---- SUPPLY_CHAIN Phase 6: Engineers ---- */
    RES_GOLD_ORE,       /* Gold Mine        (highland deposit)         */
    RES_WIRE,           /* Iron            -> Wire Mill                */
    RES_SPRINGS,        /* Iron            -> Spring Works             */
    RES_LAMPS,          /* Glass + Wire    -> Lamp Works    (Engineers)*/
    RES_POCKET_WATCHES, /* Ore+Glass+Springs -> Watchmaker's (Eng.)    */
    RES_SHELLAC,        /* Lac Grove        (jungle crop)              */
    RES_GRAMOPHONES,    /* Planks+Brass+Shellac -> Gramophone Works    */
    RES_LOBSTER,        /* Lobster Pots     (coast)                    */
    RES_BANQUET,        /* Lobster + Preserves -> Fine Kitchen (Eng.)  */

    /* ---- SUPPLY_CHAIN Phase 7: Merchants and Investors ---- */
    RES_COFFEE_BEANS,   /* Coffee Grove      (jungle)                  */
    RES_COFFEE,         /* Beans          -> Roastery      (Merchants) */
    RES_CANE,           /* Cane Field        (plantation)              */
    RES_SUGAR,          /* Cane           -> Sugar Refinery            */
    RES_RUM,            /* Sugar + Planks -> Rum House     (Merchants) */
    RES_MAIZE,          /* Maize Field       (plantation)              */
    RES_FLATBREAD,      /* Maize + Cattle -> Flatbread Kit. (Merchants)*/
    RES_ALPACA_WOOL,    /* Alpaca Pasture    (southern grazing)        */
    RES_FELT,           /* Cotton + Alpaca -> Felt Works               */
    RES_MARSH_HATS,     /* Felt           -> Hatter        (Merchants) */
    RES_WOOL_CLOAKS,    /* Alpaca Wool    -> Darning House (Merchants) */
    RES_PLANTAIN,       /* Plantain Grove    (jungle)                  */
    RES_FISH_OIL,       /* Fish           -> Fish Oil Rendery          */
    RES_PLANTAIN_FRY,   /* Plantain+Oil   -> Fry Kitchen   (Merchants) */
    RES_SAILS,          /* Cloth          -> Sail Loft   (Shipyard cost)*/
    RES_GRAPES,         /* Vineyard          (highland)                */
    RES_SPARKLING_WINE, /* Grapes         -> Sparkling Cellar (Invest.)*/
    RES_TOBACCO,        /* Tobacco Field     (plantation)              */
    RES_CIGARS,         /* Tobacco+Planks -> Cigar House   (Investors) */
    RES_COCOA,          /* Cocoa Grove       (jungle)                  */
    RES_CHOCOLATE,      /* Cocoa + Sugar  -> Chocolate Hse (Investors) */
    RES_PEARLS,         /* Pearl Beds        (atoll deposit)           */
    RES_JEWELLERY,      /* Gold Ore+Pearls -> Jeweller     (Investors) */
    RES_FLOWERS,        /* Flower Field      (temperate)               */
    RES_PERFUME,        /* Flowers+Gin    -> Perfumery     (Investors) */

    /* ---- SUPPLY_CHAIN Phase 8: the Academy and Scholars ---- */
    RES_INK,       /* Shellac        -> Ink Works                      */
    RES_PAPER,     /* Wood           -> Paper Mill                     */
    RES_BOOKS,     /* Ink + Paper    -> Bindery       (Scholars)       */
    RES_CHARTS,    /* Paper + Glass  -> Chart House   (Scholars)       */

    RES_GOLD,
    RES_COUNT          /* always last */
} ResourceType;

/* Human-readable name for each resource (for debug / future UI). */
extern const char *RESOURCE_NAMES[RES_COUNT];

/* ---- Resource categories (UI_PLAN Phase 2) ----------------
 * What KIND of good this is, for grouping in lists long enough to want
 * sections — the exchange screen today, the inventory overlay later. */
typedef enum {
    RCAT_NONE = 0,
    RCAT_RAW,        /* taken from the land or the sea               */
    RCAT_REFINED,    /* made from other goods                        */
    RCAT_CURRENCY,   /* Gold: the medium, not a good                 */
    RCAT_COUNT
} ResourceCategory;

extern const ResourceCategory RESOURCE_CATEGORIES[RES_COUNT];

/* Display name for a category ("Raw goods"). Never NULL. */
const char *resource_category_name(ResourceCategory c);

/* Baseline sell price per unit. As of Phase 3 these are no longer the
 * live prices: they are the faction's quotes at baseline inventory
 * (faction.h), from which faction_bid() moves elastically with supply.
 * RES_GOLD's slot is unused (you can't sell currency for itself). */
extern const int SELL_PRICE[RES_COUNT];

/* Baseline buy price per unit — the faction's ask at baseline inventory */
extern const int BUY_PRICE[RES_COUNT];

/* Per-resource storage cap before any Warehouse is built.
 * See building.h's WAREHOUSE_STORAGE_BONUS for how building one
 * raises this. Gold is exempt (see stockpile_add). */
#define BASE_STORAGE_CAP 100

/* ---- Stockpile ----------------------------------------- */
typedef struct {
    int amount[RES_COUNT];   /* current count per resource   */
    int capacity;            /* cap applied to amount[] (except GOLD) */
} Stockpile;

/* Initialise all amounts to zero and capacity to BASE_STORAGE_CAP. */
void stockpile_init(Stockpile *s);

/* Add `delta` units of `res` to the stockpile.
 * delta may be negative (consumption).
 * Clamps to zero on the low end — stock never goes negative. */
void stockpile_add(Stockpile *s, ResourceType res, int delta);

/* Set the storage cap applied to every non-gold resource.
 * Called by the game layer whenever the number of built
 * Warehouses changes. Existing amounts above the new cap are
 * clamped down immediately. */
void stockpile_set_capacity(Stockpile *s, int capacity);

#endif /* RESOURCE_H */
