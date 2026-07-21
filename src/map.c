/*  map.c  --  Tile map + procedural island generator  (Phase 2)
 *
 *  HOW THE GENERATOR WORKS
 *  ========================
 *  We build a heightmap using VALUE NOISE — the simplest noise
 *  algorithm that produces organic-looking terrain.
 *
 *  Step 1 – Value noise
 *    We divide the grid into a coarse lattice of "control points".
 *    Each control point gets a random float in [0, 1].
 *    Any point between lattice corners is BILINEARLY INTERPOLATED
 *    from its four surrounding corners.  This blurs the random
 *    values into smooth rolling hills.
 *
 *    We layer TWO octaves (two different lattice scales) and
 *    add them with different weights so we get both broad land
 *    masses AND fine coastal detail.
 *
 *  Step 2 – Island mask
 *    A radial falloff (distance from map centre) is multiplied
 *    onto the heightmap so the edges always become ocean.
 *    Without this, noise would produce land at the map border.
 *
 *  Step 3 – Thresholding
 *    The final float is bucketed into tile types:
 *      < 0.35  → WATER
 *      < 0.42  → SAND   (beach ring)
 *      < 0.70  → GRASS
 *      >= 0.70 → FOREST (elevated interior)
 *
 *  Step 4 – Gameplay metadata
 *    buildable and fertility are derived from type.
 *
 *  RANDOM NUMBER GENERATOR
 *  =======================
 *  We implement a minimal LCG (Linear Congruential Generator).
 *  K&R §2.7 discusses integer overflow which is central here:
 *  we deliberately let uint32_t wrap around — that wrap-around
 *  is the source of the randomness.  The multiplier and addend
 *  are the same constants used by Borland's classic C RTL.
 * 
 * FIX
 *  THE BUG THAT WAS FIXED
 *  ======================
 *  In the previous version, noise_build_lattice() was called inside
 *  the per-tile loop.  This meant every tile sampled a DIFFERENT
 *  random lattice, so there was no spatial coherence — the result
 *  looked like static noise rather than smooth terrain.
 *
 *  The fix: build each octave's lattice ONCE before the tile loop,
 *  then sample the same lattice for every tile in that octave.
 *
 *  NOISE OVERVIEW
 *  ==============
 *  We use VALUE NOISE with two octaves:
 *    Octave 0 (coarse, weight 0.70) — large land masses
 *    Octave 1 (fine,   weight 0.30) — coastal detail, inlets
 *
 *  An ISLAND MASK (radial falloff from centre) is multiplied onto
 *  the combined height so the map edges are always ocean.
 *
 *  HEIGHT → TILE TYPE thresholds:
 *    < 0.30  → WATER
 *    < 0.40  → SAND   (beach ring)
 *    < 0.72  → GRASS
 *    >= 0.72 → FOREST (elevated interior)
 */

#include "map.h"
#include <stddef.h>   /* NULL         */
#include <string.h>   /* memset       */

/* =========================================================
 * Section 1 – Minimal LCG random number generator
 * ========================================================= */

/* LCG state — local to this translation unit (static). */
static uint32_t lcg_state = 0;

static void lcg_seed(uint32_t seed)
{
    lcg_state = seed;
}

/* Returns a pseudo-random uint32_t and advances the state. */
static uint32_t lcg_next(void)
{
    /* Borland LCG constants */
    lcg_state = lcg_state * 22695477u + 1u;
    return lcg_state;
}

/* Returns a float in [0.0, 1.0). */
static float lcg_float(void)
{
    /* Use the upper 16 bits — they have better statistical
     * quality in an LCG than the lower bits. */
    return (float)((lcg_next() >> 16) & 0xFFFF) / 65536.0f;
}

/* =========================================================
 * Section 2 – Value noise helpers
 * ========================================================= */

/* A smaller lattice = larger, smoother blobs of terrain.
 * LATTICE_SIZE 6 means the entire 64-tile map is covered by
 * a 6x6 grid of control points — each cell spans ~10 tiles,
 * producing broad, island-sized land masses. */
#define LATTICE_SIZE 6

/* Two separate lattices — one per octave.
 * Both are filled once before the tile loop. */
static float lattice0[LATTICE_SIZE][LATTICE_SIZE];
static float lattice1[LATTICE_SIZE][LATTICE_SIZE];

static void build_lattice(float lat[LATTICE_SIZE][LATTICE_SIZE])
{
    int r, c;
    for (r = 0; r < LATTICE_SIZE; r++)
        for (c = 0; c < LATTICE_SIZE; c++)
            lat[r][c] = lcg_float();
}

