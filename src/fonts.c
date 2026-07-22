/*  fonts.c  --  SDL_ttf wrapper
 *
 *  We keep a static array of two TTF_Font pointers, both loaded from
 *  the font bundled in assets/fonts/ next to the executable.
 *
 *  Resolution order (see font_resolve_path):
 *    1. <exe dir>/assets/fonts/...   — the shipped layout
 *    2. ./assets/fonts/...           — running from the source tree
 *    3. the old Fedora system path   — last-resort fallback
 *
 *  1 and 2 differ because the build puts the binary in build/ while
 *  the assets live at the repo root, so a developer running
 *  ./build/saltmarch from the project directory has a different
 *  relative layout to an installed copy. Trying both means the same
 *  binary works either way with no install step.
 */

#include "fonts.h"
#include <SDL3/SDL.h>


/* Point sizes for each FontSize enum value */
static const float FONT_PT[FONT_SIZE_COUNT] = { 14.0f, 11.0f };

/* Static font handles — NULL until fonts_init() succeeds */
static TTF_Font *fonts[FONT_SIZE_COUNT] = { NULL, NULL };
static int       fonts_ready = 0;

/* The path fonts_init() actually succeeded with, for logging. */
static char      fonts_path[1024];

/* Fill `out` with the first candidate path that exists, returning 1,
 * or 0 if none do. SDL_GetBasePath() is what makes this portable: it
 * returns the executable's directory on Linux, macOS and Windows
 * alike, so the bundled asset is found regardless of the working
 * directory the game was launched from. */
static int font_resolve_path(char *out, size_t out_len)
{
    const char *base = SDL_GetBasePath();   /* SDL-owned, do not free */
    const char *candidates[3];
    char        exe_rel[1024];
    int         i, n = 0;

    if (base) {
        SDL_snprintf(exe_rel, sizeof(exe_rel), "%s%s", base, FONT_RELATIVE_PATH);
        candidates[n++] = exe_rel;
    }
    candidates[n++] = FONT_RELATIVE_PATH;   /* cwd — source-tree runs */
    /* Last resort: the distro-installed copy this game used to require
     * outright. Harmless where it does not exist. */
    candidates[n++] = "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf";

    for (i = 0; i < n; i++) {
        SDL_IOStream *probe = SDL_IOFromFile(candidates[i], "rb");
        if (probe) {
            SDL_CloseIO(probe);
            SDL_strlcpy(out, candidates[i], out_len);
            return 1;
        }
    }
    return 0;
}

/* ---- fonts_init ---------------------------------------- */
int fonts_init(void)
{
    int i;

    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return 0;
    }

    if (!font_resolve_path(fonts_path, sizeof(fonts_path))) {
        SDL_Log("Font not found. Looked for %s next to the executable, "
                "in the working directory, and in the system font path.",
                FONT_RELATIVE_PATH);
        TTF_Quit();
        return 0;
    }

    for (i = 0; i < FONT_SIZE_COUNT; i++) {
        fonts[i] = TTF_OpenFont(fonts_path, FONT_PT[i]);
        if (!fonts[i]) {
            SDL_Log("TTF_OpenFont(%s, %f) failed: %s",
                    fonts_path, (double)FONT_PT[i], SDL_GetError());
            /* Close any already-opened fonts and bail */
            while (--i >= 0) TTF_CloseFont(fonts[i]);
            TTF_Quit();
            return 0;
        }
    }

    fonts_ready = 1;
    SDL_Log("Fonts loaded: %s", fonts_path);
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
