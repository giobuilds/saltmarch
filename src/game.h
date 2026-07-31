#ifndef GAME_H
#define GAME_H

/* =========================================================
 * game.h  --  Top-level game state
 *
 * GameState owns the archipelago plus everything genuinely global:
 * input, frame timing, which island is being viewed, and the UI
 * overlay flags. A single pointer to GameState is stored in SDL's
 * appstate so all three callbacks (AppInit, AppEvent, AppIterate)
 * can reach it.
 *
 * Everything that is per-landmass — map, camera, stockpile,
 * buildings, population, agents — lives in Island (island.h). The
 * game logic here operates on the CURRENT island via cur()/
 * game_cur_island(); the per-island simulation itself is
 * island_update(), which runs for every settled island each frame,
 * not just the one on screen.
 *
 * This header is SDL-free (MMO_PLAN Phase 6): GameState and the sim
 * entry points below compile into libsaltmarch_sim, which the headless
 * replay tool and the future server host link WITHOUT SDL. The
 * SDL-facing client half — the per-frame camera/hover update and event
 * handling — lives in client.h.
 * ========================================================= */

#include <stdint.h>
#include <stddef.h>
#include "map.h"
#include "camera.h"      /* also provides SCREEN_W / SCREEN_H */
#include "input.h"
#include "building.h"
#include "resource.h"
#include "population.h"
#include "agent.h"
#include "island.h"
#include "ship.h"
#include "command.h"
#include "faction.h"
#include "sea.h"
#include "orderbook.h"
#include "knowledge.h"
#include "survey.h"
#include "pirate.h"
#include "intent.h"

/* Gold a new game's starting island begins with. */
#define STARTING_GOLD 1000

/* Player identity (MMO_PLAN Phase 5). PLAYER_NONE marks an unowned
 * island; real players count from 1. Single player is always player 1;
 * a co-op guest gets its id from the host at join. */
#define PLAYER_NONE 0u
/* PLAYER_FACTION lives in island.h, beside the `owner` field that
 * holds it — the NPC market owns islands, so the constant has to be
 * visible to code that only knows about islands. */

/* Fixed-timestep clock constants (SIM_TICK_MS, SIM_TICK_NS, ...). */
#include "simclock.h"

/* What a pending confirmation is: the command it would submit, the
 * alternative if the action offers one, and which is selected. Kept in
 * GameState beside the other overlay flags — client state, never
 * hashed, never saved. */
typedef enum {
    CONFIRM_NONE = 0,
    CONFIRM_BUILD,
    CONFIRM_DEMOLISH,
    CONFIRM_UPGRADE,
    CONFIRM_SHIP
} ConfirmKind;

typedef struct {
    int         open;
    ConfirmKind kind;
    Command     cmd;      /* what "confirm" submits                     */
    Command     alt;      /* the other payment option; kind CMD_COUNT
                           * when the action has only one              */
    int         chosen;   /* 0 = cmd, 1 = alt                          */
} ConfirmState;

/* ---- what happened to my command (UI_PLAN M1) --------------
 * Every command applied at a tick boundary leaves one of these behind.
 * The UI drains them each frame and matches them against its pending
 * ring by (player_id, seq); anything it does not recognise — a replayed
 * command, another player's — has no pending entry and is silently
 * dropped, which is how feedback stays local without special-casing.
 *
 * A small ring: results older than a few frames are of no use to
 * anybody, and dropping the oldest is better than growing forever. */
#define SIM_RESULT_RING 32

typedef struct {
    uint32_t     seq;
    uint32_t     player_id;
    uint64_t     tick;
    CommandKind  kind;
    RejectReason reason;
} SimResult;


/* Tagged (rather than an anonymous typedef) so the net hooks below can
 * name the type from inside the struct that owns them. */
