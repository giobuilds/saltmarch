/*  game.c  --  Game state management  (Phase 5)  */

#include "game.h"
#include "building.h"
#include "resource.h"
#include "population.h"
#include "connectivity.h"
#include "agent.h"
#include "island.h"
#include "ship.h"
#include "simlog.h"
#include "snapshot.h"
#include "orderbook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A seed for "new world, no seed given". The sim owns no clock (see. */
static uint32_t seed_from_clock(void)
{
    return (uint32_t)time(NULL) * 2654435761u + (uint32_t)clock();
}

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
    gs->confirm.open          = 0;
    gs->confirm.kind          = CONFIRM_NONE;
    gs->escrow_open           = 0;
    gs->book_open             = 0;
    gs->charts_open           = 0;
    gs->yard_open             = 0;
    gs->inventory_open        = 0;
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

/* ---- the scrubber (MMO_PLAN later phases) ------------------ */

int game_scrubbing(const GameState *gs) { return gs->scrub_active; }

uint64_t game_scrub_max(const GameState *gs)
{
    return gs->scrub_active ? gs->scrub_live_tick : gs->sim_tick_no;
}

uint64_t game_scrub_min(const GameState *gs)
{
    return gs->history_floor_tick;
}

/* Adopt `buf` as the floor this world stands on. Takes a COPY: callers
 * hand over buffers they own (a file's bytes, a wire frame) and must
 * not have their lifetime tangled with the world's. */
int game_set_history_floor(GameState *gs, const unsigned char *buf,
                           size_t len, uint64_t tick)
{
    unsigned char *copy = (unsigned char *)malloc(len ? len : 1);

    if (!copy) return 0;
    if (len) memcpy(copy, buf, len);

    free(gs->floor_snap);
    gs->floor_snap         = copy;
    gs->floor_snap_len     = len;
    gs->history_floor_tick = tick;
    gs->replay_valid       = 0;
    return 1;
}

void game_clear_history_floor(GameState *gs)
{
    free(gs->floor_snap);
    gs->floor_snap         = NULL;
    gs->floor_snap_len     = 0;
    gs->history_floor_tick = 0;
}

void game_truncate_log(GameState *gs)
{
    unsigned char *snap = NULL;
    size_t         snap_len = 0;
    int            first = gs->cmd_applied;
    int            keep;

    if (first <= 0) return;                 /* nothing applied yet */

    /* Capture the state the discarded commands produced BEFORE
     * discarding them. Without this the world would still be correct
     * going forward but unreconstructable backward, and the scrubber
     * would have nothing to stand on. */
    if (!snapshot_encode(gs, &snap, &snap_len)) {
        sim_log("game_truncate_log: could not snapshot; keeping history");
        return;
    }
    if (!game_set_history_floor(gs, snap, snap_len, gs->sim_tick_no)) {
        sim_log("game_truncate_log: out of memory; keeping history");
        free(snap);
        return;
    }
    free(snap);
    if (first > gs->cmd_count) first = gs->cmd_count;
    keep = gs->cmd_count - first;

    /* Only the applied PREFIX goes. The tail is commands stamped for
     * ticks that have not run yet -- accepted and acknowledged, but not
     * applied -- and discarding those would un-accept work the players
     * were already told had landed. */
    if (keep > 0)
        memmove(gs->cmd_log, gs->cmd_log + first,
                sizeof(Command) * (size_t)keep);
    gs->cmd_count   = keep;
    gs->cmd_applied = 0;

    sim_log("log truncated at tick %llu (%d pending commands kept)",
            (unsigned long long)gs->sim_tick_no, keep);
}

void game_scrub_begin(GameState *gs)
{
    if (gs->scrub_active) return;
    gs->scrub_active    = 1;
    gs->scrub_live_tick = gs->sim_tick_no;
}

void game_scrub_to(GameState *gs, uint64_t tick)
{
    Command *saved;
    int      count;
    uint32_t seed;

    if (!gs->scrub_active) return;
    if (tick > gs->scrub_live_tick) tick = gs->scrub_live_tick;
    /* Not below the floor. game_install_world rebuilds from the seed at */
    if (tick < gs->history_floor_tick) tick = gs->history_floor_tick;

    /* The log is the world's history and must survive the trip. Copy it
     * out, rebuild from the seed, put it back: install_world resets the
     * log to what it is given, so handing it the live log keeps every
     * later command available for scrubbing forward again. */
    count = gs->cmd_count;
    seed  = gs->world_seed;
    saved = (Command *)malloc(sizeof(Command) * (size_t)(count > 0 ? count : 1));
    if (!saved) return;
    if (count > 0) memcpy(saved, gs->cmd_log, sizeof(Command) * (size_t)count);

    if (gs->floor_snap)
        game_install_from_snapshot(gs, gs->floor_snap, gs->floor_snap_len,
                                   tick, saved, count);
    else
        game_install_world(gs, seed, tick, saved, count);
    free(saved);

    /* install_world cleared these; scrubbing is a view, not a new
     * world, so restore the fact that we are in it. */
    gs->scrub_active = 1;
    /* scrub_live_tick survives install_world (it is not world state),
     * but be explicit: the live head is wherever we came from. */
    if (gs->scrub_live_tick < tick) gs->scrub_live_tick = tick;
}

void game_scrub_end(GameState *gs)
{
    uint64_t live;

    if (!gs->scrub_active) return;

    live = gs->scrub_live_tick;
    game_scrub_to(gs, live);
    gs->scrub_active = 0;
}

/* ---- the overlay arbiter (UI_PLAN Phase 4) ----------------- */
GameOverlay game_topmost_overlay(const GameState *gs)
{
    /* Topmost first. The menu is modal over everything; the world map
     * is the backdrop the others can open on top of. */
    if (gs->menu_open)             return UI_OVERLAY_MENU;
    if (gs->confirm.open)          return UI_OVERLAY_CONFIRM;
    if (gs->trade_open)            return UI_OVERLAY_TRADE;
    if (gs->escrow_open)           return UI_OVERLAY_ESCROW;
    if (gs->book_open)             return UI_OVERLAY_BOOK;
    if (gs->charts_open)           return UI_OVERLAY_CHARTS;
    if (gs->yard_open)             return UI_OVERLAY_YARD;
    if (gs->inventory_open)        return UI_OVERLAY_INVENTORY;
    if (gs->world_open)            return UI_OVERLAY_WORLD;
    return UI_OVERLAY_NONE;
}

int game_overlay_open(const GameState *gs)
{
    return game_topmost_overlay(gs) != UI_OVERLAY_NONE;
}

/* The archipelago's fixed make-up. Island 0 is always Saltford. */
/* Four northern, four southern (SUPPLY_CHAIN Phase 5). The order is
 * load-bearing: island 0 is always the starting island, and the
 * southern four sit at the end so an existing world's island indices
 * keep meaning what they meant. */
static const MapProfile ISLAND_PROFILES[MAX_ISLANDS] = {
    PROFILE_TEMPERATE, PROFILE_HIGHLAND, PROFILE_WOODLAND, PROFILE_ATOLL,
    PROFILE_PLANTATION, PROFILE_PLANTATION, PROFILE_JUNGLE, PROFILE_JUNGLE
};
static const char *ISLAND_NAMES[MAX_ISLANDS] = {
    "Saltford", "Brinehold", "Tidefast", "Marrowbay",
    "Canereach", "Palmfast", "Vinemarch", "Thornhollow"
};

/* ---- game_reset_world -----------------------------------
 * Regenerates the whole archipelago and clears all per-island state. */
static void game_reset_world(GameState *gs, uint32_t seed)
{
    int i;

    gs->world_seed = seed;

    /* A world built from a seed has its whole history ahead of it. */
    game_clear_history_floor(gs);

    for (i = 0; i < MAX_ISLANDS; i++) {
        /* Derive each island's seed from the world seed so one number
         * still reproduces the entire archipelago. */
        uint32_t isl_seed = seed + (uint32_t)i * 2654435761u;

        island_reset(&gs->islands[i], isl_seed, ISLAND_PROFILES[i],
                     ISLAND_NAMES[i], i == 0);

        /* Stagger the job-assignment phase across islands. */
        gs->islands[i].agent_assign_timer =
            i * AGENT_ASSIGN_INTERVAL_TICKS / MAX_ISLANDS;
    }

    /* The sea is generated from the same seed as the islands it
     * separates, so an archipelago and the water around it are one
     * deterministic object. */
    sea_init(&gs->sea, seed, MAX_ISLANDS);

    gs->current_island = 0;
    gs->world_open     = 0;
    memset(gs->ships, 0, sizeof(gs->ships));
    gs->ship_count          = 0;
    gs->world_selected_ship = -1;

    stockpile_add(&cur(gs)->stockpile, RES_GOLD, STARTING_GOLD);

    /* The world's first player is ALWAYS player 1, regardless of who */
    gs->islands[0].owner = 1u;

    /* The market's home ports (MARITIME_PLAN Phase 2). Settled and
     * owned from tick 0, by index rather than by anything random, so
     * every client agrees about where the market IS without being
     * told. They are not colonisable — see sim_colonise. */
    for (i = 0; i < MAX_ISLANDS; i++)
        if (faction_is_home_port(i)) {
            gs->islands[i].settled = 1;
            gs->islands[i].owner   = PLAYER_FACTION;
        }

    /* The market starts at baseline, so day-one quotes equal the old
     * fixed SELL_PRICE/BUY_PRICE until the player trades. */
    faction_init(&gs->faction);
    faction_init_routes(&gs->faction, &gs->sea);
    orderbook_init(&gs->book);
    knowledge_init(&gs->knowledge);
    survey_init(&gs->surveys);
    pirate_init(&gs->pirates, &gs->sea, seed);

    /* A fresh world is a fresh history: discard any previous command */
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
    /* calloc, not malloc. The explicit initialisation below stays -- */
    GameState *gs = (GameState *)calloc(1, sizeof(GameState));
    if (!gs) return NULL;

    /* Plain data, zeroed here rather than through input.c: the device
     * itself is the client's business (see input.h). */
    memset(&gs->input, 0, sizeof(gs->input));
    gs->last_tick  = 0;   /* seeded by client_update on its first frame */
    gs->delta_time = 0.0f;

    /* The command log starts empty. game_reset_world resets the
     * counters but relies on the pointer/cap being valid. */
    gs->cmd_log     = NULL;
    gs->cmd_count   = 0;
    gs->intent_log  = NULL;
    gs->intent_count = 0;
    gs->intent_cap   = 0;
    gs->cmd_cap     = 0;
    gs->cmd_applied = 0;
    gs->sim_tick_no = 0;
    gs->sim_acc_ns  = 0;

    gs->replay_live_hash   = 0;
    gs->replay_replay_hash = 0;
    gs->replay_tick        = 0;

    gs->cmd_seq_next    = 1u;
    gs->cmd_seq_last    = 0u;
    gs->scrub_active    = 0;
    gs->scrub_live_tick = 0;
    /* Freed by game_reset_world below, so it has to be a real pointer
     * before that runs -- see the calloc note above. */
    gs->floor_snap         = NULL;
    gs->floor_snap_len     = 0;
    gs->history_floor_tick = 0;
    gs->result_count    = 0;
    gs->local_player_id = 1u;
    gs->net             = NULL;   /* attached by net_attach when hosting/joining */
    gs->net_submit      = NULL;

    game_reset_world(gs, seed_from_clock());

    return gs;
}

/* ---- game_free ----------------------------------------- */
void game_free(GameState *gs)
{
    game_clear_history_floor(gs);
    if (!gs) return;
    command_log_free(gs);
    intent_log_free(gs);
    free(gs);
}

/* ---- game_new -------------------------------------------- */
void game_new(GameState *gs)
{
    gs->local_player_id = 1u;
    game_reset_world(gs, seed_from_clock());
}

/* ---- game_new_seeded ------------------------------------- */
void game_new_seeded(GameState *gs, uint32_t seed)
{
    gs->local_player_id = 1u;
    game_reset_world(gs, seed);
}

/* ---- Save format v5: the world as (seed + command log) ---- */
/* ---- Save format v10: optionally, state instead of history ---- */
#define SAVE_FLAG_SNAPSHOT 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t world_seed;
    int32_t  current_island;
    uint64_t sim_tick_no;
    int32_t  cmd_count;
    int32_t  intent_count;   /* v8: the recorded input stream */
    uint32_t flags;          /* v10: SAVE_FLAG_SNAPSHOT              */
    int32_t  snapshot_bytes; /* v10: size of the snapshot section    */
} SaveHeader;

#define SAVE_MAGIC   0x53414C54u  /* "SALT" */
/* v6 (Phase 5): commands carry a meaningful player_id and sim_apply */
#define SAVE_VERSION 37u

/* Plain stdio rather than SDL_IOStream (MMO_PLAN Phase 6): a save IS. */
/* The one writer. `snap` is NULL for a history save and a full-state
 * snapshot for a checkpoint; `cmds`/`n` are the commands that follow
 * it, which for a checkpoint is only the tail that has not been applied
 * yet. */
static int save_write(const GameState *gs, const char *path,
                      const unsigned char *snap, size_t snap_len,
                      const Command *cmds, int n)
{
    FILE         *f = fopen(path, "wb");
    SaveHeader    hdr;
    size_t        log_bytes    = sizeof(Command) * (size_t)n;
    size_t        intent_bytes = sizeof(Intent) * (size_t)gs->intent_count;
    int           ok;

    if (!f) {
        sim_log("game_save: could not open %s for writing", path);
        return 0;
    }

    /* The header has trailing padding (cmd_count sits at offset 24 in. */
    memset(&hdr, 0, sizeof(hdr));

    hdr.magic          = SAVE_MAGIC;
    hdr.version        = SAVE_VERSION;
    hdr.world_seed     = gs->world_seed;
    hdr.current_island = gs->current_island;
    hdr.sim_tick_no    = gs->sim_tick_no;
    hdr.cmd_count      = n;
    hdr.intent_count   = gs->intent_count;
    hdr.flags          = snap ? SAVE_FLAG_SNAPSHOT : 0u;
    hdr.snapshot_bytes = snap ? (int32_t)snap_len : 0;

    ok = fwrite(&hdr, sizeof(hdr), 1, f) == 1
      && (!snap || fwrite(snap, snap_len, 1, f) == 1)
      && (log_bytes == 0 ||
          fwrite(cmds, log_bytes, 1, f) == 1)
      && (intent_bytes == 0 ||
          fwrite(gs->intent_log, intent_bytes, 1, f) == 1);

    /* fclose can fail where fwrite succeeded (a full disk only surfaces
     * at flush), so a save is not saved until the close says so. */
    if (fclose(f) != 0) ok = 0;

    if (!ok) {
        sim_log("game_save: write to %s failed", path);
        return 0;
    }

    if (snap)
        sim_log("Checkpoint written to %s (tick %llu, %llu-byte snapshot, "
                "%d pending commands, %d intents)",
                path, (unsigned long long)gs->sim_tick_no,
                (unsigned long long)snap_len, n, gs->intent_count);
    else
        sim_log("Game saved to %s (seed %u, tick %llu, %d commands, %d intents)",
                path, gs->world_seed,
                (unsigned long long)gs->sim_tick_no, n, gs->intent_count);
    return 1;
}

