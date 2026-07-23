/*  faction.c  --  The NPC market counterparty (MMO_PLAN Phase 3)  */

#include "faction.h"

void faction_init(Faction *f)
{
    int i;
    f->gold         = FACTION_START_GOLD;
    f->revert_timer = 0;
    for (i = 0; i < RES_COUNT; i++)
        f->inventory[i] = (i == RES_GOLD) ? 0 : FACTION_BASE_INVENTORY;
}

/* Linear elastic quote from a base price and the current inventory:
 *   inventory 0            -> 2 * base_price   (scarce: pays/charges more)
 *   inventory baseline     ->     base_price   (day-one neutral)
 *   inventory 2*baseline   -> 0  -> clamped    (glutted: pays/charges little)
 * Multiply-then-divide keeps resolution so a good priced at 2-3 still
 * moves; clamped to [1, 4*base] so a quote is never free or unbounded. */
static int quote(int base_price, int32_t inventory)
{
    int q = base_price * (2 * FACTION_BASE_INVENTORY - (int)inventory)
                       / FACTION_BASE_INVENTORY;
    if (q < 1)              q = 1;
    if (q > base_price * 4) q = base_price * 4;
    return q;
}

int faction_bid(const Faction *f, ResourceType r)
{
    if (r < 0 || r >= RES_COUNT || r == RES_GOLD) return 0;
    return quote(SELL_PRICE[r], f->inventory[r]);
}

int faction_ask(const Faction *f, ResourceType r)
{
    if (r < 0 || r >= RES_COUNT || r == RES_GOLD) return 0;
    return quote(BUY_PRICE[r], f->inventory[r]);
}

void faction_tick(Faction *f)
{
    int i;
    if (++f->revert_timer < FACTION_REVERT_INTERVAL_TICKS) return;
    f->revert_timer = 0;

    for (i = 0; i < RES_COUNT; i++) {
        if (i == RES_GOLD) continue;
        if (f->inventory[i] > FACTION_BASE_INVENTORY)      f->inventory[i]--;
        else if (f->inventory[i] < FACTION_BASE_INVENTORY) f->inventory[i]++;
    }
}
