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
 * ========================================================= */

#include <SDL3/SDL.h>
#include "map.h"
#include "camera.h"
#include "input.h"
#include "building.h"

#define SCREEN_W 1920
#define SCREEN_H 1080

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
} GameState;

/* Allocate and initialise a new GameState.
 * Returns NULL on allocation failure. */
GameState *game_init(void);

/* Free a GameState allocated by game_init(). */
void game_free(GameState *gs);

/* Called once per frame.  Moves the camera based on held
 * keys and updates the hovered tile from mouse position. */
void game_update(GameState *gs, SDL_Renderer *renderer);  /* CHANGED: needs renderer for coord conversion */

/* Called when the player left-clicks on the map.
 * Attempts to place selected_building at the hovered tile. */
void game_place_building(GameState *gs);

#endif /* GAME_H */
