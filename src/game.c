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

/* The archipelago's fixed make-up. Island 0 is always Saltford, the
 * temperate home island the player starts on; the rest each hold
 * something Saltford lacks, which is what turns colonisation from
 * optional into the way you get hops (and therefore Beer) at all.
 *
 * Names are place names; the MapProfile beside each is the TERRAIN it
 * sits on. The two are deliberately independent — Brinehold is a
 * settlement that happens to occupy highland, the way a real town name
 * says nothing about its geology. */
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

    /* The world's first player is ALWAYS player 1, regardless of who
     * created it: this function is also how load and the F9 verifier
     * rebuild tick 0, so island 0's owner must be a pure function of
     * the seed — never of which client happens to be reconstructing.
     * (game_new/game_new_seeded make the local player id 1 to match;
     * a co-op guest acquires its island via a logged CMD_GRANT_START.) */
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
 * Gold and now means Charcoal.
 *
 * v12 (SUPPLY_CHAIN Phase 5): four more goods, and MAX_ISLANDS 4 -> 8.
 * The second half is the one that matters — a v11 log describes a
 * FOUR-island world, so its island indices, its grants and its
 * voyages all mean something different in an eight-island one.
 *
 * v13 (SUPPLY_CHAIN Phase 6): nine more goods for the Engineers line.
 *
 * v14 (SUPPLY_CHAIN Phase 7): twenty-five more goods, and
 * MAX_TIER_GOODS 5 -> 6. TierDef is not saved, but the resource
 * vocabulary shifts again.
 *
 * v15 (SUPPLY_CHAIN Phase 8): four more goods, and CMD_UPGRADE_HOUSE
 * gained a meaning for `c` — a v14 log's upgrades all carry c = 0,
 * which happens to be the branch they meant, but the field is no
 * longer ignorable.
 *
 * v16 (MARITIME_PLAN Phase 2): the order book. Two new command kinds,
 * and — the part that makes old logs unreplayable rather than merely
 * incomplete — the book matches at every tick boundary, so a v15 log
 * replayed under these rules would still produce a v15 world only by
 * accident. It is also the first hashed state that is not attached to
 * an island or a ship.
 *
 * v17 (MARITIME_PLAN Phase 2, merchants): a booking now holds a
 * merchant and a hull from the selling island for the round trip, and
 * the matcher skips an ask whose island has neither free. A v16 log
 * replays a world where every crossing was possible, so its fills are
 * not this world's fills.
 *
 * v18 (MARITIME_PLAN Phase 2, the market maker): the faction holds the
 * last two islands as home ports from tick 0 and posts standing orders
 * there every FACTION_QUOTE_INTERVAL_TICKS. A v17 log describes a world
 * with two more colonisable islands and no NPC orders in the book, so
 * its colonisations and its fills are both about somewhere else.
 *
 * v19 (MARITIME_PLAN Phase 3): three routes per island pair, and
 * SEA_UNITS_PER_TICK re-fitted from 21 to 26 to hold the pace. The Sea
 * is regenerated rather than saved, which is exactly why this is a log
 * break and not merely a format one: a v18 log replayed against this
 * generator has every voyage arriving on a different tick.
 *
 * v20 and v21 WERE NEVER ISSUED. Phase 3b (route knowledge and charts)
 * and Phase 3c (per-route insurance and shipment raids) both changed
 * what a log means and both should have bumped this; the edits went
 * astray and only NET_PROTO_VERSION and SNAPSHOT_VERSION moved. The
 * numbers are burned rather than reused so that anything that recorded
 * a version in between is not silently reinterpreted, and so the gap
 * stays legible instead of looking like a miscount.
 *
 * v22 (MARITIME_PLAN Phase 3b, 3c and 3d together): charts route a
 * cargo down water a v19 log had no concept of, raided shipments now
 * fail to arrive at all, and the survey mission adds two command kinds
 * with expeditions, research boats and scholars behind them. A v19 log
 * replayed under any of this describes a different world.
 *
 * v23 (MARITIME_PLAN Phase 3e): private passages rotate. Each pair now
 * generates a pool and keeps two in play, and which two changes as the
 * world runs — so a v22 log's cargoes sail water this world is not
 * using.
 *
 * v24 (MARITIME_PLAN Phase 5): ships have a class, guns and a hull, an
 * interception is decided by those rather than a flat coin flip, and
 * CMD_BUILD_SHIP's `b` now names which hull to lay down where it used
 * to be an ignored index. A v23 log's shipyards would build nothing.
 *
 * v25 (MARITIME_PLAN Phase 5b): pirates are entities. A raid now
 * happens because a fleet was in the water a cargo passed through,
 * rather than because a hash of the shipment said so at dispatch — so
 * a v24 log loses different cargoes, at different times, to something
 * that is now somewhere.
 *
 * v26 (UI_PLAN N3): IntentUiState carries the order book's page and the
 * draft order on it, so the intent section's records are a different
 * size. Like v7, this changes nothing about the world a log replays to
 * — intents are cosmetic to the sim — but the bytes no longer line up,
 * and a misread intent stream would hit-test recorded clicks against
 * the wrong screen, which is exactly the failure the format exists to
 * make impossible.
 *
 * v27 (UI_PLAN N4): IntentUiState gains the passages overlay's page,
 * for the same reason and at the same cost as v26 — which page a click
 * landed on decides which rows were under the cursor.
 *
 * v28 (UI_PLAN N6): and the shipyard's, on the same argument.
 *
 * v29 (NEEDS_PLAN Phase 1): PopData records the house type it was
 * upgraded from, because a Scholar's House needs the basics of
 * wherever its people came from. World state — hashed, and written
 * into the checkpoint beside the residents it belongs to.
 *
 * v30 (NEEDS_PLAN Phase 2): the happiness flag became a 0..10 ladder,
 * so the byte that held it is an int now. The ladder is also the
 * buffer that stops one missed tick costing a resident, which is why
 * it had to be a number rather than a bit.
 *
 * v31 (NEEDS_PLAN Phase 3): no field changed shape — HOUSE_CAPACITY
 * did. A world saved at ten residents a house would load into a game
 * whose ceiling is six and quietly shed people until it fit, which is
 * a different world from the one that was saved. Refusing it is
 * cheaper to explain than repairing it.
 *
 * v32 (LIFE_PLAN Phase 1): no field changed shape here either — a
 * workplace holds a crew now, and production advances by the headcount
 * rather than by one. A save is a seed plus an ordered command log, so
 * a world recorded when one agent could claim a whole Fisher's Hut
 * replays into a different world under a rule where five can. The
 * bytes would load; the island they described would not come back.
 *
 * v33 (LIFE_PLAN Phase 2): a full crew is worth more than the sum of
 * its hands — the production clock advances 2w-1 rather than w — so
 * every island in every recorded log produces at a different rate from
 * the one it was recorded at. Same reason as v32, larger effect. */