int game_save(const GameState *gs, const char *path)
{
    return save_write(gs, path, NULL, 0, gs->cmd_log, gs->cmd_count);
}

int game_save_checkpoint(const GameState *gs, const char *path)
{
    unsigned char *snap = NULL;
    size_t         snap_len = 0;
    int            ok, first, n;

    if (!snapshot_encode(gs, &snap, &snap_len)) {
        sim_log("game_save_checkpoint: could not encode the world");
        return 0;
    }

    /* The tail is not optional. Commands are stamped */
    first = gs->cmd_applied;
    if (first < 0) first = 0;
    if (first > gs->cmd_count) first = gs->cmd_count;
    n = gs->cmd_count - first;

    ok = save_write(gs, path, snap, snap_len, gs->cmd_log + first, n);
    free(snap);
    return ok;
}

/* ---- game_load -------------------------------------------- */
int game_load(GameState *gs, const char *path)
{
    FILE          *f = fopen(path, "rb");
    long           size_l;
    size_t         size, need, snap_bytes;
    unsigned char *buf;
    SaveHeader     hdr;
    const unsigned char *cmds;

    if (!f) {
        sim_log("game_load: could not open %s", path);
        return 0;
    }

    if (fseek(f, 0, SEEK_END) != 0 || (size_l = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        sim_log("game_load: %s is not a seekable file", path);
        fclose(f);
        return 0;
    }
    if ((size_t)size_l < sizeof(SaveHeader)) {
        sim_log("game_load: %s is too small to be a save file", path);
        fclose(f);
        return 0;
    }
    size = (size_t)size_l;

    buf = (unsigned char *)malloc(size);
    if (!buf) { fclose(f); return 0; }

    if (fread(buf, size, 1, f) != 1) {
        sim_log("game_load: %s could not be read in full", path);
        fclose(f);
        free(buf);
        return 0;
    }
    fclose(f);

    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.magic != SAVE_MAGIC || hdr.version != SAVE_VERSION) {
        sim_log("game_load: %s is not a v%u (seed+log) save file",
                path, SAVE_VERSION);
        free(buf);
        return 0;
    }
    if (hdr.cmd_count < 0 || hdr.intent_count < 0 ||
        hdr.current_island < 0 || hdr.current_island >= MAX_ISLANDS) {
        sim_log("game_load: %s has an invalid header", path);
        free(buf);
        return 0;
    }
    if (hdr.snapshot_bytes < 0) {
        sim_log("game_load: %s has an invalid header", path);
        free(buf);
        return 0;
    }
    snap_bytes = (hdr.flags & SAVE_FLAG_SNAPSHOT)
                 ? (size_t)hdr.snapshot_bytes : 0;
    need = sizeof(hdr) + snap_bytes
                       + sizeof(Command) * (size_t)hdr.cmd_count
                       + sizeof(Intent)  * (size_t)hdr.intent_count;
    if (need > size) {
        sim_log("game_load: %s is truncated", path);
        free(buf);
        return 0;
    }

    /* Bytes, not a Command pointer: `snap_bytes` is whatever the
     * snapshot encoded to, so this address has no alignment anybody
     * chose, and the conversion alone is undefined (command.h). */
    cmds = buf + sizeof(hdr) + snap_bytes;

    if (snap_bytes) {
        /* A checkpoint: the world is restored, not re-derived. */
        if (!snapshot_decode(gs, buf + sizeof(hdr), snap_bytes)) {
            sim_log("game_load: %s carries a snapshot this build "
                    "cannot use", path);
            free(buf);
            return 0;
        }
        if (!command_log_set_bytes(gs, cmds, hdr.cmd_count)) {
            sim_log("game_load: out of memory installing %d pending commands",
                    hdr.cmd_count);
            free(buf);
            return 0;
        }
        /* F9 rebuilds by replaying from tick 0, and below this
         * checkpoint there is no longer a tick 0 to replay. Recording
         * the floor is what keeps the scrubber honest above it and
         * stops it fabricating a past below it. */
        if (!game_set_history_floor(gs, buf + sizeof(hdr), snap_bytes,
                                    gs->sim_tick_no)) {
            sim_log("game_load: out of memory keeping the history floor");
            free(buf);
            return 0;
        }
    } else if (!game_install_world(gs, hdr.world_seed, hdr.sim_tick_no,
                                   cmds, hdr.cmd_count)) {
        /* A history save: rebuild tick 0 from the seed (which sets
         * replay_valid = 1), install the log, replay to the saved
         * tick. Loading IS the determinism test. */
        sim_log("game_load: out of memory installing %d commands",
                hdr.cmd_count);
        free(buf);
        return 0;
    }
    /* The intents are cargo: they describe how the world was reached,
     * not what it is, so a failure to install them is worth a line in
     * the log and nothing more. */
    if (hdr.intent_count > 0) {
        const Intent *ins = (const Intent *)(buf + sizeof(hdr) + snap_bytes +
                                sizeof(Command) * (size_t)hdr.cmd_count);
        if (!intent_log_set(gs, ins, hdr.intent_count))
            sim_log("game_load: could not install %d intents",
                    hdr.intent_count);
    } else {
        gs->intent_count = 0;
    }

    free(buf);

    game_set_current_island(gs, hdr.current_island);

    sim_log("Game loaded from %s (%s to tick %llu, %d commands)",
            path, snap_bytes ? "restored" : "replayed",
            (unsigned long long)gs->sim_tick_no, gs->cmd_count);
    return 1;
}

int game_load_commands(const char *path, Command **out_cmds, int *out_count)
{
    FILE       *f = fopen(path, "rb");
    SaveHeader  hdr;
    Command    *cmds;
    long        size_l;

    *out_cmds  = NULL;
    *out_count = 0;

    if (!f) return 0;

    if (fseek(f, 0, SEEK_END) != 0 || (size_l = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    if ((size_t)size_l < sizeof(hdr)) { fclose(f); return 0; }

    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return 0; }
    if (hdr.magic != SAVE_MAGIC || hdr.version != SAVE_VERSION ||
        hdr.cmd_count < 0) { fclose(f); return 0; }

    /* A checkpoint is deliberately refused rather than skipped past. */
    if (hdr.flags & SAVE_FLAG_SNAPSHOT) {
        sim_log("game_load_commands: %s is a checkpoint (state, not "
                "history) — a recorded session is needed here", path);
        fclose(f);
        return 0;
    }

    if (hdr.cmd_count == 0) { fclose(f); return 1; }

    cmds = (Command *)malloc(sizeof(Command) * (size_t)hdr.cmd_count);
    if (!cmds) { fclose(f); return 0; }
    if (fread(cmds, sizeof(Command) * (size_t)hdr.cmd_count, 1, f) != 1) {
        free(cmds);
        fclose(f);
        return 0;
    }
    fclose(f);

    *out_cmds  = cmds;
    *out_count = hdr.cmd_count;
    return 1;
}

/* ---- game_install_world -----------------------------------
 * The (seed, log, tick) -> world constructor shared by game_load and
 * the net layer's join/resync path. See game.h. */
/* game_install_world's sibling for a world whose history starts at a
 * snapshot instead of a seed. Decode, install the surviving tail, and
 * run forward to `tick` — the same shape as the seed path, with the
 * snapshot playing the part tick 0 used to. */
int game_install_from_snapshot(GameState *gs, const unsigned char *snap,
                               size_t snap_len, uint64_t tick,
                               const void *cmds, int n)
{
    if (!snapshot_decode(gs, snap, snap_len)) return 0;
    if (!command_log_set_bytes(gs, cmds, n))  return 0;

    if (!game_set_history_floor(gs, snap, snap_len, gs->sim_tick_no))
        return 0;

    while (gs->sim_tick_no < tick) sim_run_one_tick(gs);
    return 1;
}

int game_install_world(GameState *gs, uint32_t seed, uint64_t tick,
                       const void *cmds, int n)
{
    game_reset_world(gs, seed);

    if (!command_log_set_bytes(gs, cmds, n))
        return 0;

    while (gs->sim_tick_no < tick)
        sim_run_one_tick(gs);

    return 1;
}

/* ---- interception (MMO_PLAN later phases) ------------------ */
/* Take a hull to a pirate lair (MARITIME_PLAN Phase 5b). */
static RejectReason sim_attack_pirate(GameState *gs, int ship_idx,
                                      int pirate_idx, uint32_t player)
{
    Ship   *sh;
    Pirate *pr;
    int     wins, r;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return REJ_UNAVAILABLE;
    if (pirate_idx < 0 || pirate_idx >= gs->pirates.count) return REJ_NO_TARGET;

    sh = &gs->ships[ship_idx];
    pr = &gs->pirates.fleet[pirate_idx];

    if (!sh->active || sh->owner != player) return REJ_NOT_OWNER;
    if (sh->at_island >= 0) return REJ_UNAVAILABLE;   /* must be at sea */
    if (!pr->active) return REJ_NO_TARGET;

    /* You have to actually be there. A fleet you can attack from your
     * own harbour is a menu item, not a place. */
    {
        const Route *route = sea_route_between(&gs->sea, sh->from_island,
                                               sh->to_island);
        SeaPos       me    = sea_route_point(&gs->sea, route,
                                 (uint32_t)(gs->sim_tick_no -
                                            sh->departure_tick));
        if (sea_distance(me, pirate_pos(&gs->pirates, &gs->sea, pirate_idx)) >
            (uint32_t)PIRATE_STRIKE_RADIUS)
            return REJ_NO_TARGET;
    }

    wins = intercept_attacker_wins(gs->world_seed, ship_idx,
                                   sh->departure_tick,
                                   0x1E00 + pirate_idx, pr->last_move_tick,
                                   ship_fighting_strength(sh), pr->guns);

    if (wins) {
        pr->hull -= sh->guns > 0 ? sh->guns : 1;
        if (pr->hull <= 0) {
            /* Cleared. Everything they were sitting on comes aboard,
             * as far as the hold allows — a merchantman that beat a
             * fleet cannot carry all of it, which is one more reason
             * the hull you brought was a decision. */
            for (r = 0; r < RES_COUNT; r++) {
                int room, take;
                if (pr->plunder[r] <= 0) continue;
                room = ship_hold_capacity(sh) - sh->cargo[r];
                if (room <= 0) continue;
                take = pr->plunder[r] < room ? pr->plunder[r] : room;
                sh->cargo[r]   += take;
                pr->plunder[r] -= take;
            }
            /* And the chart they carried, which is how hunting yields
             * geography as well as goods. */
            if (pr->chart >= 0)
                knowledge_add_charts(&gs->knowledge, player, pr->chart, 1);

            sim_log("Ship %d cleared the fleet at %s", ship_idx,
                    gs->sea.waypoint[pr->waypoint].name);
            memset(pr, 0, sizeof(*pr));
            pr->chart = -1;
        } else {
            sim_log("Ship %d drove off the fleet at %s", ship_idx,
                    gs->sea.waypoint[pr->waypoint].name);
        }
    } else {
        sh->hull -= pr->guns;
        if (sh->hull < 1) sh->hull = 1;   /* a ship is an evening */
        for (r = 0; r < RES_COUNT; r++) {
            if (sh->cargo[r] <= 0) continue;
            pr->plunder[r] += sh->cargo[r];
            sh->cargo[r]    = 0;
        }
        sim_log("Ship %d was beaten off by the fleet at %s", ship_idx,
                gs->sea.waypoint[pr->waypoint].name);
    }
    return REJ_OK;
}

static RejectReason sim_intercept(GameState *gs, int my_idx, int target_idx,
                                  uint64_t target_departure, uint32_t player)
{
    Ship *mine, *target;
    int   attacker_wins, r, defence = 0, escorts = 0;
    Ship *winner, *loser;

    if (my_idx < 0 || my_idx >= gs->ship_count)         return REJ_UNAVAILABLE;
    if (target_idx < 0 || target_idx >= gs->ship_count) return REJ_NO_TARGET;
    if (my_idx == target_idx)                           return REJ_NO_TARGET;

    mine   = &gs->ships[my_idx];
    target = &gs->ships[target_idx];

    if (!mine->active || mine->at_island >= 0)     return REJ_UNAVAILABLE;
    if (!target->active || target->at_island >= 0) return REJ_NO_TARGET;

    /* Your own convoy is not a target. */
    if (target->owner == player) return REJ_NO_TARGET;

    /* The reference is bound to a voyage, not a ship: if the target
     * has sailed again since the attacker committed, this command names
     * something that no longer exists. */
    if (target->departure_tick != target_departure) return REJ_NO_TARGET;

    /* The defence is the target's guns plus every escort sailing. */
    {
        int e;
        defence = ship_fighting_strength(target);
        for (e = 0; e < gs->ship_count; e++) {
            const Ship *esc = &gs->ships[e];
            if (!esc->active || e == target_idx) continue;
            if (esc->escorting != target_idx) continue;
            if (esc->owner != target->owner) continue;
            if (esc->at_island >= 0) continue;             /* still in port */
            if (esc->departure_tick != target->departure_tick) continue;
            if (esc->to_island != target->to_island) continue;
            defence += ship_fighting_strength(esc);
            escorts++;
        }
    }

    attacker_wins = intercept_attacker_wins(gs->world_seed, my_idx,
                                            mine->departure_tick,
                                            target_idx,
                                            target->departure_tick,
                                            ship_fighting_strength(mine),
                                            defence);
    winner = attacker_wins ? mine   : target;
    loser  = attacker_wins ? target : mine;

    for (r = 0; r < RES_COUNT; r++) {
        int room, take;
        if (loser->cargo[r] <= 0) continue;
        room = ship_hold_capacity(winner) - winner->cargo[r];
        if (room <= 0) continue;
        take = loser->cargo[r] < room ? loser->cargo[r] : room;
        loser->cargo[r]  -= take;
        winner->cargo[r] += take;
    }

    /* A fight costs the loser more than its cargo: guns wear a hull
     * down, and a worn hull fights worse (see intercept_strength). */
    loser->hull -= winner->guns > 0 ? winner->guns : 1;
    if (loser->hull < 1) loser->hull = 1;

    sim_log("Ship %d intercepted ship %d at sea (%d guns against %d, %d "
            "escort%s) — %s prevailed",
            my_idx, target_idx, mine->guns, defence, escorts,
            escorts == 1 ? "" : "s",
            attacker_wins ? "the attacker" : "the defender");
    return REJ_OK;
}

int game_intercept(GameState *gs, int my_ship, int target_ship,
                   uint64_t target_departure)
{
    Command c = {0};
    c.kind = CMD_INTERCEPT;
    c.a    = my_ship;
    c.b    = target_ship;
    c.c    = (int32_t)target_departure;
    return command_submit(gs, &c);
}

/* ---- charters (MMO_PLAN later phases) ---------------------- */
static void sim_charter_tick(GameState *gs, int island)
{
    Island *isl = &gs->islands[island];

    if (!isl->settled || isl->owner == PLAYER_NONE) return;

    /* The market does not rent its own harbours from itself. Left
     * unguarded a home port would accrue arrears against a stockpile
     * that is never where its gold is, lapse, and relist the market's
     * own port as colonisable ground. */
    if (isl->owner == PLAYER_FACTION) return;

    if (++isl->charter_timer < CHARTER_UPKEEP_TICKS) return;
    isl->charter_timer = 0;

    if (isl->stockpile.amount[RES_GOLD] >= CHARTER_UPKEEP_GOLD) {
        stockpile_add(&isl->stockpile, RES_GOLD, -CHARTER_UPKEEP_GOLD);
        gs->faction.gold += CHARTER_UPKEEP_GOLD;
        isl->charter_arrears = 0;
        return;
    }

    isl->charter_arrears++;
    sim_log("[%s] charter payment missed (%d of %d)", isl->name,
            isl->charter_arrears, CHARTER_GRACE_PAYMENTS);

    if (isl->charter_arrears < CHARTER_GRACE_PAYMENTS) return;

    /* Lapsed. The buildings stay standing — an abandoned colony is a
     * better prize than bare ground, and a ruin with a road network
     * tells a story the map otherwise cannot. */
    sim_log("[%s] CHARTER LAPSED — the island is relisted", isl->name);
    isl->settled         = 0;
    isl->owner           = PLAYER_NONE;
    isl->charter_arrears = 0;
    isl->charter_timer   = 0;
}

/* ---- migration between islands (LIFE_PLAN Phase 7) -------- */
static int game_migrate_to(GameState *gs, Island *from, int idx,
                           uint32_t owner, int want_owner)
{
    int i, b;

    for (i = 0; i < MAX_ISLANDS; i++) {
        Island *dst = &gs->islands[i];

        if (dst == from || !dst->settled)                       continue;
        if (want_owner  && dst->owner != owner)                 continue;
        if (!want_owner && (dst->owner == owner || !dst->owner)) continue;

        for (b = 0; b < dst->building_count; b++) {
            int slot;

            if (!dst->pop_data[b].active)                        continue;
            if (dst->pop_data[b].residents >= HOUSE_CAPACITY)    continue;
            if (dst->resident_count >= MAX_RESIDENTS)            continue;

            /* Copy the person across, then vacate the old slot. Their
             * marriage does not survive the crossing — the spouse is an
             * index into the ISLAND they left, and a stale one would
             * marry them to a stranger on arrival. */
            slot = dst->resident_count++;
            dst->residents[slot]             = from->residents[idx];
            dst->residents[slot].home_idx    = b;
            dst->residents[slot].spouse      = -1;
            dst->residents[slot].birth_house = -1;   /* no kin here */
            dst->pop_data[b].residents++;
            dst->pop_data[b].founded = 1;

            sim_log("%s: somebody arrives from %s", dst->name, from->name);
            return 1;
        }
    }
    return 0;
}

static int game_migrate_resident(void *ctx, int resident_idx)
{
    GameState *gs   = (GameState *)ctx;
    Island    *from = &gs->islands[gs->migrate_from];
    uint32_t   owner = from->owner;

    if (game_migrate_to(gs, from, resident_idx, owner, 1)) return 1;
    return game_migrate_to(gs, from, resident_idx, owner, 0);
}

/* ---- sim_run_one_tick -----------------------------------
 * The heartbeat. See the header-comment contract in game.h. Command
 * application happens first and before any island updates, so a command
 * submitted for tick N is visible to tick N's simulation. */
/* Defined below with the other sim_* rules, beside the mutators it
 * shares stockpile access with; declared here because the tick loop
 * runs it. */
static void book_match(GameState *gs);

void sim_run_one_tick(GameState *gs)
{
    int i;

    /* 1. Apply every command whose tick has now arrived, in log order.
     * command_submit stamps with the then-current sim_tick_no, so the
     * pending tail is exactly the commands for this tick (<= guards
     * against any straggler rather than deadlocking the cursor). */
    while (gs->cmd_applied < gs->cmd_count &&
           gs->cmd_log[gs->cmd_applied].tick <= gs->sim_tick_no) {
        const Command *c      = &gs->cmd_log[gs->cmd_applied];
        RejectReason   reason = sim_apply_reason(gs, c);

        /* Leave a note of what happened for the UI to collect (UI_PLAN
         * M1). This is client-facing bookkeeping, not world state: it is
         * not hashed, not saved, and a replay that never drains the ring
         * simply overwrites it. */
        if (gs->result_count < SIM_RESULT_RING) {
            SimResult *r = &gs->results[gs->result_count++];
            r->seq       = c->seq;
            r->player_id = c->player_id;
            r->tick      = gs->sim_tick_no;
            r->kind      = c->kind;
            r->reason    = reason;
        }
        gs->cmd_applied++;
    }

    /* A predicting client stops here with the rest of the world */
    if (gs->predict_only != 0u) {
        for (i = 0; i < MAX_ISLANDS; i++)
            if (gs->islands[i].owner == gs->predict_only)
                island_update(&gs->islands[i], gs->world_seed, gs->sim_tick_no,
                              &gs->faction);
        gs->sim_tick_no++;
        return;
    }

    /* 2. The order book: deliver what has arrived, then match what */
    book_match(gs);

    /* 3. Every settled island's full pipeline, one tick, in order — */
    for (i = 0; i < MAX_ISLANDS; i++) {
        gs->migrate_from        = i;
        gs->islands[i].emigrate     = game_migrate_resident;
        gs->islands[i].emigrate_ctx = gs;
        island_update(&gs->islands[i], gs->world_seed, gs->sim_tick_no,
                              &gs->faction);
    }

    /* 4. Voyages advance independently of any island. Insurance is
     * settled either side of the move: what was at sea before, and
     * what has arrived after. */
    {
        int s2;
        for (s2 = 0; s2 < gs->ship_count; s2++)
            gs->ships[s2].was_at_sea = (gs->ships[s2].at_island < 0);
    }
    ships_update(&gs->sea, gs->ships, gs->ship_count, gs->islands, MAX_ISLANDS,
                 gs->sim_tick_no, gs->world_seed);

    /* Anything that just made port with a policy on it gets settled,
     * and the lane's premium learns from the outcome either way. */
    for (i = 0; i < gs->ship_count; i++) {
        Ship *sh = &gs->ships[i];
        int   raided;

        if (!sh->active || !sh->insured) continue;
        if (!sh->was_at_sea || sh->at_island < 0) continue;   /* still out */

        raided = voyage_is_raided(gs->world_seed, i, sh->departure_tick,
                                  sh->from_island, sh->to_island);
        if (raided) {
            int payout = sh->insured_value / 2;   /* pirates took half */
            if (payout > gs->faction.gold) payout = gs->faction.gold;
            if (payout > 0) {
                gs->faction.gold -= payout;
                stockpile_add(&gs->islands[sh->at_island].stockpile,
                              RES_GOLD, payout);
                sim_log("Insurance paid %d Gold on ship %d's raided voyage",
                        payout, i);
            }
        }
        /* A player ship sails the public lane: it carries cargo, not a
         * chart. Its experience therefore prices the lane, which is
         * the water it was actually on. */
        faction_route_experience(&gs->faction,
            sea_route_id(&gs->sea,
                sea_route_between(&gs->sea, sh->from_island, sh->to_island)),
            raided);
        sh->insured       = 0;
        sh->insured_value = 0;
    }

    /* Ships in port refit, where there is a yard to do it
     * (MARITIME_PLAN Phase 5). Damage from an interception is not
     * permanent — it is a reason to go home. */
    for (i = 0; i < gs->ship_count; i++) {
        Ship   *sh = &gs->ships[i];
        Island *isl;
        int     b, has_yard = 0, full;

        if (!sh->active || sh->at_island < 0) continue;
        if (sh->klass < 0 || sh->klass >= SHIP_CLASS_COUNT) continue;
        full = SHIP_CLASSES[sh->klass].hull;
        if (sh->hull >= full) continue;

        isl = &gs->islands[sh->at_island];
        for (b = 0; b < isl->building_count; b++)
            if (isl->buildings[b].active &&
                isl->buildings[b].type == BUILDING_SHIPYARD) { has_yard = 1; break; }
        if (!has_yard) continue;

        /* Staggered by ship index so a fleet in one harbour does not
         * all gain a point on the same tick — cosmetic, but it also
         * keeps the work spread rather than spiking. */
        if ((gs->sim_tick_no + (uint64_t)i) % SHIP_REFIT_TICKS_PER_HULL != 0)
            continue;
        sh->hull++;
    }

    /* 4. Charters fall due (MMO_PLAN later phases). Before the market
     * tick so an island that just paid its upkeep is priced against the
     * same faction gold every other trade this tick saw. */
    for (i = 0; i < MAX_ISLANDS; i++)
        sim_charter_tick(gs, i);

    /* 5. The market drifts back toward baseline (price recovery). */
    faction_tick(&gs->faction);

    /* 6. Advance the world clock. */
    gs->sim_tick_no++;
}

/* ---- sim_hash -------------------------------------------
 * FNV-1a over exactly the state that defines the world (see game.h). */
static void fnv_bytes(uint64_t *h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < n; i++) {
        *h ^= p[i];
        *h *= 1099511628211ULL;   /* FNV-1a 64-bit prime */
    }
}

