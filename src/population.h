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

#define HOUSE_CAPACITY      10     /* max residents per house       */
#define NEEDS_INTERVAL      30.0f  /* seconds between needs checks  */
#define GOLD_PER_RESIDENT    2     /* gold generated per resident   */

/* Most goods any single population tier needs at once (Farmers need
 * 2 today; raise if a future tier needs more). */
#define MAX_TIER_GOODS 3

/* One population tier's need-list, keyed by the house BuildingType
 * that represents it. RES_COUNT in a needs[] slot means "unused" —
 * same sentinel convention as BuildingDef.consumes[]. Defined here
 * (not just in population.c) in case a future file needs to inspect
 * a tier's requirements directly (e.g. a UI showing "needs Beer"). */
typedef struct {
    BuildingType house_type;
    ResourceType needs[MAX_TIER_GOODS];
} TierDef;

/* ---- Per-house population data ------------------------- */
typedef struct {
    int   active;       /* 1 if this slot holds a House             */
    int   residents;    /* current population (0–HOUSE_CAPACITY)    */
    float timer;        /* seconds since last needs tick            */
    int   happy;        /* 1 = needs met last tick, 0 = unhappy     */
} PopData;

/* Initialise a PopData block for a newly placed house.
 * Starts with 5 residents — enough to feel alive immediately. */
void pop_init(PopData *p);

/* Called every frame for all active houses.
 * Advances timers, fires needs ticks, updates stockpile.
 * `pop`       – array of PopData, one per building slot
 * `buildings` – parallel array (same indexing) — buildings[i].connected
 *               gates whether pop[i]'s needs can be met at all this tick
 * `count`     – number of building slots to check
 * `s`         – global stockpile (read and written) */
void pop_update(PopData pop[], const Building buildings[], int count,
               Stockpile *s, float dt);

/* Return the total population across all active houses. */
int pop_total(const PopData pop[], int count);

#endif /* POPULATION_H */
