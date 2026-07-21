/*  island.c  --  Per-island map, economy and population  */

#include "island.h"
#include "connectivity.h"
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
static void island_tick_buildings(Island *isl, float dt)
{
    int i, j;
    for (i = 0; i < isl->building_count; i++) {
        Building          *b   = &isl->buildings[i];
        const BuildingDef *def = &BUILDING_DEFS[b->type];
        int                can_run = 1;

        if (!b->active || def->tick_seconds <= 0.0f) continue;
        if (!b->connected) continue;      /* needs a road to a Warehouse   */
        if (b->worker_count < 1) continue; /* needs a worker present       */

        b->timer += dt;
        if (b->timer < def->tick_seconds) continue;
        b->timer = 0.0f;

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

/* ---- island_update -------------------------------------- */
void island_update(Island *isl, float dt)
{
    if (!isl->settled) return;

    /* Recompute road-network reachability before anything this frame
     * reads Building.connected. */
    connectivity_update(isl->buildings, isl->building_count);

    /* island_tick_buildings() reads worker_count as of the END of last
     * frame's agents_update() call below — a harmless one-frame lag,
     * the same pattern already established for `connected` relative to
     * a newly-placed building. */
    island_tick_buildings(isl, dt);

    /* Population needs (uses this frame's `connected`). */
    pop_update(isl->pop_data, isl->buildings, isl->building_count,
               &isl->stockpile, dt);

    /* Reconcile agents[] against the residents counts pop_update() may
     * have just changed, periodically assign jobs, then advance every
     * agent's state machine/position and retally worker_count for next
     * frame's island_tick_buildings(). */
    agents_sync(isl->agents, &isl->agent_count, isl->buildings,
                isl->pop_data, isl->building_count);

    isl->agent_assign_timer += dt;
    if (isl->agent_assign_timer >= AGENT_ASSIGN_INTERVAL) {
        isl->agent_assign_timer = 0.0f;
        agents_assign_jobs(isl->agents, isl->agent_count,
                           isl->buildings, isl->building_count);
    }

    agents_update(isl->agents, isl->agent_count, isl->buildings,
                  isl->building_count, dt);
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