int sim_results_drain(GameState *gs, SimResult *out, int max)
{
    int n = gs->result_count < max ? gs->result_count : max;
    int i;

    for (i = 0; i < n; i++) out[i] = gs->results[i];

    /* Drained means gone. Anything beyond `max` is dropped rather than
     * kept: a caller that cannot keep up with its own commands has a
     * bigger problem than the ones it missed. */
    gs->result_count = 0;
    return n;
}

uint64_t sim_hash(const GameState *gs)
{
    uint64_t h = 14695981039346656037ULL;   /* FNV-1a 64-bit offset */
    int      i, b, s;

    fnv_bytes(&h, &gs->sim_tick_no, sizeof(gs->sim_tick_no));

    for (i = 0; i < MAX_ISLANDS; i++) {
        const Island *isl = &gs->islands[i];

        fnv_bytes(&h, &isl->settled, sizeof(isl->settled));
        fnv_bytes(&h, &isl->charter_timer, sizeof(isl->charter_timer));
        fnv_bytes(&h, &isl->charter_arrears, sizeof(isl->charter_arrears));
        fnv_bytes(&h, isl->stockpile.amount, sizeof(isl->stockpile.amount));
        fnv_bytes(&h, &isl->stockpile.capacity, sizeof(isl->stockpile.capacity));
        /* Phase 5: ownership and the harbor airlock are world state. */
        fnv_bytes(&h, &isl->owner, sizeof(isl->owner));
        fnv_bytes(&h, &isl->docking_allowed, sizeof(isl->docking_allowed));
        fnv_bytes(&h, isl->escrow, sizeof(isl->escrow));
        /* Trade capacity committed (MARITIME Phase 2). Only the "out"
         * counts: the capacity itself is derived from the buildings,
         * which are hashed just below. */
        fnv_bytes(&h, &isl->merchants_out, sizeof(isl->merchants_out));
        fnv_bytes(&h, &isl->hulls_out, sizeof(isl->hulls_out));
        fnv_bytes(&h, &isl->insure_shipments, sizeof(isl->insure_shipments));
        fnv_bytes(&h, &isl->research_boats, sizeof(isl->research_boats));
        fnv_bytes(&h, &isl->research_boats_out, sizeof(isl->research_boats_out));
        fnv_bytes(&h, &isl->scholars_out, sizeof(isl->scholars_out));

        /* Residents (LIFE_PLAN Phase 3). Hashed even though nothing */
        /* The treasury and the allowance (LIFE_PLAN Phase 7). Every one */
        fnv_bytes(&h, &isl->founder_allowance, sizeof(isl->founder_allowance));
        fnv_bytes(&h, &isl->tax_rate_permille, sizeof(isl->tax_rate_permille));
        fnv_bytes(&h, &isl->compliance_permille,
                  sizeof(isl->compliance_permille));
        fnv_bytes(&h, &isl->unhappy_streak, sizeof(isl->unhappy_streak));
        fnv_bytes(&h, &isl->tax_base, sizeof(isl->tax_base));

        fnv_bytes(&h, &isl->next_resident_id, sizeof(isl->next_resident_id));
        fnv_bytes(&h, &isl->resident_count, sizeof(isl->resident_count));
        for (b = 0; b < isl->resident_count; b++) {
            const Resident *p = &isl->residents[b];
            fnv_bytes(&h, &p->active, sizeof(p->active));
            if (!p->active) continue;
            fnv_bytes(&h, &p->home_idx, sizeof(p->home_idx));
            fnv_bytes(&h, &p->id, sizeof(p->id));
            fnv_bytes(&h, &p->age_months, sizeof(p->age_months));
            fnv_bytes(&h, &p->spouse, sizeof(p->spouse));
            fnv_bytes(&h, &p->tenure_months, sizeof(p->tenure_months));
            fnv_bytes(&h, &p->sex, sizeof(p->sex));
            fnv_bytes(&h, &p->pregnancy, sizeof(p->pregnancy));
            fnv_bytes(&h, &p->birth_house, sizeof(p->birth_house));
            fnv_bytes(&h, &p->children, sizeof(p->children));
            fnv_bytes(&h, &p->birth_cooldown, sizeof(p->birth_cooldown));
            fnv_bytes(&h, &p->reserve_since, sizeof(p->reserve_since));
        }

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
                fnv_bytes(&h, &p->happiness, sizeof(p->happiness));
                fnv_bytes(&h, &p->origin_tier, sizeof(p->origin_tier));
                fnv_bytes(&h, &p->timer, sizeof(p->timer));
            }
        }
    }

    /* The order book is world state (MARITIME_PLAN Phase 2): two
     * clients whose books disagreed would fill different trades and
     * diverge from there, so it is hashed like everything else. */
    {
        int live = orderbook_open_live(&gs->book);
        fnv_bytes(&h, &live, sizeof(live));
    }
    for (s = 0; s < gs->book.order_count; s++) {
        const Order *o = &gs->book.order[s];
        if (!o->active) continue;
        fnv_bytes(&h, &o->id, sizeof(o->id));
        fnv_bytes(&h, &o->owner, sizeof(o->owner));
        fnv_bytes(&h, &o->island, sizeof(o->island));
        fnv_bytes(&h, &o->what, sizeof(o->what));
        fnv_bytes(&h, &o->side, sizeof(o->side));
        fnv_bytes(&h, &o->qty, sizeof(o->qty));
        fnv_bytes(&h, &o->limit, sizeof(o->limit));
        fnv_bytes(&h, &o->reserved_gold, sizeof(o->reserved_gold));
        fnv_bytes(&h, &o->placed_tick, sizeof(o->placed_tick));
    }
    {
        int live = orderbook_booking_live(&gs->book);
        fnv_bytes(&h, &live, sizeof(live));
    }
    for (s = 0; s < gs->book.booking_count; s++) {
        const Booking *bk = &gs->book.booking[s];
        if (!bk->active) continue;
        fnv_bytes(&h, &bk->what, sizeof(bk->what));
        fnv_bytes(&h, &bk->qty, sizeof(bk->qty));
        fnv_bytes(&h, &bk->price, sizeof(bk->price));
        fnv_bytes(&h, &bk->from_island, sizeof(bk->from_island));
        fnv_bytes(&h, &bk->to_island, sizeof(bk->to_island));
        fnv_bytes(&h, &bk->buyer, sizeof(bk->buyer));
        fnv_bytes(&h, &bk->seller, sizeof(bk->seller));
        fnv_bytes(&h, &bk->arrive_tick, sizeof(bk->arrive_tick));
        fnv_bytes(&h, &bk->return_tick, sizeof(bk->return_tick));
        fnv_bytes(&h, &bk->delivered, sizeof(bk->delivered));
        fnv_bytes(&h, &bk->route_id, sizeof(bk->route_id));
        fnv_bytes(&h, &bk->raided, sizeof(bk->raided));
        fnv_bytes(&h, &bk->insured_value, sizeof(bk->insured_value));
    }

    for (s = 0; s < gs->ship_count; s++) {
        const Ship *sh = &gs->ships[s];
        fnv_bytes(&h, &sh->active, sizeof(sh->active));
        if (!sh->active) continue;
        fnv_bytes(&h, &sh->owner, sizeof(sh->owner));   /* Phase 5 */
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
        fnv_bytes(&h, &sh->insured, sizeof(sh->insured));
        fnv_bytes(&h, &sh->insured_value, sizeof(sh->insured_value));
        /* What kind of hull, and who it is guarding (Phase 5). */
        fnv_bytes(&h, &sh->klass, sizeof(sh->klass));
        fnv_bytes(&h, &sh->guns, sizeof(sh->guns));
        fnv_bytes(&h, &sh->hull, sizeof(sh->hull));
        fnv_bytes(&h, &sh->escorting, sizeof(sh->escorting));
    }

    /* The fleets: where they are, what they hold, what it cost them
     * (MARITIME Phase 5b). */
    for (s = 0; s < gs->pirates.count; s++) {
        const Pirate *pr = &gs->pirates.fleet[s];
        fnv_bytes(&h, &pr->active, sizeof(pr->active));
        if (!pr->active) continue;
        fnv_bytes(&h, &pr->waypoint, sizeof(pr->waypoint));
        fnv_bytes(&h, &pr->guns, sizeof(pr->guns));
        fnv_bytes(&h, &pr->hull, sizeof(pr->hull));
        fnv_bytes(&h, pr->plunder, sizeof(pr->plunder));
        fnv_bytes(&h, &pr->chart, sizeof(pr->chart));
        fnv_bytes(&h, &pr->last_move_tick, sizeof(pr->last_move_tick));
    }

    /* Which passages are currently in play (MARITIME Phase 3e). The
     * only part of a Sea that is world state — everything else about
     * it regenerates from the seed. */
    fnv_bytes(&h, gs->sea.pair_cursor, sizeof(gs->sea.pair_cursor));

    /* Expeditions in progress (MARITIME Phase 3d). Live entries only,
     * and no slot layout — same rule as the order book: a mission is
     * not addressed by index, so a compacted checkpoint must hash the
     * same as the world it came from. */
    {
        int live = 0;
        for (s = 0; s < gs->surveys.count; s++)
            if (gs->surveys.mission[s].active) live++;
        fnv_bytes(&h, &live, sizeof(live));
    }
    for (s = 0; s < gs->surveys.count; s++) {
        const Survey *m = &gs->surveys.mission[s];
        if (!m->active) continue;
        fnv_bytes(&h, &m->owner, sizeof(m->owner));
        fnv_bytes(&h, &m->from_island, sizeof(m->from_island));
        fnv_bytes(&h, &m->to_island, sizeof(m->to_island));
        fnv_bytes(&h, &m->route_id, sizeof(m->route_id));
        fnv_bytes(&h, &m->finish_tick, sizeof(m->finish_tick));
        fnv_bytes(&h, &m->succeeds, sizeof(m->succeeds));
        fnv_bytes(&h, &m->lost, sizeof(m->lost));
    }

    /* What each player knows of the sea (MARITIME Phase 3b). Hashed
     * because route choice depends on it: two clients disagreeing
     * about a seller's charts would disagree about when the cargo
     * lands, which is a desync that looks like a gameplay bug. */
    fnv_bytes(&h, &gs->knowledge, sizeof(gs->knowledge));

    /* The market is world state too (Phase 3). */
    fnv_bytes(&h, &gs->faction.gold, sizeof(gs->faction.gold));
    fnv_bytes(&h, gs->faction.inventory, sizeof(gs->faction.inventory));
    fnv_bytes(&h, &gs->faction.revert_timer, sizeof(gs->faction.revert_timer));
    fnv_bytes(&h, gs->faction.route_premium, sizeof(gs->faction.route_premium));

    /* The price history too (UI_PLAN M3). It is state the sim produces
     * and the UI renders, so a replay that reproduced everything except
     * the chart would be a replay with a hole in it. */
    /* The standing quotes are state too (MARITIME Phase 2): they name
     * live orders, and a client that forgot which ones were its own
     * would leave them in the book forever on the next refresh. */
    fnv_bytes(&h, gs->faction.quote_order, sizeof(gs->faction.quote_order));
    fnv_bytes(&h, &gs->faction.quote_timer, sizeof(gs->faction.quote_timer));
    fnv_bytes(&h, gs->faction.chart_order, sizeof(gs->faction.chart_order));
    fnv_bytes(&h, &gs->faction.chart_cursor, sizeof(gs->faction.chart_cursor));

    fnv_bytes(&h, gs->faction.hist, sizeof(gs->faction.hist));
    fnv_bytes(&h, &gs->faction.hist_head, sizeof(gs->faction.hist_head));
    fnv_bytes(&h, &gs->faction.hist_count, sizeof(gs->faction.hist_count));

    return h;
}

