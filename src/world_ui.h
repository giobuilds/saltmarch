#ifndef WORLD_UI_H
#define WORLD_UI_H

/* =========================================================
 * world_ui.h  --  The archipelago overview overlay
 *
 * Because islands are separate Maps rather than landmasses on one
 * shared grid, there is no sea for the tile view to show — so the
 * relationship BETWEEN islands needs its own screen. This is that
 * screen: every island as a node, its profile and settled state
 * legible at a glance, click one to sail the camera there.
 *
 * Built as an overlay in the same shape as the other four
 * (*_ui.c pattern: sizing #defines, panel_rect()-derived helpers,
 * point_in(), fully independent draw/hit_test, a *Hit enum) rather
 * than as a separate render mode. An overlay slots into main.c's
 * existing click-priority cascade and right-click-closes-topmost
 * convention for free; a render mode would mean two mutually
 * exclusive paths through SDL_AppIterate for no benefit.
 *
 * Node positions are fixed rather than procedural — with a handful
 * of islands there is nothing to gain from laying them out
 * dynamically, and fixed points make hit-testing, and later the
 * ship lanes drawn between them, trivial and deterministic.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "island.h"
#include "ship.h"

#define WORLD_NODE_ZOOM   2.2f   /* island diamond size vs a map tile */
#define WORLD_TITLE_Y      40

#define WORLD_PANEL_W    360   /* selected-ship panel, right side */
#define WORLD_ROW_H       26

typedef enum {
    WORLD_HIT_NONE     = 0,  /* empty sea — no effect                  */
    WORLD_HIT_CLOSE    = 1,  /* the Close button                       */
    WORLD_HIT_ISLAND   = 2,  /* an island node — *out_island is set    */
    WORLD_HIT_SHIP     = 3,  /* a ship marker — *out_ship is set       */
    WORLD_HIT_LOAD     = 4,  /* load a resource  — *out_res is set     */
    WORLD_HIT_UNLOAD   = 5,  /* unload a resource — *out_res is set    */
    WORLD_HIT_COLONISE = 6,  /* found a colony with the selected ship  */
    WORLD_HIT_ROUTE_OUT   = 7, /* cycle the outbound (A->B) good       */
    WORLD_HIT_ROUTE_BACK  = 8, /* cycle the return (B->A) good         */
    WORLD_HIT_ROUTE_TOGGLE= 9  /* start/stop the repeating route       */
} WorldHit;

/* Draw the overview. `islands` is the whole archipelago and
 * `current` the one being viewed (highlighted). */
void world_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const Island islands[], int island_count, int current,
                   const Ship ships[], int ship_count, int selected_ship,
                   int mouse_x, int mouse_y);

/* Hit-test a click. On WORLD_HIT_ISLAND, *out_island is the index. */
WorldHit world_ui_hit_test(int screen_w, int screen_h, int island_count,
                           const Ship ships[], int ship_count,
                           int selected_ship, int mouse_x, int mouse_y,
                           int *out_island, int *out_ship, ResourceType *out_res);

#endif /* WORLD_UI_H */
