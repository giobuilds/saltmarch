/*  pirate.c  --  Something to hunt
 *                (MARITIME_PLAN Phase 5b)
 *
 *  See pirate.h for why piracy stopped being a hash and became a thing
 *  in the water. This file holds the fleet, where it sits and when it
 *  moves; the rules that take cargo off a shipment and hand plunder to
 *  whoever kills a fleet live in game.c beside the other sim mutators,
 *  because they touch bookings, stockpiles and knowledge.
 *
 *  Integer-only and seeded, like every other derivation in this sim: a
 *  fleet's lair and its wanderings must come out the same on every
 *  machine, or two clients disagree about which water is dangerous.
 */

#include "pirate.h"

#include <stddef.h>
#include <string.h>

/* The same shape of mix as survey.c's, and for the same reason: fed
 * whole words and read through a small modulus, FNV's low bits stay
 * correlated across the narrow ranges this is called with — a handful
 * of fleet indices and a handful of move intervals. Byte-wise, with a
 * finishing avalanche. */
static uint32_t pirate_hash(uint32_t seed, uint32_t a, uint32_t b)
{
    uint32_t h = 2166136261u;
    uint32_t parts[3];
    int      i, k;

    parts[0] = seed;
    parts[1] = a;
    parts[2] = b;

    for (i = 0; i < 3; i++)
        for (k = 0; k < 4; k++) {
            h ^= (parts[i] >> (k * 8)) & 0xFFu;
            h *= 16777619u;
        }

    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

void pirate_init(PirateSea *ps, const Sea *sea, uint32_t seed)
{
    int i;

    memset(ps, 0, sizeof(*ps));
    if (sea->waypoint_count <= 0) return;

    ps->count = MAX_PIRATES;
    for (i = 0; i < MAX_PIRATES; i++) {
        Pirate *p = &ps->fleet[i];

        p->active   = 1;
        p->waypoint = (int32_t)(pirate_hash(seed, 0xB0A7u, (uint32_t)i) %
                                (uint32_t)sea->waypoint_count);
        p->guns     = PIRATE_GUNS;
        p->hull     = PIRATE_HULL;
        p->chart    = -1;
        p->last_move_tick = 0;
    }
}

SeaPos pirate_pos(const PirateSea *ps, const Sea *sea, int i)
{
    SeaPos zero;

    zero.x = 0;
    zero.y = 0;

    if (i < 0 || i >= ps->count) return zero;
    if (!ps->fleet[i].active) return zero;
    if (ps->fleet[i].waypoint < 0 ||
        ps->fleet[i].waypoint >= sea->waypoint_count) return zero;

    return sea->waypoint[ps->fleet[i].waypoint].pos;
}

int pirate_at(const PirateSea *ps, const Sea *sea, SeaPos p)
{
    int i;

    for (i = 0; i < ps->count; i++) {
        SeaPos lair;

        if (!ps->fleet[i].active) continue;
        lair = pirate_pos(ps, sea, i);
        if (sea_distance(lair, p) <= (uint32_t)PIRATE_STRIKE_RADIUS)
            return i;
    }
    return -1;
}

void pirate_update(PirateSea *ps, const Sea *sea, uint64_t tick,
                   uint32_t seed)
{
    int i;

    if (sea->waypoint_count <= 0) return;
    if (tick == 0 || tick % PIRATE_MOVE_INTERVAL_TICKS != 0) return;

    for (i = 0; i < ps->count; i++) {
        Pirate  *p = &ps->fleet[i];
        uint32_t era, pick;

        if (!p->active) continue;

        /* Keyed on which interval it is rather than on the raw tick, so
         * a world restored from a checkpoint mid-interval moves to the
         * same place as the one that saved it. */
        era  = (uint32_t)(tick / PIRATE_MOVE_INTERVAL_TICKS);
        pick = pirate_hash(seed, (uint32_t)i ^ 0x5A17u, era) %
               (uint32_t)sea->waypoint_count;

        /* Never stand still — a "move" that landed on the same water
         * would read as the fleet having been missed. */
        if ((int32_t)pick == p->waypoint)
            pick = (pick + 1u) % (uint32_t)sea->waypoint_count;

        p->waypoint       = (int32_t)pick;
        p->last_move_tick = tick;
    }
}
