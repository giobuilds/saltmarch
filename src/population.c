/*  population.c  --  Residents and needs  (Phase 5, tier-driven
 *  needs added in the production-chains pass)  */

#include "population.h"
#include "simlog.h"

/* ---- Per-tier needs table --------------------------------
 * Keyed by the house's actual BuildingType, so upgrading a house
 * (game_upgrade_house, game.c — just mutates buildings[idx].type in
 * place) automatically changes what pop_update() requires next tick,
 * with zero other state to migrate. RES_COUNT in a needs[] slot means
 * "unused" — same sentinel convention as BuildingDef.consumes[]. */
/* SUPPLY_CHAIN Phase 3: the two northern BASE tiers, with the needs
 * the plan gives them. Two things changed from the old ladder.
 *
 * Grain stopped being eaten directly. It is milled into Flour and
 * baked into Bread, which is what makes the Windmill and the Bakehouse
 * worth building rather than decorative.
 *
 * Neither tier upgrades. They are the bottoms of two different lines —
 * Marshfolk climb to Artisans (Phase 4) and Wrights to Engineers
 * (Phase 6) — so next_tier is BUILDING_NONE for both, and the confirm
 * popup correctly reports there is nowhere to go. The Cottage ->
 * Wright's House edge that used to exist is gone: a Wright's House is
 * now something you build. */
static const TierDef TIER_DEFS[] = {
    /* SUPPLY_CHAIN Phase 4 gives the Marsh Cottage somewhere to go.
     * Until now both base tiers were terminal and sim_upgrade_house's
     * accept path had no content to run on; this is the edge that puts
     * it back under test end to end. */
    { BUILDING_HOUSE,
      { RES_FISH, RES_OILSKINS, RES_MARSH_GIN, RES_COUNT, RES_COUNT },
      BUILDING_HOUSE_ARTISAN, 400, BUILDING_NONE },
    /* SUPPLY_CHAIN Phase 6 completes the second line. */
    { BUILDING_HOUSE_WORKER,
      { RES_SAUSAGES, RES_BREAD, RES_SOAP, RES_BEER, RES_COUNT },
      BUILDING_HOUSE_ENGINEER, 600, BUILDING_NONE },
    /* Phase 4 shipped this tier with four needs because Fur Coats
     * needs Cloth, Cloth needs Cotton, and no northern profile grows
     * cotton. SUPPLY_CHAIN Phase 5 settles that debt: the fifth need
     * arrives with the southern islands, and with it the property the
     * whole climate exists for — an Artisans neighbourhood cannot be
     * reached without a colony in the south and a ship between.
     *
     * Five is also MAX_TIER_GOODS exactly, so this row is the first
     * real content to use the full width Phase 2 reserved. */
    { BUILDING_HOUSE_ARTISAN,
      { RES_PRESERVES, RES_SEWING_MACHINES, RES_SPECTACLES, RES_WINDOWS,
        RES_FUR_COATS },
      BUILDING_NONE, 0, BUILDING_NONE },
    /* Engineers (Phase 6) are the deepest tier so far and the first
     * that needs the WHOLE archipelago: Pocket Watches want highland
     * gold ore, Gramophones want jungle shellac, a Banquet wants a
     * coast for its lobster, and Lamps want the northern glass and
     * iron behind everything else. */
    { BUILDING_HOUSE_ENGINEER,
      { RES_LAMPS, RES_POCKET_WATCHES, RES_GRAMOPHONES, RES_BANQUET,
        RES_COUNT },
      BUILDING_NONE, 0, BUILDING_NONE },
};
#define TIER_DEF_COUNT (int)(sizeof(TIER_DEFS) / sizeof(TIER_DEFS[0]))

const TierDef *tier_def_for(BuildingType type)
{
    int i;
    for (i = 0; i < TIER_DEF_COUNT; i++)
        if (TIER_DEFS[i].house_type == type)
            return &TIER_DEFS[i];
    return NULL;   /* not a house type pop_update recognizes */
}

BuildingType tier_upgrade_requires(BuildingType from)
{
    const TierDef *tier = tier_def_for(from);
    const TierDef *next;

    if (!tier || tier->next_tier == BUILDING_NONE) return BUILDING_NONE;

    /* The prerequisite belongs to the tier being entered, not the one
     * being left: it is the Academy that makes Scholars possible, and
     * a Marsh Cottage is not waiting on anything to become Artisans. */
    next = tier_def_for(tier->next_tier);
    return next ? next->requires_building : BUILDING_NONE;
}

