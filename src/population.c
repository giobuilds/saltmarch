/*  population.c  --  Residents and needs  (Phase 5, tier-driven
 *  needs added in the production-chains pass)  */

#include "population.h"
#include "simlog.h"
#include "resource.h"
#include <stdio.h>

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
    /* Marshfolk. Fish and Grain are a fisher's hut and a farm — the
     * opening every player reaches for, which until NEEDS_PLAN kept
     * nobody alive because the tier wanted Oilskins and Marsh Gin
     * instead, two chains behind fertility checks. Those are what
     * happiness is now made of. */
    { BUILDING_HOUSE,
      { RES_FISH, RES_GRAIN, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT },
      { RES_OILSKINS, RES_MARSH_GIN, RES_COUNT, RES_COUNT, RES_COUNT,
        RES_COUNT },
      BUILDING_HOUSE_ARTISAN, 400, BUILDING_NONE },

    /* Wrights. The second base tier, and the other line's floor.
     * Plantain Fry is a luxury here as well as for Merchants: a
     * southern good with two customers rather than one. */
    { BUILDING_HOUSE_WORKER,
      { RES_SAUSAGES, RES_BREAD, RES_COUNT, RES_COUNT, RES_COUNT,
        RES_COUNT },
      { RES_SOAP, RES_BEER, RES_PLANTAIN_FRY, RES_COUNT, RES_COUNT,
        RES_COUNT },
      BUILDING_HOUSE_ENGINEER, 600, BUILDING_NONE },

    /* Artisans inherit Fish and Grain, which is the whole point of
     * inheritance: a home island's first two chains are still wanted
     * by the tier that outgrew them. Sewing Machines sit in luxury
     * rather than basic so an Artisan neighbourhood survives a Machine
     * Shop outage instead of dying of one — a change of meaning, not
     * of cost, since a refined good is charged per house either way. */
    { BUILDING_HOUSE_ARTISAN,
      { RES_FISH, RES_GRAIN, RES_PRESERVES, RES_COUNT, RES_COUNT,
        RES_COUNT },
      { RES_SEWING_MACHINES, RES_FUR_COATS, RES_SPECTACLES, RES_WINDOWS,
        RES_COUNT, RES_COUNT },
      BUILDING_NONE, 0, BUILDING_NONE },

    /* Engineers inherit the Wrights' table. */
    { BUILDING_HOUSE_ENGINEER,
      { RES_SAUSAGES, RES_BREAD, RES_LAMPS, RES_POCKET_WATCHES,
        RES_COUNT, RES_COUNT },
      { RES_GRAMOPHONES, RES_BANQUET, RES_COUNT, RES_COUNT, RES_COUNT,
        RES_COUNT },
      BUILDING_NONE, 0, BUILDING_NONE },

    /* Merchants: the third line's floor, southern from the first day. */
    { BUILDING_HOUSE_MERCHANT,
      { RES_COFFEE, RES_FLATBREAD, RES_COUNT, RES_COUNT, RES_COUNT,
        RES_COUNT },
      { RES_RUM, RES_MARSH_HATS, RES_WOOL_CLOAKS, RES_PLANTAIN_FRY,
        RES_COUNT, RES_COUNT },
      BUILDING_HOUSE_INVESTOR, 900, BUILDING_NONE },

    /* Investors are the scarcity tier: grapes only on the highland,
     * pearls only off an atoll, and Jewellery wanting both of the
     * rarest deposits in the world at once. Five basics is the widest
     * list here and the reason MAX_TIER_GOODS is not four. */
    { BUILDING_HOUSE_INVESTOR,
      { RES_COFFEE, RES_FLATBREAD, RES_SPARKLING_WINE, RES_CIGARS,
        RES_CHOCOLATE, RES_COUNT },
      { RES_JEWELLERY, RES_PERFUME, RES_COUNT, RES_COUNT, RES_COUNT,
        RES_COUNT },
      BUILDING_NONE, 0, BUILDING_NONE },

    /* Scholars are on no line: reached from ANY house, and only where
     * an Academy stands. Their basics are Books plus WHATEVER THE HOUSE
     * THEY CAME FROM ATE — see tier_basic_needs(). The basic[] list
     * here is therefore the fallback for a Scholar's House with no
     * recorded origin, and Books is the only entry every scholar
     * shares. */
    { BUILDING_HOUSE_SCHOLAR,
      { RES_BOOKS, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT, RES_COUNT },
      { RES_CHARTS, RES_COFFEE, RES_SPECTACLES, RES_COUNT, RES_COUNT,
        RES_COUNT },
      BUILDING_NONE, 0, BUILDING_ACADEMY },
};
#define TIER_DEF_COUNT (int)(sizeof(TIER_DEFS) / sizeof(TIER_DEFS[0]))

