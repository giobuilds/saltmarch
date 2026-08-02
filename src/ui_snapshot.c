/*  ui_snapshot.c  --  Taking the picture (UI_PLAN Phase 0)
 *
 *  The only file in the UI layer that sees a GameState. Everything
 *  downstream — every overlay builder, every hit-test — takes the
 *  snapshot instead, which is what makes UI code structurally unable to
 *  mutate the world or step its RNG.
 *
 *  Still SDL-free: this compiles into libsaltmarch_ui, which links no
 *  SDL, so the headless harness can drive real UI code.
 */

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
#include <string.h>

static void snapshot_island(UiIsland *out, const Island *isl,
                            uint32_t local_player)
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

    /* Buildings are copied in slot order, INCLUDING inactive slots.
     * Compacting them here would renumber every building whenever one
     * was demolished, and UI ids are identities — a widget id that
     * shifts when an unrelated building is destroyed is exactly the
     * positional-id bug UI_PLAN decision 2 bans. */
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

/* The bounds this header declares must actually hold the sim's world,
 * and the two sets of constants live in different files on purpose —
 * a UI file should not have to include orderbook.h to know how many
 * rows a list can have. These make "on purpose" checkable rather than
 * a comment somebody keeps in step by hand. */
typedef char ui_snapshot_bounds_check[
    (UI_MAX_ORDERS   >= ORDERBOOK_MAX_ORDERS   &&
     UI_MAX_BOOKINGS >= ORDERBOOK_MAX_BOOKINGS &&
     UI_MAX_ROUTES   >= SEA_MAX_ROUTES         &&
     UI_MAX_PAIRS    >= SEA_MAX_PAIRS          &&
     UI_MAX_SURVEYS  >= MAX_SURVEYS            &&
     UI_MAX_PIRATES  >= MAX_PIRATES) ? 1 : -1];

/* ---- the maritime world (UI_PLAN N1) ----------------------
 * THE SEA IS NOT COPIED. Routes are about two hundred entries of fixed
 * geometry — positions, names, per-leg durations — regenerated from
 * the world seed and identical on every client. Copying them every frame
 * would take this snapshot from under 10 KB to about 60 KB to say the
 * same thing every time.
 *
 * So the UI reads `Sea` directly, and takes through here only the one
 * part of it that is world state: the per-pair cursor saying which two
 * private passages are currently in play. That is a deliberate,
 * recorded exception to UI_PLAN decision 1, and it is safe precisely
 * because a Sea is GENERATED rather than owned — there is nothing in
 * it a UI could mutate that would not be rebuilt identically.
 */
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
                        gs->local_player_id);

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
 * The two queries an overlay needs that are not a plain field read.
 * Both live here rather than in the overlay that first wanted them,
 * so a second overlay asking the same question gets the same answer.
 */

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
