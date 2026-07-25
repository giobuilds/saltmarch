#ifndef POPULATION_H
#define POPULATION_H

/* =========================================================
 * population.h  --  Residents and needs  (Phase 5)
 *
 * DESIGN
 * ======
 * Each House building has a PopData block that tracks:
 *   - current resident count (0–HOUSE_CAPACITY)
 *   - a needs timer (fires every NEEDS_INTERVAL seconds)
 *   - a happiness flag (1 = needs met last tick, 0 = not)
 *
 * On each needs tick:
 *   If the house is road-connected to a Warehouse (Phase 3) AND every
 *   good its tier's TierDef (below) lists is in stock:
 *     consume 1 of each listed good
 *     generate GOLD_PER_RESIDENT * residents Gold
 *     happiness = 1
 *     if residents < HOUSE_CAPACITY: residents++  (growth)
 *   Else:
 *     happiness = 0
 *     if residents > 0: residents--  (decline)
 *
 * Production chains, Phase 1: a house's needs list is now data-driven
 * by its actual BuildingType (Farmers' BUILDING_HOUSE vs. Workers'
 * BUILDING_HOUSE_WORKER — see TIER_DEFS in population.c), all-or-
 * nothing across however many goods that tier lists, same as
 * game_tick_buildings' multi-input production. Upgrading a house
 * (game_upgrade_house, game.c) is just mutating its BuildingType in
 * place — this PopData block, and everything else that indexes by
 * building slot, needs no migration.
 *
 * PopData blocks are stored in a parallel array in GameState
 * indexed by building slot — pop_data[i] corresponds to
 * buildings[i].  Non-house buildings have inactive PopData.
 * ========================================================= */

#include "resource.h"
#include "building.h"   /* Phase 3: Building.connected */
#include "simclock.h"   /* Phase 1b: fixed-tick clock  */
#include <stdint.h>

#define HOUSE_CAPACITY      10     /* max residents per house       */
#define NEEDS_INTERVAL      30.0f  /* seconds between needs checks  */
/* The needs check fires every NEEDS_INTERVAL seconds, counted in whole
 * sim ticks so the F9 hash never reads an accumulating float. */
#define NEEDS_INTERVAL_TICKS \
    ((uint32_t)(NEEDS_INTERVAL * SIM_TICKS_PER_SEC))
#define GOLD_PER_RESIDENT    2     /* gold generated per resident   */

/* Gold to walk the first tier's upgrade edge. Lives here rather than
 * in game.h since SUPPLY_CHAIN Phase 2: the price of an edge belongs
 * beside the edge, and each tier now carries its own in
 * TierDef.upgrade_gold. */
#define TIER_UPGRADE_COST_GOLD 300

/* Most goods any single population tier needs at once. Raised from 3
 * in SUPPLY_CHAIN Phase 2: Artisans and Investors both list five. */
#define MAX_TIER_GOODS 5

/* One population tier's need-list, keyed by the house BuildingType
 * that represents it. RES_COUNT in a needs[] slot means "unused" —
 * same sentinel convention as BuildingDef.consumes[]. Defined here
 * (not just in population.c) in case a future file needs to inspect
 * a tier's requirements directly (e.g. a UI showing "needs Beer").
 *
 * ---- the upgrade edge (SUPPLY_CHAIN Phase 2) ----
 * `next_tier` is what this house upgrades INTO, or BUILDING_NONE for a
 * tier with nowhere to go. That makes the tier model a graph rather
 * than a ladder, which is what the plan's three house lines need:
 * Marshfolk → Artisans, Wrights → Engineers, Merchants → Investors are
 * three edges in this table, not three branches in code. The Academy's
 * "any house → Scholars" is a fourth.
 *
 * `upgrade_gold` is what that edge costs, per tier rather than one
 * global constant, since a Merchant house is not priced like a
 * cottage.
 *
 * `requires_building` is a building the island must have, active and
 * connected, before the edge can be walked — BUILDING_NONE for "no
 * prerequisite". It exists for the Academy (Phase 8) and is otherwise
 * unused today; the rule is written now so Phase 8 adds a table row
 * rather than a special case. */
