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

/* Gold a new game's starting island begins with. */
#define STARTING_GOLD 1000

/* Player identity (MMO_PLAN Phase 5). PLAYER_NONE marks an unowned
 * island; real players count from 1. Single player is always player 1;
 * a co-op guest gets its id from the host at join. */
#define PLAYER_NONE 0u

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

    /* Who this client is (Phase 5). CLIENT state, not world state: it
     * is never hashed and never saved — it says which player's commands
     * this process emits (command_submit stamps it), not anything about
     * the world. 1 in single player; a co-op guest is assigned its id
     * by the host at join and keeps it after a disconnect. */
    uint32_t  local_player_id;

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
    UI_OVERLAY_WORLD
} GameOverlay;

GameOverlay game_topmost_overlay(const GameState *gs);

/* 1 if any overlay is open — the common question, asked by the camera
 * (do not zoom), the hover logic (do not highlight tiles) and the
 * drag-placement loop (do not lay road under a popup). */
int game_overlay_open(const GameState *gs);

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

/* Serialize gs (map seed, buildings, population, stockpile, camera)
 * to `path`. Returns 1 on success, 0 on failure (see SDL_GetError()).
 * The map itself is not written — map_init(seed) regenerates it
 * deterministically. Used by the "Save" menu button. */
int  game_save(const GameState *gs, const char *path);

/* Inverse of game_save(): restores buildings, population,
 * stockpile and camera from `path`, regenerating the map from
 * its stored seed. Returns 1 on success; on failure (missing,
 * corrupt, or wrong-version file) returns 0 and leaves gs
 * untouched. Used by the "Load" menu button. */
int  game_load(GameState *gs, const char *path);

#define SAVE_FILE_PATH "saltmarch_save.dat"

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

/* Gold cost to upgrade a Farmers' House (BUILDING_HOUSE) to a
 * Workers' House (BUILDING_HOUSE_WORKER) via game_upgrade_house(). */
#define TIER_UPGRADE_COST_GOLD 300

/* Upgrades buildings[idx] from BUILDING_HOUSE to BUILDING_HOUSE_WORKER:
 * checks Gold affordability, deducts TIER_UPGRADE_COST_GOLD, then
 * mutates the building's type in place. Nothing else needs to change
 * — PopData's residents/happy/timer stay exactly as they were (same
 * array index), agents' home_idx references stay valid (the home
 * didn't move), and pop_update()/connectivity/rendering/worker-
 * assignment all already look up BUILDING_DEFS live by the (now
 * different) type. No-op if idx is out of range, inactive, not a
 * BUILDING_HOUSE, or Gold is insufficient. */
void game_upgrade_house(GameState *gs, int idx);

/* Gold cost of laying down a new ship at a Shipyard. */
#define SHIP_BUILD_COST_GOLD 350

/* Builds a ship, docked at the current island, paid for out of that
 * island's stockpile. Returns the new ship's index, or -1 if the
 * fleet is full or the island cannot afford it. */
int game_build_ship(GameState *gs);

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

/* Cycle the resource carried on one leg of ship `ship_idx`'s trade
 * route: `leg` 0 is the outbound (A->B) slot, 1 the return (B->A) slot.
 * The cycle runs through every good and RES_COUNT ("carry nothing"). */
int game_ship_set_route_res(GameState *gs, int ship_idx, int leg);

/* Toggle ship `ship_idx`'s trade route on or off. When arming, the
 * route repeats the ship's last voyage (from_island -> to_island). */
int game_ship_toggle_route(GameState *gs, int ship_idx);

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

/* Owner only: allow (1) or forbid (0) foreign ships transferring at
 * `island_idx`. A ship that can't dock can't deliver — blockade. */
int game_set_docking(GameState *gs, int island_idx, int allow);

#endif /* GAME_H */
