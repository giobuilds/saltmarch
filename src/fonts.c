/* fonts.c  --  SDL_ttf wrapper */

#include "fonts.h"
#include <SDL3/SDL.h>
#include <string.h>

/* Point sizes for each FontSize enum value */
static const float FONT_PT[FONT_SIZE_COUNT] = { 14.0f, 11.0f };

/* Static font handles — NULL until fonts_init() succeeds */
static TTF_Font *fonts[FONT_SIZE_COUNT] = { NULL, NULL };
static int       fonts_ready = 0;

/* The path fonts_init() actually succeeded with, for logging. */
static char      fonts_path[1024];

/* ---- the text cache (UI_PLAN M4) --------------------------- */
#define TEXT_CACHE_SLOTS 256

typedef struct {
    TTF_Text *text;
    char      str[64];
    int       size;
    int       used;
} TextCacheEntry;

static TTF_TextEngine *text_engine = NULL;
static TextCacheEntry  text_cache[TEXT_CACHE_SLOTS];

/* Counters for the F10 overlay: evidence rather than a claim that the
 * cache is doing something. */
static int cache_hits, cache_misses;

static uint32_t text_key_hash(int size, const char *s)
{
    uint32_t h = 2166136261u ^ (uint32_t)size;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

/* The TTF_Text for this (size, string), created on first use. NULL if
 * the string is too long for the cache, in which case the caller falls
 * back to the old uncached path — correctness first, speed second. */
static TTF_Text *text_cached(SDL_Renderer *renderer, int size,
                             const char *str)
{
    uint32_t        slot;
    TextCacheEntry *e;

    if (!text_engine) {
        text_engine = TTF_CreateRendererTextEngine(renderer);
        if (!text_engine) return NULL;
    }
    if (strlen(str) >= sizeof(e->str)) return NULL;

    slot = text_key_hash(size, str) % TEXT_CACHE_SLOTS;
    e    = &text_cache[slot];

    if (e->used && e->size == size && strcmp(e->str, str) == 0) {
        cache_hits++;
        return e->text;
    }

    cache_misses++;
    if (e->text) TTF_DestroyText(e->text);

    e->text = TTF_CreateText(text_engine, fonts[size], str, 0);
    if (!e->text) { e->used = 0; return NULL; }

    SDL_strlcpy(e->str, str, sizeof(e->str));
    e->size = size;
    e->used = 1;
    return e->text;
}

static void text_cache_clear(void)
{
    int i;
    for (i = 0; i < TEXT_CACHE_SLOTS; i++) {
        if (text_cache[i].text) TTF_DestroyText(text_cache[i].text);
        text_cache[i].text = NULL;
        text_cache[i].used = 0;
    }
    if (text_engine) {
        TTF_DestroyRendererTextEngine(text_engine);
        text_engine = NULL;
    }
}

void fonts_cache_stats(int *hits, int *misses)
{
    if (hits)   *hits   = cache_hits;
    if (misses) *misses = cache_misses;
}



/* Fill `out` with the first candidate path that exists, returning. */
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
    /* Before the fonts: a TTF_Text holds a reference to the font it was
     * created from. */
    text_cache_clear();
    for (i = 0; i < FONT_SIZE_COUNT; i++) {
        if (fonts[i]) TTF_CloseFont(fonts[i]);
        fonts[i] = NULL;
    }
    TTF_Quit();
    fonts_ready = 0;
}

/* ---- font_draw_text ------------------------------------ */
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
    TTF_Text    *cached;

    if (!fonts_ready || size >= FONT_SIZE_COUNT) return 0;
    if (!text || text[0] == '\0') return 0;

    /* The fast path: a prepared TTF_Text, recoloured and drawn. */
    cached = text_cached(renderer, (int)size, text);
    if (cached) {
        TTF_SetTextColor(cached, colour.r, colour.g, colour.b, colour.a);
        return TTF_DrawRendererText(cached, (float)x, (float)y) ? 1 : 0;
    }

    /* The slow path, kept for strings too long to cache and for the
     * case where the text engine could not be created at all: render a
     * surface, upload it, draw it, throw it away. */
    surf = TTF_RenderText_Blended(fonts[size], text, 0, colour);
    if (!surf) return 0;

    tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    if (!tex) return 0;

    dst.x = (float)x;
    dst.y = (float)y;
    dst.w = (float)tex->w;
    dst.h = (float)tex->h;
    ret = SDL_RenderTexture(renderer, tex, NULL, &dst) ? 1 : 0;
    SDL_DestroyTexture(tex);
    return ret;
}

/* ---- font_measure_text ----------------------------------- */
int font_measure_text(FontSize size, const char *text,
                      int *out_w, int *out_h)
{
    if (!fonts_ready || size >= FONT_SIZE_COUNT) return 0;
    if (!text || text[0] == '\0') return 0;
    return TTF_GetStringSize(fonts[size], text, 0, out_w, out_h) ? 1 : 0;
}
