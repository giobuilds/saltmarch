/*  game.c  --  Game state management  (Phase 5)  */

#include "game.h"
#include "render.h"
#include "building.h"
#include "resource.h"
#include "population.h"
#include "connectivity.h"
#include "agent.h"
#include "island.h"
#include "ship.h"
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
    gs->ship_build_open       = 0;
    gs->ship_build_idx        = -1;
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

/* The archipelago's fixed make-up. Island 0 is always Saltford, the
 * temperate home island the player starts on; the rest each hold
 * something Saltford lacks, which is what turns colonisation from
 * optional into the way you get hops (and therefore Beer) at all.
 *
 * Names are place names; the MapProfile beside each is the TERRAIN it
 * sits on. The two are deliberately independent — Brinehold is a
 * settlement that happens to occupy highland, the way a real town name
 * says nothing about its geology. */
static const MapProfile ISLAND_PROFILES[MAX_ISLANDS] = {
    PROFILE_TEMPERATE, PROFILE_HIGHLAND, PROFILE_WOODLAND, PROFILE_ATOLL
};
static const char *ISLAND_NAMES[MAX_ISLANDS] = {
    "Saltford", "Brinehold", "Tidefast", "Marrowbay"
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

    gs->world_seed = seed;

    for (i = 0; i < MAX_ISLANDS; i++) {
        /* Derive each island's seed from the world seed so one number
         * still reproduces the entire archipelago. */
        uint32_t isl_seed = seed + (uint32_t)i * 2654435761u;

        island_reset(&gs->islands[i], isl_seed, ISLAND_PROFILES[i],
                     ISLAND_NAMES[i], i == 0);

        /* Stagger the job-assignment phase across islands.
         * agents_assign_jobs() runs a full BFS per unemployed agent,
         * so leaving every island in phase would bunch all of that
         * onto the same tick every AGENT_ASSIGN_INTERVAL and read as
         * a periodic hitch. Integer ticks now (Phase 1b). */
        gs->islands[i].agent_assign_timer =
            i * AGENT_ASSIGN_INTERVAL_TICKS / MAX_ISLANDS;
    }

    gs->current_island = 0;
    gs->world_open     = 0;
    memset(gs->ships, 0, sizeof(gs->ships));
    gs->ship_count          = 0;
    gs->world_selected_ship = -1;
    gs->ship_build_open     = 0;
    gs->ship_build_idx      = -1;

    stockpile_add(&cur(gs)->stockpile, RES_GOLD, STARTING_GOLD);

    /* A fresh world is a fresh history: discard any previous command
     * log and reset the world clock. The starting state above is a
     * deterministic function of the seed, so replay reconstructs it by
     * re-running this function, then replaying the (now empty) log.
     * The allocation itself is kept for reuse. */
    gs->cmd_count   = 0;
    gs->cmd_applied = 0;
    gs->sim_tick_no = 0;
    gs->sim_acc_ns  = 0;

    /* This world IS the replay of (world_seed, empty log) from tick 0,
     * so the F9 self-check is meaningful from here on. */
    gs->replay_valid         = 1;
    gs->replay_state         = 0;
    gs->replay_show_until_ns = 0;

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

    /* The command log starts empty. Zero it before anything can push,
     * since malloc does not, and game_reset_world resets the counters
     * but relies on the pointer/cap being valid. */
    gs->cmd_log     = NULL;
    gs->cmd_count   = 0;
    gs->cmd_cap     = 0;
    gs->cmd_applied = 0;
    gs->sim_tick_no = 0;
    gs->sim_acc_ns  = 0;

    gs->replay_live_hash   = 0;
    gs->replay_replay_hash = 0;
    gs->replay_tick        = 0;

    game_reset_world(gs, (uint32_t)SDL_GetTicksNS());

    return gs;
}

/* ---- game_free ----------------------------------------- */
void game_free(GameState *gs)
{
    if (!gs) return;
    command_log_free(gs);
    free(gs);
}

/* ---- game_new -------------------------------------------- */
void game_new(GameState *gs)
{
    game_reset_world(gs, (uint32_t)SDL_GetTicksNS());
}

/* ---- game_new_seeded ------------------------------------- */
void game_new_seeded(GameState *gs, uint32_t seed)
{
    game_reset_world(gs, seed);
}

