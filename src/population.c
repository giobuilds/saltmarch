/*  population.c  --  Residents and needs  (Phase 5, tier-driven
 *  needs added in the production-chains pass)  */

#include "population.h"
#include <SDL3/SDL.h>   /* SDL_Log */

/* ---- Per-tier needs table --------------------------------
 * Keyed by the house's actual BuildingType, so upgrading a house
 * (game_upgrade_house, game.c — just mutates buildings[idx].type in
 * place) automatically changes what pop_update() requires next tick,
 * with zero other state to migrate. RES_COUNT in a needs[] slot means
 * "unused" — same sentinel convention as BuildingDef.consumes[]. */
static const TierDef TIER_DEFS[] = {
    { BUILDING_HOUSE,        { RES_FISH, RES_GRAIN, RES_COUNT } },
    { BUILDING_HOUSE_WORKER, { RES_FISH, RES_GRAIN, RES_BEER  } },
};
#define TIER_DEF_COUNT (int)(sizeof(TIER_DEFS) / sizeof(TIER_DEFS[0]))

static const TierDef *tier_def_for(BuildingType type)
{
    int i;
    for (i = 0; i < TIER_DEF_COUNT; i++)
        if (TIER_DEFS[i].house_type == type)
            return &TIER_DEFS[i];
    return NULL;   /* not a house type pop_update recognizes */
}

/* ---- pop_init ------------------------------------------ */
void pop_init(PopData *p)
{
    p->active    = 1;
    p->residents = 5;        /* start half-full so growth is visible */
    p->timer     = 0.0f;
    p->happy     = 0;
}

/* ---- pop_update ----------------------------------------
 * The needs loop.  Runs once per frame for every house.
 *
 * We use a single timer per house rather than a global
 * tick so houses placed at different times stagger their
 * consumption — avoiding a sudden stockpile spike every
 * NEEDS_INTERVAL seconds.
 * -------------------------------------------------------- */
void pop_update(PopData pop[], const Building buildings[], int count,
               Stockpile *s, float dt)
{
    int i, j;

    for (i = 0; i < count; i++) {
        PopData       *p    = &pop[i];
        const TierDef *tier;
        int            needs_met, k;

        if (!p->active) continue;

        p->timer += dt;
        if (p->timer < NEEDS_INTERVAL) continue;
        p->timer = 0.0f;

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

            SDL_Log("House %d: happy, %d residents, +%d gold",
                i, p->residents,
                GOLD_PER_RESIDENT * p->residents);

        } else {
            /* Needs not met — residents leave */
            p->happy = 0;
            if (p->residents > 0)
                p->residents--;

            SDL_Log("House %d: unhappy (%s), %d residents",
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
