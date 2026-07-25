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
#include "game.h"
#include "faction.h"
#include "population.h"
#include "simclock.h"
#include <string.h>

static void snapshot_island(UiIsland *out, const Island *isl)
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
        u->happy        = (uint8_t)(isl->pop_data[i].active
                                    ? (isl->pop_data[i].happy ? 1 : 0) : 1);
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
        snapshot_island(&out->islands[i], &gs->islands[i]);

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
                                    BuildingType *out_to)
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

    return tier_upgrade_check(from, stock,
                              snapshot_has_building(isl,
                                  tier_upgrade_requires(from)),
                              out_to);
}
