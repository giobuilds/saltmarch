/*  island.c  --  Per-island map, economy and population  */

#include "island.h"
#include "calendar.h"
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
    /* snprintf rather than SDL_strlcpy: this file is part of the SDL-free */
    snprintf(isl->name, ISLAND_NAME_LEN, "%s", name ? name : "Island");

    /* Ownership starts empty (game_reset_world assigns the starting
     * island; colonisation/grants assign the rest) and docking open —
     * closed harbors are an owner's explicit choice, not a default. */
    isl->owner           = 0;
    isl->docking_allowed = 1;
    isl->charter_timer   = 0;
    isl->charter_arrears = 0;
    /* The settlers an island is allowed to import (LIFE_PLAN Phase 6c).
     * Everything past this has to be born here. */
    isl->founder_allowance = FOUNDER_ALLOWANCE;
    /* Tax starts modest and fully paid; unhappiness is what erodes it. */
    isl->tax_rate_permille   = TAX_RATE_DEFAULT_PERMILLE;
    isl->compliance_permille = COMPLIANCE_FULL_PERMILLE;
    isl->unhappy_streak      = 0;
    /* escrow[] was zeroed by the memset above. */

    sim_log("Island '%s' generated (seed=%u, profile=%d, settled=%d)",
            isl->name, seed, (int)profile, settled);
}

/* ---- island_tick_buildings ------------------------------ */
/* The average productivity of the crew standing in each building right
 * now, as a percentage (LIFE_PLAN Phase 8). */
static void island_crew_productivity(const Island *isl,
                                     const int house_prod[],
                                     int out[MAX_BUILDINGS])
{
    int sum[MAX_BUILDINGS], n[MAX_BUILDINGS];
    int i;

    for (i = 0; i < isl->building_count; i++) {
        sum[i] = 0; n[i] = 0; out[i] = PRODUCTIVITY_BASE;
    }

    for (i = 0; i < isl->agent_count; i++) {
        const Agent *a = &isl->agents[i];
        int          h;

        if (!a->active || a->state != AGENT_WORKING)      continue;
        if (a->work_idx < 0 || a->work_idx >= isl->building_count) continue;
        h = a->home_idx;
        if (h < 0 || h >= isl->building_count)            continue;

        sum[a->work_idx] += house_prod[h];
        n[a->work_idx]++;
    }

    for (i = 0; i < isl->building_count; i++)
        if (n[i] > 0) out[i] = sum[i] / n[i];
}

static void island_tick_buildings(Island *isl, const Faction *market,
                                  const int house_prod[])
{
    int i, j;
    int crew[MAX_BUILDINGS];
    /* The taxable base for the WHOLE ISLAND this tick, summed before
     * anything is taken from it. */
    int32_t base = 0;

    island_crew_productivity(isl, house_prod, crew);

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
        /* BOTH SIDES SCALED BY PRODUCTIVITY_BASE (LIFE_PLAN Phase 8),
         * so the crew's percentage multiplies the advance without ever
         * dividing it. */
        period *= PRODUCTIVITY_BASE;

        /* EVERY WORKER ADVANCES THE CLOCK (LIFE_PLAN Phase 1). Five
         * people in a Fisher's Hut land five fish where one lands one:
         * the def's rate is the PER-WORKER rate, not the building's. */
        /* AND WHAT THE CREW IS WORTH (LIFE_PLAN Phase 8). The headcount */
        b->timer += (uint32_t)(building_work_advance(def, b->worker_count)
                               * crew[i]);

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

                /* ---- what the work is worth (Phase 7) -------- */
                if (market) {
                    int32_t revenue = (int32_t)faction_bid(market,
                                                           def->produces)
                                    * def->produce_amt;
                    int32_t wages   = WAGE_PER_WORKER * b->worker_count;
                    int32_t profit  = revenue - wages;

                    /* A business that cannot cover its wages makes no
                     * profit; it does not make a NEGATIVE one that
                     * shelters the rest of the island from tax. */
                    if (profit < 0) profit = 0;
                    base += wages + profit;
                }
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

    /* Banked, not taxed — the levy is monthly (island_update). */
    isl->tax_base += base;
}

