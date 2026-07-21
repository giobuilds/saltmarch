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
 * ========================================================= */

#include <SDL3/SDL.h>
#include "map.h"
#include "camera.h"      /* also provides SCREEN_W / SCREEN_H */
#include "input.h"
#include "building.h"
#include "resource.h"
#include "population.h"
#include "agent.h"
#include "island.h"

/* Gold a new game's starting island begins with. */
#define STARTING_GOLD 1000

typedef struct {
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
    * last_tick  – SDL timestamp (ms) at the end of the previous frame.
    * delta_time – seconds elapsed since that frame (e.g. 0.016 at 60fps).
    * All per-frame movement is multiplied by delta_time so the game
    * behaves identically at 30, 60, or 144 fps. */
    Uint64 last_tick;
    float delta_time;

    /* Which building type the player has selected from the HUD.
     * BUILDING_NONE (-1) means nothing selected. */
    BuildingType selected_building;

    /* 1 if the current hover position is a valid placement spot
     * for selected_building.  Used by render to colour the ghost. */
    int placement_valid;

    int menu_open;  /* 1 when the cog menu overlay is open */

    /* Manual trade screen. trade_open mirrors menu_open's overlay
     * pattern; trade_building_idx indexes the CURRENT island's
     * buildings[] — every *_idx below is current-island-relative,
     * which is safe because game_set_current_island() closes every
     * overlay rather than trying to keep them alive across a switch. */
    int trade_open;
    int trade_building_idx;

    /* Build-confirmation popup: every building except Road goes
     * through this instead of placing instantly (Road is exempt —
     * see game_try_place_road()'s doc comment). row/col are captured
     * at popup-open time, NOT read from hovered_row/col at confirm
     * time — by then the mouse has moved to the popup's buttons,
     * which live in a totally different screen region and would
     * resolve to an unrelated tile if hover were re-queried. */
    int build_confirm_open;
    int build_confirm_row, build_confirm_col;
    int build_confirm_payment;   /* 0 = pay resources, 1 = pay Gold */

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

    /* Bulldozer confirmation popup: opened instead of an immediate
     * game_demolish_building() call when the demolish tool is active
     * and the player clicks a building. */
    int demolish_confirm_open;
    int demolish_confirm_idx;

    /* Tier-upgrade confirmation popup: opened when the player clicks
     * an active, connected BUILDING_HOUSE with nothing selected and
     * demolish_mode off. */
    int tier_upgrade_open;
    int tier_upgrade_idx;
} GameState;

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

#define SAVE_FILE_PATH "annoclone_save.dat"

/* Called once per frame.  Moves the camera based on held
 * keys and updates the hovered tile from mouse position. */
void game_update(GameState *gs, SDL_Renderer *renderer);  /* CHANGED: needs renderer for coord conversion */

/* Attempts to place a Road at (row, col) directly — no confirmation
 * popup. Roads are the one building type exempt from
 * game_place_building_confirmed()'s popup: a drag gesture placing
 * many tiles can't reasonably pop up a per-tile confirmation, and a
 * single non-dragged click on Road behaves the same way for
 * consistency. Checks placement validity and affordability itself;
 * returns 1 on success, 0 otherwise (no side effects on failure). */
int game_try_place_road(GameState *gs, int row, int col);

/* Commits the pending build-confirmation popup: places
 * selected_building at build_confirm_row/col (not the live hover —
 * see GameState's doc comment on why) and deducts payment.
 * pay_with_gold selects which of the popup's two options was chosen:
 * 0 deducts the normal per-resource cost[] (as building_can_afford
 * checks), 1 deducts building_gold_equivalent_cost() in Gold only.
 * No-op (no placement, no deduction) if the chosen payment can't
 * actually be afforded. */
void game_place_building_confirmed(GameState *gs, int pay_with_gold);

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

#endif /* GAME_H */
