#ifndef GAME_H
#define GAME_H

/* =========================================================
 * game.h  --  Top-level game state
 *
 * GameState owns every sub-system: the map, the camera,
 * and the input tracker.  A single pointer to GameState
 * is stored in SDL's appstate so all three callbacks
 * (AppInit, AppEvent, AppIterate) can reach it.
 * 
 * New in Phase 3:
 *   - buildings[] array + building_count
 *   - selected_building (what the player has picked to place)
 *   - placement_valid flag (can we place at the hovered tile?)
 * 
 * New in Phase 5: PopData array and fonts added.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "map.h"
#include "camera.h"
#include "input.h"
#include "building.h"
#include "resource.h"
#include "population.h"   /* Phase 5 */

#define SCREEN_W 1920
#define SCREEN_H 1080

/* Gold the player starts every new game with. */
#define STARTING_GOLD 1000

typedef struct {
    Map        map;
    Camera     camera;
    InputState input;

    /* Tile currently under the mouse cursor (-1 if none) */
    int hovered_row;
    int hovered_col;

    /* Time tracking for frame-rate-independent movement.
    * last_tick  – SDL timestamp (ms) at the end of the previous frame.
    * delta_time – seconds elapsed since that frame (e.g. 0.016 at 60fps).
    * All per-frame movement is multiplied by delta_time so the game
    * behaves identically at 30, 60, or 144 fps. */
    Uint64 last_tick;
    float delta_time;

    /* ---- Phase 3: building placement ---- */
    Building     buildings[MAX_BUILDINGS];
    int          building_count;
 
    /* Which building type the player has selected from the HUD.
     * BUILDING_NONE (-1) means nothing selected. */
    BuildingType selected_building;
 
    /* 1 if the current hover position is a valid placement spot
     * for selected_building.  Used by render to colour the ghost. */
    int placement_valid;

    int menu_open;  /* 1 when the cog menu overlay is open */
    Stockpile stockpile;

    /* Phase 5: one PopData per building slot.
     * Only slots where buildings[i].type == BUILDING_HOUSE
     * and pop_data[i].active == 1 are meaningful. */
    PopData      pop_data[MAX_BUILDINGS];
} GameState;

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

/* Called when the player left-clicks on the map.
 * Attempts to place selected_building at the hovered tile. */
void game_place_building(GameState *gs);

#endif /* GAME_H */
