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
    GameState *gs = (GameState *)malloc(sizeof(GameState));
    if (!gs) return NULL;

    /* Plain data, zeroed here rather than through input.c: the device
     * itself is the client's business (see input.h). */
    memset(&gs->input, 0, sizeof(gs->input));
    gs->last_tick  = 0;   /* seeded by client_update on its first frame */
    gs->delta_time = 0.0f;

    /* The command log starts empty. Zero it before anything can push,
     * since malloc does not, and game_reset_world resets the counters
     * but relies on the pointer/cap being valid. */
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
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t world_seed;
    int32_t  current_island;
    uint64_t sim_tick_no;
    int32_t  cmd_count;
    int32_t  intent_count;   /* v8: the recorded input stream */
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
 * nothing to replay. */
#define SAVE_VERSION 8u

/* Plain stdio rather than SDL_IOStream (MMO_PLAN Phase 6): a save IS the
 * server's checkpoint format and the CI fixture format, so reading and
 * writing one must not require a client. "wb"/"rb" are load-bearing on
 * Windows — the log is raw Command structs, and text mode would mangle
 * every 0x0A byte in them. */
int game_save(const GameState *gs, const char *path)
{
    FILE         *f = fopen(path, "wb");
    SaveHeader    hdr;
    size_t        log_bytes    = sizeof(Command) * (size_t)gs->cmd_count;
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
    hdr.cmd_count      = gs->cmd_count;
    hdr.intent_count   = gs->intent_count;

    ok = fwrite(&hdr, sizeof(hdr), 1, f) == 1
      && (log_bytes == 0 ||
          fwrite(gs->cmd_log, log_bytes, 1, f) == 1)
      && (intent_bytes == 0 ||
          fwrite(gs->intent_log, intent_bytes, 1, f) == 1);

    /* fclose can fail where fwrite succeeded (a full disk only surfaces
     * at flush), so a save is not saved until the close says so. */
    if (fclose(f) != 0) ok = 0;

    if (!ok) {
        sim_log("game_save: write to %s failed", path);
        return 0;
    }

    sim_log("Game saved to %s (seed %u, tick %llu, %d commands, %d intents)",
            path, gs->world_seed,
            (unsigned long long)gs->sim_tick_no, gs->cmd_count,
            gs->intent_count);
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
    FILE          *f = fopen(path, "rb");
    long           size_l;
    size_t         size, need;
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
    need = sizeof(hdr) + sizeof(Command) * (size_t)hdr.cmd_count
                       + sizeof(Intent)  * (size_t)hdr.intent_count;
    if (need > size) {
        sim_log("game_load: %s is truncated", path);
        free(buf);
        return 0;
    }

    /* Rebuild tick 0 from the seed (this sets replay_valid = 1), install
     * the logged commands, then replay them up to the saved tick. */
    cmds = (const Command *)(buf + sizeof(hdr));
    if (!game_install_world(gs, hdr.world_seed, hdr.sim_tick_no,
                            cmds, hdr.cmd_count)) {
        sim_log("game_load: out of memory installing %d commands",
                hdr.cmd_count);
        free(buf);
        return 0;
    }
    /* The intents are cargo: they describe how the world was reached,
     * not what it is, so a failure to install them is worth a line in
     * the log and nothing more. */
    if (hdr.intent_count > 0) {
        const Intent *ins = (const Intent *)(buf + sizeof(hdr) +
                                sizeof(Command) * (size_t)hdr.cmd_count);
        if (!intent_log_set(gs, ins, hdr.intent_count))
            sim_log("game_load: could not install %d intents",
                    hdr.intent_count);
    } else {
        gs->intent_count = 0;
    }

    free(buf);

    game_set_current_island(gs, hdr.current_island);

    sim_log("Game loaded from %s (seed %u, replayed to tick %llu, %d commands)",
            path, hdr.world_seed,
            (unsigned long long)gs->sim_tick_no, gs->cmd_count);
    return 1;
}