typedef struct GameState {
    /* ---- The archipelago ----------------------------------
     * Every island exists from world-gen; `settled` (island.h)
     * decides which are simulated and buildable. current_island is
     * the one being viewed and the one all placement/UI actions
     * apply to — see cur() in game.c and game_set_current_island(). */
    Island islands[MAX_ISLANDS];
    int    current_island;

    InputState input;

    /* Tile currently under the mouse cursor on the CURRENT island
     * (-1 if none). Global rather than per-island: it describes where
     * the pointer is right now, not a property of a landmass. */
    int hovered_row;
    int hovered_col;

    /* Time tracking for frame-rate-independent movement.
    * last_tick  – client timestamp (ns) at the end of the previous frame,
    *              0 before the first frame (client_update seeds it).
    * delta_time – seconds elapsed since that frame (e.g. 0.016 at 60fps).
    * All per-frame movement is multiplied by delta_time so the game
    * behaves identically at 30, 60, or 144 fps. */
    uint64_t last_tick;
    float delta_time;

    /* Which building type the player has selected from the HUD.
     * BUILDING_NONE (-1) means nothing selected. */
    BuildingType selected_building;

    /* 1 if the current hover position is a valid placement spot
     * for selected_building.  Used by render to colour the ghost.
     * placement_reason is WHY not (a RejectReason; REJ_OK when valid) —
     * the same vocabulary sim_apply rejects with, so the message under
     * the cursor is definitionally the reason placement would fail
     * rather than a client-side guess (UI_PLAN decision 3). Both are
     * client-side view state: recomputed every frame from the hover,
     * never hashed, never saved. */
    int placement_valid;
    int placement_reason;

    int menu_open;  /* 1 when the cog menu overlay is open */

    /* Manual trade screen. trade_open mirrors menu_open's overlay
     * pattern; trade_building_idx indexes the CURRENT island's
     * buildings[] — every *_idx below is current-island-relative,
     * which is safe because game_set_current_island() closes every
     * overlay rather than trying to keep them alive across a switch. */
    int trade_open;
    int trade_building_idx;

    /* ---- the confirmation popup (UI_PLAN Phase 6) ---------
     * One popup where there were four (build, demolish, tier upgrade,
     * ship build). It holds the COMMAND it would submit rather than the
     * ingredients to build one later, which is what lets it render the
     * literal thing sim_apply will receive.
     *
     * That also preserves the property the build popup was careful
     * about: the command is built when the popup OPENS, from the tile
     * that was clicked, not re-derived at confirm time — by then the
     * cursor is over a button in a different screen region and would
     * resolve to an unrelated tile.
     *
     * `alt` is the second option where one exists (paying Gold instead
     * of goods); its kind is CMD_COUNT when there is none. */
    ConfirmState confirm;

    /* Road drag-placement: the last tile this drag already placed
     * at, so holding the button over one tile doesn't re-place every
     * frame. Reset to -1 whenever the button isn't held, so a new
     * drag's first tile is never skipped as "unchanged". */
    int drag_last_row, drag_last_col;

    /* Demolish tool: 1 while active (mutually exclusive with
     * selected_building — selecting either clears the other).
     * Clicking a building while active opens the demolish-
     * confirmation popup below rather than destroying immediately. */
    int demolish_mode;



    /* Archipelago overview overlay (world_ui.c). Unlike the other
     * overlays this one is not tied to any building, so it survives
     * game_set_current_island() -- clicking an island in it switches
     * the view and keeps the map open, which is the whole point. */
    int world_open;

    /* Ships are world-scoped, not per-island: one in transit belongs
     * to neither end of its voyage (see ship.h). */
    Ship ships[MAX_SHIPS];
    int  ship_count;

    /* Which ship the world overlay has selected, or -1. Selecting a
     * ship then clicking an island is how a voyage is ordered —
     * the same select-then-click grammar as the HUD's
     * pick-a-building-then-click-a-tile. */
    int  world_selected_ship;


    /* Market debug overlay toggle (F10) — cosmetic, not sim state. */
    int  faction_debug;

    /* Inventory overlay (UI_PLAN Phase 4): the full goods list, paged.
     * The corner panel shows what fits; this shows everything. */
    int  inventory_open;

    /* Harbor escrow panel (Phase 5): open for the CURRENT island's
     * harbor. Owner-gated at open time; current-island-relative like
     * every other overlay, so island switches close it. */
    int  escrow_open;

    /* The order book (UI_PLAN N3): your resting orders at the CURRENT
     * island, and the strip that composes a new one. Like the others,
     * a flag here and nothing else — the draft itself is UiState, on
     * the client, because a half-written order is not world state. */
    int  book_open;

    /* The passages overlay (UI_PLAN N4): the routes out of the CURRENT
     * island, what this player knows of them, and what a map of one
     * costs on the book. Its rows are retained like the book's, so the
     * view lives on the client beside them and only the flag is here. */
    int  charts_open;

    /* The shipyard (UI_PLAN N6): the hulls on offer and the fleet
     * already afloat. Rebuilt from the snapshot each frame, so only
     * the flag lives here. */
    int  yard_open;

    /* ---- The command funnel (MMO_PLAN Phase 1a) -----------
     * Every world mutation is recorded here as a Command, in the order
     * it was applied, and re-running the log from the world seed
     * reproduces the world exactly. cmd_log is a grow-by-doubling heap
     * array owned by GameState (freed in game_free via
     * command_log_free). sim_tick_no is the world clock; today it is a
     * plain frame counter, but Phase 1b makes it the authoritative
     * fixed-timestep tick number. See command.h. */
    Command  *cmd_log;
    int       cmd_count;
    int       cmd_cap;      /* allocated capacity of cmd_log            */
    int       cmd_applied;  /* commands already applied by the sim;
                             * cmd_log[cmd_applied..cmd_count) are still
                             * pending, waiting for their tick boundary  */
    uint64_t  sim_tick_no;  /* completed ticks; the tick about to run    */
    uint64_t  sim_acc_ns;   /* real-time accumulator feeding the tick
                             * loop — the ONLY wall clock the sim sees    */

    /* Client-local command sequence, and the results of commands as
     * they apply (UI_PLAN M1). Neither is world state: the sequence is
     * per-machine and the ring is drained by the UI. Not hashed, not
     * saved. */
    /* The recorded input stream (UI_PLAN M1). Written beside the
     * command log by game_save, replayed by the CI UI harness, ignored
     * by the sim entirely — a world is still a pure function of (seed,
     * commands). Recording is opt-in: only a client that calls
     * intent_record() produces any. */
    Intent   *intent_log;
    int       intent_count;
    int       intent_cap;

    uint32_t  cmd_seq_next;
    uint32_t  cmd_seq_last;   /* what the most recent submit stamped —
                               * how the UI learns which sequence to
                               * expect an answer for                  */
    SimResult results[SIM_RESULT_RING];
    int       result_count;

    /* The NPC market counterparty (Phase 3). World sim state: hashed,
     * mutated only in sim_apply (trades) and sim_run_one_tick
     * (reversion). One faction serves every island's marketplace. */
    Faction   faction;

    /* The water between the islands (MARITIME_PLAN Phase 1). A pure
     * function of world_seed, like every Map — regenerated by
     * game_reset_world and by snapshot_decode, never saved and never
     * hashed, because there is nothing about it a checkpoint could
     * disagree with. */
    Sea       sea;

    /* The order book (MARITIME_PLAN Phase 2). World state: hashed,
     * replayed, snapshotted. Matching runs at tick boundaries so a
     * replay fills exactly the trades the original run filled. */
    OrderBook book;

    /* What each player knows of the sea, and the charts they hold
     * (MARITIME_PLAN Phase 3b). The first state in this struct that
     * belongs to a PLAYER rather than to a place or a thing — a player
     * who loses their colony does not forget the passages. World state:
     * hashed, snapshotted, replayed, because which route a booking
     * takes depends on what its seller knows. */
    Knowledge knowledge;

    /* Expeditions in progress (MARITIME_PLAN Phase 3d). World state:
     * hashed, snapshotted, replayed. Each one has a scholar, a boat
     * and a blank chart committed to it, and an outcome fixed when it
     * sailed. */
    SurveyBoard surveys;

    /* The fleets working the waypoints (MARITIME_PLAN Phase 5b). Where
     * they lurk is generated from the seed; what they have taken, what
     * damage they carry and whether they are still afloat is world
     * state — hashed, snapshotted, replayed. */
    PirateSea pirates;

    /* Who this client is (Phase 5). CLIENT state, not world state: it
     * is never hashed and never saved — it says which player's commands
     * this process emits (command_submit stamps it), not anything about
     * the world. 1 in single player; a co-op guest is assigned its id
     * by the host at join and keeps it after a disconnect. */
    uint32_t  local_player_id;

    /* Predict only this player's own islands (SERVER_AUTHORITY.md
     * Phase 2). 0 means "simulate the whole world", which is what
     * offline play, the dedicated server, every replay and every test
     * do — and what this field must stay at for all of them.
     *
     * When a server is the authority the client is not computing the
     * world, it is GUESSING AHEAD of the next push. Guessing about
     * somebody else's island is wasted work at best and visible
     * nonsense at worst, because since Phase 3 those islands arrive
     * redacted: a client that ran production on them would be
     * simulating an empty harbour and drawing the result.
     *
     * CLIENT STATE, NOT WORLD STATE. Never hashed, never saved, never
     * snapshotted, and set in exactly one place — client.c, from
     * net_server_authoritative(). If this is ever non-zero on the
     * server or in a replay, the sim stops being a pure function of
     * (seed, log) and every guarantee in this codebase built on that
     * sentence goes with it. */
    uint32_t  predict_only;

    /* The lockstep co-op session, or NULL offline (Phase 5). Client
     * infrastructure like local_player_id: owned by App (main.c),
     * referenced here only so command_submit can route submissions and
     * the tick loop can respect the guest's authorisation horizon.
     * Never hashed, never saved.
     *
     * net_submit is how the sim reaches a session without knowing what
     * one is (MMO_PLAN Phase 6): net.c is a CLIENT file, so a direct
     * call to net_submit_local() from command.c would drag the whole
     * transport into the headless sim library. net_attach() installs
     * the hook; it is NULL whenever `net` is, and the sim treats both
     * cases identically. (The tick gate needs no hook — the tick pump
     * itself is client-side, in client.c.) */
    struct NetSession *net;
    int (*net_submit)(struct NetSession *ns, struct GameState *gs,
                      const Command *c);

    /* World seed the whole archipelago is generated from. Stored so the
     * F9 self-check (and, in Phase 1d, load) can rebuild the tick-0
     * world and replay the log against it. */
    uint32_t  world_seed;

    /* ---- the scrubber (MMO_PLAN later phases) --------------
     * A world is (seed, ordered log), so any past tick is reachable by
     * re-simulating to it. While scrubbing, `scrub_live_tick` remembers
     * where the world actually is, the sim does not advance, and
     * command_submit refuses everything — acting in the past would
     * append commands stamped behind the log's own head and corrupt the
     * one thing the whole architecture rests on. */
    int       scrub_active;
    uint64_t  scrub_live_tick;

    /* The earliest tick this process can still rebuild (SERVER.md,
     * "Log truncation"). Zero for a world that still has its whole
     * history; the checkpoint tick for one restored from a snapshot or
     * whose log has been truncated behind it.
     *
     * Everything that reconstructs a past tick does so by replaying
     * from the beginning, so below this line there is no beginning to
     * replay from — and replaying the surviving tail against a fresh
     * seed would silently produce a DIFFERENT world rather than fail.
     * The scrubber clamps to it; F9 stands down above it. */
    uint64_t  history_floor_tick;

    /* The snapshot the floor stands on, kept so the scrubber still
     * works INSIDE the retained window: rebuilding a tick above the
     * floor means decoding this and replaying the surviving tail,
     * rather than replaying from a seed whose history is gone. Owned
     * here, freed by game_free, NULL whenever history_floor_tick is 0. */
    unsigned char *floor_snap;
    size_t         floor_snap_len;

    /* ---- F9 determinism self-check (MMO_PLAN Phase 1c) ----
     * replay_valid is 1 while the live world is exactly what replaying
     * (world_seed, cmd_log) from tick 0 produces — true after
     * game_new/init and normal play, false after a full-state load
     * (whose world is not derived from the log; Phase 1d makes load a
     * replay and restores this). The rest hold the last check's result
     * for the HUD; replay_show_until_ns is a wall-clock draw deadline,
     * the one cosmetic field here. */
    int       replay_valid;
    int       replay_state;   /* 0 none, 1 pass, 2 desync, 3 n/a         */
    uint64_t  replay_live_hash;
    uint64_t  replay_replay_hash;
    uint64_t  replay_tick;
    uint64_t  replay_show_until_ns;
} GameState;

