#ifndef GAME_H
#define GAME_H

/* game.h -- GameState: the archipelago plus everything global (input,
 * frame timing, the viewed island, overlay flags). Per-landmass state
 * lives in Island (island.h). */

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
#define STARTING_GOLD 10000

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
 * Every command applied at a tick boundary leaves one of these behind. */
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
     * Every island exists from world-gen; `settled` (island.h) */
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
     * 0 before the first frame (client_update seeds it). */
    uint64_t last_tick;
    float delta_time;

    /* Which building type the player has selected from the HUD.
     * BUILDING_NONE (-1) means nothing selected. */
    BuildingType selected_building;

    /* 1 if the current hover position is a valid placement spot
     * for selected_building.  Used by render to colour the ghost. */
    int placement_valid;
    int placement_reason;

    int menu_open;  /* 1 when the cog menu overlay is open */

    /* Manual trade screen. trade_open mirrors menu_open's overlay */
    int trade_open;
    int trade_building_idx;

    /* ---- the confirmation popup (UI_PLAN Phase 6) --------- */
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

    /* ---- The command funnel (MMO_PLAN Phase 1a) ----------- */
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
    /* The recorded input stream (UI_PLAN M1). Written beside. */
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

    /* The water between the islands (MARITIME_PLAN Phase 1). A pure */
    Sea       sea;

    /* The order book (MARITIME_PLAN Phase 2). World state: hashed,
     * replayed, snapshotted. Matching runs at tick boundaries so a
     * replay fills exactly the trades the original run filled. */
    OrderBook book;

    /* What each player knows of the sea, and the charts they hold */
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

    /* Who this client is (Phase 5). CLIENT state, not world state:. */
    uint32_t  local_player_id;

    /* Predict only this player's own islands (SERVER_AUTHORITY.md */
    uint32_t  predict_only;

    /* The lockstep co-op session, or NULL offline (Phase 5). Client */
    struct NetSession *net;
    int (*net_submit)(struct NetSession *ns, struct GameState *gs,
                      const Command *c);

    /* World seed the whole archipelago is generated from. Stored so the
     * F9 self-check (and, in Phase 1d, load) can rebuild the tick-0
     * world and replay the log against it. */
    uint32_t  world_seed;

    /* Which island is currently ticking, so the migration hook every
     * Island carries knows who is asking (LIFE_PLAN Phase 7). Derived
     * scratch for the duration of one island's update — never hashed,
     * never saved. */
    int       migrate_from;

    /* ---- the scrubber (MMO_PLAN later phases) -------------- */
    int       scrub_active;
    uint64_t  scrub_live_tick;

    /* The earliest tick this process can still rebuild. */
    uint64_t  history_floor_tick;

    /* The snapshot the floor stands on, kept so the scrubber still */
    unsigned char *floor_snap;
    size_t         floor_snap_len;

    /* ---- F9 determinism self-check (MMO_PLAN Phase 1c) ---- */
    int       replay_valid;
    int       replay_state;   /* 0 none, 1 pass, 2 desync, 3 n/a         */
    uint64_t  replay_live_hash;
    uint64_t  replay_replay_hash;
    uint64_t  replay_tick;
    uint64_t  replay_show_until_ns;
} GameState;

/* ---- The command funnel --------------------------------- */
int  command_submit(GameState *gs, const Command *c);
int  sim_apply(GameState *gs, const Command *c);

/* The same dispatch, reporting WHY rather than just whether (UI_PLAN */
RejectReason sim_apply_reason(GameState *gs, const Command *c);

/* Copy out (and clear) everything recorded since the last drain.
 * Returns how many were written, at most `max`. */
int sim_results_drain(GameState *gs, SimResult *out, int max);

/* Advance the world by exactly one fixed tick: apply every command
 * stamped for this tick (in log order), run each settled island's full
 * pipeline and every voyage for one tick, then increment sim_tick_no. */
void sim_run_one_tick(GameState *gs);

/* Canonical hash of the simulated world state (MMO_PLAN Phase 1c): */
uint64_t sim_hash(const GameState *gs);

/* The F9 self-check: rebuild a scratch world from world_seed, replay */
int game_verify_determinism(GameState *gs);

/* Replace the command log with a copy of `n` commands from. */
int  command_log_set(GameState *gs, const Command *cmds, int n);

/* The same, from bytes that may not be aligned for a Command. */
int  command_log_set_bytes(GameState *gs, const void *bytes, int n);

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
                        const void *cmds, int n);

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

/* ---- the overlay arbiter (UI_PLAN Phase 4) ----------------- */
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

/* ---- the time-travel scrubber (MMO_PLAN later phases) ------ */
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
/* `cmds` is bytes rather than a Command array on purpose: its callers
 * point it into the middle of a save file or a network frame, just past
 * a snapshot of arbitrary length, where nothing guarantees the alignment
 * a Command wants. See command_log_set_bytes(). */
int game_install_from_snapshot(GameState *gs, const unsigned char *snap,
                               size_t snap_len, uint64_t tick,
                               const void *cmds, int n);

