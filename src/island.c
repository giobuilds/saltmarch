/*  island.c  --  Per-island map, economy and population  */

#include "island.h"
#include "connectivity.h"
#include "simclock.h"
#include "simlog.h"
#include <stdio.h>
#include <string.h>

/* ---- island_reset -------------------------------------- */
void island_reset(Island *isl, uint32_t seed, MapProfile profile,
                  const char *name, int settled)
{
    memset(isl, 0, sizeof(*isl));

    map_init(&isl->map, seed, profile);
    camera_init(&isl->camera, SCREEN_W, SCREEN_H, MAP_COLS, MAP_ROWS);

    stockpile_init(&isl->stockpile);

    isl->profile = profile;
    isl->settled = settled;
    /* snprintf rather than SDL_strlcpy: this file is part of the SDL-free
     * sim library (MMO_PLAN Phase 6). NOT strncpy — it does not
     * null-terminate on truncation and MSVC deprecates it, so /WX turns
     * it into a build failure; see set_reason() in building.c, which
     * already learned this. */
    snprintf(isl->name, ISLAND_NAME_LEN, "%s", name ? name : "Island");

    /* Ownership starts empty (game_reset_world assigns the starting
     * island; colonisation/grants assign the rest) and docking open —
     * closed harbors are an owner's explicit choice, not a default. */
    isl->owner           = 0;
    isl->docking_allowed = 1;
    isl->charter_timer   = 0;
    isl->charter_arrears = 0;
    /* escrow[] was zeroed by the memset above. */

    sim_log("Island '%s' generated (seed=%u, profile=%d, settled=%d)",
            isl->name, seed, (int)profile, settled);
}

/* ---- island_tick_buildings ------------------------------
 * Multi-input, all-or-nothing: every non-RES_COUNT slot in
 * def->consumes[] must have enough stock before ANY of them are
 * consumed (checked in a full pass first, so a building never
 * partially consumes one input while lacking another). Single-input
 * buildings behave exactly as before, since their second slot is
 * always RES_COUNT.
 *
 * Reads and writes only THIS island's stockpile — a Malthouse can
 * only consume Grain and Hops stored on its own island. */
static void island_tick_buildings(Island *isl)
{
    int i, j;
    for (i = 0; i < isl->building_count; i++) {
        Building          *b   = &isl->buildings[i];
        const BuildingDef *def = &BUILDING_DEFS[b->type];
        int                can_run, produced;
        uint32_t           period;

        if (!b->active || def->tick_seconds <= 0.0f) continue;
        if (!b->connected) continue;      /* needs a road to a Warehouse   */
        if (b->worker_count < 1) continue; /* needs a worker present       */

        /* Production fires every `period` sim ticks. The float
         * tick_seconds stays the authoring unit; the sim counts in whole
         * integer ticks so the F9 hash never sees an accumulating float.
         * +0.5 rounds to the nearest tick. */
        period = (uint32_t)(def->tick_seconds * SIM_TICKS_PER_SEC + 0.5f);
        if (period == 0) period = 1;

        /* EVERY WORKER ADVANCES THE CLOCK (LIFE_PLAN Phase 1). Five
         * people in a Fisher's Hut land five fish where one lands one:
         * the def's rate is the PER-WORKER rate, not the building's.
         *
         * Integer, and the arithmetic is the point — a rate expressed
         * as "workers per period" needs no division and no float, so
         * nothing here can round differently on another machine.
         *
         * `-= period` rather than `= 0`, so a crew that earns more than
         * one unit in a tick keeps the remainder instead of having it
         * thrown away. At one worker the timer lands exactly on `period`
         * and the two are identical, which is what made Phase 1's only
         * behavioural change the headcount itself.
         *
         * And a FULL crew is worth more than the sum of its hands
         * (Phase 2): building_work_advance() returns 2w-1, because one
         * worker alone pays an overhead the second one arrives to find
         * already paid. Still integer, still no division. */
        b->timer += (uint32_t)building_work_advance(def, b->worker_count);

        produced = 0;
        while (b->timer >= period) {
            b->timer -= period;

            can_run = building_missing_input(def, &isl->stockpile);
            if (can_run >= 0) {
                sim_log("[%s] %s idle: needs %d %s", isl->name, def->name,
                    def->consume_amt[can_run],
                    RESOURCE_NAMES[def->consumes[can_run]]);
                /* A shortage costs the whole accumulation, exactly as it
                 * did when the timer reset before the input check. */
                b->timer = 0;
                break;
            }

            for (j = 0; j < MAX_BUILDING_INPUTS; j++) {
                if (def->consumes[j] == RES_COUNT) continue;
                stockpile_add(&isl->stockpile, def->consumes[j],
                              -def->consume_amt[j]);
            }

            if (def->produces != RES_COUNT) {
                stockpile_add(&isl->stockpile, def->produces, def->produce_amt);
                produced += def->produce_amt;
            }
        }

        /* One line per building per tick, not one per unit: a six-hand
         * Foundry would otherwise say the same thing six times. */
        if (produced > 0)
            sim_log("[%s] %s produced %d %s  (total: %d)",
                isl->name, def->name, produced,
                RESOURCE_NAMES[def->produces],
                isl->stockpile.amount[def->produces]);
    }
}