/* ---- The command funnel ---------------------------------
 * command_submit() is the ONE entry point for changing world state: it
 * stamps the command with the current tick, appends it to the log, and
 * applies it via sim_apply(). Returns 1 if the command mutated state,
 * 0 if it was rejected as invalid (a replayed log must reject the same
 * commands identically each time, so rejection is not an error — it is
 * part of the deterministic result).
 *
 * sim_apply() is the sole dispatcher from a Command to the actual
 * mutation. It lives in game.c beside the per-kind mutators it calls,
 * and is the only place those mutators are invoked from. It never
 * appends to the log itself (so replay can call it directly without
 * doubling the log). */
int  command_submit(GameState *gs, const Command *c);
int  sim_apply(GameState *gs, const Command *c);

/* The same dispatch, reporting WHY rather than just whether (UI_PLAN
 * decision 3). sim_apply() is the boolean form, kept because REJ_OK is
 * 0 and mechanically converting its call sites would have inverted
 * every one of them.
 *
 * There is deliberately no second validator: the reasons come from the
 * mutators themselves, so the message a player sees is the reason the
 * sim refused rather than a client-side guess that can drift. */
RejectReason sim_apply_reason(GameState *gs, const Command *c);

/* Copy out (and clear) everything recorded since the last drain.
 * Returns how many were written, at most `max`. */
