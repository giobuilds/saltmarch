/*  ship.c  --  Vessels moving goods between islands  */

#include "ship.h"
#include "simlog.h"
#include <stdio.h>   /* snprintf for the voyage-record serialiser */

int ship_transfer_at(Ship *sh, Island *isl, ResourceType res, int qty)
{
    if (res < 0 || res >= RES_COUNT) return 0;

    if (qty > 0) {                        /* island -> ship */
        if (res != RES_GOLD) {
            int space = ship_hold_capacity(sh) - sh->cargo[res];
            if (qty > space) qty = space;
        }
        if (qty > isl->stockpile.amount[res]) qty = isl->stockpile.amount[res];
        if (qty <= 0) return 0;

        stockpile_add(&isl->stockpile, res, -qty);
        sh->cargo[res] += qty;
        return qty;
    }

    if (qty < 0) {                        /* ship -> island */
        int want = -qty;
        if (want > sh->cargo[res]) want = sh->cargo[res];

        /* stockpile_add() silently clamps non-Gold to capacity, which
         * would destroy the overflow, so only move what actually fits
         * and leave the remainder in the hold. */
        if (res != RES_GOLD) {
            int headroom = isl->stockpile.capacity - isl->stockpile.amount[res];
            if (headroom < 0) headroom = 0;
            if (want > headroom) want = headroom;
        }
        if (want <= 0) return 0;

        sh->cargo[res] -= want;
        stockpile_add(&isl->stockpile, res, want);
        return want;
    }

    return 0;
}

int ship_transfer_escrow(Ship *sh, Island *isl, ResourceType res, int qty)
{
    if (res < 0 || res >= RES_COUNT) return 0;

    if (qty > 0) {                        /* escrow -> ship */
        if (res != RES_GOLD) {
            int space = ship_hold_capacity(sh) - sh->cargo[res];
            if (qty > space) qty = space;
        }
        if (qty > isl->escrow[res]) qty = isl->escrow[res];
        if (qty <= 0) return 0;

        isl->escrow[res] -= qty;
        sh->cargo[res]   += qty;
        return qty;
    }

    if (qty < 0) {                        /* ship -> escrow (uncapped) */
        int want = -qty;
        if (want > sh->cargo[res]) want = sh->cargo[res];
        if (want <= 0) return 0;

        sh->cargo[res]   -= want;
        isl->escrow[res] += want;
        return want;
    }

    return 0;
}

/* A ship that has just docked and is running a route: drop what it
 * brought, pick up what goes back, and set sail again. */
static void route_turnaround(Ship *s, Island islands[], int island_count,
                             uint64_t sim_tick_no)
{
    ResourceType inbound, outbound;
    int          next;

    if (s->route_a < 0 || s->route_a >= island_count ||
        s->route_b < 0 || s->route_b >= island_count) {
        s->route_active = 0;
        return;
    }

    /* leg 0 means the ship was sailing A->B carrying res_ab. */
    if (s->route_leg == 0) {
        inbound  = s->route_res_ab;
        outbound = s->route_res_ba;
        next     = s->route_a;
        s->route_leg = 1;
    } else {
        inbound  = s->route_res_ba;
        outbound = s->route_res_ab;
        next     = s->route_b;
        s->route_leg = 0;
    }

    if (inbound != RES_COUNT)
        ship_transfer_at(s, &islands[s->at_island], inbound,
                         -ship_hold_capacity(s));
    if (outbound != RES_COUNT)
        ship_transfer_at(s, &islands[s->at_island], outbound, s->route_qty);

    if (next != s->at_island) {
        s->from_island    = s->at_island;
        s->to_island      = next;
        s->at_island      = -1;
        s->departure_tick = sim_tick_no;   /* the voyage starts now */
        s->progress       = 0.0f;
    }
}

