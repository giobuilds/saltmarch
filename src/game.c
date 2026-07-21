/*  game.c  --  Game state management  (Phase 5)  */

#include "game.h"
#include "render.h"
#include "building.h"
#include "resource.h"
#include "population.h"
#include "connectivity.h"
#include "agent.h"
#include "island.h"
#include "ui.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

/* Shorthand for "the island every action in this file applies to".
 * Almost every function here was written when there was exactly one
 * island; routing them all through this single accessor is what kept
 * that rewrite mechanical. */
static Island *cur(GameState *gs) { return &gs->islands[gs->current_island]; }

Island *game_cur_island(GameState *gs) { return cur(gs); }

void game_set_current_island(GameState *gs, int idx)
{
    if (idx < 0 || idx >= MAX_ISLANDS) return;

    gs->current_island = idx;

    /* Every *_idx below indexes the island we are leaving, and
     * selected_building / drag state describe an interaction with it.
     * Closing everything is simpler and safer than trying to keep any
     * of it meaningful against a different island's arrays. */
    gs->menu_open             = 0;
    gs->trade_open            = 0;
    gs->trade_building_idx    = -1;
    gs->build_confirm_open    = 0;
    gs->build_confirm_row     = -1;
    gs->build_confirm_col     = -1;
    gs->build_confirm_payment = 0;
    gs->demolish_confirm_open = 0;
    gs->demolish_confirm_idx  = -1;
    gs->tier_upgrade_open     = 0;
    gs->tier_upgrade_idx      = -1;
    gs->demolish_mode         = 0;
    gs->selected_building     = BUILDING_NONE;
    gs->placement_valid       = 0;
    gs->drag_last_row         = -1;
    gs->drag_last_col         = -1;
    gs->hovered_row           = -1;
    gs->hovered_col           = -1;
    /* world_open is deliberately NOT cleared here: switching islands
     * is the world map's own primary action, so closing it on switch
     * would dismiss the overlay the moment you used it. */
}

/* The archipelago's fixed make-up. Island 0 is always the temperate
 * home island the player starts on; the rest each hold something home
 * lacks, which is what turns colonisation from optional into the way
 * you get hops (and therefore Beer) at all. */
static const MapProfile ISLAND_PROFILES[MAX_ISLANDS] = {
    PROFILE_TEMPERATE, PROFILE_HIGHLAND, PROFILE_WOODLAND, PROFILE_ATOLL
};
static const char *ISLAND_NAMES[MAX_ISLANDS] = {
    "Home", "Highland", "Woodland", "Atoll"
};

/* ---- game_reset_world -----------------------------------
 * Regenerates the whole archipelago and clears all per-island state.
 * Shared by game_init() (on a freshly malloc'd GameState) and
 * game_new() (on a live one, for the "New Game" menu button) so the
 * two can't drift apart. Does not touch InputState or frame-timing
 * fields — those belong to the input device / clock, not the world.
 *
 * Only island 0 is settled: the rest exist and can be looked at, but
 * are not simulated and reject placement until colonised. */
static void game_reset_world(GameState *gs, uint32_t seed)
{
    int i;

    for (i = 0; i < MAX_ISLANDS; i++) {
        /* Derive each island's seed from the world seed so one number
         * still reproduces the entire archipelago. */
        uint32_t isl_seed = seed + (uint32_t)i * 2654435761u;

        island_reset(&gs->islands[i], isl_seed, ISLAND_PROFILES[i],
                     ISLAND_NAMES[i], i == 0);

        /* Stagger the job-assignment phase across islands.
         * agents_assign_jobs() runs a full BFS per unemployed agent,
         * so leaving every island in phase would bunch all of that
         * onto the same frame every AGENT_ASSIGN_INTERVAL and read as
         * a periodic hitch. */
        gs->islands[i].agent_assign_timer =
            (float)i * AGENT_ASSIGN_INTERVAL / (float)MAX_ISLANDS;
    }

    gs->current_island = 0;
    gs->world_open     = 0;

    stockpile_add(&cur(gs)->stockpile, RES_GOLD, STARTING_GOLD);

    /* No starter houses: the player places their own first House and
     * grows population from there. */
    game_set_current_island(gs, 0);
}

