/*  resource.c  --  Resource stockpile implementation  (Phase 4)  */

#include "resource.h"

const char *RESOURCE_NAMES[RES_COUNT] = {
    [RES_WOOD]  = "Wood",
    [RES_FISH]  = "Fish",
    [RES_GRAIN] = "Grain",
    [RES_HOPS]  = "Hops",
    [RES_MALT]  = "Malt",
    [RES_BEER]  = "Beer",

    [RES_PLANKS]    = "Planks",
    [RES_WOOL]      = "Wool",
    [RES_OILSKINS]  = "Oilskins",
    [RES_POTATOES]  = "Potatoes",
    [RES_MARSH_GIN] = "Marsh Gin",
    [RES_CLAY]      = "Clay",
    [RES_BRICKS]    = "Bricks",
    [RES_PIGS]      = "Pigs",
    [RES_SAUSAGES]  = "Sausages",
    [RES_TALLOW]    = "Tallow",
    [RES_SOAP]      = "Soap",
    [RES_FLOUR]     = "Flour",
    [RES_BREAD]     = "Bread",

    [RES_CHARCOAL]        = "Charcoal",
    [RES_IRON_ORE]        = "Iron Ore",
    [RES_COAL]            = "Coal",
    [RES_IRON]            = "Iron",
    [RES_STEEL_BEAMS]     = "Steel Beams",
    [RES_SAND]            = "Sand",
    [RES_GLASS]           = "Glass",
    [RES_BRASS]           = "Brass",
    [RES_WINDOWS]         = "Windows",
    [RES_SPECTACLES]      = "Spectacles",
    [RES_CATTLE]          = "Cattle",
    [RES_PEPPER]          = "Pepper",
    [RES_POTTED_MEAT]     = "Potted Meat",
    [RES_PRESERVES]       = "Preserves",
    [RES_STEEL]           = "Steel",
    [RES_SEWING_MACHINES] = "Sewing Machines",

    [RES_COTTON]    = "Cotton",
    [RES_CLOTH]     = "Cloth",
    [RES_PELTS]     = "Pelts",
    [RES_FUR_COATS] = "Fur Coats",

    [RES_GOLD_ORE]       = "Gold Ore",
    [RES_WIRE]           = "Wire",
    [RES_SPRINGS]        = "Springs",
    [RES_LAMPS]          = "Lamps",
    [RES_POCKET_WATCHES] = "Pocket Watches",
    [RES_SHELLAC]        = "Shellac",
    [RES_GRAMOPHONES]    = "Gramophones",
    [RES_LOBSTER]        = "Lobster",
    [RES_BANQUET]        = "Banquet",

    [RES_GOLD]  = "Gold",
};

/* Designated, like every other table indexed by this enum — the
 * RES_COL lesson (see building.c): a positional table here misaligned
 * silently the moment Hops/Malt/Beer were inserted. */
