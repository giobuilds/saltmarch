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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A seed for "new world, no seed given". The sim owns no clock (see the
 * determinism doctrine in MMO_PLAN.md), so this is the one place it may
 * read one: choosing a world, not simulating it. time() alone repeats
 * within a second, hence the clock() mix — two new games started in the
 * same second must not be the same world. */
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
    /* Not below the floor. game_install_world rebuilds from the seed at
     * tick 0 and replays forward; against a truncated log that would
     * not fail, it would quietly construct a DIFFERENT world -- one
     * where none of the discarded history ever happened -- and present
     * it as the past. Clamping is the difference between "you cannot
     * look further back" and a convincing lie. */
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
    if (gs->inventory_open)        return UI_OVERLAY_INVENTORY;
    if (gs->world_open)            return UI_OVERLAY_WORLD;
    return UI_OVERLAY_NONE;
}

int game_overlay_open(const GameState *gs)
{
    return game_topmost_overlay(gs) != UI_OVERLAY_NONE;
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

    /* A world built from a seed has its whole history ahead of it, so
     * any floor a previous life left behind is not merely stale, it
     * belongs to a DIFFERENT world — and the scrubber would happily
     * rebuild from that other world's snapshot. Cleared here rather
     * than in the callers because this is the one place a world starts
     * over. */
    game_clear_history_floor(gs);

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

    stockpile_add(&cur(gs)->stockpile, RES_GOLD, STARTING_GOLD);

    /* The world's first player is ALWAYS player 1, regardless of who
     * created it: this function is also how load and the F9 verifier
     * rebuild tick 0, so island 0's owner must be a pure function of
     * the seed — never of which client happens to be reconstructing.
     * (game_new/game_new_seeded make the local player id 1 to match;
     * a co-op guest acquires its island via a logged CMD_GRANT_START.) */
    gs->islands[0].owner = 1u;

    /* The market starts at baseline, so day-one quotes equal the old
     * fixed SELL_PRICE/BUY_PRICE until the player trades. */
    faction_init(&gs->faction);

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
    /* calloc, not malloc. The explicit initialisation below stays --
     * it documents intent and sets the fields whose correct start is
     * not zero -- but a field added to GameState and forgotten here
     * must not be able to arrive as garbage.
     *
     * That is not hypothetical: history_floor_tick's `floor_snap`
     * pointer was added without a line here, game_reset_world frees it
     * before assigning, and free() on an uninitialised pointer is
     * undefined. Linux and macOS hand out zeroed pages, so free(NULL)
     * quietly did nothing and every test passed on both; MSVC's debug
     * CRT fills fresh allocations with 0xCD, so the same code segfaulted
     * at startup before it could log a single line. Zeroing first costs
     * one memset at launch and removes the whole class. */
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

/* ---- game_new --------------------------------------------
 * Starting a NEW world makes you its first player (game_reset_world
 * gives island 0 to player 1 unconditionally — see the note there), so
 * the local id snaps back to 1 even for an ex-guest. game_reset_world
 * itself must NOT touch local_player_id: load and the F9 verifier call
 * it to rebuild worlds this client doesn't own. */
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
/* ---- Save format v10: optionally, state instead of history ----
 * SERVER.md, "Log truncation". A save may now carry a SNAPSHOT section
 * before its commands. When it does, loading restores the world
 * directly and applies only the commands that follow it; when it does
 * not, loading is exactly the v5 replay above.
 *
 * Both shapes exist on purpose. A world that has been running for
 * months cannot be joined or restarted by replaying every tick from
 * zero -- that cost is proportional to AGE and accrues whether or not
 * anyone played, which is the whole reason this section exists. But the
 * determinism gate (`--record` then `--replay`) proves what it proves
 * PRECISELY BECAUSE a fixture is (seed, full log) and replaying
 * re-derives the world; turn every save into a snapshot and "replay"
 * silently degrades into "load", and CI's central guarantee evaporates
 * while still reporting success.
 *
 * So: game_save writes history, and fixtures and the gate keep proving
 * determinism. game_save_checkpoint writes state, and the server keeps
 * a bounded file. One reader handles both.
 * -------------------------------------------------------- */
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
/* v6 (Phase 5): commands carry a meaningful player_id and sim_apply
 * enforces ownership, so a v5 log (player_id 0 throughout) would replay
 * to a world of rejected commands. Same bytes, different meaning — the
 * version bump is the rejection.
 *
 * v7 (UI_PLAN M1): Command gained a client-local `seq`, so the struct
 * this file writes is a different size. The field is ignored by the
 * sim — a v6 log would replay to the same world — but the bytes no
 * longer line up, and silently misreading a log is exactly what a
 * version number is for.
 *
 * v8 (UI_PLAN M1): the file gained a second section — the recorded
 * intent stream, appended after the commands. A v7 log describes the
 * same world; it simply has no clicks recorded, so the UI harness has
 * nothing to replay.
 *
 * v9 (SUPPLY_CHAIN Phase 3): thirteen goods inserted before RES_GOLD,
 * which shifts its value and every resource index a command carries.
 * The bytes of a v8 log are unchanged but their MEANING is not — a
 * recorded "sell 5 of resource 6" was Gold and is now Bricks. Nothing
 * about the format changed; the vocabulary did, which is the harder
 * kind of incompatibility to notice and the reason the plan's ground
 * rule 5 says every content phase bumps this.
 *
 * v10 (SERVER.md, "Log truncation"): the header gained flags and a
 * snapshot length, and a save may now carry state instead of history.
 * A real format change, unlike v9's.
 *
 * v11 (SUPPLY_CHAIN Phase 4): sixteen more goods before RES_GOLD, for
 * iron, glass, preserves and sewing machines. Same shape as v9 and the
 * same hazard — a v10 log's bytes parse, but its "resource 20" was
 * Gold and now means Charcoal. */
#define SAVE_VERSION 11u

/* Plain stdio rather than SDL_IOStream (MMO_PLAN Phase 6): a save IS the
 * server's checkpoint format and the CI fixture format, so reading and
 * writing one must not require a client. "wb"/"rb" are load-bearing on
 * Windows — the log is raw Command structs, and text mode would mangle
 * every 0x0A byte in them. */
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

    /* The header has trailing padding (cmd_count sits at offset 24 in a
     * 32-byte, 8-aligned struct). Writing it uninitialised leaked four
     * bytes of stack into every save and made two recordings of the same
     * session differ byte-for-byte — harmless while a save was only ever
     * a local file, not harmless now that this format is also the CI
     * fixture and the server's checkpoint. */
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

    /* The tail is not optional. Commands are stamped
     * NET_CMD_DELAY_TICKS into the future, so at any instant the log
     * holds accepted, acknowledged commands that have not been applied
     * yet. The snapshot describes the world BEFORE them; dropping them
     * would silently un-accept work the players were already told had
     * landed. */
    first = gs->cmd_applied;
    if (first < 0) first = 0;
    if (first > gs->cmd_count) first = gs->cmd_count;
    n = gs->cmd_count - first;

    ok = save_write(gs, path, snap, snap_len, gs->cmd_log + first, n);
    free(snap);
    return ok;
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
    FILE          *f = fopen(path, "rb");
    long           size_l;
    size_t         size, need, snap_bytes;
    unsigned char *buf;
    SaveHeader     hdr;
    const Command *cmds;

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

    cmds = (const Command *)(buf + sizeof(hdr) + snap_bytes);

    if (snap_bytes) {
        /* A checkpoint: the world is restored, not re-derived. The
         * commands that follow are the tail that had not been applied
         * when it was taken, so they are installed as PENDING and the
         * sim applies each at its own stamped tick, exactly as it would
         * have if nothing had been written to disk. */
        if (!snapshot_decode(gs, buf + sizeof(hdr), snap_bytes)) {
            sim_log("game_load: %s carries a snapshot this build "
                    "cannot use", path);
            free(buf);
            return 0;
        }
        if (!command_log_set(gs, cmds, hdr.cmd_count)) {
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

    /* A checkpoint is deliberately refused rather than skipped past.
     * The caller wants a recorded SESSION -- somebody's actual play, to
     * be re-addressed onto an NPC island -- and a checkpoint holds
     * state plus the handful of commands that had not been applied
     * when it was written. Reading those would hand back a four-command
     * "session" and a ghost that does nothing, which is a far more
     * confusing failure than saying so. */
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
                               const Command *cmds, int n)
{
    if (!snapshot_decode(gs, snap, snap_len)) return 0;
    if (!command_log_set(gs, cmds, n))        return 0;

    if (!game_set_history_floor(gs, snap, snap_len, gs->sim_tick_no))
        return 0;

    while (gs->sim_tick_no < tick) sim_run_one_tick(gs);
    return 1;
}

int game_install_world(GameState *gs, uint32_t seed, uint64_t tick,
                       const Command *cmds, int n)
{
    game_reset_world(gs, seed);

    if (!command_log_set(gs, cmds, n))
        return 0;

    while (gs->sim_tick_no < tick)
        sim_run_one_tick(gs);

    return 1;
}

/* ---- interception (MMO_PLAN later phases) ------------------
 * The engagement, resolved from the log. Cargo is the stake: the winner
 * takes what the loser was carrying, up to its own hold's capacity, and
 * a failed attack costs the attacker the same way. Nothing is destroyed
 * that was not aboard, and no ship is ever sunk — losing a hold is a
 * setback, losing a ship would be an evening's work gone. */
static RejectReason sim_intercept(GameState *gs, int my_idx, int target_idx,
                                  uint64_t target_departure, uint32_t player)
{
    Ship *mine, *target;
    int   attacker_wins, r;
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

    attacker_wins = intercept_attacker_wins(gs->world_seed, my_idx,
                                            mine->departure_tick,
                                            target_idx,
                                            target->departure_tick);
    winner = attacker_wins ? mine   : target;
    loser  = attacker_wins ? target : mine;

    for (r = 0; r < RES_COUNT; r++) {
        int room, take;
        if (loser->cargo[r] <= 0) continue;
        room = SHIP_CARGO_CAPACITY - winner->cargo[r];
        if (room <= 0) continue;
        take = loser->cargo[r] < room ? loser->cargo[r] : room;
        loser->cargo[r]  -= take;
        winner->cargo[r] += take;
    }

    sim_log("Ship %d intercepted ship %d at sea — %s prevailed",
            my_idx, target_idx, attacker_wins ? "the attacker"
                                              : "the defender");
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

/* ---- charters (MMO_PLAN later phases) ----------------------
 * One island's upkeep, once per tick. An island that cannot pay
 * accrues arrears; enough of them and the charter lapses, which
 * relists the island: unowned and dormant, buildings intact, ready for
 * the next charter. That is how a persistent world hands islands to
 * new players without anyone administering it. */
static void sim_charter_tick(GameState *gs, int island)
{
    Island *isl = &gs->islands[island];

    if (!isl->settled || isl->owner == PLAYER_NONE) return;

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

    /* 2. Every settled island's full pipeline, one tick, in order —
     * see island_update()'s ordering constraint. */
    for (i = 0; i < MAX_ISLANDS; i++)
        island_update(&gs->islands[i]);

    /* 3. Voyages advance independently of any island. Insurance is
     * settled either side of the move: what was at sea before, and
     * what has arrived after. */
    {
        int s2;
        for (s2 = 0; s2 < gs->ship_count; s2++)
            gs->ships[s2].was_at_sea = (gs->ships[s2].at_island < 0);
    }
    ships_update(gs->ships, gs->ship_count, gs->islands, MAX_ISLANDS,
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
        faction_lane_experience(&gs->faction, sh->from_island, sh->to_island,
                                raided);
        sh->insured       = 0;
        sh->insured_value = 0;
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
    }

    /* The market is world state too (Phase 3). */
    fnv_bytes(&h, &gs->faction.gold, sizeof(gs->faction.gold));
    fnv_bytes(&h, gs->faction.inventory, sizeof(gs->faction.inventory));
    fnv_bytes(&h, &gs->faction.revert_timer, sizeof(gs->faction.revert_timer));
    fnv_bytes(&h, gs->faction.lane_premium, sizeof(gs->faction.lane_premium));

    /* The price history too (UI_PLAN M3). It is state the sim produces
     * and the UI renders, so a replay that reproduced everything except
     * the chart would be a replay with a hole in it. */
    fnv_bytes(&h, gs->faction.hist, sizeof(gs->faction.hist));
    fnv_bytes(&h, &gs->faction.hist_head, sizeof(gs->faction.hist_head));
    fnv_bytes(&h, &gs->faction.hist_count, sizeof(gs->faction.hist_count));

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

/* ---- sim_place_building / game_place_building_confirmed ----
 * The sim body validates everything itself (type range, settled,
 * affordability) so it is safe to call from a replayed log where the
 * accompanying GameState fields no longer describe the moment of
 * submission. */
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
    Command       none;

    if (building_idx < 0 || building_idx >= isl->building_count) return;
    if (!isl->buildings[building_idx].active) return;
    if (isl->buildings[building_idx].type != BUILDING_HOUSE) return;

    memset(&none, 0, sizeof(none));
    confirm_set(gs, CONFIRM_UPGRADE,
                cmd_one_building(CMD_UPGRADE_HOUSE, gs->current_island,
                                 building_idx), none, 0);
}

void game_confirm_ship(GameState *gs)
{
    Command c, none;

    memset(&c, 0, sizeof(c));
    memset(&none, 0, sizeof(none));
    c.kind = CMD_BUILD_SHIP;
    c.a    = gs->current_island;
    confirm_set(gs, CONFIRM_SHIP, c, none, 0);
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

/* ---- sim_sell / game_sell_resource -------------------------
 * Sells the island's goods to the NPC faction at its current bid. The
 * faction pays out of its own finite gold and takes the goods into its
 * inventory (raising it, which lowers the next bid). Player gold rises
 * by exactly what the faction's falls — the conservation invariant. The
 * faction cannot pay for more than its gold covers, so qty is clamped to
 * that; a broke faction buys nothing (returns 0). */
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

/* ---- sim_buy / game_buy_resource ----------------------------
 * Buys goods from the faction at its current ask. The faction can only
 * sell what it actually holds, so qty is clamped by its inventory as
 * well as by the player's storage headroom and Gold. qty < 0 means "buy
 * as much as all three allow", resolved here against live state so it
 * replays correctly. Player gold falls by exactly what the faction's
 * rises (conservation); the faction's inventory drops, lifting the ask. */
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
/* Is an active, road-connected building of `type` standing on this
 * island? The prerequisite side of the upgrade rule — the one part
 * tier_upgrade_check() cannot answer for itself, because the sim looks
 * it up in Island and the UI looks it up in a snapshot. Connected, not
 * merely placed: an Academy nobody can reach teaches nobody. */
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

static RejectReason sim_upgrade_house(GameState *gs, int island, int idx)
{
    Island       *isl = &gs->islands[island];
    BuildingType  from, to;
    RejectReason  why;
    const TierDef *tier;

    if (idx < 0 || idx >= isl->building_count) return REJ_UNAVAILABLE;
    if (!isl->buildings[idx].active) return REJ_UNAVAILABLE;

    from = isl->buildings[idx].type;
    tier = tier_def_for(from);
    if (!tier) return REJ_UNAVAILABLE;   /* not a house at all */

    why = tier_upgrade_check(from, isl->stockpile.amount,
                             island_has_building(isl,
                                 tier_upgrade_requires(from)),
                             &to);
    if (why != REJ_OK) return why;

    stockpile_add(&isl->stockpile, RES_GOLD, -tier->upgrade_gold);
    isl->buildings[idx].type = to;
    return REJ_OK;
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
static int sim_build_ship(GameState *gs, int island, uint32_t player)
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
    gs->ships[slot].owner       = player;   /* commanded by its builder */
    gs->ships[slot].at_island   = island;
    gs->ships[slot].from_island = island;
    gs->ships[slot].to_island   = island;

    stockpile_add(&isl->stockpile, RES_GOLD, -SHIP_BUILD_COST_GOLD);

    sim_log("Ship %d launched at %s", slot, isl->name);
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
 * must be docked at `island`. At the player's OWN island this is the
 * ordinary stockpile transfer. At a FOREIGN island (Phase 5) the only
 * permitted exchange is ship <-> harbor escrow, and only if the owner
 * allows docking and an active Harbor stands there — a ship that can't
 * dock can't deliver, which is where blockade comes from. Clamping is
 * deferred to ship_transfer_at / ship_transfer_escrow so the manual
 * path cannot disagree with what trade routes do. */
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

    premium = faction_lane_premium(&gs->faction, sh->at_island, dest_island);
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

    /* The founding gold leaves the hold and splits two ways: the
     * charter bid goes to the faction (the plan's "a bid paid TO the
     * faction" — and the economy's first real gold sink), the rest
     * becomes the colony's treasury. Without that remainder the new
     * island could not pay for so much as a road, since every cost is
     * denominated in its own Gold. */
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

/* ---- sim_grant_start / game_grant_start ---------------------
 * The co-op join bootstrap: settle an untouched island as `player`'s
 * start, with the standard treasury. Validated so it can't be abused
 * as free expansion: the island must be virgin AND the player must own
 * nothing anywhere. Mirrors sim_colonise's settle block, minus a ship. */
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

/* ---- sim_escrow_put / take / sim_set_docking ----------------
 * The owner's side of the harbor airlock. PUT moves stockpile->escrow
 * (clamped to stock); TAKE moves escrow->stockpile (clamped to escrow
 * and, for goods, to storage headroom — the escrow holds overflow
 * rather than destroying it, same rule as unloading a ship). Ownership
 * is enforced centrally in sim_apply. */
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

int game_set_docking(GameState *gs, int island_idx, int allow)
{
    Command c = {0};
    c.kind = CMD_SET_DOCKING;
    c.a    = island_idx;
    c.b    = allow;
    return command_submit(gs, &c);
}

/* ---- Ownership gates (Phase 5) ------------------------------
 * Checked centrally in sim_apply so no dispatch path can forget them:
 * you may only act on an island you own and command a ship you own.
 * This is the whole privacy model — two players cannot edit each
 * other's islands because the validation refuses, not because anything
 * is hidden. Bounds are checked here too, so the gates subsume the old
 * per-case range checks. */
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

/* ---- sim_apply ----------------------------------------------
 * The single dispatch from a Command to the mutation that carries it
 * out. The ONLY caller of the sim_* bodies above, and the only place
 * world state changes. Never appends to the log (command_submit does
 * that, and replay calls sim_apply directly). Returns 1 if the command
 * mutated state, 0 if it was rejected — rejection is deterministic and
 * not an error. Payload decoding mirrors command.h. */
RejectReason sim_apply_reason(GameState *gs, const Command *c)
{
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
        return sim_upgrade_house(gs, c->a, c->b);
    case CMD_BUILD_SHIP:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_build_ship(gs, c->a, c->player_id) >= 0 ? REJ_OK : REJ_UNAVAILABLE;
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
    default:
        return REJ_UNAVAILABLE;
    }
}

int sim_apply(GameState *gs, const Command *c)
{
    return sim_apply_reason(gs, c) == REJ_OK;
}