/* ---- game_init ----------------------------------------- */
GameState *game_init(void)
{
    GameState *gs = (GameState *)malloc(sizeof(GameState));
    if (!gs) return NULL;

    input_init(&gs->input);
    gs->last_tick  = SDL_GetTicksNS();
    gs->delta_time = 0.0f;

    game_reset_world(gs, (uint32_t)SDL_GetTicksNS());

    return gs;
}

/* ---- game_free ----------------------------------------- */
void game_free(GameState *gs)
{
    free(gs);
}

/* ---- game_new -------------------------------------------- */
void game_new(GameState *gs)
{
    game_reset_world(gs, (uint32_t)SDL_GetTicksNS());
}

/* ---- Save format ------------------------------------------
 * v2: the archipelago. Header, then one record per island followed
 * by that island's live buildings[] and pop_data[] entries.
 *
 * Tile grids are still never written — map_init(seed) regenerates
 * each deterministically. Agents are still never written; they are
 * rebuilt from pop_data via agents_sync() after load.
 *
 * profile/settled/name are in the record from the start even though
 * only PROFILE_TEMPERATE and island 0 use them today, so adding
 * per-profile generation later needs no further version bump.
 * -------------------------------------------------------- */
typedef struct {
    uint32_t magic;
    uint32_t version;
    int32_t  island_count;
    int32_t  current_island;
} SaveHeader;

typedef struct {
    uint32_t  seed;
    int32_t   profile;
    int32_t   settled;
    char      name[ISLAND_NAME_LEN];
    int32_t   building_count;
    float     cam_offset_x;
    float     cam_offset_y;
    float     cam_zoom;
    Stockpile stockpile;
} IslandRecord;

#define SAVE_MAGIC   0x414E4E4Fu  /* "ANNO" */
#define SAVE_VERSION 2u

int game_save(const GameState *gs, const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    SaveHeader    hdr;
    int           i, ok = 1;

    if (!io) {
        SDL_Log("game_save: could not open %s: %s", path, SDL_GetError());
        return 0;
    }

    hdr.magic          = SAVE_MAGIC;
    hdr.version        = SAVE_VERSION;
    hdr.island_count   = MAX_ISLANDS;
    hdr.current_island = gs->current_island;

    ok = (SDL_WriteIO(io, &hdr, sizeof(hdr)) == sizeof(hdr));

    for (i = 0; ok && i < MAX_ISLANDS; i++) {
        const Island *isl = &gs->islands[i];
        IslandRecord  rec;
        size_t        b_bytes = sizeof(Building) * (size_t)isl->building_count;
        size_t        p_bytes = sizeof(PopData)  * (size_t)isl->building_count;

        memset(&rec, 0, sizeof(rec));
        rec.seed           = isl->map.seed;
        rec.profile        = (int32_t)isl->profile;
        rec.settled        = isl->settled;
        rec.building_count = isl->building_count;
        rec.cam_offset_x   = isl->camera.offset_x;
        rec.cam_offset_y   = isl->camera.offset_y;
        rec.cam_zoom       = isl->camera.zoom;
        rec.stockpile      = isl->stockpile;
        SDL_strlcpy(rec.name, isl->name, ISLAND_NAME_LEN);

        ok = SDL_WriteIO(io, &rec, sizeof(rec)) == sizeof(rec)
          && (b_bytes == 0 || SDL_WriteIO(io, isl->buildings, b_bytes) == b_bytes)
          && (p_bytes == 0 || SDL_WriteIO(io, isl->pop_data,  p_bytes) == p_bytes);
    }

    if (!ok) {
        SDL_Log("game_save: write to %s failed: %s", path, SDL_GetError());
        SDL_CloseIO(io);
        return 0;
    }

    SDL_CloseIO(io);
    SDL_Log("Game saved to %s (%d islands)", path, MAX_ISLANDS);
    return 1;
}

/* ---- game_load --------------------------------------------
 * Inverse of game_save(). Genuinely atomic, unlike the version this
 * replaces: that one memset gs->buildings/pop_data BEFORE reading and
 * could return 0 part-way through a truncated file, leaving the world
 * half-clobbered despite its doc comment promising otherwise. With N
 * islands that would leave islands 0..k restored and the rest garbage
 * with nonsense `settled` flags. So: slurp the whole file, validate
 * every declared count against the real byte length, and only then
 * commit anything into gs. */
