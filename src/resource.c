/*  resource.c  --  Resource stockpile implementation  (Phase 4)  */

#include "resource.h"

const char *RESOURCE_NAMES[RES_COUNT] = {
    "Wood",
    "Fish",
    "Grain",
    "Gold"
};

const int SELL_PRICE[RES_COUNT] = {
    [RES_WOOD]  = 2,
    [RES_FISH]  = 3,
    [RES_GRAIN] = 2,
    /* RES_GOLD left at 0 — unused, can't sell gold for gold */
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