/* ---- island_update --------------------------------------
 * Advances this island by exactly one sim tick (see the ordering
 * constraint in island.h). Takes no dt: the timestep is fixed. Discrete
 * timers count integer ticks; agent movement still advances by the
 * constant SIM_TICK_SECONDS, which is deterministic on one machine and
 * outside the F9 hash anyway. */
void island_update(Island *isl, uint32_t world_seed)
{
    if (!isl->settled) return;

    /* Recompute road-network reachability before anything this tick
     * reads Building.connected. */
    connectivity_update(isl->buildings, isl->building_count);

    /* island_tick_buildings() reads worker_count as of the END of last
     * tick's agents_update() call below — a harmless one-tick lag, the
     * same pattern already established for `connected` relative to a
     * newly-placed building. */
    island_tick_buildings(isl);

    /* Population needs (uses this tick's `connected`). */
    pop_update(isl->pop_data, isl->buildings, isl->building_count,
               &isl->stockpile);

    /* Reconcile agents[] against the residents counts pop_update() may
     * have just changed, periodically assign jobs, then advance every
     * agent's state machine/position and retally worker_count for next
     * tick's island_tick_buildings(). */
    agents_sync(isl->agents, &isl->agent_count, isl->buildings,
                isl->pop_data, isl->building_count);

    /* The same reconciliation, over identity rather than motion. After
     * agents_sync so the two see the same pop_data in the same tick. */
    residents_sync(isl->residents, &isl->resident_count,
                   &isl->next_resident_id, isl->buildings, isl->pop_data,
                   isl->building_count, world_seed);

    if (++isl->agent_assign_timer >= AGENT_ASSIGN_INTERVAL_TICKS) {
        isl->agent_assign_timer = 0;
        agents_assign_jobs(isl->agents, isl->agent_count,
                           isl->buildings, isl->building_count);
    }

    agents_update(isl->agents, isl->agent_count, isl->buildings,
                  isl->building_count, SIM_TICK_SECONDS);
}

/* ---- island_recompute_storage_capacity ------------------ */
void island_recompute_storage_capacity(Island *isl)
{
    int i, warehouses = 0;

    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_WAREHOUSE)
            warehouses++;

    stockpile_set_capacity(&isl->stockpile,
        BASE_STORAGE_CAP + warehouses * WAREHOUSE_STORAGE_BONUS);
}

/* ---- trade capacity (MARITIME_PLAN Phase 2) --------------
 * Derived from the buildings standing, never stored: capacity that
 * followed a demolished Merchant House only because someone remembered
 * to decrement it would eventually be wrong, and it would be wrong in
 * the hashed state, which is the expensive kind of wrong.
 *
 * A house with nobody in it supplies no merchant. That is the whole
 * reason the population line and the trade line touch at all — an
 * unfed Merchant House loses residents and, with them, the trade it was
 * carrying capacity for. */
int island_merchant_capacity(const Island *isl)
{
    int i, n = TRADE_BASE_MERCHANTS;

    if (!isl->settled) return 0;
    if (isl->owner == PLAYER_FACTION) return FACTION_PORT_MERCHANTS;

    for (i = 0; i < isl->building_count; i++) {
        const Building *b = &isl->buildings[i];
        if (!b->active) continue;
        if (!isl->pop_data[i].active || isl->pop_data[i].residents <= 0)
            continue;
        if (b->type == BUILDING_HOUSE_MERCHANT)
            n += TRADE_MERCHANTS_PER_HOUSE;
        else if (b->type == BUILDING_HOUSE_INVESTOR)
            n += TRADE_MERCHANTS_PER_INVESTOR;
    }
    return n;
}

int island_hull_capacity(const Island *isl)
{
    int i, n = TRADE_BASE_HULLS;

    if (!isl->settled) return 0;
    if (isl->owner == PLAYER_FACTION) return FACTION_PORT_HULLS;

    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_SHIPYARD)
            n += TRADE_HULLS_PER_SHIPYARD;
    return n;
}

int island_scholar_capacity(const Island *isl)
{
    int i, n = 0;

    if (!isl->settled) return 0;

    for (i = 0; i < isl->building_count; i++) {
        const Building *b = &isl->buildings[i];
        if (!b->active || b->type != BUILDING_HOUSE_SCHOLAR) continue;
        if (!isl->pop_data[i].active || isl->pop_data[i].residents <= 0)
            continue;
        n++;
    }
    return n;
}

int island_can_survey(const Island *isl)
{
    return isl->scholars_out < island_scholar_capacity(isl) &&
           isl->research_boats_out < isl->research_boats &&
           isl->stockpile.amount[RES_CHARTS] > 0;
}

int island_can_dispatch(const Island *isl)
{
    return isl->merchants_out < island_merchant_capacity(isl) &&
           isl->hulls_out     < island_hull_capacity(isl);
}

uint32_t island_escrow_nonce(const Island *isl)
{
    uint32_t h = 2166136261u;
    int      r;

    for (r = 0; r < RES_COUNT; r++) {
        h ^= (uint32_t)isl->escrow[r];
        h *= 16777619u;
    }
    h ^= (uint32_t)isl->docking_allowed;
    h *= 16777619u;
    return h ? h : 1u;
}