int game_load(GameState *gs, const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    Sint64        size_s;
    size_t        size, off;
    unsigned char *buf;
    SaveHeader    hdr;
    int           i;

    if (!io) {
        SDL_Log("game_load: could not open %s: %s", path, SDL_GetError());
        return 0;
    }

    size_s = SDL_GetIOSize(io);
    if (size_s < (Sint64)sizeof(SaveHeader)) {
        SDL_Log("game_load: %s is too small to be a save file", path);
        SDL_CloseIO(io);
        return 0;
    }
    size = (size_t)size_s;

    buf = (unsigned char *)malloc(size);
    if (!buf) { SDL_CloseIO(io); return 0; }

    if (SDL_ReadIO(io, buf, size) != size) {
        SDL_Log("game_load: %s could not be read in full", path);
        SDL_CloseIO(io);
        free(buf);
        return 0;
    }
    SDL_CloseIO(io);

    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.magic != SAVE_MAGIC || hdr.version != SAVE_VERSION ||
        hdr.island_count <= 0 || hdr.island_count > MAX_ISLANDS ||
        hdr.current_island < 0 || hdr.current_island >= hdr.island_count) {
        SDL_Log("game_load: %s is not a valid v%u save file", path, SAVE_VERSION);
        free(buf);
        return 0;
    }

    /* Validation pass: walk the whole file without touching gs. */
    off = sizeof(hdr);
    for (i = 0; i < hdr.island_count; i++) {
        IslandRecord rec;
        size_t       need;

        if (off + sizeof(rec) > size) { free(buf); goto truncated; }
        memcpy(&rec, buf + off, sizeof(rec));
        off += sizeof(rec);

        if (rec.building_count < 0 || rec.building_count > MAX_BUILDINGS) {
            SDL_Log("game_load: %s declares a bad building count", path);
            free(buf);
            return 0;
        }
        need = (sizeof(Building) + sizeof(PopData)) * (size_t)rec.building_count;
        if (off + need > size) { free(buf); goto truncated; }
        off += need;
    }

    /* Commit pass: everything validated, so nothing below can fail. */
    off = sizeof(hdr);
    for (i = 0; i < hdr.island_count; i++) {
        IslandRecord rec;
        Island      *isl = &gs->islands[i];
        size_t       b_bytes, p_bytes;

        memcpy(&rec, buf + off, sizeof(rec));
        off += sizeof(rec);

        b_bytes = sizeof(Building) * (size_t)rec.building_count;
        p_bytes = sizeof(PopData)  * (size_t)rec.building_count;

        island_reset(isl, rec.seed, (MapProfile)rec.profile,
                     rec.name, rec.settled);

        if (b_bytes) memcpy(isl->buildings, buf + off, b_bytes);
        off += b_bytes;
        if (p_bytes) memcpy(isl->pop_data, buf + off, p_bytes);
        off += p_bytes;

        isl->building_count  = rec.building_count;
        isl->stockpile       = rec.stockpile;
        isl->camera.offset_x = rec.cam_offset_x;
        isl->camera.offset_y = rec.cam_offset_y;
        isl->camera.zoom     = rec.cam_zoom;

        /* Agents aren't persisted (see agent.h) — rebuild them from
         * the just-restored pop_data. island_reset() already zeroed
         * the array and its count. */
        agents_sync(isl->agents, &isl->agent_count, isl->buildings,
                    isl->pop_data, isl->building_count);
    }
    free(buf);

    game_set_current_island(gs, hdr.current_island);

    SDL_Log("Game loaded from %s (%d islands)", path, hdr.island_count);
    return 1;

truncated:
    SDL_Log("game_load: %s is truncated", path);
    return 0;
}

/* ---- game_update ---------------------------------------
 * Splits cleanly in two: everything view/input related applies to the
 * CURRENT island only, then every SETTLED island is simulated —
 * including ones you aren't looking at, so a colony keeps producing
 * while you manage another. */