/* ---- Save format v5: the world as (seed + command log) ----
 * MMO_PLAN Phase 1d. A save is no longer a snapshot of buildings,
 * population and stockpiles — it is the world seed, the tick the world
 * had reached, and the ordered command log. Loading reconstructs the
 * world by regenerating from the seed and replaying the log, so LOADING
 * IS THE F9 TEST: a save that loads to the same place it was saved from
 * is a save whose determinism just got proven end to end.
 *
 * This makes saves tiny (a few hundred commands, not four 64x64 worlds)
 * and is the exact shape a server checkpoint or a shared replay file
 * takes later. The .smlog files the --replay CLI consumes are simply
 * these save files.
 *
 * Pre-v5 full-state saves are intentionally NOT loadable: the game is
 * pre-release, and maintaining a second (now derivable) load path earns
 * nothing. A pre-v5 file is rejected with a clear message.
 * -------------------------------------------------------- */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t world_seed;
    int32_t  current_island;
    uint64_t sim_tick_no;
    int32_t  cmd_count;
} SaveHeader;

#define SAVE_MAGIC   0x53414C54u  /* "SALT" */
#define SAVE_VERSION 5u           /* v5: seed + command log (Phase 1d) */

int game_save(const GameState *gs, const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    SaveHeader    hdr;
    size_t        log_bytes = sizeof(Command) * (size_t)gs->cmd_count;
    int           ok;

    if (!io) {
        SDL_Log("game_save: could not open %s: %s", path, SDL_GetError());
        return 0;
    }

    hdr.magic          = SAVE_MAGIC;
    hdr.version        = SAVE_VERSION;
    hdr.world_seed     = gs->world_seed;
    hdr.current_island = gs->current_island;
    hdr.sim_tick_no    = gs->sim_tick_no;
    hdr.cmd_count      = gs->cmd_count;

    ok = SDL_WriteIO(io, &hdr, sizeof(hdr)) == sizeof(hdr)
      && (log_bytes == 0 ||
          SDL_WriteIO(io, gs->cmd_log, log_bytes) == log_bytes);

    if (!ok) {
        SDL_Log("game_save: write to %s failed: %s", path, SDL_GetError());
        SDL_CloseIO(io);
        return 0;
    }

    SDL_CloseIO(io);
    SDL_Log("Game saved to %s (seed %u, tick %llu, %d commands)",
            path, gs->world_seed,
            (unsigned long long)gs->sim_tick_no, gs->cmd_count);
    return 1;
}

/* ---- game_load --------------------------------------------
 * Reconstruct the world from a v5 save: regenerate from the seed, load
 * the command log, and replay it up to the saved tick. On success the
 * world equals what F9 would rebuild, so replay_valid stays 1 (unlike
 * the old full-state load) — the self-check works immediately after a
 * load. Validates the file fully before touching gs, so a truncated or
 * wrong-version file leaves the current world untouched. */