#define SAVE_VERSION 33u

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

/* ---- interception (MMO_PLAN later phases) ------------------
 * The engagement, resolved from the log. Cargo is the stake: the winner
 * takes what the loser was carrying, up to its own hold's capacity, and
 * a failed attack costs the attacker the same way. Nothing is destroyed
 * that was not aboard, and no ship is ever sunk — losing a hold is a
 * setback, losing a ship would be an evening's work gone. */
/* Take a hull to a pirate lair (MARITIME_PLAN Phase 5b).
 *
 * The same guns-and-hull rule as an interception, because a fight is a
 * fight and having two combat systems would mean having one of them be
 * wrong. What differs is the stake: a pirate has no cargo of its own
 * and everything it holds was taken from somebody, so winning is
 * recovery rather than robbery — often of goods that were never yours,
 * which makes clearing a lair a service to every trader on that water.
 *
 * This is also the only reason to own guns that is not aimed at another
 * player. Phase 5a gave hulls teeth and nothing but neighbours to bite. */
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

    /* The defence is the target's guns plus every escort sailing with
     * them — same owner, same crossing, same tick out of harbour. An
     * escort that has not left port, or left on a different voyage, is
     * not there to help, which is what makes forming a convoy a
     * decision rather than a label. */
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
     * down, and a worn hull fights worse (see intercept_strength).
     * That gives losing a consequence which outlasts the engagement
     * without ever taking the ship — test_intercept has said since it
     * was written that "a hold is a setback, a ship is an evening",
     * and it is right. Sinking a hull somebody spent an evening on is
     * a different game from this one, and PvP that can cost you the
     * evening is PvP most people decline to be in.
     *
     * So the floor is 1 and the repair is a Shipyard: come home, refit,
     * go out again. */
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

    /* A predicting client stops here with the rest of the world
     * (SERVER_AUTHORITY.md Phase 2). Everything below that is not this
     * player's own island belongs to the server: the market, other
     * people's harbours, every ship, the charters. Guessing at them
     * buys nothing — the next push overwrites it — and since Phase 3
     * those islands arrive REDACTED, so a prediction would be running
     * production on an empty harbour and drawing the answer.
     *
     * What is still predicted is exactly what makes the game feel
     * responsive: a command you just issued, applied above, and the
     * pipeline of the island you are looking at. Placement is the verb
     * of a city builder and it cannot wait for a round trip. */
    if (gs->predict_only != 0u) {
        for (i = 0; i < MAX_ISLANDS; i++)
            if (gs->islands[i].owner == gs->predict_only)
                island_update(&gs->islands[i], gs->world_seed, gs->sim_tick_no);
        gs->sim_tick_no++;
        return;
    }

    /* 2. The order book: deliver what has arrived, then match what
     * crosses. Before the islands tick, so goods delivered this tick
     * are available to production this tick rather than next — a
     * shipment that lands is stock, and stock is what a workshop
     * consumes. */
    book_match(gs);

    /* 3. Every settled island's full pipeline, one tick, in order —
     * see island_update()'s ordering constraint. */
    for (i = 0; i < MAX_ISLANDS; i++)
        island_update(&gs->islands[i], gs->world_seed, gs->sim_tick_no);

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
        /* Trade capacity committed (MARITIME Phase 2). Only the "out"
         * counts: the capacity itself is derived from the buildings,
         * which are hashed just below. */
        fnv_bytes(&h, &isl->merchants_out, sizeof(isl->merchants_out));
        fnv_bytes(&h, &isl->hulls_out, sizeof(isl->hulls_out));
        fnv_bytes(&h, &isl->insure_shipments, sizeof(isl->insure_shipments));
        fnv_bytes(&h, &isl->research_boats, sizeof(isl->research_boats));
        fnv_bytes(&h, &isl->research_boats_out, sizeof(isl->research_boats_out));
        fnv_bytes(&h, &isl->scholars_out, sizeof(isl->scholars_out));

        /* Residents (LIFE_PLAN Phase 3). Hashed even though nothing
         * reads them yet: state outside the hash is state the F9
         * self-check and the cross-platform gate cannot see, and the
         * whole point of landing this phase inert is to prove the
         * serialisation before behaviour depends on it. next_resident_id
         * goes in too — it decides what everybody is called, so two
         * worlds that disagree about it are two different worlds.
         *
         * Field by field rather than a struct dump: Resident has
         * padding, and hashing padding is hashing uninitialised bytes,
         * which is stable within one run and different across machines
         * (the exact failure ci/sanitize.sh's MSan pass exists for). */
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
     * diverge from there, so it is hashed like everything else.
     *
     * Live entries only, and — unlike ships above — the dead slots
     * between them contribute NOTHING, not even their `active` flag.
     * That is deliberate and it is what lets a checkpoint compact the
     * book: an order is addressed by id, never by slot, so a book
     * holding one live order in slot 5 is the same world as the same
     * order in slot 0, and the hash has to agree. Hashing a dead slot's
     * flag would make "how many orders have ever been cancelled here"
     * part of the world, and a restore would desync on nothing.
     *
     * The live counts go in explicitly so the order and booking runs
     * cannot be read as each other. */
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
    Command       none, first, second;
    BuildingType  type;
    int           branch[2], n;

    if (building_idx < 0 || building_idx >= isl->building_count) return;
    if (!isl->buildings[building_idx].active) return;

    /* Any house type, not just a Marsh Cottage. This was
     * `type != BUILDING_HOUSE` while Marsh Cottage was the only tier
     * with anywhere to go; SUPPLY_CHAIN Phase 8 gives EVERY house a
     * possible future through the Academy, so the question is whether
     * this is a house at all — which pop_is_house_type answers, and
     * which agents_sync once got wrong the same way. */
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

    /* Both branches cost the same: upgrade_gold belongs to the tier
     * being LEFT, and tier_upgrade_check_def charges it. Deliberately
     * not split per branch — the gate on Scholars is the Academy and
     * four goods, not a bigger number, which is the rule this whole
     * mechanic exists to teach. */
    stockpile_add(&isl->stockpile, RES_GOLD, -tier->upgrade_gold);
    /* Where these people came from, recorded on every upgrade rather
     * than only on the one that reads it: a Scholar's House wants the
     * basics of the house it grew out of, and a field written on one
     * path and read on another is a field that eventually is not
     * written (NEEDS_PLAN Phase 1). */
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

    /* The convoy sails together (MARITIME_PLAN Phase 5). An escort
     * that had to be ordered out separately would be an escort that
     * arrives on a different tick and defends nobody — and it is the
     * DEPARTURE TICK the intercept rule matches on, so "we left
     * together" has to be true in the data and not merely in the
     * player's intention.
     *
     * Escorts are not insured with their charge: a policy is bought
     * per hull, and a warship carrying nothing has nothing to declare. */
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