/* ---- game_verify_determinism ---------------------------- */
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
        sim_log("game_verify_determinism: out of memory for scratch world");
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
 * Shared by game_try_place_road() and game_place_building_confirmed(): */
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

    /* If a house was just placed, activate its PopData and try to put. */
    if (pop_is_house_type(type)) {
        pop_init(&isl->pop_data[idx]);
        island_settle_house(isl, idx, gs->world_seed);
    }

    /* Warehouses raise how much of each non-gold resource THIS
     * island's stockpile can hold; recompute so a newly built
     * Warehouse takes effect immediately. */
    if (type == BUILDING_WAREHOUSE)
        island_recompute_storage_capacity(isl);

    return idx;
}

/* ---- game_try_place_road ---------------------------------- */
static RejectReason sim_place_road(GameState *gs, int island,
                                   int row, int col)
{
    Island            *isl = &gs->islands[island];
    const BuildingDef *def = &BUILDING_DEFS[BUILDING_ROAD];
    RejectReason       why;

    if (!isl->settled) return REJ_NOT_OWNER;

    why = building_place_check(&isl->map, BUILDING_ROAD, row, col);
    if (why != REJ_OK) return why;

    if (!building_can_afford(&isl->stockpile, BUILDING_ROAD))
        return REJ_CANT_AFFORD;

    if (commit_placement(gs, island, BUILDING_ROAD, row, col) < 0)
        return REJ_OCCUPIED;

    stockpile_add(&isl->stockpile, RES_GOLD, -def->cost[RES_GOLD]);
    return REJ_OK;
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

/* ---- sim_place_building / game_place_building_confirmed ---- */
static RejectReason sim_place_building(GameState *gs, int island,
                                       int row, int col,
                                       BuildingType type, int pay_with_gold)
{
    Island            *isl = &gs->islands[island];
    const BuildingDef *def;
    RejectReason       why;
    int                i;

    if (type <= BUILDING_NONE || type >= BUILDING_TYPE_COUNT)
        return REJ_UNAVAILABLE;
    if (!isl->settled) return REJ_NOT_OWNER;

    /* The same check the hover ghost and the HUD tooltip call, giving
     * the same answer — one validator, not a prediction beside an
     * authority (UI_PLAN's dual-validation risk). */
    why = building_place_check(&isl->map, type, row, col);
    if (why != REJ_OK) return why;

    def = &BUILDING_DEFS[type];

    if (pay_with_gold) {
        int gold_cost = building_gold_equivalent_cost(type, &gs->faction);
        if (isl->stockpile.amount[RES_GOLD] < gold_cost)
            return REJ_CANT_AFFORD;

        if (commit_placement(gs, island, type, row, col) < 0)
            return REJ_OCCUPIED;

        stockpile_add(&isl->stockpile, RES_GOLD, -gold_cost);
    } else {
        if (!building_can_afford(&isl->stockpile, type))
            return REJ_CANT_AFFORD;

        if (commit_placement(gs, island, type, row, col) < 0)
            return REJ_OCCUPIED;

        for (i = 0; i < RES_COUNT; i++)
            if (def->cost[i] > 0)
                stockpile_add(&isl->stockpile, (ResourceType)i, -def->cost[i]);
    }
    return REJ_OK;
}

/* ---- command builders --------------------------------------
 * One place per kind where a payload is encoded. The confirm layer
 * stores what these produce and submits exactly that, so the popup's
 * preview and sim_apply's input cannot be different things. */
static Command cmd_place_building(int island, int row, int col,
                                  BuildingType type, int pay_with_gold)
{
    Command c;
    memset(&c, 0, sizeof(c));
    c.kind = CMD_PLACE_BUILDING;
    c.a    = island;
    c.b    = row;
    c.c    = col;
    /* Pack type and the payment bit into one slot — see command.h. */
    c.d    = (int32_t)((int)type * 2 + (pay_with_gold ? 1 : 0));
    return c;
}

static Command cmd_one_building(CommandKind kind, int island, int idx)
{
    Command c;
    memset(&c, 0, sizeof(c));
    c.kind = kind;
    c.a    = island;
    c.b    = idx;
    return c;
}

int game_place_building(GameState *gs, int row, int col,
                        BuildingType type, int pay_with_gold)
{
    Command c;

    if (type == BUILDING_NONE || row < 0 || col < 0) return 0;
    c = cmd_place_building(gs->current_island, row, col, type, pay_with_gold);
    return command_submit(gs, &c);
}

/* ---- the confirmation layer (UI_PLAN Phase 6) -------------- */

static void confirm_set(GameState *gs, ConfirmKind kind,
                        Command primary, Command alternative, int has_alt)
{
    gs->confirm.open   = 1;
    gs->confirm.kind   = kind;
    gs->confirm.cmd    = primary;
    gs->confirm.alt    = alternative;
    gs->confirm.chosen = 0;
    if (!has_alt) gs->confirm.alt.kind = CMD_COUNT;
}

void game_confirm_build(GameState *gs, int row, int col, BuildingType type)
{
    Command pay_goods, pay_gold;

    if (type == BUILDING_NONE || type >= BUILDING_TYPE_COUNT) return;
    if (row < 0 || col < 0) return;

    pay_goods = cmd_place_building(gs->current_island, row, col, type, 0);
    pay_gold  = cmd_place_building(gs->current_island, row, col, type, 1);
    confirm_set(gs, CONFIRM_BUILD, pay_goods, pay_gold, 1);
}

void game_confirm_demolish(GameState *gs, int building_idx)
{
    const Island *isl = &gs->islands[gs->current_island];
    Command       none;

    if (building_idx < 0 || building_idx >= isl->building_count) return;
    if (!isl->buildings[building_idx].active) return;

    memset(&none, 0, sizeof(none));
    confirm_set(gs, CONFIRM_DEMOLISH,
                cmd_one_building(CMD_DEMOLISH, gs->current_island,
                                 building_idx), none, 0);
}

void game_confirm_upgrade(GameState *gs, int building_idx)
{
    const Island *isl = &gs->islands[gs->current_island];
    Command       none, first, second;
    BuildingType  type;
    int           branch[2], n;

    if (building_idx < 0 || building_idx >= isl->building_count) return;
    if (!isl->buildings[building_idx].active) return;

    /* Any house type, not just a Marsh Cottage. This. */
    type = isl->buildings[building_idx].type;
    if (!pop_is_house_type(type)) return;

    /* One Command per available branch, in the same order the popup
     * lists them (tier_branches), so button 0 submits branch 0. */
    n = tier_branches(type, branch);
    if (n == 0) return;              /* nowhere to go: no popup at all */

    memset(&none, 0, sizeof(none));
    first = cmd_one_building(CMD_UPGRADE_HOUSE, gs->current_island,
                             building_idx);
    first.c = branch[0];
    if (n > 1) {
        second   = first;
        second.c = branch[1];
    }
    confirm_set(gs, CONFIRM_UPGRADE, first, n > 1 ? second : none, n > 1);
}

void game_confirm_ship_class(GameState *gs, int klass)
{
    Command c, none;

    memset(&c, 0, sizeof(c));
    memset(&none, 0, sizeof(none));
    c.kind = CMD_BUILD_SHIP;
    c.a    = gs->current_island;
    c.b    = klass;   /* which hull — the yard offers three (N6) */
    confirm_set(gs, CONFIRM_SHIP, c, none, 0);
}

void game_confirm_ship(GameState *gs)
{
    game_confirm_ship_class(gs, SHIP_MERCHANTMAN);
}

void game_confirm_choose(GameState *gs, int which)
{
    if (!gs->confirm.open) return;
    if (which == 1 && gs->confirm.alt.kind == CMD_COUNT) return;
    gs->confirm.chosen = which ? 1 : 0;
}

int game_confirm_accept(GameState *gs)
{
    Command c;

    if (!gs->confirm.open) return 0;

    c = gs->confirm.chosen ? gs->confirm.alt : gs->confirm.cmd;
    gs->confirm.open = 0;
    gs->confirm.kind = CONFIRM_NONE;

    /* Submitted verbatim: the popup showed this struct, and this struct
     * is what sim_apply gets. */
    return command_submit(gs, &c);
}

void game_confirm_cancel(GameState *gs)
{
    gs->confirm.open = 0;
    gs->confirm.kind = CONFIRM_NONE;
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

/* ---- sim_sell / game_sell_resource ------------------------- */
static RejectReason sim_sell(GameState *gs, int island, ResourceType res,
                             int qty, int limit)
{
    Island  *isl = &gs->islands[island];
    Faction *fac = &gs->faction;
    int      price, revenue, wanted = qty;

    if (res < 0 || res >= RES_COUNT || res == RES_GOLD) return REJ_UNAVAILABLE;

    price = faction_bid(fac, res);

    /* The screen said `limit`; if the market has since moved against
     * the seller, refuse rather than fill at the worse price. */
    if (limit > 0 && price < limit) return REJ_PRICE_MOVED;

    if (qty > isl->stockpile.amount[res]) qty = isl->stockpile.amount[res];
    if (price > 0 && qty > fac->gold / price) qty = fac->gold / price;
    if (qty <= 0) {
        /* Which of the two clamps bit decides what the player is told:
         * an empty warehouse and a broke counterparty are different
         * problems with different answers. */
        if (isl->stockpile.amount[res] <= 0) return REJ_NO_STOCK;
        if (wanted > 0 && price > 0 && fac->gold < price)
            return REJ_COUNTERPARTY_NO_GOLD;
        return REJ_NO_STOCK;
    }

    revenue = qty * price;
    stockpile_add(&isl->stockpile, res, -qty);
    stockpile_add(&isl->stockpile, RES_GOLD, revenue);
    fac->gold          -= revenue;
    fac->inventory[res] += qty;
    return REJ_OK;
}

void game_sell_resource_limit(GameState *gs, ResourceType res, int qty,
                              int limit)
{
    Command c = {0};
    c.kind = CMD_SELL_RESOURCE;
    c.a    = gs->current_island;
    c.b    = (int32_t)res;
    c.c    = qty;
    c.d    = limit;
    command_submit(gs, &c);
}

void game_sell_resource(GameState *gs, ResourceType res, int qty)
{
    game_sell_resource_limit(gs, res, qty, 0);
}

/* ---- sim_buy / game_buy_resource ---------------------------- */
static RejectReason sim_buy(GameState *gs, int island, ResourceType res,
                            int qty, int limit)
{
    Island  *isl = &gs->islands[island];
    Faction *fac = &gs->faction;
    int      price, headroom, max_affordable, max_stock, cost;

    if (res < 0 || res >= RES_COUNT || res == RES_GOLD) return REJ_UNAVAILABLE;

    price    = faction_ask(fac, res);
    if (limit > 0 && price > limit) return REJ_PRICE_MOVED;

    headroom = isl->stockpile.capacity - isl->stockpile.amount[res];
    if (headroom < 0) headroom = 0;

    max_affordable = (price > 0) ? isl->stockpile.amount[RES_GOLD] / price : 0;
    max_stock      = fac->inventory[res];   /* can't sell what it lacks */

    if (qty < 0) {
        qty = headroom;
        if (max_affordable < qty) qty = max_affordable;
        if (max_stock      < qty) qty = max_stock;
    }
    if (qty > headroom)       qty = headroom;
    if (qty > max_affordable) qty = max_affordable;
    if (qty > max_stock)      qty = max_stock;
    if (qty <= 0) {
        /* Three ways to buy nothing, three different things to say. */
        if (headroom <= 0)       return REJ_NO_STORAGE;
        if (max_stock <= 0)      return REJ_NO_STOCK;
        return REJ_CANT_AFFORD;
    }

    cost = qty * price;
    stockpile_add(&isl->stockpile, RES_GOLD, -cost);
    stockpile_add(&isl->stockpile, res, qty);
    fac->gold          += cost;
    fac->inventory[res] -= qty;
    return REJ_OK;
}

void game_buy_resource_limit(GameState *gs, ResourceType res, int qty,
                             int limit)
{
    Command c = {0};
    c.kind = CMD_BUY_RESOURCE;
    c.a    = gs->current_island;
    c.b    = (int32_t)res;
    c.c    = qty;
    c.d    = limit;
    command_submit(gs, &c);
}

void game_buy_resource(GameState *gs, ResourceType res, int qty)
{
    game_buy_resource_limit(gs, res, qty, 0);
}

/* ---- sim_demolish / game_demolish_building ------------------- */
static RejectReason sim_demolish(GameState *gs, int island, int idx)
{
    Island      *isl = &gs->islands[island];
    BuildingType type;
    int          i;

    if (idx < 0 || idx >= isl->building_count) return REJ_UNAVAILABLE;
    if (!isl->buildings[idx].active) return REJ_UNAVAILABLE;

    type = isl->buildings[idx].type;

    isl->buildings[idx].active       = 0;
    isl->buildings[idx].connected    = 0;
    isl->buildings[idx].worker_count = 0;

    if (isl->pop_data[idx].active) {
        isl->pop_data[idx].active    = 0;
        isl->pop_data[idx].residents = 0;
    }

    /* Clean up any agents referencing this building — otherwise. */
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
    return REJ_OK;
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
/* Is an active, road-connected building of `type` standing on this */
int island_has_building(const Island *isl, BuildingType type)
{
    int i;

    if (type == BUILDING_NONE) return 1;   /* nothing required */
    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == type &&
            isl->buildings[i].connected)
            return 1;
    return 0;
}

static RejectReason sim_upgrade_house(GameState *gs, int island, int idx,
                                      int branch)
{
    Island       *isl = &gs->islands[island];
    BuildingType  from, to;
    RejectReason  why;
    const TierDef *tier;

    if (idx < 0 || idx >= isl->building_count) return REJ_UNAVAILABLE;
    if (!isl->buildings[idx].active) return REJ_UNAVAILABLE;
    if (branch != TIER_BRANCH_LINE && branch != TIER_BRANCH_ACADEMY)
        return REJ_UNAVAILABLE;

    from = isl->buildings[idx].type;
    tier = tier_def_for(from);
    if (!tier) return REJ_UNAVAILABLE;   /* not a house at all */

    why = tier_upgrade_check(from, branch, isl->stockpile.amount,
                             island_has_building(isl,
                                 tier_upgrade_requires(from, branch)),
                             &to);
    if (why != REJ_OK) return why;

    /* Both branches cost the same: upgrade_gold belongs to the tier */
    stockpile_add(&isl->stockpile, RES_GOLD, -tier->upgrade_gold);
    /* Where these people came from, recorded on every upgrade. */
    isl->pop_data[idx].origin_tier = (int)from;
    isl->buildings[idx].type = to;
    return REJ_OK;
}

void game_upgrade_house(GameState *gs, int idx, int branch)
{
    Command c = {0};
    c.kind = CMD_UPGRADE_HOUSE;
    c.a    = gs->current_island;
    c.b    = idx;
    c.c    = branch;
    command_submit(gs, &c);
}

/* ---- sim_build_ship / game_build_ship -----------------------
 * Returns the new ship's slot index, or -1 on failure. Slot choice
 * (reuse-first-inactive, else append) is a deterministic function of
 * the ship array, so a replayed log lands the ship in the same slot. */
static int sim_build_ship(GameState *gs, int island, int klass,
                          uint32_t player)
{
    Island *isl = &gs->islands[island];
    int     i, slot = -1, cost;

    if (!isl->settled) return -1;
    if (klass < 0 || klass >= SHIP_CLASS_COUNT) return -1;

    cost = SHIP_CLASSES[klass].gold;
    if (isl->stockpile.amount[RES_GOLD] < cost) return -1;

    for (i = 0; i < gs->ship_count; i++)
        if (!gs->ships[i].active) { slot = i; break; }
    if (slot < 0) {
        if (gs->ship_count >= MAX_SHIPS) return -1;
        slot = gs->ship_count++;
    }

    memset(&gs->ships[slot], 0, sizeof(Ship));
    gs->ships[slot].active      = 1;
    gs->ships[slot].owner       = player;   /* commanded by its builder */
    gs->ships[slot].at_island   = island;
    gs->ships[slot].from_island = island;
    gs->ships[slot].to_island   = island;
    gs->ships[slot].klass       = klass;
    gs->ships[slot].guns        = SHIP_CLASSES[klass].guns;
    gs->ships[slot].hull        = SHIP_CLASSES[klass].hull;
    gs->ships[slot].escorting   = -1;

    stockpile_add(&isl->stockpile, RES_GOLD, -cost);

    sim_log("%s %d launched at %s", SHIP_CLASSES[klass].name, slot,
            isl->name);
    return slot;
}

int game_build_ship_class(GameState *gs, int klass)
{
    Command c = {0};
    c.kind = CMD_BUILD_SHIP;
    c.a    = gs->current_island;
    c.b    = klass;   /* which hull; slot b used to be an unused index */
    return command_submit(gs, &c);
}

int game_build_ship(GameState *gs)
{
    return game_build_ship_class(gs, SHIP_MERCHANTMAN);
}

int game_attack_pirate(GameState *gs, int ship_idx, int pirate_idx)
{
    Command c = {0};
    c.kind = CMD_ATTACK_PIRATE;
    c.a    = ship_idx;
    c.b    = pirate_idx;
    return command_submit(gs, &c);
}

int game_set_escort(GameState *gs, int ship_idx, int target_idx)
{
    Command c = {0};
    c.kind = CMD_SET_ESCORT;
    c.a    = ship_idx;
    c.b    = target_idx;
    return command_submit(gs, &c);
}

/* ---- sim_ship_transfer / game_ship_transfer ----------------- */
static int island_has_active_harbor(const Island *isl)
{
    int i;
    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_HARBOR)
            return 1;
    return 0;
}

static RejectReason sim_ship_transfer(GameState *gs, int ship_idx,
                                      ResourceType res, int qty, int island,
                                      uint32_t player)
{
    Ship   *sh;
    Island *isl;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return REJ_UNAVAILABLE;
    if (res < 0 || res >= RES_COUNT) return REJ_UNAVAILABLE;
    sh = &gs->ships[ship_idx];
    if (!sh->active) return REJ_UNAVAILABLE;
    if (sh->at_island != island) return REJ_UNAVAILABLE;
    isl = &gs->islands[island];

    if (isl->owner == player)
        return ship_transfer_at(sh, isl, res, qty) != 0 ? REJ_OK
                                                        : REJ_NO_STOCK;

    /* Foreign dock: escrow only, and only with permission + a harbor.
     * These two are the ones a player most needs explained (UI_PLAN M5)
     * — a blockade and a missing harbour look identical from a ship's
     * deck, and both used to be a button that did nothing. */
    if (!isl->docking_allowed)          return REJ_ESCROW_REFUSED;
    if (!island_has_active_harbor(isl)) return REJ_ESCROW_REFUSED;
    return ship_transfer_escrow(sh, isl, res, qty) != 0 ? REJ_OK
                                                        : REJ_NO_STOCK;
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
static RejectReason sim_ship_depart(GameState *gs, int ship_idx, int dest,
                                    int insure)
{
    Ship *sh;
    int   premium = 0;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return REJ_UNAVAILABLE;
    if (dest < 0 || dest >= MAX_ISLANDS) return REJ_UNAVAILABLE;
    sh = &gs->ships[ship_idx];
    if (!sh->active) return REJ_UNAVAILABLE;
    if (sh->at_island < 0) return REJ_UNAVAILABLE;   /* already at sea */
    if (sh->at_island == dest) return REJ_UNAVAILABLE;

    /* The premium is charged before the ship leaves, from the island it
     * is leaving — an underwriter is paid up front or not at all. */
    if (insure) {
        Island *home = &gs->islands[sh->at_island];
        premium = game_insurance_quote(gs, ship_idx, dest);
        if (premium > 0) {
            if (home->stockpile.amount[RES_GOLD] < premium)
                return REJ_CANT_AFFORD;
            stockpile_add(&home->stockpile, RES_GOLD, -premium);
            gs->faction.gold += premium;
        }
    }

    /* The declared value is fixed here, at departure. Settling a raid
     * against the hold as it arrives would pay out on what the pirates
     * left rather than what they took. */
    sh->insured       = insure ? 1 : 0;
    sh->insured_value = 0;
    if (insure) {
        int r;
        for (r = 0; r < RES_COUNT; r++) {
            if (sh->cargo[r] <= 0) continue;
            sh->insured_value += (r == (int)RES_GOLD)
                ? sh->cargo[r]
                : sh->cargo[r] * faction_bid(&gs->faction, (ResourceType)r);
        }
    }

    sh->from_island    = sh->at_island;
    sh->to_island      = dest;
    sh->at_island      = -1;                 /* now at sea           */
    sh->departure_tick = gs->sim_tick_no;    /* fixes the whole voyage */
    sh->progress       = 0.0f;

    /* The convoy sails together (MARITIME_PLAN Phase 5). An escort */
    {
        int e;
        for (e = 0; e < gs->ship_count; e++) {
            Ship *esc = &gs->ships[e];
            if (e == ship_idx || !esc->active) continue;
            if (esc->escorting != ship_idx) continue;
            if (esc->owner != sh->owner) continue;
            if (esc->at_island != sh->from_island) continue;  /* not here */

            esc->from_island    = esc->at_island;
            esc->to_island      = dest;
            esc->at_island      = -1;
            esc->departure_tick = sh->departure_tick;
            esc->progress       = 0.0f;
            esc->insured        = 0;
            esc->insured_value  = 0;
        }
    }
    return REJ_OK;
}

int game_insurance_quote(const GameState *gs, int ship_idx, int dest_island)
{
    const Ship *sh;
    int         value = 0, r, premium, cost;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return 0;
    sh = &gs->ships[ship_idx];
    if (!sh->active || sh->at_island < 0) return 0;

    /* Declared value is what the faction would PAY for the hold — the
     * price it would have to make good, not the price the owner hoped
     * for. Gold aboard is worth its face. */
    for (r = 0; r < RES_COUNT; r++) {
        if (sh->cargo[r] <= 0) continue;
        value += (r == (int)RES_GOLD)
                 ? sh->cargo[r]
                 : sh->cargo[r] * faction_bid(&gs->faction, (ResourceType)r);
    }
    if (value <= 0) return 0;

    premium = faction_route_premium(&gs->faction,
        sea_route_id(&gs->sea,
            sea_route_between(&gs->sea, sh->at_island, dest_island)));
    cost    = (value * premium) / 1000;
    if (cost < INSURANCE_MIN_PREMIUM_GOLD) cost = INSURANCE_MIN_PREMIUM_GOLD;
    return cost;
}

int game_ship_depart_insured(GameState *gs, int ship_idx, int dest_island)
{
    Command c = {0};
    c.kind = CMD_SHIP_DEPART;
    c.a    = ship_idx;
    c.b    = dest_island;
    c.c    = 1;                     /* insure this voyage */
    return command_submit(gs, &c);
}

int game_ship_depart(GameState *gs, int ship_idx, int dest_island)
{
    Command c = {0};
    c.kind = CMD_SHIP_DEPART;
    c.a    = ship_idx;
    c.b    = dest_island;
    return command_submit(gs, &c);
}

/* ---- sim_colonise / game_colonise ----------------------------
 * Ownership is recorded at colonisation (Phase 5): the island belongs
 * to whoever's ship founded it. */
static int sim_colonise(GameState *gs, int ship_idx, int island_idx,
                        uint32_t player)
{
    Ship   *sh;
    Island *isl;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return 0;
    if (island_idx < 0 || island_idx >= MAX_ISLANDS) return 0;

    sh  = &gs->ships[ship_idx];
    isl = &gs->islands[island_idx];

    if (!sh->active) return 0;
    if (sh->at_island != island_idx) return 0;     /* must be there   */
    if (isl->settled) return 0;                    /* already claimed */
    if (isl->owner != PLAYER_NONE && isl->owner != player) return 0;
    if (sh->cargo[RES_GOLD] < COLONY_FOUNDING_GOLD) return 0;

    /* The founding gold leaves the hold and splits two ways:. */
    sh->cargo[RES_GOLD] -= COLONY_FOUNDING_GOLD;
    gs->faction.gold    += CHARTER_BID_GOLD;

    /* A relisted island keeps whatever its last holder built; only the
     * treasury is fresh. Ruins with a road network are a better prize
     * than bare ground, and it gives an abandoned colony a history. */
    stockpile_init(&isl->stockpile);
    stockpile_add(&isl->stockpile, RES_GOLD,
                  COLONY_FOUNDING_GOLD - CHARTER_BID_GOLD);
    isl->settled         = 1;
    isl->owner           = player;
    isl->charter_timer   = 0;
    isl->charter_arrears = 0;
    camera_init(&isl->camera, SCREEN_W, SCREEN_H, MAP_COLS, MAP_ROWS);

    sim_log("Charter bought on %s: %d Gold to the faction, %d Gold "
            "treasury (player %u)", isl->name, CHARTER_BID_GOLD,
            COLONY_FOUNDING_GOLD - CHARTER_BID_GOLD, player);
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

/* ---- sim_set_route_res / game_ship_set_route_res ------------ */
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

/* ---- sim_toggle_route / game_ship_toggle_route -------------- */
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
        sh->route_qty    = ship_hold_capacity(sh);
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

/* ---- sim_grant_start / game_grant_start --------------------- */
static int sim_grant_start(GameState *gs, int island_idx, uint32_t player)
{
    Island *isl;
    int     i;

    if (island_idx < 0 || island_idx >= MAX_ISLANDS) return 0;
    if (player == PLAYER_NONE) return 0;

    isl = &gs->islands[island_idx];
    if (isl->settled || isl->owner != PLAYER_NONE) return 0;

    for (i = 0; i < MAX_ISLANDS; i++)
        if (gs->islands[i].owner == player) return 0;   /* has a home */

    stockpile_init(&isl->stockpile);
    stockpile_add(&isl->stockpile, RES_GOLD, STARTING_GOLD);
    isl->settled = 1;
    isl->owner   = player;
    camera_init(&isl->camera, SCREEN_W, SCREEN_H, MAP_COLS, MAP_ROWS);

    sim_log("Starting island %s granted to player %u", isl->name, player);
    return 1;
}

int game_grant_start(GameState *gs, int island_idx)
{
    Command c = {0};
    c.kind = CMD_GRANT_START;
    c.a    = island_idx;
    return command_submit(gs, &c);
}

/* ---- sim_escrow_put / take / sim_set_docking ---------------- */
static RejectReason sim_escrow_put(GameState *gs, int island,
                                   ResourceType res, int qty, uint32_t nonce)
{
    Island *isl = &gs->islands[island];

    if (res < 0 || res >= RES_COUNT) return REJ_UNAVAILABLE;
    if (nonce != 0 && nonce != island_escrow_nonce(isl))
        return REJ_OFFER_CHANGED;
    if (qty > isl->stockpile.amount[res]) qty = isl->stockpile.amount[res];
    if (qty <= 0) return REJ_NO_STOCK;

    stockpile_add(&isl->stockpile, res, -qty);
    isl->escrow[res] += qty;
    return REJ_OK;
}

static RejectReason sim_escrow_take(GameState *gs, int island,
                                    ResourceType res, int qty,
                                    uint32_t nonce)
{
    Island *isl     = &gs->islands[island];
    int     in_hold = 0;

    if (res < 0 || res >= RES_COUNT) return REJ_UNAVAILABLE;
    if (nonce != 0 && nonce != island_escrow_nonce(isl))
        return REJ_OFFER_CHANGED;

    in_hold = isl->escrow[res];
    if (qty > in_hold) qty = in_hold;
    if (res != RES_GOLD) {
        int headroom = isl->stockpile.capacity - isl->stockpile.amount[res];
        if (headroom < 0) headroom = 0;
        if (qty > headroom) qty = headroom;
    }
    if (qty <= 0) {
        /* Empty quay and full warehouse are different problems: one
         * waits for a ship, the other for a Warehouse. */
        if (in_hold <= 0) return REJ_NO_STOCK;
        return REJ_NO_STORAGE;
    }

    isl->escrow[res] -= qty;
    stockpile_add(&isl->stockpile, res, qty);
    return REJ_OK;
}

static RejectReason sim_set_docking(GameState *gs, int island, int allow)
{
    gs->islands[island].docking_allowed = allow ? 1 : 0;
    return REJ_OK;
}

/* What the treasury takes from wages and from business profit */
static RejectReason sim_set_tax_rate(GameState *gs, int island, int permille)
{
    if (permille < 0)                      permille = 0;
    if (permille > TAX_RATE_MAX_PERMILLE)  permille = TAX_RATE_MAX_PERMILLE;
    gs->islands[island].tax_rate_permille = permille;
    return REJ_OK;
}

int game_escrow_put_nonce(GameState *gs, int island_idx, ResourceType res,
                          int qty, uint32_t nonce)
{
    Command c = {0};
    c.kind = CMD_ESCROW_PUT;
    c.a    = island_idx;
    c.b    = (int32_t)res;
    c.c    = qty;
    c.d    = (int32_t)nonce;
    return command_submit(gs, &c);
}

int game_escrow_put(GameState *gs, int island_idx, ResourceType res, int qty)
{
    return game_escrow_put_nonce(gs, island_idx, res, qty, 0u);
}

int game_escrow_take_nonce(GameState *gs, int island_idx, ResourceType res,
                           int qty, uint32_t nonce)
{
    Command c = {0};
    c.kind = CMD_ESCROW_TAKE;
    c.a    = island_idx;
    c.b    = (int32_t)res;
    c.c    = qty;
    c.d    = (int32_t)nonce;
    return command_submit(gs, &c);
}

int game_escrow_take(GameState *gs, int island_idx, ResourceType res, int qty)
{
    return game_escrow_take_nonce(gs, island_idx, res, qty, 0u);
}

int game_build_research_boat(GameState *gs, int island_idx)
{
    Command c = {0};
    c.kind = CMD_BUILD_RESEARCH_BOAT;
    c.a    = island_idx;
    return command_submit(gs, &c);
}

int game_survey(GameState *gs, int from_island, int to_island)
{
    Command c = {0};
    c.kind = CMD_SURVEY;
    c.a    = from_island;
    c.b    = to_island;
    return command_submit(gs, &c);
}

int game_set_insurance(GameState *gs, int island_idx, int on)
{
    Command c = {0};
    c.kind = CMD_SET_INSURANCE;
    c.a    = island_idx;
    c.b    = on ? 1 : 0;
    return command_submit(gs, &c);
}

int game_set_docking(GameState *gs, int island_idx, int allow)
{
    Command c = {0};
    c.kind = CMD_SET_DOCKING;
    c.a    = island_idx;
    c.b    = allow;
    return command_submit(gs, &c);
}

int game_set_tax_rate(GameState *gs, int island_idx, int permille)
{
    Command c = {0};
    c.kind = CMD_SET_TAX_RATE;
    c.a    = island_idx;
    c.b    = permille;
    return command_submit(gs, &c);
}

/* The two submission helpers for the book. Like every other game_*,
 * these only queue the command — whether it is accepted is decided when
 * its tick runs, and comes back as a RejectReason the flash correlates
 * by {player_id, seq}. */
int game_place_order(GameState *gs, int island_idx, TradeKind kind,
                     uint16_t what, int qty, int limit)
{
    Command c = {0};
    c.kind = CMD_PLACE_ORDER;
    c.a    = island_idx;
    c.b    = TRADE_PACK(kind, what);
    c.c    = qty;                /* the sign is the side: + buys, - sells */
    c.d    = limit;
    return command_submit(gs, &c);
}

int game_cancel_order(GameState *gs, uint32_t order_id)
{
    Command c = {0};
    c.kind = CMD_CANCEL_ORDER;
    c.a    = (int32_t)order_id;
    return command_submit(gs, &c);
}


/* ---- the order book (MARITIME_PLAN Phase 2) ----------------- */

/* ---- whose purse a trade touches ------------------------- */
static int trade_balance(const GameState *gs, uint32_t owner, int island,
                         TradeId what)
{
    if (what.kind == TRADE_ROUTE_CHART)
        return knowledge_charts(&gs->knowledge, owner, (int)what.id);

    if (owner == PLAYER_FACTION)
        return (what.id == (uint16_t)RES_GOLD)
             ? gs->faction.gold : gs->faction.inventory[what.id];
    return gs->islands[island].stockpile.amount[what.id];
}

static void trade_credit(GameState *gs, uint32_t owner, int island,
                         TradeId what, int qty)
{
    if (what.kind == TRADE_ROUTE_CHART) {
        knowledge_add_charts(&gs->knowledge, owner, (int)what.id, qty);
        return;
    }

    if (owner == PLAYER_FACTION) {
        if (what.id == (uint16_t)RES_GOLD) gs->faction.gold += qty;
        else                               gs->faction.inventory[what.id] += qty;
        return;
    }
    stockpile_add(&gs->islands[island].stockpile, (ResourceType)what.id, qty);
}

/* Gold is the one thing every trade is denominated in, and it is never
 * a chart, so it gets its own spelling rather than making every caller
 * build a TradeId for it. */
static TradeId trade_gold(void)
{
    TradeId t;
    t.kind = TRADE_RESOURCE;
    t.id   = (uint16_t)RES_GOLD;
    return t;
}

static RejectReason sim_place_order(GameState *gs, int island, int32_t packed,
                                    int qty, int limit, uint32_t player)
{
    OrderBook *b  = &gs->book;
    TradeKind kind = (TradeKind)TRADE_KIND_OF(packed);
    uint16_t  id   = TRADE_ID_OF(packed);
    int       side = qty >= 0 ? ORDER_BUY : ORDER_SELL;
    int       slot, i;
    TradeId   what;

    what.kind = (uint16_t)kind;
    what.id   = id;

    if (qty == 0) return REJ_UNAVAILABLE;
    if (qty < 0) qty = -qty;
    if (limit <= 0) return REJ_UNAVAILABLE;

    /* Both kinds are tradeable now (Phase 3b). The matcher never had
     * to learn what either means — it compares (kind, id) — which is
     * exactly what the pair was for. */
    if (kind == TRADE_RESOURCE) {
        if (id >= (uint16_t)RES_COUNT || id == (uint16_t)RES_GOLD)
            return REJ_UNAVAILABLE;
    } else if (kind == TRADE_ROUTE_CHART) {
        /* A chart names a route by its sea id. You cannot sell a map
         * of a passage that does not exist, and a chart of the public
         * lane would be a map of something everybody already has. */
        if (id >= (uint16_t)gs->sea.route_count) return REJ_UNAVAILABLE;
        if (!gs->sea.route[id].is_private) return REJ_UNAVAILABLE;
    } else {
        return REJ_UNAVAILABLE;
    }

    /* The per-player cap does not apply to the market maker: it is what */
    if (player != PLAYER_FACTION &&
        orderbook_open_count(b, player) >= ORDERBOOK_MAX_PER_PLAYER)
        return REJ_UNAVAILABLE;

    /* Find a slot: reuse an inactive one before growing, the same
     * find-or-append the building and agent arrays use, so a replay
     * lands every order in the same slot. */
    slot = -1;
    for (i = 0; i < b->order_count; i++)
        if (!b->order[i].active) { slot = i; break; }
    if (slot < 0) {
        if (b->order_count >= ORDERBOOK_MAX_ORDERS) return REJ_UNAVAILABLE;
        slot = b->order_count++;
    }

    if (side == ORDER_SELL) {
        if (trade_balance(gs, player, island, what) < qty)
            return REJ_NO_STOCK;
        trade_credit(gs, player, island, what, -qty);
    } else {
        int cost = qty * limit;
        if (trade_balance(gs, player, island, trade_gold()) < cost)
            return REJ_CANT_AFFORD;
        trade_credit(gs, player, island, trade_gold(), -cost);
    }

    memset(&b->order[slot], 0, sizeof(b->order[slot]));
    b->order[slot].active        = 1;
    b->order[slot].id            = b->next_order_id++;
    b->order[slot].owner         = player;
    b->order[slot].island        = island;
    b->order[slot].what          = what;
    b->order[slot].side          = side;
    b->order[slot].qty           = qty;
    b->order[slot].limit         = limit;
    b->order[slot].reserved_gold = (side == ORDER_BUY) ? qty * limit : 0;
    b->order[slot].placed_tick   = gs->sim_tick_no;
    return REJ_OK;
}

/* Give back whatever the order is still holding. Shared by cancel and
 * by the matcher, so a partially filled order that is withdrawn cannot
 * return more than it has left. */
static void order_refund(GameState *gs, Order *o)
{
    if (o->side == ORDER_SELL)
        trade_credit(gs, o->owner, o->island, o->what, o->qty);
    else
        trade_credit(gs, o->owner, o->island, trade_gold(), o->reserved_gold);
    o->active = 0;
}

static RejectReason sim_cancel_order(GameState *gs, uint32_t order_id,
                                     uint32_t player)
{
    Order *o = orderbook_find(&gs->book, order_id);

    /* Not "not possible right now": the book screen keeps a filled
     * order on screen as a struck-through row (UI_PLAN N3), so a click
     * landing on one is an ordinary race rather than a mystery, and it
     * deserves a sentence that says what happened. */
    if (!o) return REJ_ORDER_GONE;
    if (o->owner != player) return REJ_NOT_OWNER;
    order_refund(gs, o);
    return REJ_OK;
}

/* ---- expeditions (MARITIME_PLAN Phase 3d) ----------------- */
static RejectReason sim_build_research_boat(GameState *gs, int island)
{
    Island *isl = &gs->islands[island];
    int     i, has_yard = 0;

    if (!isl->settled) return REJ_UNAVAILABLE;

    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_SHIPYARD) { has_yard = 1; break; }
    if (!has_yard) return REJ_UNAVAILABLE;

    if (isl->stockpile.amount[RES_GOLD] < RESEARCH_BOAT_GOLD)
        return REJ_CANT_AFFORD;
    if (isl->stockpile.amount[RES_PLANKS] < RESEARCH_BOAT_PLANKS)
        return REJ_NO_STOCK;

    stockpile_add(&isl->stockpile, RES_GOLD, -RESEARCH_BOAT_GOLD);
    stockpile_add(&isl->stockpile, RES_PLANKS, -RESEARCH_BOAT_PLANKS);
    isl->research_boats++;

    sim_log("Research boat laid down at %s", isl->name);
    return REJ_OK;
}

/* Which passage an expedition would chart: the fastest private route
 * between the two islands that this player does not already know.
 * Returns -1 if there is nothing left to find, which is a real answer
 * and not an error — you have charted that crossing. */
static int survey_target_route(const GameState *gs, int from, int to,
                               uint32_t player)
{
    const Route *best = NULL;
    int          best_id = -1, v;

    for (v = 0; v < SEA_ROUTES_PER_PAIR; v++) {
        const Route *r = sea_route_variant(&gs->sea, from, to, v);
        int          id;

        if (!r || !r->is_private) continue;
        id = sea_route_id(&gs->sea, r);
        if (id < 0) continue;
        if (knowledge_knows(&gs->knowledge, player, id, 1)) continue;

        if (!best || r->total_ticks < best->total_ticks ||
            (r->total_ticks == best->total_ticks && id < best_id)) {
            best    = r;
            best_id = id;
        }
    }
    return best_id;
}

static RejectReason sim_survey(GameState *gs, int from, int to,
                               uint32_t player)
{
    Island      *isl = &gs->islands[from];
    SurveyBoard *b   = &gs->surveys;
    int          route_id, slot, i;

    if (from == to) return REJ_UNAVAILABLE;
    if (to < 0 || to >= MAX_ISLANDS) return REJ_UNAVAILABLE;
    if (!isl->settled) return REJ_UNAVAILABLE;

    /* Each of the three costs refuses in its own words (UI_PLAN N7).
     * They were one generic refusal, which said "not possible right
     * now" to a player who had charted the crossing already — a fact
     * about the world dressed up as a temporary problem. */
    if (isl->scholars_out >= island_scholar_capacity(isl))
        return REJ_NO_CREW;
    if (isl->research_boats_out >= isl->research_boats)
        return REJ_NO_BOAT;
    if (isl->stockpile.amount[RES_CHARTS] < 1) return REJ_NO_STOCK;

    route_id = survey_target_route(gs, from, to, player);
    if (route_id < 0) return REJ_NOTHING_TO_FIND;

    slot = -1;
    for (i = 0; i < b->count; i++)
        if (!b->mission[i].active) { slot = i; break; }
    if (slot < 0) {
        if (b->count >= MAX_SURVEYS) return REJ_UNAVAILABLE;
        slot = b->count++;
    }

    /* The blank chart is spent now, not on return. It is the paper the
     * survey is drawn on; whether anything gets drawn is the gamble. */
    stockpile_add(&isl->stockpile, RES_CHARTS, -1);
    isl->scholars_out++;
    isl->research_boats_out++;

    memset(&b->mission[slot], 0, sizeof(b->mission[slot]));
    b->mission[slot].active      = 1;
    b->mission[slot].owner       = player;
    b->mission[slot].from_island = from;
    b->mission[slot].to_island   = to;
    b->mission[slot].route_id    = route_id;
    b->mission[slot].finish_tick = gs->sim_tick_no + SURVEY_TICKS;

    /* Fixed when it sails, applied when it lands — so a late tick
     * cannot change what already happened at sea. */
    b->mission[slot].succeeds = survey_succeeds(gs->world_seed, route_id,
                                                gs->sim_tick_no, player);
    b->mission[slot].lost     = survey_is_lost(gs->world_seed, route_id,
                                               gs->sim_tick_no, player);

    sim_log("Expedition sailed from %s in search of a passage to %s",
            isl->name, gs->islands[to].name);
    return REJ_OK;
}

/* Take one resident from a Scholars' House: the cost of an expedition
 * that did not come back. Prefers the fullest house, so a loss does
 * not empty a one-resident house and silently remove the island's
 * whole capacity to send another. */
static void scholar_lost(Island *isl)
{
    int i, best = -1;

    for (i = 0; i < isl->building_count; i++) {
        const Building *bd = &isl->buildings[i];
        if (!bd->active || bd->type != BUILDING_HOUSE_SCHOLAR) continue;
        if (!isl->pop_data[i].active || isl->pop_data[i].residents <= 0)
            continue;
        if (best < 0 ||
            isl->pop_data[i].residents > isl->pop_data[best].residents)
            best = i;
    }
    if (best >= 0) isl->pop_data[best].residents--;
}

/* ---- the sea changes shape (MARITIME_PLAN Phase 3e) ------- */
static void sea_rotation_update(GameState *gs)
{
    int pairs = gs->sea.island_count * (gs->sea.island_count - 1) / 2;
    int p;

    for (p = 0; p < pairs && p < SEA_MAX_PAIRS; p++) {
        if (sea_pair_next_rotation(gs->sea.island_count, p,
                                   gs->sim_tick_no) != gs->sim_tick_no)
            continue;

        {
            int retired = sea_rotate_pair(&gs->sea, p);
            if (retired >= 0) {
                knowledge_void_charts(&gs->knowledge, retired);
                sim_log("The passage %s has gone out of use",
                        gs->sea.route[retired].name);
            }
        }
    }
}

static void surveys_update(GameState *gs)
{
    SurveyBoard *b = &gs->surveys;
    int          i;

    for (i = 0; i < b->count; i++) {
        Survey *m = &b->mission[i];
        Island *isl;

        if (!m->active) continue;
        if (gs->sim_tick_no < m->finish_tick) continue;

        isl = &gs->islands[m->from_island];

        if (m->succeeds) {
            knowledge_add_charts(&gs->knowledge, m->owner, m->route_id, 1);
            sim_log("Expedition charted %s",
                    gs->sea.route[m->route_id].name);
        } else if (m->lost) {
            /* The boat is gone, and so is the scholar. The house that
             * sent them is smaller for it. */
            isl->research_boats--;
            scholar_lost(isl);
            sim_log("Expedition to %s never returned",
                    gs->islands[m->to_island].name);
        } else {
            sim_log("Expedition returned to %s having found nothing",
                    isl->name);
        }

        /* The commitments end either way; what differs is whether the
         * boat and the scholar still exist to be committed again. */
        if (isl->scholars_out > 0)       isl->scholars_out--;
        if (isl->research_boats_out > 0) isl->research_boats_out--;
        if (isl->research_boats < 0)     isl->research_boats = 0;

        m->active = 0;
    }
}

/* ---- choosing a passage (MARITIME_PLAN Phase 3b) ---------- */
static const Route *pick_route(GameState *gs, int from, int to,
                               uint32_t seller, int *out_id)
{
    const Route *best = NULL;
    int          best_id = -1;
    int          v;

    for (v = 0; v < SEA_ROUTES_PER_PAIR; v++) {
        const Route *r = sea_route_variant(&gs->sea, from, to, v);
        int          id;

        if (!r) continue;
        id = sea_route_id(&gs->sea, r);
        if (id < 0) continue;

        if (r->is_private) {
            if (!knowledge_knows(&gs->knowledge, seller, id, 1)) continue;
            if (knowledge_charts(&gs->knowledge, seller, id) <= 0) continue;
        }

        if (!best || r->total_ticks < best->total_ticks ||
            (r->total_ticks == best->total_ticks && id < best_id)) {
            best    = r;
            best_id = id;
        }
    }

    if (best && best->is_private)
        knowledge_add_charts(&gs->knowledge, seller, best_id, -1);

    *out_id = best_id;
    return best;
}

/* ---- the faction as market maker (MARITIME_PLAN Phase 2) ---- */
static void faction_quote_refresh(GameState *gs)
{
    Faction *f = &gs->faction;
    int      pick[FACTION_QUOTE_GOODS];
    int      npick = 0;
    int      r, p, i, s;

    if (++f->quote_timer < FACTION_QUOTE_INTERVAL_TICKS) return;
    f->quote_timer = 0;

    /* Top-N by |inventory - baseline|, ties going to the lower resource
     * index. An insertion into a fixed array rather than a sort: N is
     * six and the comparison has to be total, or two clients could pick
     * different goods and diverge. */
    for (r = 0; r < RES_COUNT; r++) {
        int dev, j, k;

        if (r == RES_GOLD) continue;
        dev = f->inventory[r] - FACTION_BASE_INVENTORY;
        if (dev < 0) dev = -dev;

        for (j = 0; j < npick; j++) {
            int d = f->inventory[pick[j]] - FACTION_BASE_INVENTORY;
            if (d < 0) d = -d;
            if (dev > d) break;
        }
        if (j >= FACTION_QUOTE_GOODS) continue;

        if (npick < FACTION_QUOTE_GOODS) npick++;
        for (k = npick - 1; k > j; k--) pick[k] = pick[k - 1];
        pick[j] = r;
    }

    /* Withdraw everything first. The selection changes between
     * refreshes, so a slot-by-slot replace would strand the quote of a
     * good that dropped out — and a market maker that leaves its old
     * orders behind fills the book with its own history. */
    for (p = 0; p < FACTION_PORT_COUNT; p++)
        for (i = 0; i < FACTION_QUOTE_GOODS; i++)
            for (s = 0; s < 2; s++) {
                Order *o;
                if (!f->quote_order[p][i][s]) continue;
                o = orderbook_find(&gs->book, f->quote_order[p][i][s]);
                if (o) order_refund(gs, o);       /* also deactivates it */
                f->quote_order[p][i][s] = 0u;
            }

    /* Chart offers, withdrawn and re-posted with the rest. They rotate */
    for (i = 0; i < FACTION_CHART_ROUTES; i++) {
        Order *o;
        if (!f->chart_order[i]) continue;
        o = orderbook_find(&gs->book, f->chart_order[i]);
        if (o) order_refund(gs, o);
        f->chart_order[i] = 0u;
    }
    if (gs->sea.route_count > 0) {
        int offered = 0;
        int scanned;

        for (scanned = 0; scanned < gs->sea.route_count &&
                          offered < FACTION_CHART_ROUTES; scanned++) {
            int          rid = (int)((f->chart_cursor + (uint32_t)scanned) %
                                     (uint32_t)gs->sea.route_count);
            const Route *rt  = &gs->sea.route[rid];
            const Route *lane;
            int          price;
            uint32_t     id_before;

            if (!rt->is_private) continue;

            /* Worth what it saves. A passage that shaves ten ticks off
             * the lane is a curiosity; one that halves it is an asset,
             * and the price should say which is which. */
            lane = sea_route_variant(&gs->sea, rt->from_island,
                                     rt->to_island, SEA_ROUTE_PUBLIC);
            price = FACTION_CHART_MIN_PRICE;
            if (lane && lane->total_ticks > rt->total_ticks)
                price += (int)(lane->total_ticks - rt->total_ticks) *
                         FACTION_CHART_GOLD_PER_TICK_SAVED;

            id_before = gs->book.next_order_id;
            if (sim_place_order(gs,
                                MAX_ISLANDS - FACTION_PORT_COUNT +
                                    (offered % FACTION_PORT_COUNT),
                                TRADE_PACK(TRADE_ROUTE_CHART, (uint16_t)rid),
                                -FACTION_CHART_LOT, price,
                                PLAYER_FACTION) == REJ_OK)
                f->chart_order[offered] = id_before;
            offered++;
        }
        f->chart_cursor = (f->chart_cursor + (uint32_t)FACTION_CHART_ROUTES) %
                          (uint32_t)gs->sea.route_count;
    }

    for (p = 0; p < FACTION_PORT_COUNT; p++) {
        int island = MAX_ISLANDS - FACTION_PORT_COUNT + p;

        for (i = 0; i < npick; i++) {
            ResourceType res    = (ResourceType)pick[i];
            int32_t      packed = TRADE_PACK(TRADE_RESOURCE, (uint16_t)res);
            int          bid    = faction_bid(f, res);
            int          ask    = faction_ask(f, res);
            uint32_t     id_before;

            /* The ask is always above the bid (faction.h keeps the
             * spread), so the market's own two orders never cross each
             * other — and the self-trade rule would refuse them if a
             * future quote curve ever let them. */
            if (bid > 0) {
                id_before = gs->book.next_order_id;
                if (sim_place_order(gs, island, packed, FACTION_QUOTE_LOT,
                                    bid, PLAYER_FACTION) == REJ_OK)
                    f->quote_order[p][i][ORDER_BUY] = id_before;
            }
            if (ask > 0) {
                id_before = gs->book.next_order_id;
                if (sim_place_order(gs, island, packed, -FACTION_QUOTE_LOT,
                                    ask, PLAYER_FACTION) == REJ_OK)
                    f->quote_order[p][i][ORDER_SELL] = id_before;
            }
        }
    }
}

/* ---- matching ----------------------------------------------- */
static int better_order(const Order *cand, const Order *best, int side)
{
    if (!best) return 1;
    if (side == ORDER_BUY) {
        if (cand->limit != best->limit) return cand->limit > best->limit;
    } else {
        if (cand->limit != best->limit) return cand->limit < best->limit;
    }
    if (cand->placed_tick != best->placed_tick)
        return cand->placed_tick < best->placed_tick;
    return cand->id < best->id;
}

/* Choose the pair to fill for one good, or return 0 if nothing can. */
static int book_best_cross(const GameState *gs, OrderBook *b, TradeId what,
                           Order **out_bid, Order **out_ask)
{
    Order *bid = NULL, *ask = NULL, *bid2 = NULL, *ask2 = NULL;
    int    i;

    /* An ask whose island has no merchant or no hull free cannot carry */
#define ASK_ELIGIBLE(o) island_can_dispatch(&gs->islands[(o)->island])

    for (i = 0; i < b->order_count; i++) {
        Order *o = &b->order[i];
        if (!o->active) continue;
        if (o->what.kind != what.kind || o->what.id != what.id) continue;
        if (o->side == ORDER_BUY) {
            if (better_order(o, bid, ORDER_BUY)) bid = o;
        } else {
            if (!ASK_ELIGIBLE(o)) continue;
            if (better_order(o, ask, ORDER_SELL)) ask = o;
        }
    }

    if (!bid || !ask) return 0;
    if (bid->limit < ask->limit) return 0;          /* no crossing */

    if (bid->owner != ask->owner) {                 /* the usual case */
        *out_bid = bid;
        *out_ask = ask;
        return 1;
    }

    /* Same owner on both sides. Find the best bid and the best ask
     * belonging to anybody else. */
    for (i = 0; i < b->order_count; i++) {
        Order *o = &b->order[i];
        if (!o->active) continue;
        if (o->what.kind != what.kind || o->what.id != what.id) continue;
        if (o->owner == bid->owner) continue;
        if (o->side == ORDER_BUY) {
            if (better_order(o, bid2, ORDER_BUY)) bid2 = o;
        } else {
            if (!ASK_ELIGIBLE(o)) continue;
            if (better_order(o, ask2, ORDER_SELL)) ask2 = o;
        }
    }
#undef ASK_ELIGIBLE

    {
        int cross_a = bid2 && bid2->limit >= ask->limit;   /* (B2, A) */
        int cross_b = ask2 && bid->limit >= ask2->limit;   /* (B, A2) */

        if (cross_a && cross_b) {
            /* Both blocked orders belong to the same player, so the
             * claim to be served is the outsider's: whichever of the
             * two has been waiting longer, by the same price-time rule
             * the rest of the book uses. */
            int b2_first = (bid2->placed_tick != ask2->placed_tick)
                         ? (bid2->placed_tick < ask2->placed_tick)
                         : (bid2->id < ask2->id);
            if (b2_first) cross_b = 0; else cross_a = 0;
        }
        if (cross_a) { *out_bid = bid2; *out_ask = ask;  return 1; }
        if (cross_b) { *out_bid = bid;  *out_ask = ask2; return 1; }
    }
    return 0;
}

/* Where a shipment is right now, along the route it took. */
static SeaPos booking_pos(const GameState *gs, const Booking *bk)
{
    const Route *r;
    uint64_t     elapsed, total;
    SeaPos       zero;

    zero.x = 0;
    zero.y = 0;
    if (bk->route_id < 0 || bk->route_id >= gs->sea.route_count) return zero;

    r     = &gs->sea.route[bk->route_id];
    total = r->total_ticks;
    if (bk->arrive_tick < total) return zero;

    elapsed = gs->sim_tick_no - (bk->arrive_tick - total);
    if (elapsed > total) elapsed = total;
    return sea_route_point(&gs->sea, r, (uint32_t)elapsed);
}

/* Pirates take what passes them (MARITIME_PLAN Phase 5b). Checked every
 * tick against every shipment still outbound, so a raid happens at a
 * place and a time rather than being decided before the ship left. */
static void book_raid_check(GameState *gs)
{
    OrderBook *b = &gs->book;
    int        i;

    for (i = 0; i < b->booking_count; i++) {
        Booking *bk = &b->booking[i];
        int      pi, r;

        if (!bk->active || bk->delivered || bk->raided) continue;
        if (gs->sim_tick_no >= bk->arrive_tick) continue;   /* as good as home */

        pi = pirate_at(&gs->pirates, &gs->sea, booking_pos(gs, bk));
        if (pi < 0) continue;

        /* The lane is patrolled, and that is the whole of "public. */
        if (bk->route_id >= 0 && !gs->sea.route[bk->route_id].is_private &&
            shipment_is_raided(gs->world_seed, bk->route_id,
                               bk->arrive_tick, bk->seller,
                               CONVOY_ESCORT_DRIVES_OFF))
            continue;

        bk->raided = 1;

        /* Into their hold, not out of the world. A chart is not cargo
         * and does not travel this way — it is knowledge, and Phase 3b
         * put it somewhere a hold cannot reach. */
        if (bk->what.kind == TRADE_RESOURCE) {
            r = (int)bk->what.id;
            gs->pirates.fleet[pi].plunder[r] += bk->qty;
        } else if (gs->pirates.fleet[pi].chart < 0) {
            /* A stolen chart joins the one they carry, which is how a
             * private passage leaks: hunt the fleet that took it and
             * the passage is yours too. */
            gs->pirates.fleet[pi].chart = (int32_t)bk->what.id;
        }

        sim_log("Pirates took a shipment near %s",
                gs->sea.waypoint[gs->pirates.fleet[pi].waypoint].name);
    }
}

static void book_settle_arrivals(GameState *gs)
{
    OrderBook *b = &gs->book;
    int        i;

    for (i = 0; i < b->booking_count; i++) {
        Booking *bk = &b->booking[i];
        Island  *to, *from;

        if (!bk->active) continue;

        to   = &gs->islands[bk->to_island];
        from = &gs->islands[bk->from_island];

        /* Outbound. A raided shipment lands nothing: the goods. */
        if (!bk->delivered && gs->sim_tick_no >= bk->arrive_tick &&
            bk->raided) {
            trade_credit(gs, bk->buyer, bk->to_island, trade_gold(),
                         bk->qty * bk->price);
            if (bk->insured_value > 0) {
                int payout = bk->insured_value;
                if (payout > gs->faction.gold) payout = gs->faction.gold;
                gs->faction.gold -= payout;
                trade_credit(gs, bk->seller, bk->from_island, trade_gold(),
                             payout);
                sim_log("Insurance paid %d Gold on a raided shipment", payout);
            }
            faction_route_experience(&gs->faction, bk->route_id, 1);
            sim_log("Pirates took %d %s off %s", bk->qty,
                    bk->what.kind == TRADE_ROUTE_CHART ? "chart(s)"
                                                       : RESOURCE_NAMES[bk->what.id],
                    bk->route_id >= 0 ? gs->sea.route[bk->route_id].name
                                      : "the crossing");
            bk->delivered = 1;
        } else if (!bk->delivered && gs->sim_tick_no >= bk->arrive_tick) {
            trade_credit(gs, bk->buyer, bk->to_island, bk->what, bk->qty);
            trade_credit(gs, bk->seller, bk->from_island, trade_gold(),
                         bk->qty * bk->price);
            bk->delivered = 1;

            if (bk->what.kind == TRADE_ROUTE_CHART)
                sim_log("Chart filled: %d chart(s) of %s to %s at %d each",
                        bk->qty, gs->sea.route[bk->what.id].name, to->name,
                        bk->price);
            else
                sim_log("Order filled: %d %s delivered to %s at %d each",
                        bk->qty, RESOURCE_NAMES[bk->what.id], to->name,
                        bk->price);

            faction_route_experience(&gs->faction, bk->route_id, 0);
        }

        /* Homeward: the merchant and the hull are free again. Only now
         * is the booking done — the trade completed at arrival, but the
         * capital it tied up is still at sea until here. */
        if (gs->sim_tick_no >= bk->return_tick) {
            if (from->merchants_out > 0) from->merchants_out--;
            if (from->hulls_out     > 0) from->hulls_out--;
            bk->active = 0;
        }
    }
}

static void book_match(GameState *gs)
{
    OrderBook *b = &gs->book;
    int        kind, id, guard;
    /* Widest id space either kind uses. */
#define BOOK_MAX_TRADE_IDS ((int)RES_COUNT > SEA_MAX_ROUTES \
                            ? (int)RES_COUNT : SEA_MAX_ROUTES)
    unsigned char present[TRADE_KIND_COUNT * BOOK_MAX_TRADE_IDS];

    pirate_update(&gs->pirates, &gs->sea, gs->sim_tick_no, gs->world_seed);
    book_raid_check(gs);
    book_settle_arrivals(gs);
    sea_rotation_update(gs);
    surveys_update(gs);
    faction_quote_refresh(gs);

    /* Which (kind, id) pairs the book actually holds an order for. */
    {
        unsigned char *seen = present;
        int            n;

        memset(present, 0, sizeof(present));
        for (n = 0; n < b->order_count; n++) {
            const Order *o = &b->order[n];
            if (!o->active) continue;
            if (o->what.kind >= TRADE_KIND_COUNT) continue;
            if (o->what.id >= BOOK_MAX_TRADE_IDS) continue;
            seen[o->what.kind * BOOK_MAX_TRADE_IDS + o->what.id] = 1;
        }
    }

    /* Both kinds, each over its own id space: resources over
     * RES_COUNT, charts over the routes that exist. The matcher itself
     * never learned the difference — it compares (kind, id) — so this
     * loop is the only place that knows there is more than one. */
    for (kind = 0; kind < TRADE_KIND_COUNT; kind++) {
      int id_count = (kind == TRADE_RESOURCE) ? RES_COUNT
                                              : gs->sea.route_count;
      for (id = 0; id < id_count; id++) {
        TradeId what;

        if (!present[kind * BOOK_MAX_TRADE_IDS + id]) continue;
        if (kind == TRADE_RESOURCE && id == RES_GOLD) continue;
        if (kind == TRADE_ROUTE_CHART && !gs->sea.route[id].is_private)
            continue;
        what.kind = (uint16_t)kind;
        what.id   = (uint16_t)id;

        /* Bounded: each pass fills at least one order completely, so a
         * book of N orders cannot loop more than N times. The guard is
         * belt and braces against a future rule that fills nothing. */
        for (guard = 0; guard < ORDERBOOK_MAX_ORDERS; guard++) {
            Order *bid = NULL, *ask = NULL;
            int    i, qty, slot;

            if (!book_best_cross(gs, b, what, &bid, &ask)) break;

            qty = bid->qty < ask->qty ? bid->qty : ask->qty;

            slot = -1;
            for (i = 0; i < b->booking_count; i++)
                if (!b->booking[i].active) { slot = i; break; }
            if (slot < 0) {
                if (b->booking_count >= ORDERBOOK_MAX_BOOKINGS) break;
                slot = b->booking_count++;
            }

            memset(&b->booking[slot], 0, sizeof(b->booking[slot]));
            b->booking[slot].active      = 1;
            b->booking[slot].what        = ask->what;
            b->booking[slot].qty         = qty;
            /* The resting order sets the price: the one that was there
             * first is the quote the other side chose to take. */
            b->booking[slot].price       = (ask->placed_tick <= bid->placed_tick)
                                         ? ask->limit : bid->limit;
            b->booking[slot].from_island = ask->island;
            b->booking[slot].to_island   = bid->island;
            b->booking[slot].buyer       = bid->owner;
            b->booking[slot].seller      = ask->owner;
            {
                int          route_id = -1;
                const Route *r = pick_route(gs, ask->island, bid->island,
                                            ask->owner, &route_id);
                uint32_t crossing = r ? r->total_ticks
                                      : sea_crossing_ticks(&gs->sea,
                                                           ask->island,
                                                           bid->island);
                b->booking[slot].route_id    = route_id;
                b->booking[slot].arrive_tick = gs->sim_tick_no + crossing;
                /* Home again by the same water — one chart, one round
                 * trip, which is what "consumed when travel is done"
                 * means. The round trip is also what the merchant and
                 * the hull are committed for. */
                b->booking[slot].return_tick = gs->sim_tick_no + crossing * 2u;
            }

            /* Take the merchant and the hull. book_best_cross only
             * offered this ask because both were free, and it re-reads
             * the counts on every pass, so a second match out of the
             * same island this tick sees them already committed. */
            gs->islands[ask->island].merchants_out++;
            gs->islands[ask->island].hulls_out++;

            /* Whether pirates take it is no longer decided here */
            {
                Booking *bk    = &b->booking[slot];
                Island  *from  = &gs->islands[ask->island];
                int      value = qty * bk->price;

                /* A standing policy insures at the route's premium. */
                if (from->insure_shipments && value > 0) {
                    int prem = faction_route_premium(&gs->faction,
                                                     bk->route_id);
                    int cost = (value * prem) / 1000;
                    if (cost < INSURANCE_MIN_PREMIUM_GOLD)
                        cost = INSURANCE_MIN_PREMIUM_GOLD;
                    if (trade_balance(gs, ask->owner, ask->island,
                                      trade_gold()) >= cost) {
                        trade_credit(gs, ask->owner, ask->island,
                                     trade_gold(), -cost);
                        gs->faction.gold += cost;
                        bk->insured_value = value;
                    }
                }
            }

            /* The buyer reserved at their limit; a fill at the resting
             * price can only be cheaper, and the difference goes back. */
            bid->reserved_gold -= qty * bid->limit;
            if (bid->limit > b->booking[slot].price)
                trade_credit(gs, bid->owner, bid->island, trade_gold(),
                             qty * (bid->limit - b->booking[slot].price));

            bid->qty -= qty;
            ask->qty -= qty;
            if (bid->qty == 0) bid->active = 0;
            if (ask->qty == 0) ask->active = 0;
        }
      }
    }
}

/* ---- Ownership gates (Phase 5) ------------------------------
 * Checked centrally in sim_apply so no dispatch path can forget them:
 * you may only act on an island you own and command a ship you own. */
static int owns_island(const GameState *gs, int idx, uint32_t player)
{
    return idx >= 0 && idx < MAX_ISLANDS &&
           player != PLAYER_NONE && gs->islands[idx].owner == player;
}

static int owns_ship(const GameState *gs, int idx, uint32_t player)
{
    return idx >= 0 && idx < gs->ship_count && gs->ships[idx].active &&
           player != PLAYER_NONE && gs->ships[idx].owner == player;
}

/* ---- sim_apply ---------------------------------------------- */
RejectReason sim_apply_reason(GameState *gs, const Command *c)
{
    /* Nobody commands the market (MARITIME_PLAN Phase 2). The faction */
    if (c->player_id == PLAYER_FACTION) return REJ_NOT_OWNER;

    switch (c->kind) {
    case CMD_PLACE_BUILDING: {
        BuildingType type = (BuildingType)(c->d / 2);
        int          pay  = c->d & 1;
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_place_building(gs, c->a, c->b, c->c, type, pay);
    }
    case CMD_PLACE_ROAD:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_place_road(gs, c->a, c->b, c->c);
    case CMD_DEMOLISH:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_demolish(gs, c->a, c->b);
    case CMD_SELL_RESOURCE:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_sell(gs, c->a, (ResourceType)c->b, c->c, c->d);
    case CMD_BUY_RESOURCE:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_buy(gs, c->a, (ResourceType)c->b, c->c, c->d);
    case CMD_UPGRADE_HOUSE:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_upgrade_house(gs, c->a, c->b, c->c);
    case CMD_BUILD_SHIP:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_build_ship(gs, c->a, c->b, c->player_id) >= 0 ? REJ_OK : REJ_UNAVAILABLE;
    case CMD_SHIP_TRANSFER:
        /* Your ship, any island — WHOSE island decides stockpile vs
         * escrow inside the body. */
        if (!owns_ship(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        if (c->d < 0 || c->d >= MAX_ISLANDS) return REJ_UNAVAILABLE;
        return sim_ship_transfer(gs, c->a, (ResourceType)c->b, c->c, c->d,
                                 c->player_id);
    case CMD_SHIP_DEPART:
        if (!owns_ship(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_ship_depart(gs, c->a, c->b, c->c);
    case CMD_COLONISE:
        if (!owns_ship(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_colonise(gs, c->a, c->b, c->player_id) ? REJ_OK : REJ_UNAVAILABLE;
    case CMD_SET_ROUTE_RES:
        if (!owns_ship(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_set_route_res(gs, c->a, c->b) ? REJ_OK : REJ_UNAVAILABLE;
    case CMD_TOGGLE_ROUTE:
        if (!owns_ship(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_toggle_route(gs, c->a) ? REJ_OK : REJ_UNAVAILABLE;
    case CMD_GRANT_START:
        /* Deliberately ungated: its precondition is owning NOTHING —
         * sim_grant_start validates that itself. */
        return sim_grant_start(gs, c->a, c->player_id) ? REJ_OK : REJ_UNAVAILABLE;
    case CMD_ESCROW_PUT:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_escrow_put(gs, c->a, (ResourceType)c->b, c->c,
                              (uint32_t)c->d);
    case CMD_ESCROW_TAKE:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_escrow_take(gs, c->a, (ResourceType)c->b, c->c,
                               (uint32_t)c->d);
    case CMD_INTERCEPT:
        /* Your ship, anyone else's voyage. Ownership of the ATTACKER is
         * checked here; the target's ownership is checked in the body,
         * where "not yours" is the whole point rather than a rejection. */
        if (!owns_ship(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_intercept(gs, c->a, c->b, (uint64_t)(uint32_t)c->c,
                             c->player_id);
    case CMD_SET_DOCKING:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_set_docking(gs, c->a, c->b);
    case CMD_SET_TAX_RATE:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_set_tax_rate(gs, c->a, c->b);
    case CMD_PLACE_ORDER:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_place_order(gs, c->a, c->b, c->c, c->d, c->player_id);
    case CMD_CANCEL_ORDER:
        /* Ownership is the order's, not an island's, and is checked in
         * the body where the order can be looked up. */
        return sim_cancel_order(gs, (uint32_t)c->a, c->player_id);
    case CMD_ATTACK_PIRATE:
        return sim_attack_pirate(gs, c->a, c->b, c->player_id);
    case CMD_SET_ESCORT: {
        Ship *sh;
        if (c->a < 0 || c->a >= gs->ship_count) return REJ_UNAVAILABLE;
        sh = &gs->ships[c->a];
        if (!sh->active || sh->owner != c->player_id) return REJ_NOT_OWNER;
        if (c->b < 0) { sh->escorting = -1; return REJ_OK; }
        if (c->b >= gs->ship_count || c->b == c->a) return REJ_NO_TARGET;
        /* You escort your own. A hull that could attach itself to a
         * stranger's convoy would be a way to read where they are
         * going, which is exactly what Phase 3 stopped telling you. */
        if (!gs->ships[c->b].active ||
            gs->ships[c->b].owner != c->player_id) return REJ_NOT_OWNER;
        sh->escorting = c->b;
        return REJ_OK;
    }
    case CMD_SET_INSURANCE:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        gs->islands[c->a].insure_shipments = c->b ? 1 : 0;
        return REJ_OK;
    case CMD_BUILD_RESEARCH_BOAT:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_build_research_boat(gs, c->a);
    case CMD_SURVEY:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_survey(gs, c->a, c->b, c->player_id);
    default:
        return REJ_UNAVAILABLE;
    }
}

int sim_apply(GameState *gs, const Command *c)
{
    return sim_apply_reason(gs, c) == REJ_OK;
}
