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
    /* TWO, and they are a couple (LIFE_PLAN Phase 6b). A house used to
     * open at five so that growth would be visible within a minute of
     * laying it; it opens at two now because everybody after those two
     * has to be born here. What the player watches is no longer a
     * number ticking up toward six — it is a household, and the way to
     * have more people is to give more couples a roof. */
    /* ZERO SINCE PHASE 6c: a house is laid empty and becomes a
     * household when island_settle_house can find it one — from the
     * founder allowance, or out of the reserve. */
    p->residents = 0;
    p->timer     = 0;
    /* Neutral, not zero: a house that has just been built is neither
     * delighted nor about to empty, and starting at 0 would mean the
     * first missed tick of its life cost a resident (NEEDS_PLAN Ph.2). */
    p->happiness   = HAPPINESS_NEUTRAL;
    p->origin_tier = BUILDING_NONE;
}

/* ---- pop_update ----------------------------------------
 * The needs loop.  Runs once per sim tick for every house.
 *
 * We use a single timer per house rather than a global
 * tick so houses placed at different times stagger their
 * consumption — avoiding a sudden stockpile spike every
 * NEEDS_INTERVAL seconds.
 * -------------------------------------------------------- */
/* What a house's supplies deserve, 0..HAPPINESS_MAX, and what they cost.
 *
 * BASICS ARE NOT ALL-OR-NOTHING any more, which is the change this
 * phase exists for. A house short of one of its two basics is not
 * dead, it is miserable: it scores a fraction of NEUTRAL and eats what
 * there is. Only a house with NOTHING drifts to zero, and only a house
 * at zero loses anybody.
 *
 * Luxuries are read only when every basic is met — people buy gin after
 * bread, not instead of it — and each one met is an equal share of the
 * distance from NEUTRAL to MAX.
 *
 * Consumes what it counts, so the caller cannot forget to. */
/* How much of one good a house wants per needs tick (NEEDS_PLAN Ph.3).
 *
 * YOU EAT AS A PERSON; YOU OWN MANUFACTURED THINGS AS A HOUSEHOLD. Raw
 * goods — what the land and the sea give up directly — scale with the
 * number of mouths. Refined goods are one per house however many live
 * there, because a household owns one lamp and everybody reads by it.
 *
 * That split is not a convenience, it is what makes the economy close.
 * Charging every good per resident needs about 2.1 workers per person
 * fed, and every worker is somebody's resident, so it diverges: an
 * island can never staff its own supply. Charging raw goods only brings
 * the base tier to 0.68, which closes with room for warehouses,
 * harbours and ships. The arithmetic is in docs/NEEDS_PLAN.md.
 *
 * The classification is RESOURCE_CATEGORIES, which already existed for
 * the stores overlay — no good had to be reclassified for this. */
int tier_good_amount(ResourceType g, int residents)
{
    if (RESOURCE_CATEGORIES[g] == RCAT_RAW)
        return residents > 0 ? residents : 1;
    return 1;
}

/* What a house's supplies deserve, and what they cost.
 *
 * A good counts as MET only if the whole amount was there — feeding
 * four of six people is not feeding the house — but whatever WAS there
 * is eaten either way. A shortage should show up as an empty warehouse
 * and unhappy people, not as goods left on a shelf because the delivery
 * was short. */
static int happiness_target(const TierDef *tier, const ResourceType *basic,
                            int residents, Stockpile *s)
{
    int have = 0, want = 0, lux_have = 0, lux_want = 0, k;

    for (k = 0; k < MAX_TIER_GOODS; k++) {
        int need, got;
        if (basic[k] == RES_COUNT) continue;
        want++;
        need = tier_good_amount(basic[k], residents);
        got  = s->amount[basic[k]] < need ? s->amount[basic[k]] : need;
        if (got > 0) stockpile_add(s, basic[k], -got);
        if (got == need) have++;
    }
    if (want == 0) return HAPPINESS_NEUTRAL;      /* a tier that wants nothing */
    if (have < want)
        return (HAPPINESS_NEUTRAL * have) / want; /* fed badly, but fed */

    for (k = 0; k < MAX_TIER_GOODS; k++) {
        int need, got;
        if (tier->luxury[k] == RES_COUNT) continue;
        lux_want++;
        need = tier_good_amount(tier->luxury[k], residents);
        got  = s->amount[tier->luxury[k]] < need ? s->amount[tier->luxury[k]]
                                                 : need;
        if (got > 0) stockpile_add(s, tier->luxury[k], -got);
        if (got == need) lux_have++;
    }
    if (lux_want == 0) return HAPPINESS_MAX;      /* nothing more to want */

    return HAPPINESS_NEUTRAL +
           ((HAPPINESS_MAX - HAPPINESS_NEUTRAL) * lux_have) / lux_want;
}

