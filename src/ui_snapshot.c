/* ui_snapshot.c  --  Taking the picture (UI_PLAN Phase 0) */

#include "ui_snapshot.h"
#include "sea.h"
#include "pirate.h"
#include "survey.h"
#include "knowledge.h"
#include "orderbook.h"
#include "game.h"
#include "faction.h"
#include "population.h"
#include "simclock.h"
#include "resident.h"
#include "calendar.h"
#include <stdio.h>
#include <string.h>

/* How interesting a resident is to the player right now. Coarse bands
 * rather than a formula: unhoused first, then hungry, then the extremes
 * of age and service. Ties break on id so the cast does not reshuffle
 * under the cursor between frames. */
static int cast_notability(const Resident *r, const Island *isl)
{
    int score = 0, stage = resident_stage(r);

    if (r->home_idx == RESIDENT_HOMELESS) score += 100;
    else if (isl->pop_data[r->home_idx].happiness < HAPPINESS_NEUTRAL)
        score += 60;

    if (stage == LIFE_RETIRED)                        score += 25;
    if (r->pregnancy > 0)                             score += 30;
    if (r->tenure_months >= (uint32_t)PROD_TENURE_FULL) score += 20;
    if (r->children >= 4)                             score += 10;
    return score;
}

/* Fills out->cast with the most notable residents, most notable first. */
static void snapshot_cast(UiIsland *out, const Island *isl,
                          uint32_t world_seed)
{
    int taken[UI_CAST_MAX], n = 0, i, k;

    out->cast_count = 0;

    /* Selection sort over the residents rather than sorting the whole
     * array: the cast is ten of five hundred. */
    for (k = 0; k < UI_CAST_MAX; k++) {
        int best = -1, best_score = -1;

        for (i = 0; i < isl->resident_count; i++) {
            const Resident *r = &isl->residents[i];
            int             sc, j, already = 0;

            if (!r->active) continue;
            for (j = 0; j < n; j++) if (taken[j] == i) already = 1;
            if (already) continue;

            sc = cast_notability(r, isl);
            if (sc > best_score ||
                (sc == best_score && best >= 0 &&
                 r->id < isl->residents[best].id)) {
                best = i; best_score = sc;
            }
        }
        if (best < 0) break;
        taken[n++] = best;
    }

    for (k = 0; k < n; k++) {
        const Resident *r = &isl->residents[taken[k]];
        UiResident     *u = &out->cast[k];
        int             h = r->home_idx;

        memset(u, 0, sizeof(*u));
        u->id            = r->id;
        u->age_years     = r->age_months / MONTHS_PER_YEAR;
        u->stage         = resident_stage(r);
        u->home_idx      = h;
        u->work_idx      = -1;
        u->tenure_months = (int32_t)r->tenure_months;
        u->children      = r->children;
        u->married       = (uint8_t)(r->spouse >= 0);
        u->sex           = (uint8_t)r->sex;
        u->happiness     = h >= 0 ? isl->pop_data[h].happiness : -1;
        u->productivity  = resident_productivity(r,
                               h >= 0 ? isl->pop_data[h].happiness
                                      : HAPPINESS_NEUTRAL);
        resident_name(r, world_seed, u->name, sizeof(u->name));

        /* An Agent carries no link back to a Resident, so this names
         * the first workplace staffed from their household -- what the
         * household-level model can honestly say. */
        if (h >= 0) {
            int a;
            for (a = 0; a < isl->agent_count; a++)
                if (isl->agents[a].active && isl->agents[a].home_idx == h
                    && isl->agents[a].work_idx >= 0) {
                    u->work_idx = isl->agents[a].work_idx;
                    snprintf(u->workplace, sizeof(u->workplace), "%s",
                             BUILDING_DEFS[
                                 isl->buildings[u->work_idx].type].name);
                    break;
                }
        }
    }
    out->cast_count = n;
}