/* ---- the order book (MARITIME_PLAN Phase 2) -----------------
 * Posting reserves: a sell takes the goods out of the stockpile, a buy
 * takes the gold. Both are held against the order and returned if it
 * is cancelled. Without that, one cargo could be posted into ten books
 * and fill all ten — the same double-spend the harbour escrow exists
 * to prevent between players. */

/* ---- whose purse a trade touches -------------------------
 * A player's goods and gold are in the island's stockpile. The
 * faction's are in the company's single inventory, behind whichever of
 * its harbours the order was posted at — see faction.h on why one stock
 * across several ports is safe: posting reserves. And a ROUTE CHART is
 * in neither: it belongs to the player, not to a harbour, because
 * losing a colony does not make you forget the sea (knowledge.h).
 *
 * Everything that moves value in the book goes through these two, so
 * the reserve, the refund and the settlement cannot end up disagreeing
 * about where a counterparty keeps its money — which is exactly the
 * kind of disagreement that mints or destroys goods. */
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

    /* The per-player cap does not apply to the market maker: it is what
     * stops one trader crowding the book, and the faction's quoting is
     * already bounded by construction (ports x goods x two sides, all
     * tracked in quote_order[] and withdrawn before each refresh). The
     * global ORDERBOOK_MAX_ORDERS still holds for everyone. */
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

