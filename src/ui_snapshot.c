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
    }
}

void ui_snapshot_build(UiSnapshot *out, const struct GameState *gs)
{
    int i, r;

    memset(out, 0, sizeof(*out));

    out->tick            = gs->sim_tick_no;
    out->local_player_id = gs->local_player_id;
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
        out->bid[r] = faction_bid(&gs->faction, (ResourceType)r);
        out->ask[r] = faction_ask(&gs->faction, (ResourceType)r);
    }
    out->counterparty_gold = gs->faction.gold;
}