int shipment_is_raided(uint32_t world_seed, int route_id,
                       uint64_t booked_tick, uint32_t seller,
                       int chance_per_mille)
{
    uint32_t h = 2166136261u;
    uint32_t parts[5];
    int      i;

    parts[0] = world_seed ^ 0x5EA1D0u;   /* a different space from voyages */
    parts[1] = (uint32_t)route_id;
    parts[2] = (uint32_t)(booked_tick & 0xFFFFFFFFu);
    parts[3] = (uint32_t)(booked_tick >> 32);
    parts[4] = seller;

    for (i = 0; i < 5; i++) {
        h ^= parts[i];
        h *= 16777619u;
    }
    return (int)(h % 1000u) < chance_per_mille;
}

int voyage_is_raided(uint32_t world_seed, int ship_id,
                     uint64_t departure_tick, int from, int to)
{
    /* FNV-1a over the voyage's identity. Not cryptographic and not
     * trying to be: it needs to be well-mixed, integer-only and
     * identical on every platform, which rules out anything touching
     * floating point or the C library's rand. */
    uint32_t h = 2166136261u;
    uint32_t parts[5];
    int      i;

    parts[0] = world_seed;
    parts[1] = (uint32_t)ship_id;
    parts[2] = (uint32_t)(departure_tick & 0xFFFFFFFFu);
    parts[3] = (uint32_t)(departure_tick >> 32);
    parts[4] = (uint32_t)((from << 8) ^ to);

    for (i = 0; i < 5; i++) {
        h ^= parts[i];
        h *= 16777619u;
    }
    return (int)(h % 1000u) < PIRACY_CHANCE_PER_MILLE;
}

int intercept_odds(int attacker_guns, int defender_guns)
{
    int total, odds;

    if (attacker_guns < 0) attacker_guns = 0;
    if (defender_guns < 0) defender_guns = 0;

    /* Two unarmed hulls: a boarding scuffle, and the attacker's only
     * advantage is having chosen the moment. */
    total = attacker_guns + defender_guns;
    if (total == 0) return INTERCEPT_ATTACKER_ODDS;

    odds = attacker_guns * 100 / total;

    if (odds < INTERCEPT_MIN_ODDS) odds = INTERCEPT_MIN_ODDS;
    if (odds > INTERCEPT_MAX_ODDS) odds = INTERCEPT_MAX_ODDS;
    return odds;
}

int intercept_attacker_wins(uint32_t world_seed,
                            int attacker_ship, uint64_t attacker_departure,
                            int target_ship, uint64_t target_departure,
                            int attacker_guns, int defender_guns)
{
    /* Byte-wise FNV with a finishing avalanche, for the reason */
    uint32_t h = 2166136261u;
    uint32_t parts[6];
    int      i, b;

    parts[0] = world_seed;
    parts[1] = (uint32_t)attacker_ship;
    parts[2] = (uint32_t)(attacker_departure & 0xFFFFFFFFu);
    parts[3] = (uint32_t)target_ship;
    parts[4] = (uint32_t)(target_departure & 0xFFFFFFFFu);
    parts[5] = 0x9E3779B9u;   /* a different mix from the piracy roll */

    for (i = 0; i < 6; i++)
        for (b = 0; b < 4; b++) {
            h ^= (parts[i] >> (b * 8)) & 0xFFu;
            h *= 16777619u;
        }

    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;

    return (int)(h % 100u) < intercept_odds(attacker_guns, defender_guns);
}

/* The raid itself: pirates take a share of everything aboard. Called
 * exactly once per voyage, at the halfway tick. */
static void voyage_raid(Ship *s, int ship_id)
{
    int r, taken = 0;

    for (r = 0; r < RES_COUNT; r++) {
        int take = (s->cargo[r] * PIRACY_TAKE_NUMERATOR +
                    PIRACY_TAKE_DENOMINATOR - 1) / PIRACY_TAKE_DENOMINATOR;
        if (take <= 0) continue;
        s->cargo[r] -= take;
        taken       += take;
    }

    if (taken > 0)
        sim_log("Ship %d was raided mid-voyage: %d units taken",
                ship_id, taken);
}