static void snapshot_island(UiIsland *out, const Island *isl,
                            uint32_t local_player, uint32_t world_seed)
{
    int i;

    memcpy(out->name, isl->name, ISLAND_NAME_LEN);
    out->name[ISLAND_NAME_LEN - 1] = '\0';

    out->settled         = (uint8_t)(isl->settled ? 1 : 0);
    out->docking_allowed = (uint8_t)(isl->docking_allowed ? 1 : 0);
    out->owner           = isl->owner;
    out->capacity        = isl->stockpile.capacity;
    out->escrow_nonce    = island_escrow_nonce(isl);
    out->residents       = pop_total(isl->pop_data, isl->building_count);
    snapshot_cast(out, isl, world_seed);

    /* Phase 7. The reserve is counted here rather than stored on the
     * island: it is a property of who has a roof, and one loop over the
     * residents is cheaper than a field that could disagree. */
    out->reserve             = residents_reserve_count(isl->residents,
                                                       isl->resident_count);
    out->founder_allowance   = isl->founder_allowance;
    out->left_last_month     = isl->left_last_month;
    out->tax_rate_permille   = isl->tax_rate_permille;
    out->compliance_permille = isl->compliance_permille;
    out->tax_last_month      = isl->tax_last_month;
    {
        int b;
        out->homes_empty = 0;
        for (b = 0; b < isl->building_count; b++)
            if (isl->pop_data[b].active && isl->pop_data[b].residents == 0)
                out->homes_empty++;
    }

    /* Whether what follows is knowledge or absence (UI_PLAN N1). The
     * test is ownership because that is exactly what redact_for() keys
     * on: your own islands come through whole, everybody else's come
     * through as their public face and a great many zeroes. */
    out->detail_known = (uint8_t)(isl->owner == local_player ? 1 : 0);

    out->merchants_out     = isl->merchants_out;
    out->merchant_capacity = island_merchant_capacity(isl);
    out->hulls_out         = isl->hulls_out;
    out->hull_capacity     = island_hull_capacity(isl);
    out->scholars_out      = isl->scholars_out;
    out->scholar_capacity  = island_scholar_capacity(isl);
    out->research_boats    = isl->research_boats;
    out->insure_shipments  = (uint8_t)(isl->insure_shipments ? 1 : 0);

    for (i = 0; i < RES_COUNT; i++) {
        out->stock[i]  = isl->stockpile.amount[i];
        out->escrow[i] = isl->escrow[i];
    }

    /* Buildings are copied in slot order, INCLUDING inactive slots. */
    out->building_count = isl->building_count;
    for (i = 0; i < isl->building_count && i < MAX_BUILDINGS; i++) {
        const Building *b = &isl->buildings[i];
        UiBuilding     *u = &out->buildings[i];

        u->type         = (int16_t)b->type;
        u->row          = (int16_t)b->row;
        u->col          = (int16_t)b->col;
        u->active       = (uint8_t)(b->active ? 1 : 0);
        u->connected    = (uint8_t)(b->connected ? 1 : 0);
        u->worker_count = (uint8_t)(b->worker_count > 255 ? 255
                                                          : b->worker_count);
        u->residents    = (uint8_t)(isl->pop_data[i].active
                                    ? isl->pop_data[i].residents : 0);
        u->happiness    = (uint8_t)(isl->pop_data[i].active
                                    ? isl->pop_data[i].happiness
                                    : HAPPINESS_NEUTRAL);
        u->origin_tier  = (int16_t)isl->pop_data[i].origin_tier;
    }
}

/* The bounds this header declares must actually hold the sim's. */
typedef char ui_snapshot_bounds_check[
    (UI_MAX_ORDERS   >= ORDERBOOK_MAX_ORDERS   &&
     UI_MAX_BOOKINGS >= ORDERBOOK_MAX_BOOKINGS &&
     UI_MAX_ROUTES   >= SEA_MAX_ROUTES         &&
     UI_MAX_PAIRS    >= SEA_MAX_PAIRS          &&
     UI_MAX_SURVEYS  >= MAX_SURVEYS            &&
     UI_MAX_PIRATES  >= MAX_PIRATES) ? 1 : -1];

