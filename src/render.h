#ifndef RENDER_H
#define RENDER_H

/* =========================================================
 * render.h  --  Rendering declarations
 *
 * All SDL draw calls live in this module.  Nothing outside
 * render.c should call SDL_Render* directly – this keeps
 * the graphics layer cleanly separated from game logic.
 *
 * Isometric conversion reminder:
 *   Given grid position (row, col) and camera offset:
 *
 *   screen_x = cam->offset_x + (col - row) * (TILE_W / 2)
 *   screen_y = cam->offset_y + (col + row) * (TILE_H / 2)
 *
 *   screen_x/y is the TOP POINT of the diamond.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "map.h"
#include "camera.h"
#include "building.h"
#include "resource.h"
#include "population.h"   /* Phase 5 */
#include "sprite.h"       /* Phase 6 */

/* Draw the entire map for one frame.
 * Tiles are drawn back-to-front (painter's algorithm):
 * top rows first so nearer rows overdraw farther ones. */
void render_map(SDL_Renderer *renderer,
                const Map    *map,
                const Camera *cam);

/* Draw a single crosshair on the tile that the mouse is
 * hovering over (useful for debugging tile selection). */
void render_hovered_tile(SDL_Renderer *renderer,
                         const Camera *cam,
                         int hovered_row, int hovered_col);

/* Fill the screen with the background (sky / sea colour). */
void render_clear(SDL_Renderer *renderer);

void render_resources(SDL_Renderer *renderer,
                      const Stockpile *s);

/* ---- Utility: grid → screen ----------------------------
 * Exposed here so input.c can do the inverse transform
 * (screen → grid) without duplicating the math. */
void iso_to_screen(int row, int col, const Camera *cam,
                   int *out_x, int *out_y);

/* Inverse: screen pixel → grid cell.
 * Sets *out_row and *out_col; these may be out of bounds. */
void screen_to_iso(int sx, int sy, const Camera *cam,
                   int *out_row, int *out_col);


void render_buildings(SDL_Renderer *renderer,
                      const Building buildings[], int count,
                      const Camera *cam);
 
void render_ghost(SDL_Renderer *renderer,
                  const Camera *cam,
                  BuildingType type,
                  int row, int col,
                  int valid);

/* Phase 5: population counter top-right */
void render_population(SDL_Renderer *renderer,
                       int total_pop,
                       int screen_w);
 
void iso_to_screen(int row, int col, const Camera *cam,
                   int *out_x, int *out_y);
 
void screen_to_iso(int sx, int sy, const Camera *cam,
                   int *out_row, int *out_col);

#endif /* RENDER_H */
