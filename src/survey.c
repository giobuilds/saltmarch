/*  survey.c  --  Sending a scholar to find the fast water
 *                (MARITIME_PLAN Phase 3d)
 *
 *  See survey.h for what a survey costs and why the outcome is derived
 *  rather than rolled.
 *
 *  This file holds the container and the two outcome functions. The
 *  rules that commit a scholar, spend a chart and hand back a passage
 *  live in game.c beside the other sim_* mutators, because they touch
 *  stockpiles, population and knowledge and must run inside the same
 *  funnel as everything else that changes the world.
 */

#include "survey.h"

#include <stddef.h>
#include <string.h>

void survey_init(SurveyBoard *b)
{
    memset(b, 0, sizeof(*b));
}

int survey_active_count(const SurveyBoard *b, uint32_t player)
{
    int i, n = 0;

    for (i = 0; i < b->count; i++)
        if (b->mission[i].active && b->mission[i].owner == player) n++;
    return n;
}

/* FNV-1a over the mission's identity, with a different salt per
 * question so "did it succeed" and "was it lost" are independent.
 * Integer-only and identical on every platform, which rules out
 * anything touching floating point or the C library's rand.
 *
 * The salt goes in LAST and the result gets a finishing avalanche.
 * Plain FNV-1a over five words, with the salt folded into the first,
 * is not well enough mixed for this: the surrounding code asks the
 * question only for a narrow band of route ids and a narrow band of
 * ticks, and measured over that band one route came back with a 0%
 * loss rate across two hundred consecutive ticks while another sat at
 * 36%. Both should have been near 14%. A derived outcome that is
 * deterministic but visibly lumpy is worse than a random one, because
 * it reads to a player as the game having decided something about
 * them. Re-measure if this function changes. */
static uint32_t survey_hash(uint32_t world_seed, int route_id,
                            uint64_t start_tick, uint32_t owner,
                            uint32_t salt)
{
    uint32_t h = 2166136261u;
    uint32_t parts[6];
    int      i;

    parts[0] = world_seed;
    parts[1] = (uint32_t)route_id;
    parts[2] = (uint32_t)(start_tick & 0xFFFFFFFFu);
    parts[3] = (uint32_t)(start_tick >> 32);
    parts[4] = owner;
    parts[5] = salt;

    for (i = 0; i < 6; i++) {
        /* Byte-wise, as FNV is defined: feeding whole words in leaves
         * the low bits of a small integer doing almost all the work. */
        uint32_t v = parts[i];
        int      b;
        for (b = 0; b < 4; b++) {
            h ^= (v >> (b * 8)) & 0xFFu;
            h *= 16777619u;
        }
    }

    /* Finishing avalanche (the murmur3 finaliser). FNV's diffusion into
     * the HIGH bits is fine; it is the low bits — the ones `% 1000`
     * actually reads — that stay correlated without this. */
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

int survey_succeeds(uint32_t world_seed, int route_id, uint64_t start_tick,
                    uint32_t owner)
{
    uint32_t h = survey_hash(world_seed, route_id, start_tick, owner,
                             0x5C40u);
    return (int)(h % 1000u) >= SURVEY_FAIL_PER_MILLE;
}

int survey_is_lost(uint32_t world_seed, int route_id, uint64_t start_tick,
                   uint32_t owner)
{
    uint32_t h;

    /* Only a failed expedition can be lost — a scholar who found the
     * passage sailed home to report it. Asking the question the other
     * way round would let a success also drown its own crew. */
    if (survey_succeeds(world_seed, route_id, start_tick, owner)) return 0;

    h = survey_hash(world_seed, route_id, start_tick, owner, 0xD0FFu);
    return (int)(h % 1000u) < SURVEY_LOSS_PER_MILLE;
}
