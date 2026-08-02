#ifndef WORLD_UI_H
#define WORLD_UI_H

/* ========================================================= */

#include <SDL3/SDL.h>
#include "island.h"
#include "faction.h"
#include "ship.h"
#include "sea.h"
#include "feed.h"   /* Phase 4: ghost voyages from the shared feed */
#include "sea_view.h"  /* UI_PLAN N5: where everything spatial goes */

#define WORLD_NODE_ZOOM   2.2f   /* island diamond size vs a map tile */

/* How many ghost voyages the map will draw at once (UI_PLAN M4). */
#define WORLD_MAX_DRAWN_GHOSTS 24
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
    WORLD_HIT_ROUTE_TOGGLE= 9, /* start/stop the repeating route       */
    WORLD_HIT_INSURE      =10   /* sail the selected ship, insured      */
} WorldHit;

/* Draw the overview. `islands` is the whole archipelago. */
void world_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const Sea *sea, const Island islands[], int island_count, int current,
                   uint32_t local_player,
                   const Ship ships[], int ship_count, int selected_ship,
                   const GhostVoyage ghosts[], int ghost_count,
                   uint64_t unix_ms,
                   const Faction *faction, int insurance_quote,
                   const SeaView *view,
                   int mouse_x, int mouse_y);

/* Hit-test a click. On WORLD_HIT_ISLAND, *out_island is the index. */
WorldHit world_ui_hit_test(int screen_w, int screen_h, const Sea *sea,
                           int island_count,
                           const Ship ships[], int ship_count,
                           int selected_ship, int mouse_x, int mouse_y,
                           int *out_island, int *out_ship, ResourceType *out_res);

#endif /* WORLD_UI_H */
