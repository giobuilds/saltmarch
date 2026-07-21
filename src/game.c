/*  game.c  --  Game state management  (Phase 5)  */

#include "game.h"
#include "render.h"
#include "building.h"
#include "resource.h"
#include "population.h"
#include "ui.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static void game_recompute_storage_capacity(GameState *gs);

/* ---- game_reset_world -----------------------------------
 * Regenerates the map and clears all placed buildings, the
 * population, and the stockpile. Shared by game_init() (on a
 * freshly malloc'd GameState) and game_new() (on a live one,
 * for the "New Game" menu button) so the two can't drift apart.
 * Does not touch InputState or frame-timing fields — those
 * belong to the input device / clock, not the game world. */
static void game_reset_world(GameState *gs, uint32_t seed)
{
    SDL_Log("Map seed: %u", seed);
    map_init(&gs->map, seed);

    camera_init(&gs->camera, SCREEN_W, SCREEN_H, MAP_COLS, MAP_ROWS);

    gs->hovered_row       = -1;
    gs->hovered_col       = -1;

    memset(gs->buildings, 0, sizeof(gs->buildings));
    memset(gs->pop_data,  0, sizeof(gs->pop_data));
    gs->building_count    = 0;
    gs->selected_building = BUILDING_NONE;
    gs->placement_valid   = 0;
    gs->menu_open         = 0;

    stockpile_init(&gs->stockpile);
    stockpile_add(&gs->stockpile, RES_GOLD, STARTING_GOLD);
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

/* ---- game_save --------------------------------------------
 * Flat binary format: header, then the live building_count
 * entries of buildings[] and pop_data[] (they're parallel
 * arrays, see game.h), then the stockpile. The 64x64 tile grid
 * itself is never written — map_init(seed) regenerates it
 * deterministically, so the seed alone is enough to restore it.
 * -------------------------------------------------------- */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t seed;
    int32_t  building_count;
    float    cam_offset_x;
    float    cam_offset_y;
    float    cam_zoom;
} SaveHeader;

#define SAVE_MAGIC   0x414E4E4Fu  /* "ANNO" */
#define SAVE_VERSION 1u

int game_save(const GameState *gs, const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    SaveHeader    hdr;
    size_t        buildings_bytes = sizeof(Building) * (size_t)gs->building_count;
    size_t        pop_bytes       = sizeof(PopData)  * (size_t)gs->building_count;

    if (!io) {
        SDL_Log("game_save: could not open %s: %s", path, SDL_GetError());
        return 0;
    }

    hdr.magic          = SAVE_MAGIC;
    hdr.version        = SAVE_VERSION;
    hdr.seed           = gs->map.seed;
    hdr.building_count = gs->building_count;
    hdr.cam_offset_x   = gs->camera.offset_x;
    hdr.cam_offset_y   = gs->camera.offset_y;
    hdr.cam_zoom       = gs->camera.zoom;

    if (SDL_WriteIO(io, &hdr, sizeof(hdr)) != sizeof(hdr) ||
        SDL_WriteIO(io, gs->buildings, buildings_bytes) != buildings_bytes ||
        SDL_WriteIO(io, gs->pop_data,  pop_bytes)       != pop_bytes ||
        SDL_WriteIO(io, &gs->stockpile, sizeof(Stockpile)) != sizeof(Stockpile)) {
        SDL_Log("game_save: write to %s failed: %s", path, SDL_GetError());
        SDL_CloseIO(io);
        return 0;
    }

    SDL_CloseIO(io);
    SDL_Log("Game saved to %s (seed=%u, %d buildings)",
            path, hdr.seed, gs->building_count);
    return 1;
}

/* ---- game_load --------------------------------------------
 * Inverse of game_save(): regenerates the map from the stored
 * seed, then restores buildings/pop_data/stockpile/camera.
 * Rejects the file (returns 0, gs untouched) if it's missing,
 * has the wrong magic/version, or is truncated. */