typedef struct {
    BuildingType house_type;
    ResourceType needs[MAX_TIER_GOODS];
    BuildingType next_tier;
    int          upgrade_gold;
    BuildingType requires_building;
} TierDef;

/* The tier a house type belongs to, or NULL if it is not residential.
 * Exposed (rather than kept static in population.c) so the confirm
 * popup can show a tier's needs without keeping a second copy of the
 * table — the drift that would follow is exactly what UI_PLAN
 * decision 3 exists to prevent. */
const TierDef *tier_def_for(BuildingType type);

/* What `from` must have alongside it before it can upgrade, or
 * BUILDING_NONE. The caller looks the building up in its own world —
 * the sim in GameState, the UI in its snapshot — because that lookup
 * is the one part of the rule that genuinely differs between them. */
BuildingType tier_upgrade_requires(BuildingType from);

/* May a house of type `from` upgrade, given this island's stock and
 * whether tier_upgrade_requires()'s building is present?
 *
 * THE shared rule (UI_PLAN decision 3): sim_upgrade_house calls it to
 * decide, and the confirm popup calls it to predict, so the checklist
 * a player reads and the verdict they get cannot disagree. Returns
 * REJ_OK and writes *out_to when the upgrade may proceed; otherwise a
 * reason and BUILDING_NONE.
 *
 *   REJ_UNAVAILABLE    – not a house, or nowhere to upgrade to
 *   REJ_NEEDS_BUILDING – the prerequisite is missing
 *   REJ_NEEDS_GOODS    – the next tier's needs are not all in stock
 *   REJ_CANT_AFFORD    – not enough Gold
 *
 * Needs are checked for PRESENCE, not consumed: they are what the tier
 * will want every needs tick from then on, so requiring them is
 * asking "can you keep this neighbourhood supplied", not charging a
 * one-off price. */
RejectReason tier_upgrade_check(BuildingType from,
                                const int stock[RES_COUNT],
                                int prereq_present,
                                BuildingType *out_to);

/* The same rule against tiers the caller supplies, with
 * tier_upgrade_check() as the table-driven wrapper over it. The seam
 * exists for the same reason building_place_check_def()'s does: the
 * table has two tiers today and the rule has to be provable at five
 * needs and with a prerequisite building, neither of which exists
 * until Phases 4 and 8. */
RejectReason tier_upgrade_check_def(const TierDef *tier, const TierDef *next,
                                    const int stock[RES_COUNT],
                                    int prereq_present,
                                    BuildingType *out_to);

/* ---- Per-house population data ------------------------- */
typedef struct {
    int      active;    /* 1 if this slot holds a House             */
    int      residents; /* current population (0–HOUSE_CAPACITY)    */
    uint32_t timer;     /* sim ticks since last needs tick          */
    int      happy;     /* 1 = needs met last tick, 0 = unhappy     */
} PopData;

/* Initialise a PopData block for a newly placed house.
 * Starts with 5 residents — enough to feel alive immediately. */
void pop_init(PopData *p);

/* Called once per sim tick for all active houses.
 * Advances timers, fires needs ticks, updates stockpile.
 * `pop`       – array of PopData, one per building slot
 * `buildings` – parallel array (same indexing) — buildings[i].connected
 *               gates whether pop[i]'s needs can be met at all this tick
 * `count`     – number of building slots to check
 * `s`         – island stockpile (read and written)
 * Takes no dt: it advances one fixed tick. */
void pop_update(PopData pop[], const Building buildings[], int count,
               Stockpile *s);

/* Return the total population across all active houses. */
int pop_total(const PopData pop[], int count);

/* Returns 1 if `type` is a residential building — i.e. one that has a
 * TierDef and so is driven by pop_update(). Callers must use this
 * rather than testing `type == BUILDING_HOUSE`: agents_sync() (agent.c)
 * did exactly that and consequently stopped managing agents entirely
 * for any house upgraded to BUILDING_HOUSE_WORKER, while its PopData
 * kept updating. One predicate, one place to update per new tier. */
int pop_is_house_type(BuildingType type);

#endif /* POPULATION_H */