int sim_results_drain(GameState *gs, SimResult *out, int max);

/* Advance the world by exactly one fixed tick: apply every command
 * stamped for this tick (in log order), run each settled island's full
 * pipeline and every voyage for one tick, then increment sim_tick_no.
 * This is the sole path by which simulated time moves — game_update's
 * accumulator calls it zero or more times per frame, and replay/F9
 * (Phase 1c) call it to reconstruct the world from the log. */
void sim_run_one_tick(GameState *gs);

/* Canonical hash of the simulated world state (MMO_PLAN Phase 1c):
 * FNV-1a over sim_tick_no, then per island its stockpile and every
 * active building/PopData, then every active ship. Deliberately
 * EXCLUDES derived and cosmetic state — agents, cameras, UI flags — so
 * two runs that agree on the world proper hash equal even if their
 * agent floats or view differ. Two GameStates with the same hash have
 * the same world. */
uint64_t sim_hash(const GameState *gs);

/* The F9 self-check: rebuild a scratch world from world_seed, replay
 * the command log through sim_run_one_tick up to gs->sim_tick_no, and
 * compare sim_hash() against the live world. Returns 1 if they match
 * (deterministic), 0 if they diverge (a real bug — a mutation escaped
 * the funnel, a float leaked into the sim, or RNG was stepped outside
 * it). Fills gs->replay_* with the result for the HUD. When
 * replay_valid is 0 (e.g. just after loading a full-state save) it does
 * no work and reports state 3 = n/a. */
int game_verify_determinism(GameState *gs);

/* Replace the command log with a copy of `n` commands from `cmds`,
 * growing the allocation as needed, and reset cmd_applied to 0 so the
 * whole log is pending re-application. Used by game_load()/replay to
 * install a log read from disk. Returns 1 on success, 0 on OOM (the log
 * is left unchanged on failure). */
int  command_log_set(GameState *gs, const Command *cmds, int n);

/* Append one ALREADY-STAMPED command to the log without applying it —
 * the guest's half of lockstep: authoritative commands arrive from the
 * host stamped for a future tick, and sim_run_one_tick applies them
 * when the clock gets there. Returns 1 on success, 0 on OOM. */
int  command_log_append(GameState *gs, const Command *c);

/* Rebuild the world as (seed, log, tick) — regenerate from the seed,
 * install the log, replay to `tick`. Exactly what game_load does after
 * parsing its file; public so the net layer can install a world
 * received over the wire (join and resync). Marks replay_valid. */
int  game_install_world(GameState *gs, uint32_t seed, uint64_t tick,
                        const Command *cmds, int n);

/* Free the command log. Called by game_free(); safe on an empty log. */
void command_log_free(GameState *gs);