/* ---- the maritime world (UI_PLAN N1) ---------------------- */
static void snapshot_market(UiSnapshot *out, const struct GameState *gs)
{
    int i, n;

    n = 0;
    for (i = 0; i < gs->book.order_count && n < UI_MAX_ORDERS; i++) {
        const Order *o = &gs->book.order[i];
        UiOrder     *u;

        if (!o->active) continue;
        u = &out->order[n++];
        u->id          = o->id;
        u->owner       = o->owner;
        u->island      = o->island;
        u->kind        = o->what.kind;
        u->what        = o->what.id;
        u->side        = o->side;
        u->qty         = o->qty;
        u->limit       = o->limit;
        u->placed_tick = o->placed_tick;
        u->mine        = (uint8_t)(o->owner == gs->local_player_id);
    }
    out->order_count = n;

    n = 0;
    for (i = 0; i < gs->book.booking_count && n < UI_MAX_BOOKINGS; i++) {
        const Booking *b = &gs->book.booking[i];
        UiBooking     *u;

        if (!b->active) continue;
        u = &out->booking[n++];
        u->kind        = b->what.kind;
        u->what        = b->what.id;
        u->qty         = b->qty;
        u->price       = b->price;
        u->from_island = b->from_island;
        u->to_island   = b->to_island;
        u->route_id    = b->route_id;
        u->arrive_tick = b->arrive_tick;
        u->delivered   = (uint8_t)(b->delivered ? 1 : 0);
        u->raided      = (uint8_t)(b->raided ? 1 : 0);
        u->mine        = (uint8_t)(b->buyer  == gs->local_player_id ||
                                   b->seller == gs->local_player_id);
    }
    out->booking_count = n;

    /* What the local player knows of the sea. Only ever their own —
     * the rest is not in this process to copy. */
    for (i = 0; i < UI_MAX_ROUTES; i++) {
        int priv = (i < gs->sea.route_count) ? gs->sea.route[i].is_private : 0;
        out->chart_held[i]  = (uint8_t)knowledge_charts(&gs->knowledge,
                                  gs->local_player_id, i);
        out->route_known[i] = (uint8_t)knowledge_knows(&gs->knowledge,
                                  gs->local_player_id, i, priv);
    }
    for (i = 0; i < UI_MAX_PAIRS; i++)
        out->pair_cursor[i] = gs->sea.pair_cursor[i];

    n = 0;
    for (i = 0; i < gs->surveys.count && n < UI_MAX_SURVEYS; i++) {
        const Survey *m = &gs->surveys.mission[i];
        UiSurvey     *u;

        if (!m->active) continue;
        u = &out->survey[n++];
        u->from_island = m->from_island;
        u->to_island   = m->to_island;
        u->route_id    = m->route_id;
        u->finish_tick = m->finish_tick;
    }
    out->survey_count = n;

    /* Where the fleets are is generated from the seed and therefore no
     * secret. What they are sitting on is not copied: knowing that
     * without having been there would make hunting a lookup. */
    out->pirate_count = gs->pirates.count < UI_MAX_PIRATES
                      ? gs->pirates.count : UI_MAX_PIRATES;
    for (i = 0; i < out->pirate_count; i++) {
        out->pirate[i].waypoint = gs->pirates.fleet[i].waypoint;
        out->pirate[i].guns     = gs->pirates.fleet[i].guns;
        out->pirate[i].active   = (uint8_t)(gs->pirates.fleet[i].active ? 1 : 0);
    }
}

