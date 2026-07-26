#ifndef RESOURCE_H
#define RESOURCE_H

/* =========================================================
 * resource.h  --  Resource types and stockpile  (Phase 4)
 *
 * A Stockpile holds one integer count per ResourceType.
 * All buildings read and write the single global Stockpile
 * that lives in GameState.
 *
 * GOLD is special: it is a currency, not a physical good.
 * In Phase 4 it simply accumulates (no spending yet).
 * Spending mechanics arrive in Phase 5 with population needs.
 * ========================================================= */

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

    /* ---- SUPPLY_CHAIN Phase 3: the northern base tiers ----
     * Seven chains, each a raw good and what is made from it. They are
     * grouped by chain rather than by raw/refined so a reader can see
     * the pairs; RESOURCE_CATEGORIES says which is which.
     *
     * Inserting these before RES_GOLD shifts its value, which is why
     * this phase bumps SAVE_VERSION — a log recorded before the change
     * would replay as different commands. Every table indexed by this
     * enum is designated, so none of them silently misalign. */
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

    /* ---- SUPPLY_CHAIN Phase 4: iron, glass and the Artisans ----
     * The first tier reached by UPGRADING rather than by building, so
     * these are the first goods whose whole purpose is to promote a
     * neighbourhood rather than to keep one alive.
     *
     * Four chains, each deeper than anything in Phase 3: ore and
     * charcoal become iron before iron becomes anything else, so the
     * Bloomery is the first building whose inputs are BOTH themselves
     * manufactured. Grouped by chain, raw good first. */
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

    RES_GOLD,
    RES_COUNT          /* always last */
} ResourceType;

/* Human-readable name for each resource (for debug / future UI). */
extern const char *RESOURCE_NAMES[RES_COUNT];

/* ---- Resource categories (UI_PLAN Phase 2) ----------------
 * What KIND of good this is, for grouping in lists long enough to want
 * sections — the exchange screen today, the inventory overlay later.
 *
 * The split is by position in the production chain, which is what makes
 * a market list readable: what you dig up, what you make out of it, and
 * the money. RCAT_NONE is 0 so a resource added without a category is
 * caught by tests/test_defs.c rather than quietly filed under raw. */
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

/* Baseline buy price per unit — the faction's ask at baseline inventory
 * (faction_ask() moves it elastically from here). Deliberately pricier
 * than SELL_PRICE: a convenience markup (the market spread), so
 * gathering resources normally stays cheaper than buying around them.
 * This is the escape hatch for an island generated with no forest at
 * all: with no Lumberjack possible, Wood income may never exist, so
 * anything costing Wood (Warehouse, House, Marketplace) needs a
 * Gold-only path. RES_GOLD's slot is unused. */
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
 * Clamps to zero on the low end — stock never goes negative.
 * For every resource except RES_GOLD (a currency, not a physical
 * good — see the design note above ResourceType), also clamps to
 * s->capacity on the high end. */
void stockpile_add(Stockpile *s, ResourceType res, int delta);

/* Set the storage cap applied to every non-gold resource.
 * Called by the game layer whenever the number of built
 * Warehouses changes. Existing amounts above the new cap are
 * clamped down immediately. */
void stockpile_set_capacity(Stockpile *s, int capacity);

#endif /* RESOURCE_H */