/* Append one recorded click. Grows by doubling like the command log;
 * returns 1 on success, 0 on OOM (a dropped intent costs a test case,
 * never correctness). */
int  intent_record(GameState *gs, const Intent *in);

/* Replace the intent log wholesale — how game_load installs what it
 * read. Returns 1 on success, 0 on OOM. */
int  intent_log_set(GameState *gs, const Intent *ins, int n);

void intent_log_free(GameState *gs);

/* ---- the overlay arbiter (UI_PLAN Phase 4) -----------------
 * Which overlay is on top, or UI_OVERLAY_NONE. Every "is anything open?"
 * question routes through here rather than each caller re-listing the
 * flags — that list was already wrong once: the mouse wheel zoomed the
 * world behind an open modal because the zoom code did not consult it
 * at all.
 *
 * The order below is the layering order the click cascade in main.c
 * uses, so the two cannot disagree about what "topmost" means. */
typedef enum {
    UI_OVERLAY_NONE = 0,
    UI_OVERLAY_MENU,
    UI_OVERLAY_CONFIRM,     /* one popup, four actions (UI_PLAN Phase 6) */
    UI_OVERLAY_TRADE,
    UI_OVERLAY_ESCROW,
    UI_OVERLAY_INVENTORY,
    UI_OVERLAY_WORLD,
    UI_OVERLAY_BOOK,        /* the order book (UI_PLAN N3)             */
    UI_OVERLAY_CHARTS,      /* the passages (UI_PLAN N4)               */
    UI_OVERLAY_YARD         /* the shipyard (UI_PLAN N6)               */
} GameOverlay;

GameOverlay game_topmost_overlay(const GameState *gs);

/* 1 if any overlay is open — the common question, asked by the camera
 * (do not zoom), the hover logic (do not highlight tiles) and the
 * drag-placement loop (do not lay road under a popup). */
int game_overlay_open(const GameState *gs);

/* ---- the time-travel scrubber (MMO_PLAN later phases) ------
 * Enter scrub mode (remembering the live tick), jump to any past tick,
 * and leave again (returning to the live tick). Jumping re-simulates
 * from tick 0 through the existing log, which at a 64x64 grid costs
 * milliseconds per thousand ticks — no checkpoint machinery needed yet.
 *
 * The log is never truncated: scrubbing back and then forward again
 * lands on the same state, because the commands were always there.
 *
 * While scrubbing, the sim is frozen and submissions are refused. The
 * UI can be driven as normal — hit-testing a past screen works, because
 * an overlay only ever reads a snapshot — which is what makes this a
 * debugging tool rather than a screenshot. */
void game_scrub_begin(GameState *gs);
void game_scrub_to(GameState *gs, uint64_t tick);
void game_scrub_end(GameState *gs);

/* 1 while viewing the past. Callers that advance time or submit
 * commands must check it. */
int  game_scrubbing(const GameState *gs);

/* The furthest tick the scrubber can reach — the live world's tick. */
uint64_t game_scrub_max(const GameState *gs);

/* The earliest tick the scrubber can reach: 0 normally, the checkpoint
 * tick once history below it has been discarded. */
uint64_t game_scrub_min(const GameState *gs);

/* Install a world from a snapshot plus the commands that follow it,
 * running forward to `tick`. The snapshot-based counterpart of
 * game_install_world, and how a client joins a server whose history has
 * been truncated. Records the snapshot as this world's history floor. */
int game_install_from_snapshot(GameState *gs, const unsigned char *snap,
                               size_t snap_len, uint64_t tick,
                               const Command *cmds, int n);

/* Adopt (a copy of) `buf` as the world's history floor, or drop it. */
int  game_set_history_floor(GameState *gs, const unsigned char *buf,
                            size_t len, uint64_t tick);
void game_clear_history_floor(GameState *gs);

/* Drop every command already applied, keeping only the pending tail,
 * and record that history below the current tick is gone. This is what
 * bounds a persistent server's log and checkpoint; pair it with
 * game_save_checkpoint, which writes the state the dropped commands
 * had produced. Costs the scrubber and F9 their reach below this tick
 * — see history_floor_tick. */
void game_truncate_log(GameState *gs);

/* The island currently being viewed — the one every placement, UI
 * action and *_idx field in GameState refers to. Never NULL:
 * current_island is always a valid index. */
Island *game_cur_island(GameState *gs);

/* Switch the viewed island. Closes every overlay and clears
 * selected_building / demolish_mode / the road-drag state, because
 * all of those (and every *_idx field) are current-island-relative —
 * keeping a popup alive across a switch would leave it pointing at an
 * unrelated building on the new island. No-op if idx is out of range.
 * Switching to an unsettled island is allowed: you can look at an
 * island before you can build on it. */
void game_set_current_island(GameState *gs, int idx);

/* Allocate and initialise a new GameState.
 * Returns NULL on allocation failure. */
GameState *game_init(void);

/* Free a GameState allocated by game_init(). */
void game_free(GameState *gs);

/* Reset gs to a freshly generated world: new map seed, buildings,
 * population and stockpile all cleared. Input/timing state is left
 * untouched. Used by the "New Game" menu button. */