void pop_update(PopData pop[], const Building buildings[], int count,
               Stockpile *s,
               int (*mouths_at)(const void *ctx, int house_idx),
               const void *ctx)
{
    int i;

    for (i = 0; i < count; i++) {
        PopData       *p    = &pop[i];
        const TierDef *tier;
        ResourceType   basic[MAX_TIER_GOODS];
        int            target;

        if (!p->active) continue;

        p->timer++;
        if (p->timer < NEEDS_INTERVAL_TICKS) continue;
        p->timer = 0;

        tier = tier_def_for(buildings[i].type);
        tier_basic_needs(tier, (BuildingType)p->origin_tier, basic);

        /* A disconnected house has no route for a Warehouse to deliver
         * anything down, so it is scored as though the island were
         * empty however full the warehouse is. */
        if (!buildings[i].connected || tier == NULL || p->residents <= 0)
            target = 0;
        else {
            /* MOUTHS, NOT HEADS (LIFE_PLAN Phase 5). An adult eats a
             * whole ration, a child or an elder a half — so a house of
             * six with two children is charged for five, and only for
             * the goods that were ever per-person in the first place.
             * NULL means every head is a mouth, which is what this did
             * before ages existed. */
            int mouths = mouths_at ? mouths_at(ctx, i) : p->residents;
            if (mouths < 1) mouths = 1;
            target = happiness_target(tier, basic, mouths, s);
        }

        /* ONE STEP PER TICK, and that is the whole of the hysteresis.
         * A neighbourhood that loses its larder has ten ticks of
         * goodwill before anybody leaves; one that is rescued climbs
         * back at the same pace. Nothing counts consecutive failures,
         * because the ladder already remembers. */
        if      (p->happiness < target) p->happiness++;
        else if (p->happiness > target) p->happiness--;

        if (p->happiness > HAPPINESS_MAX) p->happiness = HAPPINESS_MAX;
        if (p->happiness < 0)             p->happiness = 0;

        /* Gold is the work these people do, so it follows being fed
         * rather than being delighted. */
        if (target >= HAPPINESS_NEUTRAL && p->residents > 0)
            stockpile_add(s, RES_GOLD, GOLD_PER_RESIDENT * p->residents);

        /* GROWTH USED TO LIVE HERE (LIFE_PLAN Phase 6b removed it). A
         * happy house gained a resident every needs tick, and where
         * that resident came from was never asked. Now a house grows
         * only by birth — residents_breed owns the upward direction
         * entirely — so happiness buys a household the room to raise
         * children rather than conjuring grown strangers into a spare
         * bed.
         *
         * Decline stays exactly where it was. The two are deliberately
         * not symmetric: leaving is a decision about this month, being
         * born takes nine of them. */
        if (p->happiness == 0 && p->residents > 0) {
            const char *why = "no road to Warehouse";
            char        buf[48];

            if (buildings[i].connected && tier) {
                int m;
                why = "nothing they need";
                for (m = 0; m < MAX_TIER_GOODS; m++) {
                    if (basic[m] == RES_COUNT) continue;
                    if (s->amount[basic[m]] > 0) continue;
                    snprintf(buf, sizeof(buf), "no %s", RESOURCE_NAMES[basic[m]]);
                    why = buf;
                    break;
                }
            }
            p->residents--;
            sim_log("House %d: emptying (%s), %d residents",
                    i, why, p->residents);
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