/* A scholar's household need not have been a merchant's first: their
 * basics are Books plus whatever the house they came from ate. See
 * population.h. Every other tier answers with a copy of basic[].
 *
 * The fallback for an unknown origin is Marshfolk's, not nothing: a
 * Scholar's House restored from a save written before origin_tier
 * existed has to want SOMETHING, and the base tier's food is the one
 * answer that is true of every island. */
int tier_basic_needs(const TierDef *tier, BuildingType origin,
                     ResourceType out[MAX_TIER_GOODS])
{
    const TierDef *from;
    int            n = 0, i;

    for (i = 0; i < MAX_TIER_GOODS; i++) out[i] = RES_COUNT;
    if (!tier) return 0;

    for (i = 0; i < MAX_TIER_GOODS; i++)
        if (tier->basic[i] != RES_COUNT) out[n++] = tier->basic[i];

    if (tier->house_type != BUILDING_HOUSE_SCHOLAR) return n;

    from = tier_def_for(origin);
    if (!from || from->house_type == BUILDING_HOUSE_SCHOLAR)
        from = tier_def_for(BUILDING_HOUSE);      /* the base tier's food */
    if (!from) return n;

    for (i = 0; i < MAX_TIER_GOODS && n < MAX_TIER_GOODS; i++) {
        int dup = 0, k;
        if (from->basic[i] == RES_COUNT) continue;
        for (k = 0; k < n; k++) if (out[k] == from->basic[i]) dup = 1;
        if (!dup) out[n++] = from->basic[i];
    }
    return n;
}

const TierDef *tier_def_for(BuildingType type)
{
    int i;
    for (i = 0; i < TIER_DEF_COUNT; i++)
        if (TIER_DEFS[i].house_type == type)
            return &TIER_DEFS[i];
    return NULL;   /* not a house type pop_update recognizes */
}

BuildingType tier_branch_target(BuildingType from, int branch)
{
    const TierDef *tier = tier_def_for(from);

    if (!tier) return BUILDING_NONE;   /* not a house at all */

    if (branch == TIER_BRANCH_ACADEMY) {
        /* Open to every house type, which is the whole point of it —
         * except to a Scholar's House, which has nowhere further to
         * go and must not be offered a promotion to itself. */
        return from == BUILDING_HOUSE_SCHOLAR ? BUILDING_NONE
                                              : BUILDING_HOUSE_SCHOLAR;
    }
    return tier->next_tier;
}

int tier_branches(BuildingType from, int out[2])
{
    int b, n = 0;

    for (b = TIER_BRANCH_LINE; b <= TIER_BRANCH_ACADEMY; b++)
        if (tier_branch_target(from, b) != BUILDING_NONE)
            out[n++] = b;
    return n;
}

BuildingType tier_upgrade_requires(BuildingType from, int branch)
{
    BuildingType   to = tier_branch_target(from, branch);
    const TierDef *next;

    if (to == BUILDING_NONE) return BUILDING_NONE;

    /* The prerequisite belongs to the tier being entered, not the one
     * being left: it is the Academy that makes Scholars possible, and
     * a Marsh Cottage is not waiting on anything to become Artisans. */
    next = tier_def_for(to);
    return next ? next->requires_building : BUILDING_NONE;
}

RejectReason tier_upgrade_check(BuildingType from, int branch,
                                const int stock[RES_COUNT],
                                int prereq_present,
                                BuildingType *out_to)
{
    const TierDef *tier = tier_def_for(from);

    if (out_to) *out_to = BUILDING_NONE;
    if (!tier) return REJ_UNAVAILABLE;

    return tier_upgrade_check_def(tier,
                                  tier_def_for(tier_branch_target(from,
                                                                  branch)),
                                  stock, prereq_present, out_to);
}

