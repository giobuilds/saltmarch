#ifndef RENDER_H
#define RENDER_H

#include <SDL3/SDL.h>
#include "map.h"
#include "camera.h"
#include "building.h"
#include "resource.h"
#include "population.h"   /* Phase 5 */
#include "agent.h"         /* Phase 5: walking population agents */

void render_clear(SDL_Renderer *renderer);

void render_map(SDL_Renderer *renderer,
                const Map *map, const Camera *cam);

void render_hovered_tile(SDL_Renderer *renderer,
                         const Camera *cam,
                         int row, int col);

void render_buildings(SDL_Renderer *renderer,
                      const Building buildings[], int count,
                      const Camera *cam);

void render_ghost(SDL_Renderer *renderer,
                  const Camera *cam,
                  BuildingType type,
                  int row, int col,
                  int valid);

void render_resources(SDL_Renderer *renderer,
                      const Stockpile *s);

/* Phase 5: population counter top-right */
void render_population(SDL_Renderer *renderer,
                       int total_pop,
                       int screen_w);

/* Phase 5: one small marker per active walking agent. */
void render_agents(SDL_Renderer *renderer,
                   const Agent agents[], int count,
                   const Camera *cam);

/* The flat-shaded isometric diamond every tile, building and ghost is
 * drawn with. Exposed (rather than kept static in render.c) so the
 * world-map overlay can draw island nodes with the same primitive
 * instead of carrying a second copy of the geometry. Size is
 * TILE_W x TILE_H scaled by `zoom`. */
void render_draw_diamond(SDL_Renderer *renderer,
                         float bx, float by, float zoom,
                         SDL_Color top_col, SDL_Color bot_col);

void render_draw_diamond_outline(SDL_Renderer *renderer,
                                 float bx, float by, float zoom,
                                 unsigned char r, unsigned char g,
                                 unsigned char b, unsigned char a);

/* CHANGED: returns float positions so zoomed tiles sit flush with no gaps.
 * Phase 5: row/col widened from int to float — every existing call
 * site passes integer tile coordinates, which convert implicitly and
 * losslessly (tile indices are 0-63), so this is source-compatible.
 * It's what lets render_agents() project an agent's fractional
 * position through the exact same isometric transform as everything
 * else, with no separate function needed. */
void iso_to_screen(float row, float col, const Camera *cam,
                   float *out_x, float *out_y);

void screen_to_iso(int sx, int sy, const Camera *cam,
                   int *out_row, int *out_col);

#endif /* RENDER_H */
