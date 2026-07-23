/*  island.c  --  Per-island map, economy and population  */

#include "island.h"
#include "connectivity.h"
#include "simclock.h"
#include <SDL3/SDL.h>
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
    SDL_strlcpy(isl->name, name ? name : "Island", ISLAND_NAME_LEN);

    /* Ownership starts empty (game_reset_world assigns the starting
     * island; colonisation/grants assign the rest) and docking open —
     * closed harbors are an owner's explicit choice, not a default. */
    isl->owner           = 0;
    isl->docking_allowed = 1;
    /* escrow[] was zeroed by the memset above. */

    SDL_Log("Island '%s' generated (seed=%u, profile=%d, settled=%d)",
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
        int                can_run = 1;
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

        b->timer++;
        if (b->timer < period) continue;
        b->timer = 0;

        for (j = 0; j < MAX_BUILDING_INPUTS; j++) {
            if (def->consumes[j] == RES_COUNT) continue;
            if (isl->stockpile.amount[def->consumes[j]] < def->consume_amt[j]) {
                SDL_Log("[%s] %s idle: needs %d %s", isl->name, def->name,
                    def->consume_amt[j], RESOURCE_NAMES[def->consumes[j]]);
                can_run = 0;
                break;
            }
        }
        if (!can_run) continue;

        for (j = 0; j < MAX_BUILDING_INPUTS; j++) {
            if (def->consumes[j] == RES_COUNT) continue;
            stockpile_add(&isl->stockpile, def->consumes[j], -def->consume_amt[j]);
        }

        if (def->produces != RES_COUNT) {
            stockpile_add(&isl->stockpile, def->produces, def->produce_amt);
            SDL_Log("[%s] %s produced %d %s  (total: %d)",
                isl->name, def->name, def->produce_amt,
                RESOURCE_NAMES[def->produces],
                isl->stockpile.amount[def->produces]);
        }
    }
}

/* ---- island_update --------------------------------------
 * Advances this island by exactly one sim tick (see the ordering
 * constraint in island.h). Takes no dt: the timestep is fixed. Discrete
 * timers count integer ticks; agent movement still advances by the
 * constant SIM_TICK_SECONDS, which is deterministic on one machine and
 * outside the F9 hash anyway. */
void island_update(Island *isl)
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