/* ---- island_update -------------------------------------- */
/* How many live residents name `idx` as home. */
static int count_live_at(const Island *isl, int idx)
{
    int i, n = 0;
    for (i = 0; i < isl->resident_count; i++)
        if (isl->residents[i].active && isl->residents[i].home_idx == idx) n++;
    return n;
}

/* The same adapter for rations. */
static int island_mouths_at(const void *ctx, int house_idx)
{
    const Island *isl = (const Island *)ctx;
    return residents_mouths_at(isl->residents, isl->resident_count, house_idx);
}

/* ---- founding a household (LIFE_PLAN Phase 7) -------------- */
int island_settle_house(Island *isl, int idx, uint32_t world_seed)
{
    int got, live;

    if (idx < 0 || idx >= isl->building_count) return 0;
    if (!isl->pop_data[idx].active)            return 0;

    /* THE COUNT IS THE AUTHORITY, not the residents array. */
    live = isl->pop_data[idx].residents;
    if (live >= 2) return 0;                    /* a household already */

    if (live == 0 && count_live_at(isl, idx) == 0
        && !isl->pop_data[idx].founded && isl->founder_allowance > 0) {
        got = residents_found_pair(isl->residents, &isl->resident_count,
                                   &isl->next_resident_id, idx, world_seed);
        if (got) isl->founder_allowance--;
    } else {
        got = residents_settle_house(isl->residents, isl->resident_count, idx);
    }

    if (got > 0) isl->pop_data[idx].founded = 1;
    isl->pop_data[idx].residents += got;
    return got;
}

int island_tax_happiness(const Island *isl)
{
    int32_t over = isl->tax_rate_permille - TAX_RATE_DEFAULT_PERMILLE;

    if (over <= 0) return 0;
    /* Linear from the default up to the maximum, then clamped. Integer,
     * and the clamp is what makes it a contributor rather than a
     * dominator. */
    {
        int32_t span = TAX_RATE_MAX_PERMILLE - TAX_RATE_DEFAULT_PERMILLE;
        int     rungs = (int)((over * TAX_HAPPINESS_MAX + span - 1) / span);
        return -(rungs > TAX_HAPPINESS_MAX ? TAX_HAPPINESS_MAX : rungs);
    }
}

void island_update_compliance(Island *isl)
{
    int i, houses = 0, unhappy = 0;

    for (i = 0; i < isl->building_count; i++) {
        if (!isl->pop_data[i].active || isl->pop_data[i].residents <= 0)
            continue;
        houses++;
        if (isl->pop_data[i].happiness < HAPPINESS_NEUTRAL) unhappy++;
    }
    if (houses == 0) return;

    /* An island is "unhappy" when most of its households are. */
    if (unhappy * 2 > houses) isl->unhappy_streak++;
    else                      isl->unhappy_streak = 0;

    if (isl->unhappy_streak > COMPLIANCE_PATIENCE_TICKS) {
        /* Only now, and only one step. */
        isl->compliance_permille -= COMPLIANCE_STEP_PERMILLE;
    } else if (isl->unhappy_streak == 0) {
        /* Recovery is twice as fast as decline: trust returns quicker
         * than it is lost, so a rescued island is not condemned by the
         * quarter it had. */
        isl->compliance_permille += COMPLIANCE_STEP_PERMILLE * 2;
    }

    if (isl->compliance_permille < COMPLIANCE_MIN_PERMILLE)
        isl->compliance_permille = COMPLIANCE_MIN_PERMILLE;
    if (isl->compliance_permille > COMPLIANCE_FULL_PERMILLE)
        isl->compliance_permille = COMPLIANCE_FULL_PERMILLE;
}