int game_load(GameState *gs, const char *path)
{
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    Sint64        size_s;
    size_t        size, need;
    unsigned char *buf;
    SaveHeader    hdr;
    const Command *cmds;

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

    if ((size_t)SDL_ReadIO(io, buf, size) != size) {
        SDL_Log("game_load: %s could not be read in full", path);
        SDL_CloseIO(io);
        free(buf);
        return 0;
    }
    SDL_CloseIO(io);

    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.magic != SAVE_MAGIC || hdr.version != SAVE_VERSION) {
        SDL_Log("game_load: %s is not a v%u (seed+log) save file",
                path, SAVE_VERSION);
        free(buf);
        return 0;
    }
    if (hdr.cmd_count < 0 ||
        hdr.current_island < 0 || hdr.current_island >= MAX_ISLANDS) {
        SDL_Log("game_load: %s has an invalid header", path);
        free(buf);
        return 0;
    }
    need = sizeof(hdr) + sizeof(Command) * (size_t)hdr.cmd_count;
    if (need > size) {
        SDL_Log("game_load: %s is truncated", path);
        free(buf);
        return 0;
    }

    /* Rebuild tick 0 from the seed (this sets replay_valid = 1), install
     * the logged commands, then replay them up to the saved tick. */
    game_reset_world(gs, hdr.world_seed);

    cmds = (const Command *)(buf + sizeof(hdr));
    if (!command_log_set(gs, cmds, hdr.cmd_count)) {
        SDL_Log("game_load: out of memory installing %d commands",
                hdr.cmd_count);
        free(buf);
        return 0;
    }
    free(buf);

    while (gs->sim_tick_no < hdr.sim_tick_no)
        sim_run_one_tick(gs);

    game_set_current_island(gs, hdr.current_island);

    SDL_Log("Game loaded from %s (seed %u, replayed to tick %llu, %d commands)",
            path, hdr.world_seed,
            (unsigned long long)gs->sim_tick_no, gs->cmd_count);
    return 1;
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

    Uint64 now      = SDL_GetTicksNS();
    Uint64 frame_ns = now - gs->last_tick;
    float  dt       = (float)frame_ns / 1000000000.0f;
    if (dt > 0.1f) dt = 0.1f;   /* cosmetic clamp for camera/hover only */
    gs->last_tick  = now;
    gs->delta_time = dt;

    if (gs->input.pan_left)  isl->camera.offset_x += CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_right) isl->camera.offset_x -= CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_up)    isl->camera.offset_y += CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_down)  isl->camera.offset_y -= CAMERA_PAN_SPEED * dt;

    /* Zoom toward cursor on mouse wheel scroll. Keeps the tile under
     * the cursor stationary while zooming — the same behaviour as
     * Google Maps. */
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

    /* Fixed-timestep simulation. Everything above this point is
     * cosmetic and per-frame (camera, hover, the drag-placement input);
     * everything the sim owns advances only here, in whole ticks, so
     * frame rate cannot change the world. Accumulate the real elapsed
     * time and spend it one tick at a time.
     *
     * The accumulator is clamped so a long stall (a breakpoint, a
     * dragged window) spends at most a bounded number of ticks catching
     * up instead of freezing in a spiral; the world simply advances a
     * little less during that stall, which is invisible in single
     * player and is what the future server's continuous ticking exists
     * to make authoritative anyway. */
    gs->sim_acc_ns += frame_ns;
    if (gs->sim_acc_ns > SIM_TICK_NS * 8)
        gs->sim_acc_ns = SIM_TICK_NS * 8;
    while (gs->sim_acc_ns >= SIM_TICK_NS) {
        sim_run_one_tick(gs);
        gs->sim_acc_ns -= SIM_TICK_NS;
    }
}

/* ---- sim_run_one_tick -----------------------------------
 * The heartbeat. See the header-comment contract in game.h. Command
 * application happens first and before any island updates, so a command
 * submitted for tick N is visible to tick N's simulation. */
void sim_run_one_tick(GameState *gs)
{
    int i;

    /* 1. Apply every command whose tick has now arrived, in log order.
     * command_submit stamps with the then-current sim_tick_no, so the
     * pending tail is exactly the commands for this tick (<= guards
     * against any straggler rather than deadlocking the cursor). */
    while (gs->cmd_applied < gs->cmd_count &&
           gs->cmd_log[gs->cmd_applied].tick <= gs->sim_tick_no) {
        sim_apply(gs, &gs->cmd_log[gs->cmd_applied]);
        gs->cmd_applied++;
    }

    /* 2. Every settled island's full pipeline, one tick, in order —
     * see island_update()'s ordering constraint. */
    for (i = 0; i < MAX_ISLANDS; i++)
        island_update(&gs->islands[i]);

    /* 3. Voyages advance independently of any island. */
    ships_update(gs->ships, gs->ship_count, gs->islands, MAX_ISLANDS,
                 gs->sim_tick_no);

    /* 4. Advance the world clock. */
    gs->sim_tick_no++;
}

/* ---- sim_hash -------------------------------------------
 * FNV-1a over exactly the state that defines the world (see game.h).
 * Byte-hashing struct fields individually — rather than memcmp-ing
 * whole structs — is what lets it skip padding and the derived/cosmetic
 * fields that would otherwise make the hash flap without a real desync. */
static void fnv_bytes(uint64_t *h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < n; i++) {
        *h ^= p[i];
        *h *= 1099511628211ULL;   /* FNV-1a 64-bit prime */
    }
}