void game_new(GameState *gs);

/* Like game_new(), but with an explicit world seed rather than a
 * time-based one — a deterministic new game. Used by tests and the
 * --record CLI so a session can be reproduced exactly. */
void game_new_seeded(GameState *gs, uint32_t seed);

/* Write the world as HISTORY: (seed, full command log, tick). Loading
 * one re-derives the world by replaying it, which is why this is what
 * fixtures and the determinism gate use — the replay is the proof.
 * Returns 1 on success. Used by the "Save" menu button. */
int  game_save(const GameState *gs, const char *path);

/* Write the world as STATE: a full snapshot plus only the commands not
 * yet applied. Loading one restores rather than replays, so its cost is
 * the size of the world instead of its age — which is what lets a
 * server that has been up for months restart, and a client join it,
 * without walking every tick since the beginning (SERVER.md, "Log
 * truncation"). Returns 1 on success.
 *
 * The trade is deliberate and it is not free: a checkpoint cannot prove
 * its world was reachable by legal play, only that it was stored
 * faithfully, and a world loaded from one cannot be scrubbed or F9'd
 * back past the checkpoint because the history is gone. Prefer
 * game_save() anywhere that history is affordable. */
int  game_save_checkpoint(const GameState *gs, const char *path);

/* Inverse of game_save(): restores buildings, population,
 * stockpile and camera from `path`, regenerating the map from
 * its stored seed. Returns 1 on success; on failure (missing,
 * corrupt, or wrong-version file) returns 0 and leaves gs
 * untouched. Used by the "Load" menu button. */
int  game_load(GameState *gs, const char *path);

#define SAVE_FILE_PATH "saltmarch_save.dat"

/* Read just the command log out of a .smlog, without touching the
 * current world. The caller owns *out_cmds and must free() it. Returns
 * 1 on success, 0 on a missing, corrupt or wrong-version file.
 *
 * Exists for ghost factions (MMO_PLAN later phases): seeding an NPC
 * island means replaying somebody else's recorded commands, which means
 * reading their log without becoming their world. */
int game_load_commands(const char *path, Command **out_cmds, int *out_count);

/* The per-frame client update (camera, hover, drag input, and the
 * accumulator that spends real time on fixed sim ticks) lives in
 * client.h/client.c — it needs SDL, so it cannot live here. */

/* Attempts to place a Road at (row, col) directly — no confirmation
 * popup. Roads are the one building type exempt from
 * game_place_building_confirmed()'s popup: a drag gesture placing
 * many tiles can't reasonably pop up a per-tile confirmation, and a
 * single non-dragged click on Road behaves the same way for
 * consistency. Checks placement validity and affordability itself;
 * returns 1 on success, 0 otherwise (no side effects on failure). */
int game_try_place_road(GameState *gs, int row, int col);

/* Place `type` at (row, col) on the current island, paying in goods or
 * in Gold. Builds the Command and submits it; the sim validates
 * everything, so an unaffordable placement is a rejected command rather
 * than a no-op here. This is the explicit form the confirm layer and
 * the record/replay harness both use — it takes what to build rather
 * than reading it back out of UI fields. */
int game_place_building(GameState *gs, int row, int col,
                        BuildingType type, int pay_with_gold);

/* ---- the confirmation layer (UI_PLAN Phase 6) --------------
 * Opening a confirmation builds the command NOW and stores it; the
 * popup renders that command; accepting submits exactly it. Nothing is
 * re-derived in between, which is the whole point — what you were shown
 * and what the sim receives are the same bytes.
 *
 * Each opener is a no-op if the action is not currently possible (no
 * such building, wrong type), so a stale click cannot open a popup that
 * would submit nonsense. */
void game_confirm_build(GameState *gs, int row, int col,
                        BuildingType type);
void game_confirm_demolish(GameState *gs, int building_idx);
void game_confirm_upgrade(GameState *gs, int building_idx);
void game_confirm_ship(GameState *gs);

/* The same, naming which hull the yard should lay down (UI_PLAN N6).
 * game_confirm_ship() is this with SHIP_MERCHANTMAN, which is what
 * every ship built before the yard had a screen was. */
void game_confirm_ship_class(GameState *gs, int klass);

/* Pick between the two payment options (0 = the primary, 1 = the
 * alternative). Ignored when the action offers only one. */
void game_confirm_choose(GameState *gs, int which);

/* Submit the chosen command and close. Returns what command_submit
 * returned, or 0 if nothing was open. */
int  game_confirm_accept(GameState *gs);

/* Close without submitting anything. */
void game_confirm_cancel(GameState *gs);

/* Returns the buildings[] index of the active building whose
 * footprint contains (row, col), or -1 if none. Used to detect a
 * click on an already-placed building (e.g. opening the Marketplace
 * trade screen) as opposed to placing a new one. */
int game_find_building_at(const GameState *gs, int row, int col);

/* Sells up to `qty` units of `res` from the stockpile for Gold at
 * SELL_PRICE[res] (resource.h). Clamps `qty` to what's in stock;
 * a no-op if res is RES_GOLD or qty <= 0. Used by the Marketplace
 * trade screen. */
void game_sell_resource(GameState *gs, ResourceType res, int qty);