int game_load(GameState *gs, const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    SaveHeader    hdr;
    size_t        buildings_bytes, pop_bytes;

    if (!io) {
        SDL_Log("game_load: could not open %s: %s", path, SDL_GetError());
        return 0;
    }

    if (SDL_ReadIO(io, &hdr, sizeof(hdr)) != sizeof(hdr) ||
        hdr.magic   != SAVE_MAGIC ||
        hdr.version != SAVE_VERSION ||
        hdr.building_count < 0 || hdr.building_count > MAX_BUILDINGS) {
        SDL_Log("game_load: %s is not a valid save file", path);
        SDL_CloseIO(io);
        return 0;
    }

    buildings_bytes = sizeof(Building) * (size_t)hdr.building_count;
    pop_bytes       = sizeof(PopData)  * (size_t)hdr.building_count;

    memset(gs->buildings, 0, sizeof(gs->buildings));
    memset(gs->pop_data,  0, sizeof(gs->pop_data));

    if (SDL_ReadIO(io, gs->buildings, buildings_bytes)    != buildings_bytes ||
        SDL_ReadIO(io, gs->pop_data,  pop_bytes)          != pop_bytes ||
        SDL_ReadIO(io, &gs->stockpile, sizeof(Stockpile)) != sizeof(Stockpile)) {
        SDL_Log("game_load: %s is truncated or corrupt", path);
        SDL_CloseIO(io);
        return 0;
    }
    SDL_CloseIO(io);

    map_init(&gs->map, hdr.seed);
    gs->building_count  = hdr.building_count;
    gs->camera.offset_x = hdr.cam_offset_x;
    gs->camera.offset_y = hdr.cam_offset_y;
    gs->camera.zoom     = hdr.cam_zoom;

    gs->hovered_row       = -1;
    gs->hovered_col       = -1;
    gs->selected_building = BUILDING_NONE;
    gs->placement_valid   = 0;
    gs->menu_open         = 0;

    game_recompute_storage_capacity(gs);

    SDL_Log("Game loaded from %s (seed=%u, %d buildings)",
            path, hdr.seed, gs->building_count);
    return 1;
}

/* ---- game_tick_buildings (unchanged from Phase 4) ------ */
static void game_tick_buildings(GameState *gs, float dt)
{
    int i;
    for (i = 0; i < gs->building_count; i++) {
        Building          *b   = &gs->buildings[i];
        const BuildingDef *def = &BUILDING_DEFS[b->type];

        if (!b->active || def->tick_seconds <= 0.0f) continue;

        b->timer += dt;
        if (b->timer < def->tick_seconds) continue;
        b->timer = 0.0f;

        if (def->consumes != RES_COUNT) {
            if (gs->stockpile.amount[def->consumes] < def->consume_amt) {
                SDL_Log("%s idle: needs %d %s", def->name,
                    def->consume_amt, RESOURCE_NAMES[def->consumes]);
                continue;
            }
            stockpile_add(&gs->stockpile, def->consumes, -def->consume_amt);
        }

        if (def->produces != RES_COUNT) {
            stockpile_add(&gs->stockpile, def->produces, def->produce_amt);
            SDL_Log("%s produced %d %s  (total: %d)",
                def->name, def->produce_amt,
                RESOURCE_NAMES[def->produces],
                gs->stockpile.amount[def->produces]);
        }
    }
}