uint64_t sim_hash(const GameState *gs)
{
    uint64_t h = 14695981039346656037ULL;   /* FNV-1a 64-bit offset */
    int      i, b, s;

    fnv_bytes(&h, &gs->sim_tick_no, sizeof(gs->sim_tick_no));

    for (i = 0; i < MAX_ISLANDS; i++) {
        const Island *isl = &gs->islands[i];

        fnv_bytes(&h, &isl->settled, sizeof(isl->settled));
        fnv_bytes(&h, isl->stockpile.amount, sizeof(isl->stockpile.amount));
        fnv_bytes(&h, &isl->stockpile.capacity, sizeof(isl->stockpile.capacity));

        for (b = 0; b < isl->building_count; b++) {
            const Building *bd = &isl->buildings[b];
            const PopData  *p  = &isl->pop_data[b];
            if (!bd->active) continue;

            fnv_bytes(&h, &bd->type, sizeof(bd->type));
            fnv_bytes(&h, &bd->row, sizeof(bd->row));
            fnv_bytes(&h, &bd->col, sizeof(bd->col));
            fnv_bytes(&h, &bd->timer, sizeof(bd->timer));
            fnv_bytes(&h, &bd->connected, sizeof(bd->connected));
            fnv_bytes(&h, &bd->worker_count, sizeof(bd->worker_count));

            if (p->active) {
                fnv_bytes(&h, &p->residents, sizeof(p->residents));
                fnv_bytes(&h, &p->happy, sizeof(p->happy));
                fnv_bytes(&h, &p->timer, sizeof(p->timer));
            }
        }
    }

    for (s = 0; s < gs->ship_count; s++) {
        const Ship *sh = &gs->ships[s];
        fnv_bytes(&h, &sh->active, sizeof(sh->active));
        if (!sh->active) continue;
        fnv_bytes(&h, &sh->at_island, sizeof(sh->at_island));
        fnv_bytes(&h, &sh->from_island, sizeof(sh->from_island));
        fnv_bytes(&h, &sh->to_island, sizeof(sh->to_island));
        /* departure_tick is the canonical voyage state; progress is a
         * derived float and deliberately excluded (Phase 2). */
        fnv_bytes(&h, &sh->departure_tick, sizeof(sh->departure_tick));
        fnv_bytes(&h, sh->cargo, sizeof(sh->cargo));
        fnv_bytes(&h, &sh->route_active, sizeof(sh->route_active));
        fnv_bytes(&h, &sh->route_a, sizeof(sh->route_a));
        fnv_bytes(&h, &sh->route_b, sizeof(sh->route_b));
        fnv_bytes(&h, &sh->route_res_ab, sizeof(sh->route_res_ab));
        fnv_bytes(&h, &sh->route_res_ba, sizeof(sh->route_res_ba));
        fnv_bytes(&h, &sh->route_qty, sizeof(sh->route_qty));
        fnv_bytes(&h, &sh->route_leg, sizeof(sh->route_leg));
    }

    return h;
}

/* ---- game_verify_determinism ----------------------------
 * The F9 self-check. Rebuilds the tick-0 world from world_seed in a
 * scratch GameState, borrows the live command log (read-only during
 * replay — sim_apply never appends), replays it tick-for-tick up to the
 * live tick, and compares hashes. See game.h. */
int game_verify_determinism(GameState *gs)
{
    GameState *scratch;
    uint64_t   h_live, h_replay;

    gs->replay_tick = gs->sim_tick_no;

    if (!gs->replay_valid) {
        gs->replay_state = 3;   /* n/a — world not derived from the log */
        return 0;
    }

    scratch = (GameState *)malloc(sizeof(GameState));
    if (!scratch) {
        SDL_Log("game_verify_determinism: out of memory for scratch world");
        gs->replay_state = 2;
        return 0;
    }

    /* Rebuild tick 0 from the same seed, then point the scratch world at
     * the live log and replay it. cmd_cap = 0 marks the log as borrowed
     * so nothing here grows or frees it; it is detached before free. */
    memset(scratch, 0, sizeof(*scratch));
    game_reset_world(scratch, gs->world_seed);
    scratch->cmd_log     = gs->cmd_log;
    scratch->cmd_count   = gs->cmd_count;
    scratch->cmd_cap     = 0;
    scratch->cmd_applied = 0;
    scratch->sim_tick_no = 0;
    scratch->sim_acc_ns  = 0;

    while (scratch->sim_tick_no < gs->sim_tick_no)
        sim_run_one_tick(scratch);

    h_live   = sim_hash(gs);
    h_replay = sim_hash(scratch);

    scratch->cmd_log = NULL;   /* detach the borrowed log before free */
    free(scratch);

    gs->replay_live_hash   = h_live;
    gs->replay_replay_hash = h_replay;
    gs->replay_state       = (h_live == h_replay) ? 1 : 2;

    return h_live == h_replay;
}

