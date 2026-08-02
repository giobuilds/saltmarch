#ifndef RENDER_H
#define RENDER_H

#include <SDL3/SDL.h>
#include "map.h"
#include "camera.h"
#include "fx_reject.h"

struct GameState;
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

/* Names the seam under the hovered tile ("Iron deposit"), in a box
 * anchored above the tile — not above the cursor. Draws nothing if the
 * tile has no deposit, so it can be called unconditionally. */
void render_deposit_label(SDL_Renderer *renderer, const Map *map,
                          const Camera *cam, int row, int col,
                          int screen_w, int screen_h);

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
/* The date, drawn under the population box (LIFE_PLAN Phase 4).
 * `tick` is the sim tick the frame's snapshot was taken at, so the date
 * cannot disagree with the world drawn around it. */
void render_date(SDL_Renderer *renderer, uint64_t tick, int screen_w);

void render_population(SDL_Renderer *renderer,
                       int total_pop,
                       int screen_w);

/* Phase 5: one small marker per active walking agent. */
void render_agents(SDL_Renderer *renderer,
                   const Agent agents[], int count,
                   const Camera *cam);

/* The flat-shaded isometric diamond every tile, building and ghost. */
/* ---- pending and rejected (UI_PLAN M1) --------------------
 * Commands apply at tick boundaries, and several ticks later under
 * lockstep. Rather than hiding that, both halves are drawn: */
void render_pending_placements(SDL_Renderer *renderer, const Camera *cam,
                               const struct GameState *gs);

void render_reject_flashes(SDL_Renderer *renderer, const Camera *cam,
                           const FxReject *fx);

/* A tiny price line in `area`: `n` samples, oldest first, scaled. */
void render_sparkline(SDL_Renderer *renderer, float x, float y,
                      float w, float h, const int16_t *vals, int n,
                      SDL_Color col);

void render_draw_diamond(SDL_Renderer *renderer,
                         float bx, float by, float zoom,
                         SDL_Color top_col, SDL_Color bot_col);

void render_draw_diamond_outline(SDL_Renderer *renderer,
                                 float bx, float by, float zoom,
                                 unsigned char r, unsigned char g,
                                 unsigned char b, unsigned char a);

/* CHANGED: returns float positions so zoomed tiles sit flush with no gaps. */
void iso_to_screen(float row, float col, const Camera *cam,
                   float *out_x, float *out_y);

void screen_to_iso(int sx, int sy, const Camera *cam,
                   int *out_row, int *out_col);

#endif /* RENDER_H */