/* ---- game_update --------------------------------------- */
void game_update(GameState *gs, SDL_Renderer *renderer)
{
    float lx, ly;

    Uint64 now = SDL_GetTicksNS();
    float  dt  = (float)(now - gs->last_tick) / 1000000000.0f;
    if (dt > 0.1f) dt = 0.1f;
    gs->last_tick  = now;
    gs->delta_time = dt;

    if (gs->input.pan_left)  gs->camera.offset_x += CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_right) gs->camera.offset_x -= CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_up)    gs->camera.offset_y += CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_down)  gs->camera.offset_y -= CAMERA_PAN_SPEED * dt;

    /* CHANGED: zoom toward cursor on mouse wheel scroll.
     * Keeps the tile under the cursor stationary while zooming —
     * the same behaviour as Google Maps or Anno 1800.
     * Steps:
     *   1. Compute cursor offset from camera origin.
     *   2. Apply zoom delta.
     *   3. Rescale that offset by the zoom ratio.
     *   4. Rewrite camera origin so cursor stays in place. */
    if (gs->input.scroll_y != 0.0f) {
        float old_zoom = gs->camera.zoom;
        float new_zoom = old_zoom + gs->input.scroll_y * ZOOM_STEP;
        if (new_zoom < ZOOM_MIN) new_zoom = ZOOM_MIN;
        if (new_zoom > ZOOM_MAX) new_zoom = ZOOM_MAX;
        if (new_zoom != old_zoom) {
            float cx    = (float)gs->input.logical_x;
            float cy    = (float)gs->input.logical_y;
            float dx    = cx - gs->camera.offset_x;
            float dy    = cy - gs->camera.offset_y;
            float ratio = new_zoom / old_zoom;
            gs->camera.offset_x = cx - dx * ratio;
            gs->camera.offset_y = cy - dy * ratio;
            gs->camera.zoom     = new_zoom;
        }
    }

    SDL_RenderCoordinatesFromWindow(renderer,
        (float)gs->input.mouse_x, (float)gs->input.mouse_y, &lx, &ly);
    gs->input.logical_x = (int)lx;
    gs->input.logical_y = (int)ly;

    if (gs->input.logical_y < SCREEN_H - HUD_HEIGHT) {
        screen_to_iso(gs->input.logical_x, gs->input.logical_y,
                      &gs->camera, &gs->hovered_row, &gs->hovered_col);
        if (gs->hovered_row < 0 || gs->hovered_row >= MAP_ROWS ||
            gs->hovered_col < 0 || gs->hovered_col >= MAP_COLS) {
            gs->hovered_row = -1;
            gs->hovered_col = -1;
        }
    } else {
        gs->hovered_row = -1;
        gs->hovered_col = -1;
    }

    gs->placement_valid = 0;
    if (gs->selected_building != BUILDING_NONE && gs->hovered_row >= 0)
        gs->placement_valid =
            building_can_place(&gs->map, gs->selected_building,
                gs->hovered_row, gs->hovered_col, NULL, 0) &&
            building_can_afford(&gs->stockpile, gs->selected_building);

    game_tick_buildings(gs, dt);

    /* Phase 5: update population needs */
    pop_update(gs->pop_data, gs->building_count, &gs->stockpile, dt);
}

/* ---- game_place_building -------------------------------
 * Phase 5: when a House is placed, initialise its PopData.
 * -------------------------------------------------------- */
void game_place_building(GameState *gs)
{
    int idx, i;
    const BuildingDef *def;

    if (gs->selected_building == BUILDING_NONE) return;
    if (gs->hovered_row < 0) return;
    if (!building_can_afford(&gs->stockpile, gs->selected_building)) return;

    idx = building_place(gs->buildings, &gs->building_count,
                         &gs->map, gs->selected_building,
                         gs->hovered_row, gs->hovered_col);

    if (idx < 0) return;

    def = &BUILDING_DEFS[gs->selected_building];
    for (i = 0; i < RES_COUNT; i++)
        if (def->cost[i] > 0)
            stockpile_add(&gs->stockpile, (ResourceType)i, -def->cost[i]);

    /* Phase 5: if a house was just placed, activate its PopData */
    if (gs->selected_building == BUILDING_HOUSE)
        pop_init(&gs->pop_data[idx]);

    /* Warehouses raise how much of each non-gold resource the
     * stockpile can hold; recompute after every placement so a
     * newly built Warehouse takes effect immediately. */
    if (gs->selected_building == BUILDING_WAREHOUSE)
        game_recompute_storage_capacity(gs);
}

/* ---- game_recompute_storage_capacity ---------------------
 * The stockpile's per-resource cap is BASE_STORAGE_CAP plus
 * WAREHOUSE_STORAGE_BONUS for every active Warehouse. Gold is
 * exempt (see resource.h) so this only affects Wood/Fish/Grain. */
static void game_recompute_storage_capacity(GameState *gs)
{
    int i, warehouses = 0;

    for (i = 0; i < gs->building_count; i++)
        if (gs->buildings[i].active &&
            gs->buildings[i].type == BUILDING_WAREHOUSE)
            warehouses++;

    stockpile_set_capacity(&gs->stockpile,
        BASE_STORAGE_CAP + warehouses * WAREHOUSE_STORAGE_BONUS);
}