/* As above, but refusing the trade if the quote has moved against the
 * player since the screen they clicked was drawn (UI_PLAN M3).
 * `limit` is the price that screen displayed; 0 means no limit, which
 * is what the plain forms above pass. */
void game_sell_resource_limit(GameState *gs, ResourceType res, int qty,
                              int limit);

/* Buys up to `qty` units of `res` for the stockpile, paying Gold at
 * BUY_PRICE[res] (resource.h) — the same markup rate the build-
 * confirmation popup's Gold-payment option uses. Clamps `qty` down
 * to whatever's actually possible: storage headroom (capacity minus
 * current amount) and Gold on hand, in that order. qty < 0 means
 * "buy as much as both allow" (resolved against the live stockpile,
 * mirroring game_sell_resource's qty < 0 = "sell all"). No-op if res
 * is RES_GOLD or the resolved quantity is <= 0. Used by the
 * Marketplace trade screen. */
void game_buy_resource(GameState *gs, ResourceType res, int qty);
void game_buy_resource_limit(GameState *gs, ResourceType res, int qty,
                             int limit);

/* Removes the building at buildings[idx] (marks it inactive — the
 * slot itself is left for building_place() to reuse later, same
 * pattern as every other active-flagged array here). Free — no
 * refund. Also cleans up anything that referenced it: a demolished
 * House's PopData is deactivated (agents_sync() despawns its agents
 * next frame); any agent with home_idx == idx is deactivated
 * immediately (its home is simply gone); any agent with
 * work_idx == idx is snapped back to unemployed and standing at
 * home, so a destroyed workplace doesn't leave it permanently
 * "employed" at a dead job (agent_assign_jobs() only reassigns
 * agents with work_idx == -1). Recomputes storage capacity if the
 * demolished building was a Warehouse. No-op if idx is out of range
 * or already inactive. */
void game_demolish_building(GameState *gs, int idx);

/* TIER_UPGRADE_COST_GOLD moved to population.h in SUPPLY_CHAIN Phase 2:
 * with the tier model a graph, the price of an edge belongs beside the
 * edge, in TierDef.upgrade_gold, and the constant is now only the
 * first tier's value rather than every tier's. */

/* Walks buildings[idx] along its tier's upgrade edge (TierDef.next_tier,
 * population.h) after tier_upgrade_check() agrees: deducts the tier's
 * Gold, then mutates the building's type in place. Nothing else needs to change
 * — PopData's residents/happy/timer stay exactly as they were (same
 * array index), agents' home_idx references stay valid (the home
 * didn't move), and pop_update()/connectivity/rendering/worker-
 * assignment all already look up BUILDING_DEFS live by the (now
 * different) type. No-op if idx is out of range, inactive, not a
 * house, or tier_upgrade_check() refuses. */
/* `branch` is a TierBranch (population.h): the house's own line, or
 * Scholars where an Academy stands. */
void game_upgrade_house(GameState *gs, int idx, int branch);

/* SHIP_BUILD_COST_GOLD moved to ship.h with the class table
 * (MARITIME_PLAN Phase 5) — what a hull costs is a property of the
 * hull, and the yard now offers three of them. */

/* Builds a ship, docked at the current island, paid for out of that
 * island's stockpile. Returns the new ship's index, or -1 if the
 * fleet is full or the island cannot afford it. */
int game_build_ship(GameState *gs);

/* The same, choosing which hull the yard lays down (MARITIME_PLAN
 * Phase 5). game_build_ship() is this with SHIP_MERCHANTMAN. */
int game_build_ship_class(GameState *gs, int klass);

/* Owner only: set `ship_idx` to escort `target_idx`, or -1 to release
 * it. An escort sails when its charge sails and adds its guns to the
 * defence if the convoy is intercepted. */
int game_set_escort(GameState *gs, int ship_idx, int target_idx);

/* Owner only: send `ship_idx`, which must be at sea and within strike
 * range of the fleet, against pirate `pirate_idx`. Resolved by the same
 * guns-and-hull rule as an interception. Winning takes their plunder —
 * which may be somebody else's cargo, and rarely a route chart. */
int game_attack_pirate(GameState *gs, int ship_idx, int pirate_idx);

/* Move `qty` units of `res` between the current island's stockpile
 * and ship `ship_idx`'s hold. Positive qty loads onto the ship,
 * negative unloads. Clamped by what is actually present, by the
 * hold's per-resource capacity, and by the receiving stockpile's
 * capacity. No-op unless the ship is docked at the current island. */
void game_ship_transfer(GameState *gs, int ship_idx, ResourceType res, int qty);

/* Found a colony on `island_idx` using ship `ship_idx`, which must be
 * docked there and carrying at least COLONY_FOUNDING_GOLD. The Gold
 * leaves the hold and becomes the new island's starting treasury;
 * the island becomes settled, and therefore simulated and buildable.
 * Returns 1 on success. */
int game_colonise(GameState *gs, int ship_idx, int island_idx);

/* Order ship `ship_idx` to sail from wherever it is docked to
 * `dest_island`. The ship must be docked (at_island >= 0) at an island
 * other than the destination. Returns 1 if the voyage was ordered.
 * This replaces the inline ship-state mutation the world overlay used
 * to do directly, routing the ship-depart order through the funnel
 * like every other mutation (MMO_PLAN Phase 1a). */
