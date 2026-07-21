#ifndef SHIP_H
#define SHIP_H

/* =========================================================
 * ship.h  --  Vessels moving goods between islands
 *
 * A ship is world-scoped, NOT part of any Island: while at sea it
 * belongs to neither end of its voyage. That is also the load-bearing
 * invariant of the whole feature — cargo in a ship's hold is in
 * nobody's stockpile, so goods genuinely travel rather than teleport.
 *
 * Because islands are separate Maps there is no shared sea to sail
 * across, so ships have no tile position at all: a voyage is a
 * `progress` fraction between two island nodes, drawn on the world
 * overlay (world_ui.c). Keeping ships entirely out of tile space is
 * what makes the separate-Map design cheap.
 *
 * The route_* fields are unused in this phase but present and zeroed
 * from the start, so adding automated trade routes needs no
 * save-format change.
 * ========================================================= */

#include "island.h"
#include "resource.h"

#define MAX_SHIPS            8
/* Per-resource hold limit for physical goods. RES_GOLD is exempt
 * (see game_ship_transfer) for the same reason it is exempt from
 * stockpile capacity: it is currency, not something that takes up
 * hold space -- and a colony's founding grant is far larger than
 * any sane bulk-cargo limit. */
#define SHIP_CARGO_CAPACITY  50
#define SHIP_VOYAGE_SECONDS  20.0f /* one island-to-island crossing */

/* Gold a ship must be carrying to found a colony. The new island
 * starts with exactly this much, which is what lets it buy its first
 * buildings — see the founding-grant note in resource.h's BUY_PRICE
 * comment: a colony that cannot pay for anything is stranded. */
#define COLONY_FOUNDING_GOLD 400

typedef struct {
    int   active;
    int   at_island;      /* island index while docked, -1 at sea    */
    int   from_island;
    int   to_island;
    float progress;       /* 0..1 along the current voyage           */
    int   cargo[RES_COUNT];

    /* Phase-4 trade-route fields: declared now so the save format
     * does not change again when routes land. */
    int          route_active;
    int          route_a, route_b;
    ResourceType route_res_ab, route_res_ba;
    int          route_qty;
    int          route_leg;      /* 0 = A->B, 1 = B->A */
} Ship;

/* Move `qty` units of `res` between a ship's hold and a SPECIFIC
 * island (positive loads onto the ship, negative unloads), clamped by
 * what is actually present, by the hold's per-resource limit, and by
 * the island's storage capacity. RES_GOLD is exempt from both limits,
 * being currency rather than bulk cargo. Returns units actually
 * moved, which may be fewer than asked — partial transfers are always
 * preferred to refusing, because a ship that insists on a full hold
 * deadlocks a route the moment supply dips.
 *
 * Shared by the manual Load/Unload buttons (via game_ship_transfer,
 * which adds the "must be docked at the island you're looking at"
 * rule) and by automated routes, so the two cannot drift apart on
 * something as easy to get wrong as capacity clamping. */
int ship_transfer_at(Ship *sh, Island *isl, ResourceType res, int qty);

/* Advance every voyage, and run any active trade route: on arrival,
 * unload the inbound good, load the outbound one, and depart again.
 * Needs the islands because a route moves goods into and out of their
 * stockpiles without the player being present. */
void ships_update(Ship ships[], int ship_count,
                  Island islands[], int island_count, float dt);

/* Total units of `res` currently in transit or sitting in holds —
 * the term that makes world conservation checkable: for any resource,
 * sum(island stockpiles) + ships_cargo_total() must never change
 * except where something is actually produced or consumed. */
int ships_cargo_total(const Ship ships[], int ship_count, ResourceType res);

#endif /* SHIP_H */
