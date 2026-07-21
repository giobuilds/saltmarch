#ifndef FONTS_H
#define FONTS_H
#define FONT_PATH "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf"

/* =========================================================
 * fonts.h  --  SDL_ttf wrapper  (Phase 5)
 *
 * Provides two font sizes used throughout the game:
 *   FONT_NORMAL (14pt) – resource counts, building names
 *   FONT_SMALL  (11pt) – tooltips, small labels
 *
 * All text rendering goes through font_draw_text() so the
 * rest of the codebase never calls SDL_ttf directly.
 * If SDL_ttf is unavailable the module degrades gracefully
 * — fonts_init() returns 0 and font_draw_text() is a no-op.
 *
 * Font path: Liberation Sans is part of the
 * liberation-fonts package, present on all Fedora systems.
 * ========================================================= */

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

/* Font size identifiers */
typedef enum {
    FONT_NORMAL = 0,   /* 14pt — main HUD text     */
    FONT_SMALL  = 1,   /* 11pt — tooltips / labels */
    FONT_SIZE_COUNT
} FontSize;

/* Initialise SDL_ttf and load both font sizes.
 * Returns 1 on success, 0 on failure (game continues without text). */
int  fonts_init(void);

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