/* Adopt (a copy of) `buf` as the world's history floor, or drop it. */
int  game_set_history_floor(GameState *gs, const unsigned char *buf,
                            size_t len, uint64_t tick);
void game_clear_history_floor(GameState *gs);

/* Drop every command already applied, keeping only the pending. */
void game_truncate_log(GameState *gs);

/* The island currently being viewed — the one every placement, UI
 * action and *_idx field in GameState refers to. Never NULL:
 * current_island is always a valid index. */
Island *game_cur_island(GameState *gs);

/* Switch the viewed island. Closes every overlay and clears */
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

/* Write the world as STATE: a full snapshot plus only the commands. */
int  game_save_checkpoint(const GameState *gs, const char *path);

/* Inverse of game_save(): restores buildings. */
int  game_load(GameState *gs, const char *path);

#define SAVE_FILE_PATH "saltmarch_save.dat"

/* Read just the command log out of a .smlog, without touching the
 * current world. The caller owns *out_cmds and must free() it. Returns
 * 1 on success, 0 on a missing, corrupt or wrong-version file. */
int game_load_commands(const char *path, Command **out_cmds, int *out_count);

/* The per-frame client update (camera, hover, drag input, and the
 * accumulator that spends real time on fixed sim ticks) lives in
 * client.h/client.c — it needs SDL, so it cannot live here. */

/* Attempts to place a Road at (row, col) directly — no confirmation */
int game_try_place_road(GameState *gs, int row, int col);

/* Place `type` at (row, col) on the current island, paying in goods or */
int game_place_building(GameState *gs, int row, int col,
                        BuildingType type, int pay_with_gold);

/* ---- the confirmation layer (UI_PLAN Phase 6) -------------- */
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

/* Buys up to `qty` units of `res` for the stockpile, paying Gold at */
void game_buy_resource(GameState *gs, ResourceType res, int qty);
void game_buy_resource_limit(GameState *gs, ResourceType res, int qty,
                             int limit);

/* Removes the building at buildings[idx] (marks it inactive —. */
void game_demolish_building(GameState *gs, int idx);

/* TIER_UPGRADE_COST_GOLD moved to population.h in SUPPLY_CHAIN Phase 2:
 * with the tier model a graph, the price of an edge belongs beside the
 * edge, in TierDef.upgrade_gold, and the constant is now only the
 * first tier's value rather than every tier's. */

/* Walks buildings[idx] along its tier's upgrade edge. */
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

/* Move `qty` units of `res` between the current island's stockpile */
void game_ship_transfer(GameState *gs, int ship_idx, ResourceType res, int qty);

/* Found a colony on `island_idx` using ship `ship_idx`, which must. */
int game_colonise(GameState *gs, int ship_idx, int island_idx);

/* Order ship `ship_idx` to sail from wherever it is docked to
 * `dest_island`. The ship must be docked (at_island >= 0) at an island
 * other than the destination. Returns 1 if the voyage was ordered. */
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

/* Attack another player's voyage with one of yours (MMO_PLAN later */
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

/* The same, stamped with the quay state the panel was showing (UI_PLAN */
int game_escrow_put_nonce(GameState *gs, int island_idx, ResourceType res,
                          int qty, uint32_t nonce);
int game_escrow_take_nonce(GameState *gs, int island_idx, ResourceType res,
                           int qty, uint32_t nonce);

/* Owner only: allow (1) or forbid (0) foreign ships transferring at
 * `island_idx`. A ship that can't dock can't deliver — blockade. */
int game_set_docking(GameState *gs, int island_idx, int allow);

/* Set what this island's treasury takes from wages and business profit,
 * in per mille (LIFE_PLAN Phase 7). Queues a command like every other
 * mutation; the rate is clamped to 0..TAX_RATE_MAX_PERMILLE when it
 * applies. */
int game_set_tax_rate(GameState *gs, int island_idx, int permille);

/* Owner only: turn this harbour's standing marine policy on or off
 * (MARITIME_PLAN Phase 3c). While on, every shipment dispatched from
 * here is insured at its ROUTE's premium — so covering a fast private
 * passage costs more than covering the patrolled lane. */
int game_set_insurance(GameState *gs, int island_idx, int on);

/* Owner only: lay down a research boat at `island_idx`, which needs a
 * Shipyard (MARITIME_PLAN Phase 3d). */
int game_build_research_boat(GameState *gs, int island_idx);

/* Owner only: send an expedition from `from_island` to look for. */
int game_survey(GameState *gs, int from_island, int to_island);

/* Post an order at `island_idx`'s harbour (MARITIME_PLAN Phase 2). */
int game_place_order(GameState *gs, int island_idx, TradeKind kind,
                     uint16_t what, int qty, int limit);

/* Withdraw an order by id, returning whatever it still reserves. Owner
 * only, and the check is the order's own, not its island's. */
int game_cancel_order(GameState *gs, uint32_t order_id);

#endif /* GAME_H */