/* ---- expeditions (MARITIME_PLAN Phase 3d) -----------------
 * A survey commits a scholar, a research boat and a blank chart, and
 * comes back with a passage or with nothing. See survey.h for why the
 * blank chart is spent either way and why the crew is at risk.
 */
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

/* ---- the sea changes shape (MARITIME_PLAN Phase 3e) -------
 * Charts expire. A private passage stays in play for
 * SEA_ROUTE_LIFETIME_TICKS and then goes out of use: the water silts
 * up, the reef shifts, the pilots who knew it die. A fresh passage from
 * the pair's pool comes in behind it, and every chart of the old one
 * becomes waste paper.
 *
 * This is what stops the map from ever being solved. Without it a
 * player who surveyed every crossing once would be permanently faster
 * than everyone who came later, and the Chart House would be a
 * building you use once.
 *
 * Pairs rotate on their OWN offsets rather than together, so the sea
 * shifts continuously instead of invalidating every chart in the world
 * on the same tick. The stagger itself lives in sea.c, because the
 * charts screen has to count down to the same instant this rotates on
 * (UI_PLAN N4) and two copies of a schedule is how a clock comes to
 * disagree with the event it is timing.
 */
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

/* ---- choosing a passage (MARITIME_PLAN Phase 3b) ----------
 * A booking sails the fastest route its SELLER can actually use: one
 * they know exists and hold a chart for. The seller chooses because
 * the seller dispatches — the merchant, the hull and the cargo all
 * leave from their harbour, and a buyer cannot lend a map to a crew
 * they never meet.
 *
 * The public lane is always available and needs no chart, so this can
 * never fail to find a route. Ties go to the lower route id, which is
 * generation order and therefore the same on every client.
 *
 * The chart is RESERVED here rather than spent on arrival, the same
 * discipline as goods and gold: a map that were only spent at the end
 * of the voyage could send out ten cargoes on one chart.
 */
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

