#ifndef SHIP_H
#define SHIP_H

/* ========================================================= */

#include "island.h"
#include "resource.h"
#include <stdint.h>
#include "sea.h"
#include <stddef.h>   /* size_t */

#define MAX_SHIPS            8

/* ---- what a hull is for (MARITIME_PLAN Phase 5) ----------- */
struct Ship;

typedef enum {
    SHIP_MERCHANTMAN = 0,   /* the hold that moves; no teeth          */
    SHIP_CUTTER      = 1,   /* escort work: guns enough to matter     */
    SHIP_WARSHIP     = 2,   /* takes anything at sea, carries nothing */
    SHIP_CLASS_COUNT
} ShipClass;

typedef struct {
    const char *name;
    int         guns;       /* what it brings to a fight             */
    int         hull;       /* what it survives                      */
    int         hold;       /* units of each good it can carry       */
    int         gold;       /* what the yard charges                 */
} ShipClassDef;

extern const ShipClassDef SHIP_CLASSES[SHIP_CLASS_COUNT];

/* The hold of a specific ship, which is its class's. Ships built
 * before classes existed decode as merchantmen and keep the capacity
 * they always had. */
int ship_hold_capacity(const struct Ship *sh);

/* What a hull actually brings to a fight: its guns, scaled by how much */
int ship_fighting_strength(const struct Ship *sh);
/* The merchantman's hold, and the historical value of this constant.
 * Kept as the class table's entry rather than deleted, because the
 * comment below is still the reason RES_GOLD is exempt. */
#define SHIP_CARGO_CAPACITY  50

/* Gold the yard charges for a merchantman; the other classes are
 * multiples of it in the table below. */
#define SHIP_BUILD_COST_GOLD 350
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

typedef struct Ship {
    int   active;

    /* Phase 5: who commands this ship. Set at build time from the
     * commanding player; sim_apply rejects ship commands from anyone
     * else. Sim state (hashed). */
    uint32_t owner;

    int   at_island;      /* island index while docked, -1 at sea    */
    int   from_island;
    int   to_island;

    /* Phase 2: the voyage is defined by the tick it began. Arrival is
     * the integer test sim_tick_no >= departure_tick + SHIP_VOYAGE_TICKS. */
    uint64_t departure_tick;
    float    progress;    /* 0..1 along the current voyage (derived)  */
    int   cargo[RES_COUNT];

    /* Marine insurance (MMO_PLAN later phases). Set at departure. */
    int          insured;
    int32_t      insured_value;
    int          was_at_sea;     /* set by the tick loop, to spot the
                                  * frame a voyage ends on           */

    /* Phase-4 trade-route fields: declared now so the save format
     * does not change again when routes land. */
    int          route_active;
    int          route_a, route_b;
    ResourceType route_res_ab, route_res_ba;
    int          route_qty;
    int          route_leg;      /* 0 = A->B, 1 = B->A */

    /* ---- what kind of ship (MARITIME_PLAN Phase 5) --------
     * Set at the yard and never changed. `hull` is the only one that
     * moves: a ship that loses a fight is damaged, and a ship out of
     * hull is gone. All sim state, hashed and snapshotted. */
    int32_t      klass;
    int32_t      guns;
    int32_t      hull;

    /* The ship this one escorts, or -1. An escort sails when its
     * charge sails and adds its guns to the defence — which is the
     * whole reason to own a ship that cannot carry anything. */
    int32_t      escorting;
} Ship;

/* Move `qty` units of `res` between a ship's hold and a SPECIFIC */
int ship_transfer_at(Ship *sh, Island *isl, ResourceType res, int qty);

/* The FOREIGN version of ship_transfer_at (Phase 5): moves goods */
int ship_transfer_escrow(Ship *sh, Island *isl, ResourceType res, int qty);

/* Advance every voyage, and run any active trade route: on arrival,
 * unload the inbound good, load the outbound one, and depart again. */
/* ---- piracy (MMO_PLAN later phases) ------------------------ */
#define PIRACY_CHANCE_PER_MILLE  80    /* 8% of voyages are raided     */
/* A private passage is fast because it runs outside patrolled water,
 * and this is the price of that (MARITIME_PLAN Phase 3c). Without it
 * "unsafe" is a word in a design document: a shortcut with no extra
 * risk is simply a better route, and no one would ever sail the lane. */
/* What the convoy escort is for (MARITIME_PLAN Phase 5b). */
#define CONVOY_ESCORT_DRIVES_OFF 820   /* per mille of lane traffic    */

#define PIRACY_CHANCE_PRIVATE   240    /* legacy; see above            */
#define PIRACY_TAKE_NUMERATOR     1    /* pirates take half the hold   */
#define PIRACY_TAKE_DENOMINATOR   2

/* Would this voyage be raided? Pure: the same arguments always give
 * the same answer, which is what makes it replayable. Exposed for the
 * insurance premium (it prices the same risk) and for tests. */
int voyage_is_raided(uint32_t world_seed, int ship_id, uint64_t departure_tick,
                     int from, int to);

/* The same question for a booking's crossing (MARITIME_PLAN Phase 3c). */
int shipment_is_raided(uint32_t world_seed, int route_id, uint64_t booked_tick,
                       uint32_t seller, int chance_per_mille);

/* ---- interception (MMO_PLAN later phases) ------------------ */
/* The odds when neither side has a gun between them: a boarding
 * scuffle, and the attacker's advantage is only that they chose the
 * moment. Everything above this is decided by what the ships are. */
#define INTERCEPT_ATTACKER_ODDS   55   /* percent, out of 100          */

/* Nothing at sea is ever certain, however lopsided. A convoy that
 * could not possibly be taken would make escorting a solved problem
 * rather than a judgement, and a warship that could not possibly lose
 * would make attacking one. */
/* Ticks in port, at an island with a Shipyard, per point of hull */
#define SHIP_REFIT_TICKS_PER_HULL 60

#define INTERCEPT_MIN_ODDS         8
#define INTERCEPT_MAX_ODDS        92

/* Does the attacker prevail? Pure and seeded, like the piracy roll — */
int intercept_attacker_wins(uint32_t world_seed,
                            int attacker_ship, uint64_t attacker_departure,
                            int target_ship, uint64_t target_departure,
                            int attacker_guns, int defender_guns);

/* The odds themselves, out of 100. Exposed so a UI can tell a player
 * what they are about to do, and so the tests can assert the shape of
 * the curve rather than sampling it. */
int intercept_odds(int attacker_guns, int defender_guns);

void ships_update(const Sea *sea, Ship ships[], int ship_count,
                  Island islands[], int island_count, uint64_t sim_tick_no,
                  uint32_t world_seed);

/* Total units of `res` currently in transit or sitting in holds —
 * the term that makes world conservation checkable: for any resource,
 * sum(island stockpiles) + ships_cargo_total() must never change
 * except where something is actually produced or consumed. */
int ships_cargo_total(const Ship ships[], int ship_count, ResourceType res);

/* ===== THIS IS THE WIRE FORMAT (MMO_PLAN Phase 2) =========== */
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
