/*  faction.c  --  The NPC market counterparty (MMO_PLAN Phase 3)  */

#include "faction.h"
#include "island.h"   /* the insurance constants, and the lane-count check */
#include <string.h>

int faction_lane_premium(const Faction *f, int from, int to)
{
    if (from < 0 || from >= MAX_ISLANDS_FOR_LANES ||
        to   < 0 || to   >= MAX_ISLANDS_FOR_LANES)
        return INSURANCE_PREMIUM_START;
    return f->lane_premium[from][to];
}

void faction_lane_experience(Faction *f, int from, int to, int raided)
{
    int p, target;

    if (from < 0 || from >= MAX_ISLANDS_FOR_LANES ||
        to   < 0 || to   >= MAX_ISLANDS_FOR_LANES)
        return;

    p      = f->lane_premium[from][to];
    target = raided ? INSURANCE_PREMIUM_MAX : INSURANCE_PREMIUM_MIN;

    /* p += (target - p) >> shift, in integers. The shift is the EMA's
     * memory: bigger means slower to believe the most recent voyage. */
    p += (target - p) >> INSURANCE_EMA_SHIFT;

    /* An integer EMA can stall short of its target when the difference
     * shifts to zero; nudge it so experience always moves the price at
     * least a little, or a lane could sit at a stale premium forever. */
    if (raided && p <= f->lane_premium[from][to]) p++;
    if (!raided && p >= f->lane_premium[from][to]) p--;

    if (p < INSURANCE_PREMIUM_MIN) p = INSURANCE_PREMIUM_MIN;
    if (p > INSURANCE_PREMIUM_MAX) p = INSURANCE_PREMIUM_MAX;
    f->lane_premium[from][to] = (int16_t)p;
}

void faction_init(Faction *f)
{
    int i;

    /* Zero the whole struct first. Every byte of this is hashed, and
     * GameState is malloc'd rather than calloc'd — a field this
     * function forgets is uninitialised memory entering sim_hash, which
     * makes two clients of the same world disagree for reasons neither
     * can see. That is not hypothetical: the price-history ring
     * (UI_PLAN M3) was added and immediately produced exactly that. */
    memset(f, 0, sizeof(*f));

    f->gold         = FACTION_START_GOLD;
    f->revert_timer = 0;
    for (i = 0; i < RES_COUNT; i++)
        f->inventory[i] = (i == RES_GOLD) ? 0 : FACTION_BASE_INVENTORY;

    {
        int a, b;
        for (a = 0; a < MAX_ISLANDS_FOR_LANES; a++)
            for (b = 0; b < MAX_ISLANDS_FOR_LANES; b++)
                f->lane_premium[a][b] = INSURANCE_PREMIUM_START;
    }
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

int faction_history(const Faction *f, ResourceType r, int16_t *out, int max)
{
    int n, i, start;

    if (r < 0 || r >= RES_COUNT || max <= 0) return 0;

    n = f->hist_count < FACTION_HIST_LEN ? f->hist_count : FACTION_HIST_LEN;
    if (n > max) n = max;

    /* Oldest first. Before the ring has wrapped the oldest is index 0;
     * after, it is whatever head points at. */
    start = (f->hist_count < FACTION_HIST_LEN)
            ? 0
            : (int)f->hist_head;

    for (i = 0; i < n; i++)
        out[i] = f->hist[r][(start + i) % FACTION_HIST_LEN];
    return n;
}

/* Sample every good's mid-price into the ring. Deliberately BEFORE the
 * reversion nudge below, so the first sample of a session is the
 * untouched baseline rather than one tick of drift. */
static void faction_sample_history(Faction *f)
{
    int r;

    for (r = 0; r < RES_COUNT; r++) {
        int mid;
        if (r == RES_GOLD) { f->hist[r][f->hist_head] = 0; continue; }
        mid = (faction_bid(f, (ResourceType)r) +
               faction_ask(f, (ResourceType)r)) / 2;
        /* int16 is ample for any price this economy produces, and keeps
         * the history cheap enough to be sim state without apology. */
        if (mid > 32767) mid = 32767;
        f->hist[r][f->hist_head] = (int16_t)mid;
    }

    f->hist_head = (uint16_t)((f->hist_head + 1) % FACTION_HIST_LEN);
    if (f->hist_count < FACTION_HIST_LEN) f->hist_count++;
}

void faction_tick(Faction *f)
{
    int i;

    /* History first: it is a record of what the price WAS during this
     * tick, and it must be sampled whether or not the reversion below
     * fires this tick. */
    if (++f->hist_timer >= FACTION_HIST_INTERVAL_TICKS || f->hist_count == 0) {
        f->hist_timer = 0;
        faction_sample_history(f);
    }

    if (++f->revert_timer < FACTION_REVERT_INTERVAL_TICKS) return;
    f->revert_timer = 0;

    for (i = 0; i < RES_COUNT; i++) {
        if (i == RES_GOLD) continue;
        if (f->inventory[i] > FACTION_BASE_INVENTORY)      f->inventory[i]--;
        else if (f->inventory[i] < FACTION_BASE_INVENTORY) f->inventory[i]++;
    }
}