/* ---- game_install_world -----------------------------------
 * The (seed, log, tick) -> world constructor shared by game_load and
 * the net layer's join/resync path. See game.h. */
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

    /* 3. Voyages advance independently of any island. */
    ships_update(gs->ships, gs->ship_count, gs->islands, MAX_ISLANDS,
                 gs->sim_tick_no);

    /* 4. The market drifts back toward baseline (price recovery). */
    faction_tick(&gs->faction);

    /* 5. Advance the world clock. */
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
    }

    /* The market is world state too (Phase 3). */
    fnv_bytes(&h, &gs->faction.gold, sizeof(gs->faction.gold));
    fnv_bytes(&h, gs->faction.inventory, sizeof(gs->faction.inventory));
    fnv_bytes(&h, &gs->faction.revert_timer, sizeof(gs->faction.revert_timer));

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
                             int qty)
{
    Island  *isl = &gs->islands[island];
    Faction *fac = &gs->faction;
    int      price, revenue, wanted = qty;

    if (res < 0 || res >= RES_COUNT || res == RES_GOLD) return REJ_UNAVAILABLE;

    price = faction_bid(fac, res);
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
 * Buys goods from the faction at its current ask. The faction can only
 * sell what it actually holds, so qty is clamped by its inventory as
 * well as by the player's storage headroom and Gold. qty < 0 means "buy
 * as much as all three allow", resolved here against live state so it
 * replays correctly. Player gold falls by exactly what the faction's
 * rises (conservation); the faction's inventory drops, lifting the ask. */
static RejectReason sim_buy(GameState *gs, int island, ResourceType res,
                            int qty)
{
    Island  *isl = &gs->islands[island];
    Faction *fac = &gs->faction;
    int      price, headroom, max_affordable, max_stock, cost;

    if (res < 0 || res >= RES_COUNT || res == RES_GOLD) return REJ_UNAVAILABLE;

    price    = faction_ask(fac, res);
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
static RejectReason sim_upgrade_house(GameState *gs, int island, int idx)
{
    Island *isl = &gs->islands[island];

    if (idx < 0 || idx >= isl->building_count) return REJ_UNAVAILABLE;
    if (!isl->buildings[idx].active) return REJ_UNAVAILABLE;
    if (isl->buildings[idx].type != BUILDING_HOUSE) return REJ_UNAVAILABLE;
    if (isl->stockpile.amount[RES_GOLD] < TIER_UPGRADE_COST_GOLD)
        return REJ_CANT_AFFORD;

    stockpile_add(&isl->stockpile, RES_GOLD, -TIER_UPGRADE_COST_GOLD);
    isl->buildings[idx].type = BUILDING_HOUSE_WORKER;
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

static int sim_ship_transfer(GameState *gs, int ship_idx, ResourceType res,
                             int qty, int island, uint32_t player)
{
    Ship   *sh;
    Island *isl;

    if (ship_idx < 0 || ship_idx >= gs->ship_count) return 0;
    if (res < 0 || res >= RES_COUNT) return 0;
    sh = &gs->ships[ship_idx];
    if (!sh->active) return 0;
    if (sh->at_island != island) return 0;
    isl = &gs->islands[island];

    if (isl->owner == player)
        return ship_transfer_at(sh, isl, res, qty) != 0;

    /* Foreign dock: escrow only, and only with permission + a harbor. */
    if (!isl->docking_allowed) return 0;
    if (!island_has_active_harbor(isl)) return 0;
    return ship_transfer_escrow(sh, isl, res, qty) != 0;
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
    if (sh->cargo[RES_GOLD] < COLONY_FOUNDING_GOLD) return 0;

    /* The grant physically leaves the hold and becomes the colony's
     * treasury — without it the new island could not pay for so much
     * as a road, since every cost is denominated in its own Gold. */
    sh->cargo[RES_GOLD] -= COLONY_FOUNDING_GOLD;

    stockpile_init(&isl->stockpile);
    stockpile_add(&isl->stockpile, RES_GOLD, COLONY_FOUNDING_GOLD);
    isl->settled = 1;
    isl->owner   = player;
    camera_init(&isl->camera, SCREEN_W, SCREEN_H, MAP_COLS, MAP_ROWS);

    sim_log("Colony founded on %s with %d Gold (player %u)",
            isl->name, COLONY_FOUNDING_GOLD, player);
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
static int sim_escrow_put(GameState *gs, int island, ResourceType res, int qty)
{
    Island *isl = &gs->islands[island];

    if (res < 0 || res >= RES_COUNT) return 0;
    if (qty > isl->stockpile.amount[res]) qty = isl->stockpile.amount[res];
    if (qty <= 0) return 0;

    stockpile_add(&isl->stockpile, res, -qty);
    isl->escrow[res] += qty;
    return 1;
}

static int sim_escrow_take(GameState *gs, int island, ResourceType res, int qty)
{
    Island *isl = &gs->islands[island];

    if (res < 0 || res >= RES_COUNT) return 0;
    if (qty > isl->escrow[res]) qty = isl->escrow[res];
    if (res != RES_GOLD) {
        int headroom = isl->stockpile.capacity - isl->stockpile.amount[res];
        if (headroom < 0) headroom = 0;
        if (qty > headroom) qty = headroom;
    }
    if (qty <= 0) return 0;

    isl->escrow[res] -= qty;
    stockpile_add(&isl->stockpile, res, qty);
    return 1;
}

static int sim_set_docking(GameState *gs, int island, int allow)
{
    gs->islands[island].docking_allowed = allow ? 1 : 0;
    return 1;
}

int game_escrow_put(GameState *gs, int island_idx, ResourceType res, int qty)
{
    Command c = {0};
    c.kind = CMD_ESCROW_PUT;
    c.a    = island_idx;
    c.b    = (int32_t)res;
    c.c    = qty;
    return command_submit(gs, &c);
}

int game_escrow_take(GameState *gs, int island_idx, ResourceType res, int qty)
{
    Command c = {0};
    c.kind = CMD_ESCROW_TAKE;
    c.a    = island_idx;
    c.b    = (int32_t)res;
    c.c    = qty;
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
        return sim_sell(gs, c->a, (ResourceType)c->b, c->c);
    case CMD_BUY_RESOURCE:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_buy(gs, c->a, (ResourceType)c->b, c->c);
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
        if (c->d < 0 || c->d >= MAX_ISLANDS) return 0;
        return sim_ship_transfer(gs, c->a, (ResourceType)c->b, c->c, c->d,
                                 c->player_id);
    case CMD_SHIP_DEPART:
        if (!owns_ship(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_ship_depart(gs, c->a, c->b) ? REJ_OK : REJ_UNAVAILABLE;
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
        return sim_escrow_put(gs, c->a, (ResourceType)c->b, c->c) ? REJ_OK : REJ_UNAVAILABLE;
    case CMD_ESCROW_TAKE:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_escrow_take(gs, c->a, (ResourceType)c->b, c->c) ? REJ_OK : REJ_UNAVAILABLE;
    case CMD_SET_DOCKING:
        if (!owns_island(gs, c->a, c->player_id)) return REJ_NOT_OWNER;
        return sim_set_docking(gs, c->a, c->b) ? REJ_OK : REJ_UNAVAILABLE;
    default:
        return REJ_UNAVAILABLE;
    }
}

int sim_apply(GameState *gs, const Command *c)
{
    return sim_apply_reason(gs, c) == REJ_OK;
}
