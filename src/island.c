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
/* The average productivity of the crew standing in each building right
 * now, as a percentage (LIFE_PLAN Phase 8).
 *
 * ONE PASS OVER THE AGENTS, not one per building. An Agent carries no
 * link to the Resident walking it — agents_sync spawns one per
 * working-age resident of a house and never records which — so what an
 * agent can say is WHICH HOUSEHOLD it came from, and the household's
 * average is the granularity that supports. Adding the link would mean
 * widening a struct that is snapshotted, for a number that changes
 * every month.
 *
 * Buildings with nobody in them are left at PRODUCTIVITY_BASE; their
 * production is already gated on worker_count elsewhere. */
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
     * anything is taken from it.
     *
     * TAXING EACH CYCLE SEPARATELY ROUNDS TO NOTHING. A single Fisher's
     * Hut cycle is a few coins of wages, and a few coins times a tenth,
     * in integers, is zero — so an island of ten huts collected nothing
     * at all, which is exactly what the first run of this measured. One
     * division over the island's whole earnings has the same meaning and
     * survives the arithmetic. */
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
         * dividing it.
         *
         * The obvious form — advance * crew / 100 — TRUNCATES TO ZERO
         * for a lone worker in a bad way: work_advance(1) is 1, and
         * 1 * 85 / 100 is 0, so a single hungry fisherman would land
         * nothing at all, forever. That is a stall rather than a
         * slowdown, and it is the same rounding trap the monthly tax
         * levy hit. Scaling the period instead keeps the arithmetic
         * exact and costs nothing: Building.timer is compared against
         * `period` and read by nothing else. */
        period *= PRODUCTIVITY_BASE;

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
        /* AND WHAT THE CREW IS WORTH (LIFE_PLAN Phase 8). The headcount
         * decides how fast the clock advances; their condition decides
         * how much each of them is worth while doing it. Integer
         * throughout — this feeds a hashed timer, and a float here
         * would fail as two machines disagreeing rather than as a wrong
         * number on one. */
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

                /* ---- what the work is worth (Phase 7) --------
                 * A completed cycle earns the market value of what it
                 * made, valued at the faction's standing bid — a price
                 * that already exists, is already hashed sim state, and
                 * already moves with supply, so nothing here needs a
                 * second price table to keep in step.
                 *
                 * The crew is paid first; what is left is the business's
                 * profit. Neither is stored — no building keeps books —
                 * because the player never sees a balance sheet, only
                 * the tax. Modelling business capital and consumer
                 * spending is explicitly a later phase
                 * (docs/new-happiness-design.md).
                 *
                 * Integer throughout, including the two divisions: this
                 * feeds the treasury, the treasury is hashed, and a
                 * float here would fail as two machines disagreeing
                 * rather than as a wrong number on one. */
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

/* ---- island_update --------------------------------------
 * Advances this island by exactly one sim tick (see the ordering
 * constraint in island.h). Takes no dt: the timestep is fixed. Discrete
 * timers count integer ticks; agent movement still advances by the
 * constant SIM_TICK_SECONDS, which is deterministic on one machine and
 * outside the F9 hash anyway. */
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

/* ---- founding a household (LIFE_PLAN Phase 7) --------------
 * A house laid on the map is an empty house. It becomes a HOUSEHOLD
 * either by spending one of the island's hundred founder places — a
 * couple off a boat — or, once those are gone, out of the reserve.
 *
 * A house that can do neither STANDS EMPTY and is asked again next
 * month. That is deliberate and is the mechanic: laying roofs faster
 * than the island can raise people leaves you with roofs.
 *
 * THE ALLOWANCE IS SPENT PER HOUSE, ONCE (pop_data.founded). A house
 * that starves to empty is re-settled from the reserve or not at all;
 * without that rule a village that keeps failing quietly burns the
 * island's whole immigration quota, which is what the prototype did —
 * a hundred places turned into sixty-three houses on seed 777.
 *
 * Returns 0, 1 or 2. ONE IS A REAL ANSWER: somebody may take a roof
 * alone and wait for a spouse, so this is also called on a house that
 * already holds a single unmarried adult. */