RejectReason tier_upgrade_check(BuildingType from,
                                const int stock[RES_COUNT],
                                int prereq_present,
                                BuildingType *out_to)
{
    const TierDef *tier = tier_def_for(from);

    if (out_to) *out_to = BUILDING_NONE;
    if (!tier) return REJ_UNAVAILABLE;

    return tier_upgrade_check_def(tier,
                                  tier_def_for(tier->next_tier),
                                  stock, prereq_present, out_to);
}

RejectReason tier_upgrade_check_def(const TierDef *tier, const TierDef *next,
                                    const int stock[RES_COUNT],
                                    int prereq_present,
                                    BuildingType *out_to)
{
    int k;

    if (out_to) *out_to = BUILDING_NONE;

    if (!tier || tier->next_tier == BUILDING_NONE) return REJ_UNAVAILABLE;
    if (!next) return REJ_UNAVAILABLE;   /* an edge to nowhere */

    if (next->requires_building != BUILDING_NONE && !prereq_present)
        return REJ_NEEDS_BUILDING;

    /* Every good the tier being ENTERED will want, present on the
     * island. Checked before Gold so the message a player sees names
     * the thing they have to go and build, not the money they happen
     * to be short of as well. */
    for (k = 0; k < MAX_TIER_GOODS; k++) {
        if (next->needs[k] == RES_COUNT) continue;
        if (stock[next->needs[k]] <= 0) return REJ_NEEDS_GOODS;
    }

    if (stock[RES_GOLD] < tier->upgrade_gold) return REJ_CANT_AFFORD;

    if (out_to) *out_to = tier->next_tier;
    return REJ_OK;
}

int pop_is_house_type(BuildingType type)
{
    return tier_def_for(type) != NULL;
}

/* ---- pop_init ------------------------------------------ */
void pop_init(PopData *p)
{
    p->active    = 1;
    p->residents = 5;        /* start half-full so growth is visible */
    p->timer     = 0;
    p->happy     = 0;
}

/* ---- pop_update ----------------------------------------
 * The needs loop.  Runs once per sim tick for every house.
 *
 * We use a single timer per house rather than a global
 * tick so houses placed at different times stagger their
 * consumption — avoiding a sudden stockpile spike every
 * NEEDS_INTERVAL seconds.
 * -------------------------------------------------------- */
void pop_update(PopData pop[], const Building buildings[], int count,
               Stockpile *s)
{
    int i, j;

    for (i = 0; i < count; i++) {
        PopData       *p    = &pop[i];
        const TierDef *tier;
        int            needs_met, k;

        if (!p->active) continue;

        p->timer++;
        if (p->timer < NEEDS_INTERVAL_TICKS) continue;
        p->timer = 0;

        tier = tier_def_for(buildings[i].type);

        /* --- Needs check: road-connected, plus every good this
         * tier's TierDef lists (all-or-nothing, same philosophy as
         * game_tick_buildings' multi-input production). A
         * disconnected house has no route for a Warehouse to deliver
         * anything, so it's treated the same as needs unmet. */
        needs_met = buildings[i].connected && tier != NULL && p->residents > 0;
        if (needs_met) {
            for (k = 0; k < MAX_TIER_GOODS; k++) {
                if (tier->needs[k] == RES_COUNT) continue;
                if (s->amount[tier->needs[k]] <= 0) { needs_met = 0; break; }
            }
        }

        if (needs_met) {
            for (j = 0; j < MAX_TIER_GOODS; j++)
                if (tier->needs[j] != RES_COUNT)
                    stockpile_add(s, tier->needs[j], -1);

            /* Generate gold proportional to residents */
            stockpile_add(s, RES_GOLD,
                          GOLD_PER_RESIDENT * p->residents);

            p->happy = 1;

            /* Population grows toward capacity when happy */
            if (p->residents < HOUSE_CAPACITY)
                p->residents++;

            sim_log("House %d: happy, %d residents, +%d gold",
                i, p->residents,
                GOLD_PER_RESIDENT * p->residents);

        } else {
            /* Needs not met — residents leave */
            p->happy = 0;
            if (p->residents > 0)
                p->residents--;

            sim_log("House %d: unhappy (%s), %d residents",
                i,
                !buildings[i].connected ? "no road to Warehouse" :
                    "missing a required good",
                p->residents);
        }
    }
}

/* ---- pop_total ----------------------------------------- */
int pop_total(const PopData pop[], int count)
{
    int i, total = 0;
    for (i = 0; i < count; i++)
        if (pop[i].active)
            total += pop[i].residents;
    return total;
}