void game_update(GameState *gs, SDL_Renderer *renderer)
{
    Island *isl = cur(gs);
    float   lx, ly;
    int     i;

    Uint64 now = SDL_GetTicksNS();
    float  dt  = (float)(now - gs->last_tick) / 1000000000.0f;
    if (dt > 0.1f) dt = 0.1f;
    gs->last_tick  = now;
    gs->delta_time = dt;

    if (gs->input.pan_left)  isl->camera.offset_x += CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_right) isl->camera.offset_x -= CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_up)    isl->camera.offset_y += CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_down)  isl->camera.offset_y -= CAMERA_PAN_SPEED * dt;

    /* Zoom toward cursor on mouse wheel scroll. Keeps the tile under
     * the cursor stationary while zooming — the same behaviour as
     * Google Maps or Anno 1800. */
    if (gs->input.scroll_y != 0.0f) {
        float old_zoom = isl->camera.zoom;
        float new_zoom = old_zoom + gs->input.scroll_y * ZOOM_STEP;
        if (new_zoom < ZOOM_MIN) new_zoom = ZOOM_MIN;
        if (new_zoom > ZOOM_MAX) new_zoom = ZOOM_MAX;
        if (new_zoom != old_zoom) {
            float cx    = (float)gs->input.logical_x;
            float cy    = (float)gs->input.logical_y;
            float dx    = cx - isl->camera.offset_x;
            float dy    = cy - isl->camera.offset_y;
            float ratio = new_zoom / old_zoom;
            isl->camera.offset_x = cx - dx * ratio;
            isl->camera.offset_y = cy - dy * ratio;
            isl->camera.zoom     = new_zoom;
        }
    }

    SDL_RenderCoordinatesFromWindow(renderer,
        (float)gs->input.mouse_x, (float)gs->input.mouse_y, &lx, &ly);
    gs->input.logical_x = (int)lx;
    gs->input.logical_y = (int)ly;

    if (gs->input.logical_y < SCREEN_H - HUD_HEIGHT) {
        screen_to_iso(gs->input.logical_x, gs->input.logical_y,
                      &isl->camera, &gs->hovered_row, &gs->hovered_col);
        if (gs->hovered_row < 0 || gs->hovered_row >= MAP_ROWS ||
            gs->hovered_col < 0 || gs->hovered_col >= MAP_COLS) {
            gs->hovered_row = -1;
            gs->hovered_col = -1;
        }
    } else {
        gs->hovered_row = -1;
        gs->hovered_col = -1;
    }

    /* Road drag-placement: while the button is held and Road is
     * selected, place at each newly-hovered tile as the cursor
     * crosses it (no confirm popup — see game_try_place_road()'s doc
     * comment on why Road is exempt). Reset drag_last_row/col to -1
     * whenever the button isn't held so the next drag's first tile
     * is never skipped as "unchanged". */
    if (!gs->input.left_down) {
        gs->drag_last_row = -1;
        gs->drag_last_col = -1;
    } else if (gs->selected_building == BUILDING_ROAD &&
              !gs->build_confirm_open && !gs->menu_open && !gs->trade_open &&
              gs->hovered_row >= 0 &&
              (gs->hovered_row != gs->drag_last_row ||
               gs->hovered_col != gs->drag_last_col)) {
        game_try_place_road(gs, gs->hovered_row, gs->hovered_col);
        gs->drag_last_row = gs->hovered_row;
        gs->drag_last_col = gs->hovered_col;
    }

    /* placement_valid reflects only "does this tile structurally
     * work" plus "is this island even settled" — affordability is a
     * per-payment-method question the build-confirmation popup
     * resolves, so the player can always open it and see both options
     * even sitting at 0 Gold. */
    gs->placement_valid = 0;
    if (isl->settled &&
        gs->selected_building != BUILDING_NONE && gs->hovered_row >= 0)
        gs->placement_valid = building_can_place(&isl->map,
            gs->selected_building, gs->hovered_row, gs->hovered_col,
            NULL, 0);

    /* Simulate every settled island, not just the visible one. Each
     * island's pipeline runs to completion before the next begins —
     * see island_update()'s ordering constraint. */
    for (i = 0; i < MAX_ISLANDS; i++)
        island_update(&gs->islands[i], dt);
}