int island_settle_house(Island *isl, int idx, uint32_t world_seed)
{
    int got, live;

    if (idx < 0 || idx >= isl->building_count) return 0;
    if (!isl->pop_data[idx].active)            return 0;

    /* THE COUNT IS THE AUTHORITY, not the residents array.
     *
     * These two normally agree — residents_sync exists to keep them
     * agreeing — but they can be out of step for a tick, and a test or
     * a snapshot may hand this function a house whose count was set
     * directly. An earlier version took `count_live_at` as the truth
     * and then ASSIGNED `live + got` back over pop_data, which silently
     * wrote a house of forty down to zero the first month it ran. Read
     * the count, add what was housed, never overwrite. */
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
    /* A MONTH OLDER, ONCE A MONTH — and some of them die of it.
     *
     * The trigger is the calendar, not a per-house timer. PopData.timer
     * runs per house and staggers with when each was built, so ageing
     * off it would have people in different streets aging at different
     * rates. CALENDAR_MONTH_TICKS is global and is the same period, so
     * every resident on every island turns a month older together.
     * That alignment is what Phase 4 was for.
     *
     * Before pop_update, so this month's household is what gets fed:
     * somebody who dies this month does not also eat this month.
     * Deaths drive the resident count DOWN; growth drives it up and
     * residents_sync follows. Keeping those two directions apart is
     * what stops the reconciliation fighting itself. */
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

        /* ---- the reserve eats (Phase 6c) ------------------
         * Half a ration of each staple per unhoused person, taken from
         * the same stockpile the houses draw on and BEFORE they do, so
         * a reserve the island cannot feed shows up as houses going
         * hungry rather than as a free crowd. Nobody in the reserve
         * works, so this is pure cost until somebody roofs them. */
        {
            int want = residents_reserve_ration(isl->residents,
                                                isl->resident_count);
            if (want > 0) {
                stockpile_add(&isl->stockpile, RES_FISH,  -want);
                stockpile_add(&isl->stockpile, RES_GRAIN, -want);
            }
        }

        /* ---- roofs with room are offered the reserve -------
         * Every month, not only when a house is laid: a house that
         * stood empty for want of settlers fills as the island grows
         * into it, and a lone occupant is offered a spouse. Fewer than
         * two live residents is the test, so both cases are the same
         * call.
         *
         * Before emigration, deliberately — somebody housed this month
         * is not then asked to leave for having waited too long. */
        for (b = 0; b < isl->building_count; b++)
            if (isl->pop_data[b].active)
                island_settle_house(isl, b, world_seed);

        /* ---- and those nobody roofed in time leave ---------
         * The only bound on population. `emigrate` is installed by the
         * world (game.c), which is the only layer that can see another
         * island to send somebody to; an island on its own can only
         * lose them. */
        isl->left_last_month = residents_emigrate(isl->residents,
                                                  isl->resident_count, tick,
                                                  isl->emigrate,
                                                  isl->emigrate_ctx);

        /* And what the island thinks of its taxes. Monthly, on the same
         * calendar trigger as everything else here, so compliance moves
         * at the pace the happiness ladder does. */
        island_update_compliance(isl);

        /* ---- and the levy (Phase 7) ------------------------
         * THE TREASURY IS THE ISLAND'S GOLD — the same RES_GOLD the
         * harbour deposits trade income into, so imports keep working
         * exactly as they did. What changed is that production no
         * longer puts gold here; tax does.
         *
         * One division over a month of earnings rather than one per
         * production cycle: see Island.tax_base for why that is
         * correctness and not tidiness. Compliance applies second, so a
         * floored island still pays its third rather than nothing. */
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