void ships_update(const Sea *sea, Ship ships[], int ship_count,
                  Island islands[], int island_count, uint64_t sim_tick_no,
                  uint32_t world_seed)
{
    int i;

    for (i = 0; i < ship_count; i++) {
        Ship    *s = &ships[i];
        uint64_t elapsed;
        uint32_t crossing;

        if (!s->active) continue;

        if (s->at_island >= 0) {
            /* Docked. Only a route makes a ship leave on its own. */
            if (s->route_active)
                route_turnaround(s, islands, island_count, sim_tick_no);
            continue;
        }

        /* At sea. Arrival is an exact integer test on the tick; progress
         * is only a cached 0..1 derivation for the renderer. */
        crossing = sea_crossing_ticks(sea, s->from_island, s->to_island);
        elapsed  = sim_tick_no - s->departure_tick;

        /* The raid check, exactly once, at the halfway mark. Testing
         * for equality rather than ">=" is what makes it once: a ship
         * that has already passed the midpoint is not robbed again by
         * the next tick. */
        if (elapsed == (uint64_t)(crossing / 2u) &&
            voyage_is_raided(world_seed, i, s->departure_tick,
                             s->from_island, s->to_island))
            voyage_raid(s, i);

        if (elapsed >= (uint64_t)crossing) {
            s->at_island = s->to_island;   /* arrived */
            s->progress  = 0.0f;
        } else {
            s->progress = (float)elapsed / (float)crossing;
        }
    }
}

/* ---- Voyage record (the wire format) --------------------- */
VoyageRecord voyage_record_make(const Ship *sh, int ship_id, uint32_t player_id)
{
    VoyageRecord v;
    int          i;

    v.player_id      = player_id;
    v.ship_id        = ship_id;
    v.from           = sh->from_island;
    v.to             = sh->to_island;
    v.departure_tick = sh->departure_tick;
    for (i = 0; i < RES_COUNT; i++)
        v.cargo[i] = sh->cargo[i];
    return v;
}

int voyage_record_to_json(const VoyageRecord *v, char *buf, size_t n)
{
    int off, i, w;

    off = snprintf(buf, n,
        "{\"player\":%u,\"ship\":%d,\"from\":%d,\"to\":%d,"
        "\"departure_tick\":%llu,\"cargo\":[",
        (unsigned)v->player_id, v->ship_id, v->from, v->to,
        (unsigned long long)v->departure_tick);
    if (off < 0 || (size_t)off >= n) return -1;

    for (i = 0; i < RES_COUNT; i++) {
        w = snprintf(buf + off, n - (size_t)off, "%s%d",
                     i ? "," : "", v->cargo[i]);
        if (w < 0 || (size_t)off + (size_t)w >= n) return -1;
        off += w;
    }

    w = snprintf(buf + off, n - (size_t)off, "]}");
    if (w < 0 || (size_t)off + (size_t)w >= n) return -1;
    return off + w;
}

int ships_cargo_total(const Ship ships[], int ship_count, ResourceType res)
{
    int i, total = 0;

    for (i = 0; i < ship_count; i++)
        if (ships[i].active)
            total += ships[i].cargo[res];

    return total;
}

/* ---- ship classes (MARITIME_PLAN Phase 5) ---------------- */
const ShipClassDef SHIP_CLASSES[SHIP_CLASS_COUNT] = {
    { "Merchantman", 0,  4, SHIP_CARGO_CAPACITY, SHIP_BUILD_COST_GOLD },
    { "Cutter",      3,  6, 20,                  SHIP_BUILD_COST_GOLD * 2 },
    { "Warship",     8, 12, 5,                   SHIP_BUILD_COST_GOLD * 4 }
};

int ship_hold_capacity(const struct Ship *sh)
{
    if (!sh) return SHIP_CARGO_CAPACITY;
    if (sh->klass < 0 || sh->klass >= SHIP_CLASS_COUNT)
        return SHIP_CARGO_CAPACITY;
    return SHIP_CLASSES[sh->klass].hold;
}

int ship_fighting_strength(const struct Ship *sh)
{
    int full, str;

    if (!sh || !sh->active || sh->guns <= 0) return 0;
    full = (sh->klass >= 0 && sh->klass < SHIP_CLASS_COUNT)
         ? SHIP_CLASSES[sh->klass].hull : 1;
    if (full <= 0) full = 1;

    str = sh->guns * sh->hull / full;
    return str < 1 ? 1 : str;
}
