/*  ship.c  --  Vessels moving goods between islands  */

#include "ship.h"
#include <stdio.h>   /* snprintf for the voyage-record serialiser */

int ship_transfer_at(Ship *sh, Island *isl, ResourceType res, int qty)
{
    if (res < 0 || res >= RES_COUNT) return 0;

    if (qty > 0) {                        /* island -> ship */
        if (res != RES_GOLD) {
            int space = SHIP_CARGO_CAPACITY - sh->cargo[res];
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
            int space = SHIP_CARGO_CAPACITY - sh->cargo[res];
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
 * brought, pick up what goes back, and set sail again.
 *
 * Departure is unconditional. Waiting for a full hold — or for any
 * cargo at all — would deadlock the route the first time the supplying
 * island ran dry, and the route would never recover even once
 * production resumed. An empty run costs only time. */
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
        ship_transfer_at(s, &islands[s->at_island], inbound, -SHIP_CARGO_CAPACITY);
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

void ships_update(Ship ships[], int ship_count,
                  Island islands[], int island_count, uint64_t sim_tick_no)
{
    int i;

    for (i = 0; i < ship_count; i++) {
        Ship    *s = &ships[i];
        uint64_t elapsed;

        if (!s->active) continue;

        if (s->at_island >= 0) {
            /* Docked. Only a route makes a ship leave on its own. */
            if (s->route_active)
                route_turnaround(s, islands, island_count, sim_tick_no);
            continue;
        }

        /* At sea. Arrival is an exact integer test on the tick; progress
         * is only a cached 0..1 derivation for the renderer. */
        elapsed = sim_tick_no - s->departure_tick;
        if (elapsed >= (uint64_t)SHIP_VOYAGE_TICKS) {
            s->at_island = s->to_island;   /* arrived */
            s->progress  = 0.0f;
        } else {
            s->progress = (float)elapsed / (float)SHIP_VOYAGE_TICKS;
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