void island_update(Island *isl, uint32_t world_seed, uint64_t tick,
                   const Faction *market)
{
    /* Per-house tallies, built in one pass over the residents and one
     * over the agents rather than one scan per house. Rebuilt after
     * the demography and the needs tick have moved people. */
    int live[MAX_BUILDINGS], workers[MAX_BUILDINGS];
    int house_prod[MAX_BUILDINGS], live_agents[MAX_BUILDINGS];
    int happiness[MAX_BUILDINGS];
    int b;

    if (!isl->settled) return;

    for (b = 0; b < isl->building_count; b++)
        happiness[b] = isl->pop_data[b].happiness;
    residents_tally(isl->residents, isl->resident_count, isl->building_count,
                    happiness, NULL, NULL, house_prod);

    /* Recompute road-network reachability before anything this tick
     * reads Building.connected. */
    connectivity_update(isl->buildings, isl->building_count,
                        &isl->conn_sig);

    /* island_tick_buildings() reads worker_count as of the END of last
     * tick's agents_update() call below — a harmless one-tick lag, the
     * same pattern already established for `connected` relative to a
     * newly-placed building. */
    island_tick_buildings(isl, market, house_prod);

    /* Population needs (uses this tick's `connected`). */
    /* A MONTH OLDER, ONCE A MONTH — and some of them die of it. */
    if (tick % CALENDAR_MONTH_TICKS == 0) {
        residents_age(isl->residents, isl->resident_count, isl->pop_data,
                      world_seed, tick);
        /* After ageing, so somebody who died this month does not marry
         * this month — and so this month's new adults are eligible on
         * the month they become adults rather than the one after. */
        residents_marry(isl->residents, isl->resident_count,
                        isl->pop_data, isl->building_count,
                        world_seed, tick);
        /* And after marriage, so a couple wed this month may begin one
         * this month. This is the ONLY thing that grows a house now
         * (LIFE_PLAN Phase 6b) — pop_update below can still empty one,
         * but it can no longer fill one. */
        residents_breed(isl->residents, &isl->resident_count,
                        &isl->next_resident_id, isl->pop_data,
                        isl->building_count, world_seed, tick);

        /* ---- the reserve eats (Phase 6c) ------------------ */
        {
            int want = residents_reserve_ration(isl->residents,
                                                isl->resident_count);
            if (want > 0) {
                stockpile_add(&isl->stockpile, RES_FISH,  -want);
                stockpile_add(&isl->stockpile, RES_GRAIN, -want);
            }
        }

        /* ---- roofs with room are offered the reserve ------- */
        for (b = 0; b < isl->building_count; b++)
            if (isl->pop_data[b].active)
                island_settle_house(isl, b, world_seed);

        /* ---- and those nobody roofed in time leave --------- */
        isl->left_last_month = residents_emigrate(isl->residents,
                                                  isl->resident_count, tick,
                                                  isl->emigrate,
                                                  isl->emigrate_ctx);

        /* And what the island thinks of its taxes. Monthly, on the same
         * calendar trigger as everything else here, so compliance moves
         * at the pace the happiness ladder does. */
        island_update_compliance(isl);

        /* ---- and the levy (Phase 7) ------------------------ */
        {
            int32_t taxed = isl->tax_base * isl->tax_rate_permille / 1000;

            taxed = taxed * isl->compliance_permille / 1000;
            if (taxed > 0) stockpile_add(&isl->stockpile, RES_GOLD, taxed);
            isl->tax_last_month = taxed;
            isl->tax_base       = 0;
        }
    }

    pop_update(isl->pop_data, isl->buildings, isl->building_count,
               &isl->stockpile, island_mouths_at, isl,
               island_tax_happiness(isl));

    /* Reconcile agents[] against the residents counts pop_update() may
     * have just changed, periodically assign jobs, then advance every
     * agent's state machine/position and retally worker_count for next
     * tick's island_tick_buildings(). */
    for (b = 0; b < isl->building_count; b++)
        happiness[b] = isl->pop_data[b].happiness;
    residents_tally(isl->residents, isl->resident_count, isl->building_count,
                    happiness, live, workers, NULL);
    agents_tally(isl->agents, isl->agent_count, isl->building_count,
                 live_agents);

    agents_sync(isl->agents, &isl->agent_count, isl->buildings,
                isl->pop_data, isl->building_count, workers, live_agents);

    /* The same reconciliation, over identity rather than motion. After
     * agents_sync so the two see the same pop_data in the same tick. */
    residents_sync(isl->residents, &isl->resident_count,
                   &isl->next_resident_id, isl->buildings, isl->pop_data,
                   isl->building_count, world_seed, live);

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

/* ---- trade capacity (MARITIME_PLAN Phase 2) -------------- */
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