/* Smooth interpolation (smoothstep).
 * t must be in [0, 1].  Returns a value in [0, 1] that
 * accelerates and decelerates at the endpoints — this
 * removes the blocky artefacts of plain linear interpolation.
 *
 *   f(t) = 3t² - 2t³
 */
static float smoothstep(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

/* Linear interpolation: a + (b-a)*t */
static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* Sample a lattice at continuous coords (u, v).
 * u and v are in [0, LATTICE_SIZE-1]. */
static float sample(float lat[LATTICE_SIZE][LATTICE_SIZE],
                    float u, float v)
{
    int   x0 = (int)u;
    int   y0 = (int)v;
    int   x1 = x0 + 1;
    int   y1 = y0 + 1;
    float fx, fy;

    /* Clamp to lattice bounds */
    if (x1 >= LATTICE_SIZE) x1 = LATTICE_SIZE - 1;
    if (y1 >= LATTICE_SIZE) y1 = LATTICE_SIZE - 1;

    fx = smoothstep(u - (float)x0);
    fy = smoothstep(v - (float)y0);

    return lerp(
        lerp(lat[y0][x0], lat[y0][x1], fx),
        lerp(lat[y1][x0], lat[y1][x1], fx),
        fy
    );
}

/* =========================================================
 * Section 3 – Island mask
 *
 * Returns 1.0 at the map centre, falling to 0.0 at the edges.
 * Using a power of 1.8 gives a rounder island than power 2
 * while still forcing clean ocean borders.
 * ========================================================= */
static float island_mask(int row, int col)
{
    float cx   = (MAP_COLS - 1) / 2.0f;
    float cy   = (MAP_ROWS - 1) / 2.0f;
    float dx   = ((float)col - cx) / cx;
    float dy   = ((float)row - cy) / cy;
    float dist = dx*dx + dy*dy;   /* 0 at centre, ~2 at corners */

    /* Clamp dist to [0, 1] then invert */
    if (dist > 1.0f) dist = 1.0f;

    /* Raise to a power < 2 for a rounder, fuller island */
    float mask = 1.0f - dist;
    /* Apply smoothstep so the falloff is gradual, not abrupt */
    return smoothstep(mask);
}

/* =========================================================
 * Section 4 – Classification and metadata
 * ========================================================= */

/* ---- Per-profile generation parameters -------------------
 * Terrain semantics live in the classification step, not in the
 * noise: biasing the noise fights the radial mask that defines island
 * shape, whereas thresholds here cleanly decide what the same
 * heightmap MEANS.
 *
 * hop_elev_min is the elevation above which grass is hop-fertile
 * rather than grain-fertile (they are exclusive — that exclusivity is
 * exactly what makes a hop-rich island grain-poor, and so what
 * creates the need to trade). 256 means "never": no elevation can
 * reach it, so the profile grows no hops at all.
 *
 * min_* are the requirements map_init()'s retry loop enforces, so a
 * Highland is never generated without the hops that are its entire
 * reason to exist, and the starting island is always playable. */
typedef struct {
    float water_max, sand_max, grass_max;
    int   hop_elev_min;
    int   min_hop, min_grain, min_forest;
} ProfileParams;

static const ProfileParams PROFILE_PARAMS[PROFILE_COUNT] = {
    /* Home: broad farmland and some woods, but NO hops — the reason
     * the player has to go looking for another island. */
    [PROFILE_TEMPERATE] = { 0.30f, 0.40f, 0.72f, 256,   0, 120,  8 },
    /* Highland: hop country. Grass sits high and splits into hop
     * above the cutoff / grain below, so grain is genuinely scarce —
     * a Malthouse here starves without imported Grain. */
    [PROFILE_HIGHLAND]  = { 0.30f, 0.38f, 0.60f, 103,  20,   8,  0 },
    /* Woodland: timber. Forest starts lower, squeezing farmland. */
    [PROFILE_WOODLAND]  = { 0.30f, 0.40f, 0.62f, 256,   0,  20, 60 },
    /* Atoll: a wide beach ring and little else. Fish and not much. */
    [PROFILE_ATOLL]     = { 0.30f, 0.50f, 0.80f, 256,   0,   0,  0 },
};

static TileType height_to_type(float h, MapProfile p)
{
    const ProfileParams *pp = &PROFILE_PARAMS[p];
    if (h < pp->water_max) return TILE_WATER;
    if (h < pp->sand_max)  return TILE_SAND;
    if (h < pp->grass_max) return TILE_GRASS;
    return TILE_FOREST;
}

static void tile_set_metadata(Tile *t, MapProfile p)
{
    switch (t->type) {
    case TILE_GRASS:
        t->buildable     = 1;
        t->fertility     = (t->elevation > PROFILE_PARAMS[p].hop_elev_min)
                           ? FERTILE_HOP : FERTILE_GRAIN;
        break;
    case TILE_SAND:
        t->buildable     = 1;
        t->fertility     = FERTILE_NONE;
        break;
    case TILE_FOREST:
        t->buildable     = 0;
        t->fertility     = FERTILE_NONE;
        break;
    case TILE_WATER:
    default:
        t->buildable     = 0;
        t->fertility     = FERTILE_NONE;
        break;
    }
}

/* =========================================================
 * Section 5 – Public API
 * ========================================================= */

static void generate_once(Map *map, uint32_t seed, MapProfile profile)
{
    int   r, c;
    float scale0, scale1;

    memset(map, 0, sizeof(Map));
    map->rows = MAP_ROWS;
    map->cols = MAP_COLS;
    map->seed    = seed;
    map->profile = profile;

    lcg_seed(seed);

    /* Build BOTH lattices once, before the tile loop.
     * This is the critical fix: every tile in octave 0 samples
     * the same lattice0, giving spatial coherence. */
    build_lattice(lattice0);
    build_lattice(lattice1);

    /* How much of the lattice each tile maps to.
     * (LATTICE_SIZE-1) / (MAP_SIZE-1) keeps the rightmost tile
     * at exactly lattice coordinate LATTICE_SIZE-1. */
    scale0 = (float)(LATTICE_SIZE - 1) / (float)(MAP_COLS - 1);
    scale1 = scale0 * 2.0f;   /* octave 1 is double frequency */

    for (r = 0; r < MAP_ROWS; r++) {
        for (c = 0; c < MAP_COLS; c++) {
            float u0, v0, u1, v1, h0, h1, h;
            Tile *t = &map->tiles[r][c];

            /* Octave 0: coarse large shapes */
            u0 = (float)c * scale0;
            v0 = (float)r * scale0;
            h0 = sample(lattice0, u0, v0);

            /* Octave 1: fine coastal detail (double frequency) */
            u1 = (float)c * scale1;
            v1 = (float)r * scale1;
            /* Clamp so we don't sample outside the lattice */
            if (u1 > LATTICE_SIZE - 1) u1 = LATTICE_SIZE - 1.0f;
            if (v1 > LATTICE_SIZE - 1) v1 = LATTICE_SIZE - 1.0f;
            h1 = sample(lattice1, u1, v1);

            /* Combine: 70% coarse + 30% fine */
            h = 0.70f * h0 + 0.30f * h1;

            /* Multiply by island mask to force ocean at edges */
            h *= island_mask(r, c);

            t->elevation = (int)(h * 255.0f);
            t->type      = height_to_type(h, profile);
            tile_set_metadata(t, profile);
        }
    }
}

/* Does this map satisfy its profile's minimum-resource contract? */
static int profile_satisfied(const Map *map, MapProfile p)
{
    const ProfileParams *pp = &PROFILE_PARAMS[p];
    int r, c, hop = 0, grain = 0, forest = 0;

    for (r = 0; r < MAP_ROWS; r++) {
        for (c = 0; c < MAP_COLS; c++) {
            const Tile *t = &map->tiles[r][c];
            if (t->type == TILE_FOREST)        forest++;
            if (t->fertility & FERTILE_HOP)    hop++;
            if (t->fertility & FERTILE_GRAIN)  grain++;
        }
    }

    return hop    >= pp->min_hop
        && grain  >= pp->min_grain
        && forest >= pp->min_forest;
}

/* ---- map_init -------------------------------------------
 * Generate, then check the profile's contract, retrying with a
 * derived seed until it holds. Without this, a "Highland" could roll
 * zero hop tiles — which is a broken game rather than interesting
 * scarcity, since hops are the whole reason to colonise it. Sampling
 * showed the old unconditional generator produced zero hop tiles on
 * roughly a third of seeds.
 *
 * map->seed keeps the REQUESTED seed, not the working one: the loop
 * is deterministic given (requested seed, profile), so saving the
 * request is what makes a save reproduce the island exactly. */
void map_init(Map *map, uint32_t seed, MapProfile profile)
{
    uint32_t working = seed;
    int      attempt;

    for (attempt = 0; attempt < 32; attempt++) {
        generate_once(map, working, profile);
        if (profile_satisfied(map, profile)) break;
        working = working * 1664525u + 1013904223u;
    }

    /* Falling out of the loop un-satisfied is possible in principle;
     * accept the last attempt rather than spin forever. */
    map->seed    = seed;
    map->profile = profile;
}

Tile *map_get_tile(Map *map, int row, int col)
{
    if (row < 0 || row >= map->rows ||
        col < 0 || col >= map->cols)
        return NULL;
    return &map->tiles[row][col];
}