/* ---- commit_placement -----------------------------------
 * Shared by game_try_place_road() and game_place_building_confirmed():
 * the actual building_place() call plus its post-placement side
 * effects (House PopData, Warehouse storage capacity). Callers are
 * responsible for their own affordability check and payment
 * deduction beforehand — this only ever runs once placement is
 * already decided. Returns the new building's index, or -1 on
 * failure (full array, invalid tile, or an unsettled island). */
static int commit_placement(GameState *gs, int island, BuildingType type,
                            int row, int col)
{
    Island *isl = &gs->islands[island];
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
static int sim_place_road(GameState *gs, int island, int row, int col)
{
    Island            *isl = &gs->islands[island];
    const BuildingDef *def = &BUILDING_DEFS[BUILDING_ROAD];

    if (!isl->settled) return 0;
    if (!building_can_place(&isl->map, BUILDING_ROAD, row, col, NULL, 0))
        return 0;
    if (!building_can_afford(&isl->stockpile, BUILDING_ROAD))
        return 0;

    if (commit_placement(gs, island, BUILDING_ROAD, row, col) < 0)
        return 0;

    stockpile_add(&isl->stockpile, RES_GOLD, -def->cost[RES_GOLD]);
    return 1;
}

int game_try_place_road(GameState *gs, int row, int col)
{
    Command c = {0};
    c.kind = CMD_PLACE_ROAD;
    c.a    = gs->current_island;
    c.b    = row;
    c.c    = col;
    return command_submit(gs, &c);
}

/* ---- sim_place_building / game_place_building_confirmed ----
 * The sim body validates everything itself (type range, settled,
 * affordability) so it is safe to call from a replayed log where the
 * accompanying GameState fields no longer describe the moment of
 * submission. */
static int sim_place_building(GameState *gs, int island, int row, int col,
                              BuildingType type, int pay_with_gold)
{
    Island            *isl = &gs->islands[island];
    const BuildingDef *def;
    int                i;

    if (type <= BUILDING_NONE || type >= BUILDING_TYPE_COUNT) return 0;
    if (!isl->settled) return 0;
    if (row < 0) return 0;
    def = &BUILDING_DEFS[type];

    if (pay_with_gold) {
        int gold_cost = building_gold_equivalent_cost(type);
        if (isl->stockpile.amount[RES_GOLD] < gold_cost) return 0;

        if (commit_placement(gs, island, type, row, col) < 0)
            return 0;

        stockpile_add(&isl->stockpile, RES_GOLD, -gold_cost);
    } else {
        if (!building_can_afford(&isl->stockpile, type)) return 0;

        if (commit_placement(gs, island, type, row, col) < 0)
            return 0;

        for (i = 0; i < RES_COUNT; i++)
            if (def->cost[i] > 0)
                stockpile_add(&isl->stockpile, (ResourceType)i, -def->cost[i]);
    }
    return 1;
}

void game_place_building_confirmed(GameState *gs, int pay_with_gold)
{
    Command c = {0};

    if (gs->selected_building == BUILDING_NONE) return;
    if (gs->build_confirm_row < 0) return;

    c.kind = CMD_PLACE_BUILDING;
    c.a    = gs->current_island;
    c.b    = gs->build_confirm_row;
    c.c    = gs->build_confirm_col;
    /* Pack type and the payment bit into one slot — see command.h. */
    c.d    = (int32_t)((int)gs->selected_building * 2 + (pay_with_gold ? 1 : 0));
    command_submit(gs, &c);
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
static int sim_sell(GameState *gs, int island, ResourceType res, int qty)
{
    Island *isl = &gs->islands[island];

    if (res < 0 || res >= RES_COUNT || res == RES_GOLD) return 0;
    if (qty > isl->stockpile.amount[res]) qty = isl->stockpile.amount[res];
    if (qty <= 0) return 0;

    stockpile_add(&isl->stockpile, res, -qty);
    stockpile_add(&isl->stockpile, RES_GOLD, qty * SELL_PRICE[res]);
    return 1;
}

void game_sell_resource(GameState *gs, ResourceType res, int qty)
{
    Command c = {0};
    c.kind = CMD_SELL_RESOURCE;
    c.a    = gs->current_island;
    c.b    = (int32_t)res;
    c.c    = qty;
    command_submit(gs, &c);
}

/* ---- sim_buy / game_buy_resource ----------------------------
 * qty < 0 means "buy as much as storage headroom and Gold allow"; that
 * is resolved here against the live stockpile, so it stays correct when
 * replayed. */
static int sim_buy(GameState *gs, int island, ResourceType res, int qty)
{
    Island *isl = &gs->islands[island];
    int     headroom, max_affordable;

    if (res < 0 || res >= RES_COUNT || res == RES_GOLD) return 0;

    headroom = isl->stockpile.capacity - isl->stockpile.amount[res];
    if (headroom < 0) headroom = 0;

    max_affordable = (BUY_PRICE[res] > 0)
                    ? isl->stockpile.amount[RES_GOLD] / BUY_PRICE[res]
                    : 0;

    if (qty < 0)
        qty = (headroom < max_affordable) ? headroom : max_affordable;
    if (qty > headroom)        qty = headroom;
    if (qty > max_affordable)  qty = max_affordable;
    if (qty <= 0) return 0;

    stockpile_add(&isl->stockpile, RES_GOLD, -(qty * BUY_PRICE[res]));
    stockpile_add(&isl->stockpile, res, qty);
    return 1;
}

void game_buy_resource(GameState *gs, ResourceType res, int qty)
{
    Command c = {0};
    c.kind = CMD_BUY_RESOURCE;
    c.a    = gs->current_island;
    c.b    = (int32_t)res;
    c.c    = qty;
    command_submit(gs, &c);
}

/* ---- sim_demolish / game_demolish_building ------------------- */
static int sim_demolish(GameState *gs, int island, int idx)
{
    Island      *isl = &gs->islands[island];
    BuildingType type;
    int          i;

    if (idx < 0 || idx >= isl->building_count) return 0;
    if (!isl->buildings[idx].active) return 0;

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
    return 1;
}

void game_demolish_building(GameState *gs, int idx)
{
    Command c = {0};
    c.kind = CMD_DEMOLISH;
    c.a    = gs->current_island;
    c.b    = idx;
    command_submit(gs, &c);
}

/* ---- sim_upgrade_house / game_upgrade_house ------------------ */
static int sim_upgrade_house(GameState *gs, int island, int idx)
{
    Island *isl = &gs->islands[island];

    if (idx < 0 || idx >= isl->building_count) return 0;
    if (!isl->buildings[idx].active) return 0;
    if (isl->buildings[idx].type != BUILDING_HOUSE) return 0;
    if (isl->stockpile.amount[RES_GOLD] < TIER_UPGRADE_COST_GOLD) return 0;

    stockpile_add(&isl->stockpile, RES_GOLD, -TIER_UPGRADE_COST_GOLD);
    isl->buildings[idx].type = BUILDING_HOUSE_WORKER;
    return 1;
}

void game_upgrade_house(GameState *gs, int idx)
{
    Command c = {0};
    c.kind = CMD_UPGRADE_HOUSE;
    c.a    = gs->current_island;
    c.b    = idx;
    command_submit(gs, &c);
}

/* ---- sim_build_ship / game_build_ship -----------------------
 * Returns the new ship's slot index, or -1 on failure. Slot choice
 * (reuse-first-inactive, else append) is a deterministic function of
 * the ship array, so a replayed log lands the ship in the same slot. */
static int sim_build_ship(GameState *gs, int island)
{
    Island *isl = &gs->islands[island];
    int     i, slot = -1;

    if (!isl->settled) return -1;
    if (isl->stockpile.amount[RES_GOLD] < SHIP_BUILD_COST_GOLD) return -1;

    for (i = 0; i < gs->ship_count; i++)
        if (!gs->ships[i].active) { slot = i; break; }
    if (slot < 0) {
        if (gs->ship_count >= MAX_SHIPS) return -1;
        slot = gs->ship_count++;
    }

    memset(&gs->ships[slot], 0, sizeof(Ship));
    gs->ships[slot].active      = 1;
    gs->ships[slot].at_island   = island;
    gs->ships[slot].from_island = island;
    gs->ships[slot].to_island   = island;

    stockpile_add(&isl->stockpile, RES_GOLD, -SHIP_BUILD_COST_GOLD);

    SDL_Log("Ship %d launched at %s", slot, isl->name);
    return slot;
}

int game_build_ship(GameState *gs)
{
    Command c = {0};
    c.kind = CMD_BUILD_SHIP;
    c.a    = gs->current_island;
    c.b    = -1;   /* shipyard index: not used by the sim yet */
    return command_submit(gs, &c);
}

/* ---- sim_ship_transfer / game_ship_transfer -----------------
 * Moves goods across a dock only, never across open water: the ship
 * must be docked at `island`. Clamping is deferred to ship_transfer_at
 * so the manual path cannot disagree with what trade routes do. */
static int sim_ship_transfer(GameState *gs, int ship_idx, ResourceType res,
                             int qty, int island)
{
    Ship *sh;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return 0;
    if (res < 0 || res >= RES_COUNT) return 0;
    sh = &gs->ships[ship_idx];
    if (!sh->active) return 0;
    if (sh->at_island != island) return 0;

    ship_transfer_at(sh, &gs->islands[island], res, qty);
    return 1;
}

void game_ship_transfer(GameState *gs, int ship_idx, ResourceType res, int qty)
{
    Command c = {0};
    c.kind = CMD_SHIP_TRANSFER;
    c.a    = ship_idx;
    c.b    = (int32_t)res;
    c.c    = qty;
    c.d    = gs->current_island;   /* the dock this transfer happens at */
    command_submit(gs, &c);
}

/* ---- sim_ship_depart / game_ship_depart ---------------------
 * Was an inline mutation in main.c's world overlay; now a command like
 * every other. The ship must be docked somewhere other than its
 * destination. */
static int sim_ship_depart(GameState *gs, int ship_idx, int dest)
{
    Ship *sh;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return 0;
    if (dest < 0 || dest >= MAX_ISLANDS) return 0;
    sh = &gs->ships[ship_idx];
    if (!sh->active) return 0;
    if (sh->at_island < 0) return 0;         /* already at sea       */
    if (sh->at_island == dest) return 0;     /* nowhere to go        */

    sh->from_island    = sh->at_island;
    sh->to_island      = dest;
    sh->at_island      = -1;                 /* now at sea           */
    sh->departure_tick = gs->sim_tick_no;    /* fixes the whole voyage */
    sh->progress       = 0.0f;
    return 1;
}

int game_ship_depart(GameState *gs, int ship_idx, int dest_island)
{
    Command c = {0};
    c.kind = CMD_SHIP_DEPART;
    c.a    = ship_idx;
    c.b    = dest_island;
    return command_submit(gs, &c);
}

/* ---- sim_colonise / game_colonise ---------------------------- */
static int sim_colonise(GameState *gs, int ship_idx, int island_idx)
{
    Ship   *sh;
    Island *isl;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return 0;
    if (island_idx < 0 || island_idx >= MAX_ISLANDS) return 0;

    sh  = &gs->ships[ship_idx];
    isl = &gs->islands[island_idx];

    if (!sh->active) return 0;
    if (sh->at_island != island_idx) return 0;     /* must be there   */
    if (isl->settled) return 0;                    /* already ours    */
    if (sh->cargo[RES_GOLD] < COLONY_FOUNDING_GOLD) return 0;

    /* The grant physically leaves the hold and becomes the colony's
     * treasury — without it the new island could not pay for so much
     * as a road, since every cost is denominated in its own Gold. */
    sh->cargo[RES_GOLD] -= COLONY_FOUNDING_GOLD;

    stockpile_init(&isl->stockpile);
    stockpile_add(&isl->stockpile, RES_GOLD, COLONY_FOUNDING_GOLD);
    isl->settled = 1;
    camera_init(&isl->camera, SCREEN_W, SCREEN_H, MAP_COLS, MAP_ROWS);

    SDL_Log("Colony founded on %s with %d Gold", isl->name, COLONY_FOUNDING_GOLD);
    return 1;
}

int game_colonise(GameState *gs, int ship_idx, int island_idx)
{
    Command c = {0};
    c.kind = CMD_COLONISE;
    c.a    = ship_idx;
    c.b    = island_idx;
    return command_submit(gs, &c);
}

/* ---- sim_set_route_res / game_ship_set_route_res ------------
 * Cycle the resource carried on one leg of a ship's trade route through
 * every good and back to RES_COUNT ("carry nothing"), which is what
 * makes one-way runs expressible. `leg` 0 is the outbound A->B slot,
 * 1 the return B->A slot. */
static int sim_set_route_res(GameState *gs, int ship_idx, int leg)
{
    Ship         *sh;
    ResourceType *slot;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return 0;
    sh = &gs->ships[ship_idx];
    if (!sh->active) return 0;

    slot  = (leg == 0) ? &sh->route_res_ab : &sh->route_res_ba;
    *slot = (*slot >= RES_COUNT) ? (ResourceType)0
                                 : (ResourceType)(*slot + 1);
    return 1;
}

int game_ship_set_route_res(GameState *gs, int ship_idx, int leg)
{
    Command c = {0};
    c.kind = CMD_SET_ROUTE_RES;
    c.a    = ship_idx;
    c.b    = leg;
    return command_submit(gs, &c);
}

/* ---- sim_toggle_route / game_ship_toggle_route --------------
 * Turn a ship's route off if on; otherwise arm it to repeat the voyage
 * the ship last made (from_island -> to_island), so there is no
 * separate pick-two-islands mode to build. No-op if the ship has no
 * distinct last voyage to repeat. */
static int sim_toggle_route(GameState *gs, int ship_idx)
{
    Ship *sh;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return 0;
    sh = &gs->ships[ship_idx];
    if (!sh->active) return 0;

    if (sh->route_active) {
        sh->route_active = 0;
        return 1;
    }
    if (sh->from_island != sh->to_island) {
        sh->route_a      = sh->from_island;
        sh->route_b      = sh->to_island;
        sh->route_qty    = SHIP_CARGO_CAPACITY;
        sh->route_leg    = (sh->at_island == sh->route_b) ? 0 : 1;
        sh->route_active = 1;
        return 1;
    }
    return 0;
}

int game_ship_toggle_route(GameState *gs, int ship_idx)
{
    Command c = {0};
    c.kind = CMD_TOGGLE_ROUTE;
    c.a    = ship_idx;
    return command_submit(gs, &c);
}

/* ---- sim_apply ----------------------------------------------
 * The single dispatch from a Command to the mutation that carries it
 * out. The ONLY caller of the sim_* bodies above, and the only place
 * world state changes. Never appends to the log (command_submit does
 * that, and replay calls sim_apply directly). Returns 1 if the command
 * mutated state, 0 if it was rejected — rejection is deterministic and
 * not an error. Payload decoding mirrors command.h. */
int sim_apply(GameState *gs, const Command *c)
{
    switch (c->kind) {
    case CMD_PLACE_BUILDING: {
        BuildingType type = (BuildingType)(c->d / 2);
        int          pay  = c->d & 1;
        if (c->a < 0 || c->a >= MAX_ISLANDS) return 0;
        return sim_place_building(gs, c->a, c->b, c->c, type, pay);
    }
    case CMD_PLACE_ROAD:
        if (c->a < 0 || c->a >= MAX_ISLANDS) return 0;
        return sim_place_road(gs, c->a, c->b, c->c);
    case CMD_DEMOLISH:
        if (c->a < 0 || c->a >= MAX_ISLANDS) return 0;
        return sim_demolish(gs, c->a, c->b);
    case CMD_SELL_RESOURCE:
        if (c->a < 0 || c->a >= MAX_ISLANDS) return 0;
        return sim_sell(gs, c->a, (ResourceType)c->b, c->c);
    case CMD_BUY_RESOURCE:
        if (c->a < 0 || c->a >= MAX_ISLANDS) return 0;
        return sim_buy(gs, c->a, (ResourceType)c->b, c->c);
    case CMD_UPGRADE_HOUSE:
        if (c->a < 0 || c->a >= MAX_ISLANDS) return 0;
        return sim_upgrade_house(gs, c->a, c->b);
    case CMD_BUILD_SHIP:
        if (c->a < 0 || c->a >= MAX_ISLANDS) return 0;
        return sim_build_ship(gs, c->a) >= 0;
    case CMD_SHIP_TRANSFER:
        if (c->d < 0 || c->d >= MAX_ISLANDS) return 0;
        return sim_ship_transfer(gs, c->a, (ResourceType)c->b, c->c, c->d);
    case CMD_SHIP_DEPART:
        return sim_ship_depart(gs, c->a, c->b);
    case CMD_COLONISE:
        return sim_colonise(gs, c->a, c->b);
    case CMD_SET_ROUTE_RES:
        return sim_set_route_res(gs, c->a, c->b);
    case CMD_TOGGLE_ROUTE:
        return sim_toggle_route(gs, c->a);
    default:
        return 0;
    }
}