/* ---- commit_placement -----------------------------------
 * Shared by game_try_place_road() and game_place_building_confirmed():
 * the actual building_place() call plus its post-placement side
 * effects (House PopData, Warehouse storage capacity). Callers are
 * responsible for their own affordability check and payment
 * deduction beforehand — this only ever runs once placement is
 * already decided. Returns the new building's index, or -1 on
 * failure (full array, invalid tile, or an unsettled island). */
static int commit_placement(GameState *gs, BuildingType type, int row, int col)
{
    Island *isl = cur(gs);
    int     idx;

    /* Gated here as well as in the UI: an unsettled island must be
     * unbuildable no matter which code path reaches this. */
    if (!isl->settled) return -1;

    idx = building_place(isl->buildings, &isl->building_count,
                         &isl->map, type, row, col);
    if (idx < 0) return -1;

    /* If a house was just placed, activate its PopData. */
    if (pop_is_house_type(type))
        pop_init(&isl->pop_data[idx]);

    /* Warehouses raise how much of each non-gold resource THIS
     * island's stockpile can hold; recompute so a newly built
     * Warehouse takes effect immediately. */
    if (type == BUILDING_WAREHOUSE)
        island_recompute_storage_capacity(isl);

    return idx;
}

/* ---- game_try_place_road ----------------------------------
 * Roads are exempt from the build-confirmation popup: they're also
 * placeable by dragging (see game_update()'s per-frame drag check),
 * and a per-tile confirmation dialog would make that gesture
 * unusable. A single non-dragged click on Road goes through this
 * same function for consistency — one tile placed the same way
 * whether it came from a click or a drag. Roads are free, so there's
 * no resources-vs-gold choice to offer anyway. */
int game_try_place_road(GameState *gs, int row, int col)
{
    Island            *isl = cur(gs);
    const BuildingDef *def = &BUILDING_DEFS[BUILDING_ROAD];

    if (!isl->settled) return 0;
    if (!building_can_place(&isl->map, BUILDING_ROAD, row, col, NULL, 0))
        return 0;
    if (!building_can_afford(&isl->stockpile, BUILDING_ROAD))
        return 0;

    if (commit_placement(gs, BUILDING_ROAD, row, col) < 0)
        return 0;

    stockpile_add(&isl->stockpile, RES_GOLD, -def->cost[RES_GOLD]);
    return 1;
}

/* ---- game_place_building_confirmed ------------------------- */
void game_place_building_confirmed(GameState *gs, int pay_with_gold)
{
    Island             *isl  = cur(gs);
    BuildingType        type = gs->selected_building;
    const BuildingDef  *def;
    int                 i;

    if (type == BUILDING_NONE) return;
    if (!isl->settled) return;
    if (gs->build_confirm_row < 0) return;
    def = &BUILDING_DEFS[type];

    if (pay_with_gold) {
        int gold_cost = building_gold_equivalent_cost(type);
        if (isl->stockpile.amount[RES_GOLD] < gold_cost) return;

        if (commit_placement(gs, type, gs->build_confirm_row,
                             gs->build_confirm_col) < 0)
            return;

        stockpile_add(&isl->stockpile, RES_GOLD, -gold_cost);
    } else {
        if (!building_can_afford(&isl->stockpile, type)) return;

        if (commit_placement(gs, type, gs->build_confirm_row,
                             gs->build_confirm_col) < 0)
            return;

        for (i = 0; i < RES_COUNT; i++)
            if (def->cost[i] > 0)
                stockpile_add(&isl->stockpile, (ResourceType)i, -def->cost[i]);
    }
}

/* ---- game_find_building_at -------------------------------
 * Searches the CURRENT island only — like every *_idx in GameState,
 * the returned index is current-island-relative. */
int game_find_building_at(const GameState *gs, int row, int col)
{
    const Island *isl = &gs->islands[gs->current_island];
    int i;

    if (row < 0 || col < 0) return -1;

    for (i = 0; i < isl->building_count; i++) {
        const Building    *b   = &isl->buildings[i];
        const BuildingDef *def;

        if (!b->active) continue;
        def = &BUILDING_DEFS[b->type];

        if (row >= b->row && row < b->row + def->tile_h &&
            col >= b->col && col < b->col + def->tile_w)
            return i;
    }

    return -1;
}