/* ---- the faction as market maker (MARITIME_PLAN Phase 2) ----
 * Every FACTION_QUOTE_INTERVAL_TICKS the market withdraws its standing
 * orders and posts fresh ones at its current bid and ask, at each of
 * its home ports. Its quotes therefore appear in the book as ordinary
 * orders: they reserve, they ship, they tie up its hulls, and a player
 * can trade inside its spread with someone else instead.
 *
 * It is a pure function of world state, so it needs no Command — the
 * same reasoning as faction_tick's mean reversion. A replay re-derives
 * every quote it ever posted.
 *
 * WHICH goods it quotes is economic rather than arbitrary: the ones it
 * is furthest from baseline on, which is where it most wants to trade.
 * It cannot quote everything (two sides x every good x every port would
 * exhaust the book by itself), and a market maker that leans against
 * its own imbalance is a better answer than a rotation. */
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

    /* Chart offers, withdrawn and re-posted with the rest. They rotate
     * through the private routes rather than offering all of them at
     * once: the market has a few maps on the counter this week, not an
     * atlas. The cursor advances every refresh, so every passage comes
     * up eventually and no player is permanently locked out of one. */
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

/* ---- matching -----------------------------------------------
 * Runs once per tick. Deterministic by construction: candidates are
 * chosen by best price, then earliest placed, then lowest id, and ids
 * are assigned in command-log order — so a replay fills exactly the
 * trades the original run filled, in the same sequence.
 *
 * A fill is not a transfer. The goods are already out of the seller's
 * stockpile and the gold out of the buyer's; what a match creates is a
 * Booking, and the goods arrive when the water between the two
 * harbours has been crossed. */
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

/* Choose the pair to fill for one good, or return 0 if nothing can.
 *
 * A player may not trade with themselves — but refusing that pair is
 * not the same as refusing the good. If the top of book is one player
 * on both sides and the matcher simply stopped there, that player
 * could shut every other trader out of a resource for as long as they
 * cared to leave the pair standing, at no cost, by posting a bid and
 * an ask nobody would ever take. So a self-crossing top of book steps
 * aside rather than ending the pass.
 *
 * It steps aside in one move, not a search. With B the best bid and A
 * the best ask, both owned by p: any crossing pair with two different
 * owners has either a bid not owned by p — and then the best such bid
 * also crosses A, because A is the cheapest ask of all — or an ask not
 * owned by p, and then that ask's best case is B. Checking (B2, A) and
 * (B, A2) is therefore exhaustive, and the common path stays a single
 * linear pass.
 */