RejectReason tier_upgrade_check_def(const TierDef *tier, const TierDef *next,
                                    const int stock[RES_COUNT],
                                    int prereq_present,
                                    BuildingType *out_to)
{
    int k;

    if (out_to) *out_to = BUILDING_NONE;

    /* Both of these used to consult tier->next_tier, which was the
     * same thing as `next` while a house had exactly one edge. Since
     * SUPPLY_CHAIN Phase 8 it is not: a terminal tier still has an
     * Academy branch, and asking about that branch must not be refused
     * because its LINE goes nowhere. `next` is the edge being asked
     * about; nothing here may reach past it. */
    if (!tier) return REJ_UNAVAILABLE;
    if (!next) return REJ_UNAVAILABLE;   /* an edge to nowhere */

    if (next->requires_building != BUILDING_NONE && !prereq_present)
        return REJ_NEEDS_BUILDING;

    /* Every good the tier being ENTERED will want, present on the
     * island. Checked before Gold so the message a player sees names
     * the thing they have to go and build, not the money they happen
     * to be short of as well. */
    for (k = 0; k < MAX_TIER_GOODS; k++) {
        /* The bar for entry is the tier's BASICS, not everything it
         * will ever want: you may move into a neighbourhood you cannot
         * yet keep in spectacles, and then go and build the spectacle
         * shop. Demanding the luxuries too would make every upgrade
         * wait on the whole chain above it. */
        if (next->basic[k] == RES_COUNT) continue;
        if (stock[next->basic[k]] <= 0) return REJ_NEEDS_GOODS;
    }

    if (stock[RES_GOLD] < tier->upgrade_gold) return REJ_CANT_AFFORD;

    /* The destination is the edge's, not the line's — the bug this
     * pair replaced would promote an Academy upgrade to whatever the
     * house's own line pointed at. */
    if (out_to) *out_to = next->house_type;
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
        ResourceType   basic[MAX_TIER_GOODS];
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
        /* PHASE 1 KEEPS ALL-OR-NOTHING, over the union of both lists.
         * The split is data this phase; what it MEANS — basics keep you
         * alive, luxuries make you happy — is Phase 2. Landing both at
         * once would move the determinism fixture's hash for two
         * reasons and leave neither attributable. */
        tier_basic_needs(tier, (BuildingType)p->origin_tier, basic);

        needs_met = buildings[i].connected && tier != NULL && p->residents > 0;
        if (needs_met) {
            for (k = 0; k < MAX_TIER_GOODS; k++) {
                if (basic[k] != RES_COUNT && s->amount[basic[k]] <= 0)
                    { needs_met = 0; break; }
                if (tier->luxury[k] != RES_COUNT &&
                    s->amount[tier->luxury[k]] <= 0) { needs_met = 0; break; }
            }
        }

        if (needs_met) {
            for (j = 0; j < MAX_TIER_GOODS; j++) {
                if (basic[j] != RES_COUNT)         stockpile_add(s, basic[j], -1);
                if (tier->luxury[j] != RES_COUNT)  stockpile_add(s, tier->luxury[j], -1);
            }

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

            /* Name it here too. "missing a required good" told the
             * player that something was wrong and not which thing,
             * which is the difference between a log line and an
             * answer. */
            {
                const char *why = "no road to Warehouse";
                char        buf[48];

                if (buildings[i].connected && tier) {
                    int m;
                    why = "needs are already met";   /* only if none is short */
                    for (m = 0; m < MAX_TIER_GOODS * 2; m++) {
                        ResourceType g = (m < MAX_TIER_GOODS)
                                       ? basic[m] : tier->luxury[m - MAX_TIER_GOODS];
                        if (g == RES_COUNT) continue;
                        if (s->amount[g] > 0) continue;
                        snprintf(buf, sizeof(buf), "no %s", RESOURCE_NAMES[g]);
                        why = buf;
                        break;
                    }
                } else if (buildings[i].connected) {
                    why = "nowhere to live";
                }

                sim_log("House %d: unhappy (%s), %d residents",
                        i, why, p->residents);
            }
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
