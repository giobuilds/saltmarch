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