static int book_best_cross(const GameState *gs, OrderBook *b, TradeId what,
                           Order **out_bid, Order **out_ask)
{
    Order *bid = NULL, *ask = NULL, *bid2 = NULL, *ask2 = NULL;
    int    i;

    /* An ask whose island has no merchant or no hull free cannot carry
     * a cargo this tick, so it is not in the book this tick. Skipping
     * it rather than stalling on it is the same rule the self-crossing
     * case above needs, and for the same reason: one seller at the top
     * of the book, out of hulls, must not stop everyone else trading
     * the good. It becomes eligible again when one of its merchants
     * gets home. */
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
 * place and a time rather than being decided before the ship left.
 *
 * The fleet KEEPS what it takes. That is the whole reason this is worth
 * more than the boolean it replaces: the goods are somewhere, and going
 * to get them is a thing a player can decide to do. */
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

        /* The lane is patrolled, and that is the whole of "public are
         * slow but protected". Without this the property would have
         * been lost with the chance constant it used to live in — and
         * lost the wrong way round, because the lane threads a wider
         * waypoint than any private passage and is therefore MORE
         * exposed on geography alone.
         *
         * Derived, like everything else here, so a replay agrees about
         * which convoys the escort happened to be with. */
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

        /* Outbound. A raided shipment lands nothing: the goods are
         * gone with the pirates, and the buyer — who paid at posting
         * and did not choose the passage — gets their gold back. The
         * seller dispatched and the seller bears it, which is what
         * makes insurance worth buying and what makes choosing the
         * fast passage a decision rather than a free upgrade. */
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

    pirate_update(&gs->pirates, &gs->sea, gs->sim_tick_no, gs->world_seed);
    book_raid_check(gs);
    book_settle_arrivals(gs);
    sea_rotation_update(gs);
    surveys_update(gs);
    faction_quote_refresh(gs);

    /* Both kinds, each over its own id space: resources over
     * RES_COUNT, charts over the routes that exist. The matcher itself
     * never learned the difference — it compares (kind, id) — so this
     * loop is the only place that knows there is more than one. */
    for (kind = 0; kind < TRADE_KIND_COUNT; kind++) {
      int id_count = (kind == TRADE_RESOURCE) ? RES_COUNT
                                              : gs->sea.route_count;
      for (id = 0; id < id_count; id++) {
        TradeId what;

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

            /* Whether pirates take it is no longer decided here
             * (MARITIME_PLAN Phase 5b). It used to be a hash of the
             * shipment's identity, fixed at dispatch; now a fleet
             * either is or is not lying in the water the cargo will
             * pass through, and book_raid_check finds out on the tick
             * it happens. Your cargo is taken because of where it
             * sailed, which is a thing a player can learn.
             *
             * The insurance below is still bought at dispatch, because
             * an underwriter is paid before the voyage or not at all. */
            {
                Booking *bk    = &b->booking[slot];
                Island  *from  = &gs->islands[ask->island];
                int      value = qty * bk->price;

                /* A standing policy insures at the route's premium, so
                 * the fast passage costs more to cover than the lane —
                 * which is the whole point of pricing risk per route
                 * rather than per pair of islands. The premium is paid
                 * at dispatch, out of the seller's own purse. */
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
             * price can only be cheaper, and the difference goes back.
             *
             * The reserve therefore falls by the LIMIT, not by the
             * price: the limit is what those units were holding, and
             * the gap between the two has just been handed back below.
             * Decrementing by the price instead leaves the difference
             * sitting in the reserve as well as in the stockpile, and
             * cancelling the remainder pays it out a second time —
             * gold minted by part-filling an order and withdrawing. */
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
    /* Nobody commands the market (MARITIME_PLAN Phase 2). The faction
     * acts inside the tick, never through the log, so a command
     * claiming its identity did not come from the sim — it came from a
     * peer that made one up. Refusing it here rather than in each
     * handler means a kind added later cannot forget to. */
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
