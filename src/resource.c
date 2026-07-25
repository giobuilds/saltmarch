/*  resource.c  --  Resource stockpile implementation  (Phase 4)  */

#include "resource.h"

const char *RESOURCE_NAMES[RES_COUNT] = {
    [RES_WOOD]  = "Wood",
    [RES_FISH]  = "Fish",
    [RES_GRAIN] = "Grain",
    [RES_HOPS]  = "Hops",
    [RES_MALT]  = "Malt",
    [RES_BEER]  = "Beer",
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
    /* RES_GOLD left at 0 — unused, can't sell gold for gold */
};

const int BUY_PRICE[RES_COUNT] = {
    [RES_WOOD]  = 5,
    [RES_FISH]  = 6,
    [RES_GRAIN] = 5,
    [RES_HOPS]  = 8,
    [RES_MALT]  = 12,
    [RES_BEER]  = 16,
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
