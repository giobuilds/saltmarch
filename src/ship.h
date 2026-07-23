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
#include <stdint.h>
#include <stddef.h>   /* size_t */

#define MAX_SHIPS            8
/* Per-resource hold limit for physical goods. RES_GOLD is exempt
 * (see game_ship_transfer) for the same reason it is exempt from
 * stockpile capacity: it is currency, not something that takes up
 * hold space -- and a colony's founding grant is far larger than
 * any sane bulk-cargo limit. */
#define SHIP_CARGO_CAPACITY  50
#define SHIP_VOYAGE_SECONDS  20.0f /* one island-to-island crossing */
/* The crossing length in whole sim ticks. A voyage departing at tick D
 * arrives at tick D + SHIP_VOYAGE_TICKS (Phase 2: derived from
 * departure_tick, no accumulating float). */
#define SHIP_VOYAGE_TICKS    ((int)(SHIP_VOYAGE_SECONDS * SIM_TICKS_PER_SEC))

/* Gold a ship must be carrying to found a colony. The new island
 * starts with exactly this much, which is what lets it buy its first
 * buildings — see the founding-grant note in resource.h's BUY_PRICE
 * comment: a colony that cannot pay for anything is stranded. */
#define COLONY_FOUNDING_GOLD 400

typedef struct {
    int   active;

    /* Phase 5: who commands this ship. Set at build time from the
     * commanding player; sim_apply rejects ship commands from anyone
     * else. Sim state (hashed). */
    uint32_t owner;

    int   at_island;      /* island index while docked, -1 at sea    */
    int   from_island;
    int   to_island;

    /* Phase 2: the voyage is defined by the tick it began. Arrival is
     * the integer test sim_tick_no >= departure_tick + SHIP_VOYAGE_TICKS.
     * `progress` is now a CACHED DERIVATION —
     * (sim_tick_no - departure_tick) / SHIP_VOYAGE_TICKS — refreshed each
     * tick purely so world_ui.c can keep drawing a 0..1 fraction without
     * knowing the clock. departure_tick is the canonical sim state (it,
     * not progress, is what sim_hash reads); progress is cosmetic. */
    uint64_t departure_tick;
    float    progress;    /* 0..1 along the current voyage (derived)  */
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

/* The FOREIGN version of ship_transfer_at (Phase 5): moves goods
 * between a ship's hold and an island's harbor ESCROW instead of its
 * stockpile — the only exchange a non-owner is ever allowed. Same sign
 * convention and hold clamping; the escrow side is uncapped (a quay,
 * not a warehouse). Ownership/docking/harbor validation is sim_apply's
 * job, not this function's. Returns units actually moved. */
int ship_transfer_escrow(Ship *sh, Island *isl, ResourceType res, int qty);

/* Advance every voyage, and run any active trade route: on arrival,
 * unload the inbound good, load the outbound one, and depart again.
 * Needs the islands because a route moves goods into and out of their
 * stockpiles without the player being present. `sim_tick_no` is the
 * current world tick, used to test arrival and refresh the cached
 * progress; ships_update does not advance the clock itself. */
void ships_update(Ship ships[], int ship_count,
                  Island islands[], int island_count, uint64_t sim_tick_no);

/* Total units of `res` currently in transit or sitting in holds —
 * the term that makes world conservation checkable: for any resource,
 * sum(island stockpiles) + ships_cargo_total() must never change
 * except where something is actually produced or consumed. */
int ships_cargo_total(const Ship ships[], int ship_count, ResourceType res);

/* ===== THIS IS THE WIRE FORMAT (MMO_PLAN Phase 2) ===========
 * A voyage is fully described by this immutable record, fixed at the
 * instant of departure. Every client can render a voyage identically
 * from it — no shared physics, no live sync — because arrival is a pure
 * function of (departure_tick, SHIP_VOYAGE_TICKS). This is exactly the
 * shape Phase 4 publishes to the shared feed; nothing here may grow a
 * pointer or a float, so it stays trivially serialisable and identical
 * across machines.
 * =========================================================== */
typedef struct {
    uint32_t player_id;         /* 0 for now; identity arrives in Phase 5 */
    int32_t  ship_id;
    int32_t  from;
    int32_t  to;
    uint64_t departure_tick;
    int32_t  cargo[RES_COUNT];
} VoyageRecord;

/* Snapshot a departing ship as a VoyageRecord. Call at (or after)
 * departure — from/to/departure_tick/cargo are read as they stand. */
VoyageRecord voyage_record_make(const Ship *sh, int ship_id,
                                uint32_t player_id);

/* Serialise a VoyageRecord as one JSON line (no trailing newline) into
 * `buf`. Returns the length written (excluding the NUL), or -1 if it did
 * not fit. Hand-rolled — deliberately no JSON dependency. */
int voyage_record_to_json(const VoyageRecord *v, char *buf, size_t n);

#endif /* SHIP_H */
