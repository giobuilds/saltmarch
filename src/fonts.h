#ifndef FONTS_H
#define FONTS_H

/* =========================================================
 * fonts.h  --  SDL_ttf wrapper
 *
 * Provides two font sizes used throughout the game:
 *   FONT_NORMAL (14pt) – resource counts, building names
 *   FONT_SMALL  (11pt) – tooltips, small labels
 *
 * All text rendering goes through font_draw_text() so the
 * rest of the codebase never calls SDL_ttf directly.
 *
 * WHERE THE FONT COMES FROM
 * =========================
 * The font is BUNDLED (assets/fonts/) and loaded relative to the
 * executable via SDL_GetBasePath(), so the same code works on Linux,
 * macOS and Windows.
 *
 * It used to be a hardcoded /usr/share/fonts/... path, which existed
 * only on Fedora. That made the game unusable anywhere else — and
 * silently so, because a missing font is not a fatal error: every
 * resource count, price and menu label simply vanished while the game
 * carried on running. fonts_init() returning 0 is now something the
 * caller is expected to treat as a real failure.
 *
 * Liberation Sans is OFL-1.1 licensed, which permits redistribution;
 * the licence travels with it in assets/fonts/.
 * ========================================================= */

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