void ui_snapshot_build(UiSnapshot *out, const struct GameState *gs)
{
    int i, r;

    memset(out, 0, sizeof(*out));

    out->tick            = gs->sim_tick_no;
    out->local_player_id = gs->local_player_id;

    /* Health the sim can answer for itself. The client fills in the
     * rest (feed age, peers) after this returns — they are its
     * business, not the world's. */
    out->health.replay_state  = gs->replay_state;
    out->health.backlog_ticks = (uint32_t)(gs->sim_acc_ns / SIM_TICK_NS);
    out->health.feed_age_s     = -1;
    out->health.net_connected  = -1;
    out->health.feed_malformed = 0;
    out->health.feed_ghosts    = 0;
    out->current_island  = gs->current_island;

    for (i = 0; i < MAX_ISLANDS; i++)
        snapshot_island(&out->islands[i], &gs->islands[i],
                        gs->local_player_id, gs->world_seed);

    out->ship_count = gs->ship_count;
    for (i = 0; i < gs->ship_count && i < MAX_SHIPS; i++) {
        const Ship *s = &gs->ships[i];
        UiShip     *u = &out->ships[i];

        u->active      = (uint8_t)(s->active ? 1 : 0);
        u->at_island   = s->at_island;
        u->from_island = s->from_island;
        u->to_island   = s->to_island;
        u->progress    = s->progress;
        for (r = 0; r < RES_COUNT; r++) u->cargo[r] = s->cargo[r];

        /* The hull, resolved through ship.c's own accessors rather than
         * by indexing the class table here — a screen that reproduced
         * the "damaged guns are worth less" rule would be a second
         * implementation of the thing a player is about to bet on. */
        u->klass     = s->klass;
        u->guns      = ship_fighting_strength(s);
        u->hull      = s->hull;
        u->hull_max  = (s->klass >= 0 && s->klass < SHIP_CLASS_COUNT)
                     ? SHIP_CLASSES[s->klass].hull : s->hull;
        u->hold      = ship_hold_capacity(s);
        u->escorting = s->escorting;
        u->mine      = (uint8_t)(s->owner == gs->local_player_id);
    }

    /* Quotes are resolved here, once, rather than in each overlay: the
     * pricing rule lives in faction.c and nowhere else, so a screen
     * cannot show a price the sim would not honour. */
    for (r = 0; r < RES_COUNT; r++) {
        out->bid[r]                = faction_bid(&gs->faction, (ResourceType)r);
        out->ask[r]                = faction_ask(&gs->faction, (ResourceType)r);
        out->counterparty_stock[r] = gs->faction.inventory[r];
        out->price_hist_count[r]   = faction_history(&gs->faction,
                                         (ResourceType)r, out->price_hist[r],
                                         FACTION_HIST_LEN);
    }
    out->counterparty_gold = gs->faction.gold;
    out->confirm           = gs->confirm;

    snapshot_market(out, gs);
}

/* ---- reading a snapshot ------------------------------------
 * The two queries an overlay needs that are not a plain field read. */

int snapshot_has_building(const UiIsland *isl, BuildingType type)
{
    int i;

    if (type == BUILDING_NONE) return 1;   /* nothing required */
    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == (int16_t)type &&
            isl->buildings[i].connected)
            return 1;
    return 0;
}

RejectReason snapshot_upgrade_check(const UiIsland *isl, int idx,
                                    int branch, BuildingType *out_to)
{
    BuildingType from;
    int          stock[RES_COUNT], r;

    if (out_to) *out_to = BUILDING_NONE;
    if (idx < 0 || idx >= isl->building_count) return REJ_UNAVAILABLE;
    if (!isl->buildings[idx].active) return REJ_UNAVAILABLE;

    from = (BuildingType)isl->buildings[idx].type;

    /* Copied rather than cast: the snapshot stores int32_t and the
     * shared rule takes int. They are the same type on every platform
     * this builds for, which is exactly the kind of thing that stops
     * being true quietly. */
    for (r = 0; r < RES_COUNT; r++) stock[r] = (int)isl->stock[r];

    return tier_upgrade_check(from, branch, stock,
                              snapshot_has_building(isl,
                                  tier_upgrade_requires(from, branch)),
                              out_to);
}