/* ---- game_sell_resource ------------------------------------
 * Trades against the CURRENT island's stockpile. Each island is
 * independently connected to the world market at the same fixed
 * prices, which is also what guarantees a new colony can buy in goods
 * its own terrain can't produce. */
void game_sell_resource(GameState *gs, ResourceType res, int qty)
{
    Island *isl = cur(gs);

    if (res == RES_GOLD) return;
    if (qty > isl->stockpile.amount[res]) qty = isl->stockpile.amount[res];
    if (qty <= 0) return;

    stockpile_add(&isl->stockpile, res, -qty);
    stockpile_add(&isl->stockpile, RES_GOLD, qty * SELL_PRICE[res]);
}

/* ---- game_buy_resource --------------------------------------- */
void game_buy_resource(GameState *gs, ResourceType res, int qty)
{
    Island *isl = cur(gs);
    int     headroom, max_affordable;

    if (res == RES_GOLD) return;

    headroom = isl->stockpile.capacity - isl->stockpile.amount[res];
    if (headroom < 0) headroom = 0;

    max_affordable = (BUY_PRICE[res] > 0)
                    ? isl->stockpile.amount[RES_GOLD] / BUY_PRICE[res]
                    : 0;

    if (qty < 0)
        qty = (headroom < max_affordable) ? headroom : max_affordable;
    if (qty > headroom)        qty = headroom;
    if (qty > max_affordable)  qty = max_affordable;
    if (qty <= 0) return;

    stockpile_add(&isl->stockpile, RES_GOLD, -(qty * BUY_PRICE[res]));
    stockpile_add(&isl->stockpile, res, qty);
}

/* ---- game_demolish_building --------------------------------- */
void game_demolish_building(GameState *gs, int idx)
{
    Island      *isl = cur(gs);
    BuildingType type;
    int          i;

    if (idx < 0 || idx >= isl->building_count) return;
    if (!isl->buildings[idx].active) return;

    type = isl->buildings[idx].type;

    isl->buildings[idx].active       = 0;
    isl->buildings[idx].connected    = 0;
    isl->buildings[idx].worker_count = 0;

    if (isl->pop_data[idx].active) {
        isl->pop_data[idx].active    = 0;
        isl->pop_data[idx].residents = 0;
    }

    /* Clean up any agents referencing this building — otherwise a
     * demolished workplace leaves an agent stuck "employed" at a
     * dead job forever (agent_assign_jobs only reassigns agents with
     * work_idx == -1), and a demolished home leaves one with nowhere
     * to be. */
    for (i = 0; i < isl->agent_count; i++) {
        Agent *a = &isl->agents[i];
        if (!a->active) continue;

        if (a->home_idx == idx) {
            a->active = 0;
        } else if (a->work_idx == idx) {
            a->work_idx    = -1;
            a->state       = AGENT_IDLE_HOME;
            a->state_timer = 0.0f;
            a->path_len    = 0;
            a->path_pos    = 0;
            /* Snap back to standing at home rather than leaving the
             * agent's dot stranded wherever it was mid-commute. */
            a->row = (float)isl->buildings[a->home_idx].row;
            a->col = (float)isl->buildings[a->home_idx].col;
        }
    }

    if (type == BUILDING_WAREHOUSE)
        island_recompute_storage_capacity(isl);
}

/* ---- game_upgrade_house -------------------------------------- */
void game_upgrade_house(GameState *gs, int idx)
{
    Island *isl = cur(gs);

    if (idx < 0 || idx >= isl->building_count) return;
    if (!isl->buildings[idx].active) return;
    if (isl->buildings[idx].type != BUILDING_HOUSE) return;
    if (isl->stockpile.amount[RES_GOLD] < TIER_UPGRADE_COST_GOLD) return;

    stockpile_add(&isl->stockpile, RES_GOLD, -TIER_UPGRADE_COST_GOLD);
    isl->buildings[idx].type = BUILDING_HOUSE_WORKER;
}
