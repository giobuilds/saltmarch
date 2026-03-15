/*  game.c  --  Game state management  */

#include "game.h"
#include "render.h"    /* screen_to_iso() */
#include "building.h"
#include "ui.h"
#include <SDL3/SDL.h>  /* SDL_GetTicksNS() */
#include <stdlib.h>    /* malloc, free    */
#include <string.h>   /* memset */

/* Forward declaration — renderer is needed for coordinate conversion.
 * We receive it via a new parameter on game_update(). */

/* ---- game_init ----------------------------------------- */
GameState *game_init(void)
{
    GameState *gs = (GameState *)malloc(sizeof(GameState));
    if (!gs) return NULL;

    uint32_t seed = (uint32_t)SDL_GetTicksNS();
    SDL_Log("Map seed: %u", seed);
    map_init(&gs->map, seed);

    camera_init(&gs->camera, SCREEN_W, SCREEN_H,
                MAP_COLS, MAP_ROWS);
    input_init(&gs->input);

    gs->hovered_row = -1;
    gs->hovered_col = -1;
    gs->last_tick   = SDL_GetTicksNS();   /* nanosecond timer – always available */
    gs->delta_time  = 0.0f;

    memset(gs->buildings, 0, sizeof(gs->buildings));
    gs->building_count    = 0;
    gs->selected_building = BUILDING_NONE;
    gs->placement_valid   = 0;
    gs->menu_open         = 0;   /* CHANGED: menu starts closed */

    return gs;
}

/* ---- game_free ----------------------------------------- */
void game_free(GameState *gs)
{
    free(gs);
}

/* ---- game_update ---------------------------------------
 * Per-frame logic:
 *   1. Delta time
 *   2. Camera pan
 *   3. Hovered tile
 *   4. Placement validity for ghost rendering
 * 
 * CHANGED: takes SDL_Renderer* so we can convert window
 * mouse coords to logical render coords via
 * SDL_RenderCoordinatesFromWindow(). This is necessary because
 * SDL_SetRenderLogicalPresentation() makes the render coordinate
 * space (1920x1080) independent of the actual window pixel size.
 * Mouse events arrive in window pixels; without conversion the
 * HUD hit-test and tile hover will be wrong on any window that
 * isn't exactly 1920x1080.
 * -------------------------------------------------------- */
void game_update(GameState *gs, SDL_Renderer *renderer)
{
    float lx, ly;     /* logical mouse coordinates */
 
    /* --- Delta time ------------------------------------ */
    Uint64 now = SDL_GetTicksNS();
    float  dt  = (float)(now - gs->last_tick) / 1000000000.0f;
    if (dt > 0.1f) dt = 0.1f;
    gs->last_tick  = now;
    gs->delta_time = dt;

    /* --- Camera panning -------------------------------- */
    if (gs->input.pan_left)  gs->camera.offset_x += CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_right) gs->camera.offset_x -= CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_up)    gs->camera.offset_y += CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_down)  gs->camera.offset_y -= CAMERA_PAN_SPEED * dt;

    /* --- Convert mouse from window pixels to logical coords --- */
    SDL_RenderCoordinatesFromWindow(renderer,
        (float)gs->input.mouse_x, (float)gs->input.mouse_y,
        &lx, &ly);
 
    /* Store converted coords back so HUD and tile picker agree */
    gs->input.logical_x = (int)lx;   /* CHANGED: new fields, see game.h */
    gs->input.logical_y = (int)ly;
 
    /* --- Hovered tile ---------------------------------- */
    if (gs->input.logical_y < SCREEN_H - HUD_HEIGHT) {
        screen_to_iso(gs->input.logical_x, gs->input.logical_y,
                      &gs->camera,
                      &gs->hovered_row, &gs->hovered_col);
 
        if (gs->hovered_row < 0 || gs->hovered_row >= MAP_ROWS ||
            gs->hovered_col < 0 || gs->hovered_col >= MAP_COLS) {
            gs->hovered_row = -1;
            gs->hovered_col = -1;
        }
    } else {
        gs->hovered_row = -1;
        gs->hovered_col = -1;
    }
 
    /* --- Placement validity ---------------------------- */
    gs->placement_valid = 0;
    if (gs->selected_building != BUILDING_NONE && gs->hovered_row >= 0) {
        gs->placement_valid = building_can_place(
            &gs->map,
            gs->selected_building,
            gs->hovered_row, gs->hovered_col,
            NULL, 0);
    }
}
 
/* ---- game_place_building ------------------------------- */
void game_place_building(GameState *gs)
{
    if (gs->selected_building == BUILDING_NONE) return;
    if (gs->hovered_row < 0) return;
 
    building_place(gs->buildings, &gs->building_count,
                   &gs->map,
                   gs->selected_building,
                   gs->hovered_row, gs->hovered_col);
    /* Note: we intentionally keep the building selected after
     * placing so the player can rapidly place multiple copies. */
}
