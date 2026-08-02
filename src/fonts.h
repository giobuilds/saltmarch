#ifndef FONTS_H
#define FONTS_H

/* ========================================================= */

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

/* Font file, relative to the directory holding the executable. */
#define FONT_RELATIVE_PATH "assets/fonts/LiberationSans-Regular.ttf"

/* Font size identifiers */
typedef enum {
    FONT_NORMAL = 0,   /* 14pt — main HUD text     */
    FONT_SMALL  = 1,   /* 11pt — tooltips / labels */
    FONT_SIZE_COUNT
} FontSize;

/* Initialise SDL_ttf and load both font sizes.
 * Returns 1 on success, 0 on failure. A return of 0 means the game
 * will draw no text at all, which is not a usable state — callers
 * should surface it rather than continue quietly. */
int  fonts_init(void);

/* Text-cache counters since startup (UI_PLAN M4). Shown by the F10
 * overlay so the cache's effect is observable rather than asserted. */
void fonts_cache_stats(int *hits, int *misses);

/* Release all font resources and shut down SDL_ttf. */
void fonts_quit(void);

/* Draw a string at (x, y) in screen/logical coordinates.
 * colour is an SDL_Color {R, G, B, A}.
 * Returns 1 on success, 0 if fonts are unavailable. */
int  font_draw_text(SDL_Renderer *renderer,
                    FontSize size,
                    const char *text,
                    int x, int y,
                    SDL_Color colour);

/* Measure the pixel size `text` would occupy at `size` without
 * drawing it (used to size tooltip boxes to fit their label).
 * Returns 1 on success, 0 if fonts are unavailable. */
int  font_measure_text(FontSize size,
                       const char *text,
                       int *out_w, int *out_h);

#endif /* FONTS_H */