const ResourceCategory RESOURCE_CATEGORIES[RES_COUNT] = {
    [RES_WOOD]  = RCAT_RAW,
    [RES_FISH]  = RCAT_RAW,
    [RES_GRAIN] = RCAT_RAW,
    [RES_HOPS]  = RCAT_RAW,
    [RES_MALT]  = RCAT_REFINED,
    [RES_BEER]  = RCAT_REFINED,

    /* Raw is what comes out of the ground, the water or an animal;
     * refined is what a building makes from another good. Wool, Pigs,
     * Potatoes and Clay are raw by that rule even though three of them
     * come off a farm rather than out of a mine. */
    [RES_PLANKS]    = RCAT_REFINED,
    [RES_WOOL]      = RCAT_RAW,
    [RES_OILSKINS]  = RCAT_REFINED,
    [RES_POTATOES]  = RCAT_RAW,
    [RES_MARSH_GIN] = RCAT_REFINED,
    [RES_CLAY]      = RCAT_RAW,
    [RES_BRICKS]    = RCAT_REFINED,
    [RES_PIGS]      = RCAT_RAW,
    [RES_SAUSAGES]  = RCAT_REFINED,
    [RES_TALLOW]    = RCAT_REFINED,
    [RES_SOAP]      = RCAT_REFINED,
    [RES_FLOUR]     = RCAT_REFINED,
    [RES_BREAD]     = RCAT_REFINED,

    /* Ore, coal, sand, cattle and pepper come out of the ground or off
     * the grass; everything else in Phase 4 is made from them. */
    [RES_IRON_ORE]        = RCAT_RAW,
    [RES_COAL]            = RCAT_RAW,
    [RES_SAND]            = RCAT_RAW,
    [RES_CATTLE]          = RCAT_RAW,
    [RES_PEPPER]          = RCAT_RAW,
    [RES_CHARCOAL]        = RCAT_REFINED,
    [RES_IRON]            = RCAT_REFINED,
    [RES_STEEL_BEAMS]     = RCAT_REFINED,
    [RES_GLASS]           = RCAT_REFINED,
    [RES_BRASS]           = RCAT_REFINED,
    [RES_WINDOWS]         = RCAT_REFINED,
    [RES_SPECTACLES]      = RCAT_REFINED,
    [RES_POTTED_MEAT]     = RCAT_REFINED,
    [RES_PRESERVES]       = RCAT_REFINED,
    [RES_STEEL]           = RCAT_REFINED,
    [RES_SEWING_MACHINES] = RCAT_REFINED,

    [RES_COTTON]    = RCAT_RAW,
    [RES_PELTS]     = RCAT_RAW,
    [RES_CLOTH]     = RCAT_REFINED,
    [RES_FUR_COATS] = RCAT_REFINED,

    [RES_GOLD_ORE]       = RCAT_RAW,
    [RES_SHELLAC]        = RCAT_RAW,
    [RES_LOBSTER]        = RCAT_RAW,
    [RES_WIRE]           = RCAT_REFINED,
    [RES_SPRINGS]        = RCAT_REFINED,
    [RES_LAMPS]          = RCAT_REFINED,
    [RES_POCKET_WATCHES] = RCAT_REFINED,
    [RES_GRAMOPHONES]    = RCAT_REFINED,
    [RES_BANQUET]        = RCAT_REFINED,

    [RES_GOLD]  = RCAT_CURRENCY,
};

const char *resource_category_name(ResourceCategory c)
{
    static const char *const NAMES[RCAT_COUNT] = {
        [RCAT_NONE]     = "Other",
        [RCAT_RAW]      = "Raw goods",
        [RCAT_REFINED]  = "Refined goods",
        [RCAT_CURRENCY] = "Currency"
    };
    if (c < 0 || c >= RCAT_COUNT || !NAMES[c]) return "Other";
    return NAMES[c];
}

const int SELL_PRICE[RES_COUNT] = {
    [RES_WOOD]  = 2,
    [RES_FISH]  = 3,
    [RES_GRAIN] = 2,
    [RES_HOPS]  = 4,
    [RES_MALT]  = 6,
    [RES_BEER]  = 8,

    /* Priced by depth in the chain: a raw good is worth a little, and
     * each step that consumes one adds to it. Nothing here is tuned —
     * these are starting numbers the elastic market moves from. */
    [RES_PLANKS]    = 4,
    [RES_WOOL]      = 3,
    [RES_OILSKINS]  = 7,
    [RES_POTATOES]  = 2,
    [RES_MARSH_GIN] = 6,
    [RES_CLAY]      = 2,
    [RES_BRICKS]    = 5,
    [RES_PIGS]      = 4,
    [RES_SAUSAGES]  = 8,
    [RES_TALLOW]    = 5,
    [RES_SOAP]      = 9,
    [RES_FLOUR]     = 4,
    [RES_BREAD]     = 7,

    /* Priced by depth: each step of a chain adds more than it consumes,
     * so refining is worth doing and the four Artisans goods sit at the
     * top of the table. */
    [RES_CHARCOAL]        = 4,
    [RES_IRON_ORE]        = 4,
    [RES_COAL]            = 5,
    [RES_IRON]            = 11,
    [RES_STEEL_BEAMS]     = 18,
    [RES_SAND]            = 2,
    [RES_GLASS]           = 8,
    [RES_BRASS]           = 17,
    [RES_WINDOWS]         = 22,
    [RES_SPECTACLES]      = 30,
    [RES_CATTLE]          = 5,
    [RES_PEPPER]          = 4,
    [RES_POTTED_MEAT]     = 12,
    [RES_PRESERVES]       = 20,
    [RES_STEEL]           = 19,
    [RES_SEWING_MACHINES] = 34,

    /* Fur Coats price in the Artisans band. Cotton is dear for a raw
     * good because it can only come off a ship. */
    [RES_COTTON]    = 6,
    [RES_CLOTH]     = 13,
    [RES_PELTS]     = 7,
    [RES_FUR_COATS] = 28,

    /* The Engineers band sits above the Artisans one: three-input
     * goods cost three chains to make. */
    [RES_GOLD_ORE]       = 9,
    [RES_WIRE]           = 14,
    [RES_SPRINGS]        = 14,
    [RES_LAMPS]          = 26,
    [RES_POCKET_WATCHES] = 48,
    [RES_SHELLAC]        = 8,
    [RES_GRAMOPHONES]    = 52,
    [RES_LOBSTER]        = 9,
    [RES_BANQUET]        = 38,
    /* RES_GOLD left at 0 — unused, can't sell gold for gold */
};