int game_ship_depart(GameState *gs, int ship_idx, int dest_island);

/* As above, but buying marine insurance for the voyage: the premium is
 * paid to the faction at departure, and a raid is compensated on
 * arrival (MMO_PLAN later phases). Refused if the premium cannot be
 * paid — insurance you could not afford is not insurance. */
int game_ship_depart_insured(GameState *gs, int ship_idx, int dest_island);

/* What a voyage would cost to insure right now, in Gold: the lane's
 * premium applied to the hold's value at the faction's bid. Zero if
 * there is nothing aboard worth insuring. */
int game_insurance_quote(const GameState *gs, int ship_idx, int dest_island);

/* Cycle the resource carried on one leg of ship `ship_idx`'s trade
 * route: `leg` 0 is the outbound (A->B) slot, 1 the return (B->A) slot.
 * The cycle runs through every good and RES_COUNT ("carry nothing"). */
int game_ship_set_route_res(GameState *gs, int ship_idx, int leg);

/* Toggle ship `ship_idx`'s trade route on or off. When arming, the
 * route repeats the ship's last voyage (from_island -> to_island). */
int game_ship_toggle_route(GameState *gs, int ship_idx);

/* Attack another player's voyage with one of yours (MMO_PLAN later
 * phases). Both ships must be at sea; `target_departure` binds the
 * command to the voyage the player actually saw, so an intercept cannot
 * land on a later voyage of the same ship. The engagement is computed
 * deterministically inside the sim — there is nothing to aim. */
int game_intercept(GameState *gs, int my_ship, int target_ship,
                   uint64_t target_departure);

/* ---- Phase 5: ownership-era commands ----------------------- */

/* Claim `island_idx` as the local player's starting island. Validated
 * by the sim: the island must be unsettled and unowned, and the player
 * must own no island yet. The co-op join bootstrap (the host emits it
 * for a fresh guest), but equally valid locally. */
int game_grant_start(GameState *gs, int island_idx);

/* Owner only: move `qty` of `res` between `island_idx`'s stockpile and
 * its harbor escrow. Direction by function; clamped to what is there
 * (and, for TAKE, to storage headroom — the escrow never destroys
 * overflow, it keeps it). */
int game_escrow_put(GameState *gs, int island_idx, ResourceType res, int qty);
int game_escrow_take(GameState *gs, int island_idx, ResourceType res, int qty);

/* The same, stamped with the quay state the panel was showing (UI_PLAN
 * M5). If the escrow has changed since — a visitor docked and took
 * something — the sim refuses with REJ_OFFER_CHANGED rather than
 * acting on an offer the player never actually saw. A nonce of 0 means
 * unstamped, which is what replayed and scripted commands carry. */
int game_escrow_put_nonce(GameState *gs, int island_idx, ResourceType res,
                          int qty, uint32_t nonce);
int game_escrow_take_nonce(GameState *gs, int island_idx, ResourceType res,
                           int qty, uint32_t nonce);

/* Owner only: allow (1) or forbid (0) foreign ships transferring at
 * `island_idx`. A ship that can't dock can't deliver — blockade. */
int game_set_docking(GameState *gs, int island_idx, int allow);

/* Owner only: turn this harbour's standing marine policy on or off
 * (MARITIME_PLAN Phase 3c). While on, every shipment dispatched from
 * here is insured at its ROUTE's premium — so covering a fast private
 * passage costs more than covering the patrolled lane. */
int game_set_insurance(GameState *gs, int island_idx, int on);

/* Owner only: lay down a research boat at `island_idx`, which needs a
 * Shipyard (MARITIME_PLAN Phase 3d). */
int game_build_research_boat(GameState *gs, int island_idx);

/* Owner only: send an expedition from `from_island` to look for an
 * uncharted passage to `to_island`. Commits a scholar, a research boat
 * and a blank chart; the chart is spent either way, and a failed
 * expedition may not come home at all.
 *
 * You cannot name the route — that is what you are paying to find
 * out. The sim picks an undiscovered private passage between the two. */
int game_survey(GameState *gs, int from_island, int to_island);

/* Post an order at `island_idx`'s harbour (MARITIME_PLAN Phase 2).
 * `qty` carries the side: positive buys, negative sells. `limit` is the
 * worst price per unit the order will accept, and posting reserves at
 * it — the goods for a sell, `qty * limit` gold for a buy.
 *
 * `kind`/`what` name the thing being traded rather than a ResourceType,
 * because a route chart is not one of a fixed set of goods; see
 * orderbook.h. Both kinds are accepted: a TRADE_ROUTE_CHART order names
 * a private passage by its sea route id, and is posted from the
 * passages screen rather than the book's composer, because a chart is
 * chosen by pointing at water (UI_PLAN N4). */
int game_place_order(GameState *gs, int island_idx, TradeKind kind,
                     uint16_t what, int qty, int limit);

/* Withdraw an order by id, returning whatever it still reserves. Owner
 * only, and the check is the order's own, not its island's. */
int game_cancel_order(GameState *gs, uint32_t order_id);

#endif /* GAME_H */
