/*  fonts.c  --  SDL_ttf wrapper  (Phase 5)
 *
 *  We keep a static array of two TTF_Font pointers.
 *  fonts_init() loads both sizes from the Liberation Sans
 *  path on Fedora.  If loading fails we log a warning and
 *  continue — the game is still playable without text.
 */

#include "fonts.h"
#include <SDL3/SDL.h>


/* Point sizes for each FontSize enum value */
static const float FONT_PT[FONT_SIZE_COUNT] = { 14.0f, 11.0f };

/* Static font handles — NULL until fonts_init() succeeds */
static TTF_Font *fonts[FONT_SIZE_COUNT] = { NULL, NULL };
static int       fonts_ready = 0;

/* ---- fonts_init ---------------------------------------- */
int fonts_init(void)
{
    int i;

    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return 0;
    }

    for (i = 0; i < FONT_SIZE_COUNT; i++) {
        fonts[i] = TTF_OpenFont(FONT_PATH, FONT_PT[i]);
        if (!fonts[i]) {
            SDL_Log("TTF_OpenFont(%s, %f) failed: %s",
                    FONT_PATH, FONT_PT[i], SDL_GetError());
            /* Close any already-opened fonts and bail */
            while (--i >= 0) TTF_CloseFont(fonts[i]);
            TTF_Quit();
            return 0;
        }
    }

    fonts_ready = 1;
    SDL_Log("Fonts loaded: %s", FONT_PATH);
    return 1;
}

/* ---- fonts_quit ---------------------------------------- */
void fonts_quit(void)
{
    int i;
    if (!fonts_ready) return;
    for (i = 0; i < FONT_SIZE_COUNT; i++) {
        if (fonts[i]) TTF_CloseFont(fonts[i]);
        fonts[i] = NULL;
    }
    TTF_Quit();
    fonts_ready = 0;
}

/* ---- font_draw_text ------------------------------------
 * Renders a UTF-8 string using TTF_RenderText_Blended
 * (anti-aliased, RGBA surface) then uploads it as a
 * temporary SDL_Texture and draws it at (x, y).
 *
 * We create and destroy a texture per call.  This is not
 * the fastest approach — a glyph cache is the Phase 6
 * optimisation — but it is simple and correct for now.
 * -------------------------------------------------------- */
int font_draw_text(SDL_Renderer *renderer,
                   FontSize size,
                   const char *text,
                   int x, int y,
                   SDL_Color colour)
{
    SDL_Surface *surf = NULL;
    SDL_Texture *tex  = NULL;
    SDL_FRect    dst;
    int          ret  = 0;

    if (!fonts_ready || size >= FONT_SIZE_COUNT) return 0;
    if (!text || text[0] == '\0') return 0;

    /* Render to an RGBA surface (anti-aliased) */
    surf = TTF_RenderText_Blended(fonts[size], text, 0, colour);
    if (!surf) {
        SDL_Log("TTF_RenderText_Blended failed: %s", SDL_GetError());
        return 0;
    }

    /* Upload to GPU as a texture */
    tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    if (!tex) {
        SDL_Log("CreateTextureFromSurface failed: %s", SDL_GetError());
        return 0;
    }

    /* Draw at requested position */
    dst.x = (float)x;
    dst.y = (float)y;
    SDL_GetTextureSize(tex, &dst.w, &dst.h);
    ret = SDL_RenderTexture(renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);

    return ret ? 1 : 0;
}

/* ---- font_measure_text ----------------------------------- */
int font_measure_text(FontSize size, const char *text,
                      int *out_w, int *out_h)
{
    if (!fonts_ready || size >= FONT_SIZE_COUNT) return 0;
    if (!text || text[0] == '\0') return 0;
    return TTF_GetStringSize(fonts[size], text, 0, out_w, out_h) ? 1 : 0;
}