const int BUY_PRICE[RES_COUNT] = {
    [RES_WOOD]  = 5,
    [RES_FISH]  = 6,
    [RES_GRAIN] = 5,
    [RES_HOPS]  = 8,
    [RES_MALT]  = 12,
    [RES_BEER]  = 16,

    [RES_PLANKS]    = 8,
    [RES_WOOL]      = 6,
    [RES_OILSKINS]  = 14,
    [RES_POTATOES]  = 5,
    [RES_MARSH_GIN] = 12,
    [RES_CLAY]      = 5,
    [RES_BRICKS]    = 10,
    [RES_PIGS]      = 8,
    [RES_SAUSAGES]  = 16,
    [RES_TALLOW]    = 10,
    [RES_SOAP]      = 18,
    [RES_FLOUR]     = 8,
    [RES_BREAD]     = 14,

    [RES_CHARCOAL]        = 8,
    [RES_IRON_ORE]        = 8,
    [RES_COAL]            = 10,
    [RES_IRON]            = 22,
    [RES_STEEL_BEAMS]     = 36,
    [RES_SAND]            = 4,
    [RES_GLASS]           = 16,
    [RES_BRASS]           = 34,
    [RES_WINDOWS]         = 44,
    [RES_SPECTACLES]      = 60,
    [RES_CATTLE]          = 10,
    [RES_PEPPER]          = 8,
    [RES_POTTED_MEAT]     = 24,
    [RES_PRESERVES]       = 40,
    [RES_STEEL]           = 38,
    [RES_SEWING_MACHINES] = 68,

    [RES_COTTON]    = 12,
    [RES_CLOTH]     = 26,
    [RES_PELTS]     = 14,
    [RES_FUR_COATS] = 56,

    [RES_GOLD_ORE]       = 18,
    [RES_WIRE]           = 28,
    [RES_SPRINGS]        = 28,
    [RES_LAMPS]          = 52,
    [RES_POCKET_WATCHES] = 96,
    [RES_SHELLAC]        = 16,
    [RES_GRAMOPHONES]    = 104,
    [RES_LOBSTER]        = 18,
    [RES_BANQUET]        = 76,
    /* RES_GOLD left at 0 — unused */
};

void stockpile_init(Stockpile *s)
{
    int i;
    for (i = 0; i < RES_COUNT; i++)
        s->amount[i] = 0;
    s->capacity = BASE_STORAGE_CAP;
}

/* stockpile_add -------------------------------------------
 * We clamp to zero rather than allowing negative stock.
 * If a building tries to consume more than is available it
 * simply does nothing — in Phase 5 this will trigger a
 * "needs not met" penalty on population happiness.
 * -------------------------------------------------------- */
void stockpile_add(Stockpile *s, ResourceType res, int delta)
{
    s->amount[res] += delta;
    if (s->amount[res] < 0)
        s->amount[res] = 0;
    if (res != RES_GOLD && s->amount[res] > s->capacity)
        s->amount[res] = s->capacity;
}

void stockpile_set_capacity(Stockpile *s, int capacity)
{
    int i;
    s->capacity = capacity;
    for (i = 0; i < RES_COUNT; i++)
        if (i != RES_GOLD && s->amount[i] > capacity)
            s->amount[i] = capacity;
}
