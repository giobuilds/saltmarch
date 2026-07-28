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
#include "sea.h"
#include <stddef.h>   /* size_t */

#define MAX_SHIPS            8

/* ---- what a hull is for (MARITIME_PLAN Phase 5) -----------
 * A ship used to be one thing: a hold that moved. Interception was a
 * flat 55% coin flip, which meant a fight was something that happened
 * TO you rather than something you had prepared for — and no decision
 * anywhere in the game led to being better at it.
 *
 * Now a hull is a choice made at the shipyard, and it is a real one
 * because the axes trade against each other: guns cost hold. A
 * merchantman carries the cargo that makes trading worth doing and
 * cannot defend it; a warship can take anything at sea and has almost
 * nowhere to put it. Which is why escorts exist — the answer to "how
 * do I move cargo through dangerous water" is a second ship, not a
 * compromise ship.
 *
 * Class 0 is the merchantman ON PURPOSE: every ship built before this
 * phase was one, and a log recorded then still means what it meant. */
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

/* What a hull actually brings to a fight: its guns, scaled by how much
 * of it is still there. A warship fresh from the yard is worth its full
 * broadside; one that has taken three fights and not gone home is not,
 * and that is the pressure that makes a Shipyard near contested water
 * worth building. Never below 1 for an armed ship — a gun is a gun.
 *
 * Exposed rather than hidden in the intercept rule because a UI has to
 * be able to tell a player what they are about to sail into. */
int ship_fighting_strength(const struct Ship *sh);
/* The merchantman's hold, and the historical value of this constant.
 * Kept as the class table's entry rather than deleted, because the
 * comment below is still the reason RES_GOLD is exempt.
 *
 * Per-resource hold limit for physical goods. RES_GOLD is exempt
 * (see game_ship_transfer) for the same reason it is exempt from
 * stockpile capacity: it is currency, not something that takes up
 * hold space -- and a colony's founding grant is far larger than
 * any sane bulk-cargo limit. */
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
     * the integer test sim_tick_no >= departure_tick + SHIP_VOYAGE_TICKS.
     * `progress` is now a CACHED DERIVATION —
     * (sim_tick_no - departure_tick) / SHIP_VOYAGE_TICKS — refreshed each
     * tick purely so world_ui.c can keep drawing a 0..1 fraction without
     * knowing the clock. departure_tick is the canonical sim state (it,
     * not progress, is what sim_hash reads); progress is cosmetic. */
    uint64_t departure_tick;
    float    progress;    /* 0..1 along the current voyage (derived)  */
    int   cargo[RES_COUNT];

    /* Marine insurance (MMO_PLAN later phases). Set at departure when
     * the player paid a premium; cleared on arrival once the outcome
     * has been settled. `insured_value` is the declared cargo value the
     * payout is computed from — recorded at departure so a raid cannot
     * be settled against a hold the pirates already emptied. */
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
/* ---- piracy (MMO_PLAN later phases) ------------------------
 * A voyage can be raided. The event is not rolled from an RNG and not
 * carried in the feed: it is DERIVED from the voyage's own identity —
 * (world seed, ship, departure tick, lane) — so every client, every
 * replay and every server computes the same raid for the same voyage
 * without anything having to tell them about it. The shared feed stays
 * a dumb log of departures, which is the property MMO_PLAN protects.
 *
 * The check happens once, mid-voyage, so a ship that is already home
 * cannot be robbed retroactively by a late tick.
 */
#define PIRACY_CHANCE_PER_MILLE  80    /* 8% of voyages are raided     */
/* A private passage is fast because it runs outside patrolled water,
 * and this is the price of that (MARITIME_PLAN Phase 3c). Without it
 * "unsafe" is a word in a design document: a shortcut with no extra
 * risk is simply a better route, and no one would ever sail the lane. */
/* What the convoy escort is for (MARITIME_PLAN Phase 5b).
 *
 * Danger used to be a per-route CHANCE, and "public are slow but
 * protected" was that number being smaller. Since a raid became a
 * matter of where a cargo sailed, that number does nothing — and the
 * property nearly went with it. Worse than nearly: the lane threads a
 * WIDER waypoint than any private passage, so on geography alone the
 * safe route had become the more exposed one.
 *
 * So the protection is now the thing it always was in the fiction: the
 * lane is patrolled. A fleet lying on convoy water mostly finds an
 * escort there and lets the convoy pass. A private passage is fast
 * because nobody patrols it, and nothing about that is free. */
#define CONVOY_ESCORT_DRIVES_OFF 820   /* per mille of lane traffic    */

#define PIRACY_CHANCE_PRIVATE   240    /* legacy; see above            */
#define PIRACY_TAKE_NUMERATOR     1    /* pirates take half the hold   */
#define PIRACY_TAKE_DENOMINATOR   2

/* Would this voyage be raided? Pure: the same arguments always give
 * the same answer, which is what makes it replayable. Exposed for the
 * insurance premium (it prices the same risk) and for tests. */
int voyage_is_raided(uint32_t world_seed, int ship_id, uint64_t departure_tick,
                     int from, int to);

/* The same question for a booking's crossing (MARITIME_PLAN Phase 3c).
 * Derived from the shipment's own identity rather than rolled, for
 * exactly the reason above: every client and every replay must reach
 * the same answer without being told. `chance_per_mille` is the
 * route's, so the lane and the passages are priced differently by the
 * caller rather than by a second copy of the rule. */
int shipment_is_raided(uint32_t world_seed, int route_id, uint64_t booked_tick,
                       uint32_t seller, int chance_per_mille);

/* ---- interception (MMO_PLAN later phases) ------------------
 * PvP that never needs a real-time arbiter. An intercept is a Command
 * naming a voyage; the engagement is computed from the ordered log plus
 * a seeded hash, so both players' clients — and the server — reach the
 * same outcome from the same log without exchanging a shot.
 *
 * "Tide-time" is the honest description: you commit to an attack and
 * the sea resolves it at a tick boundary. There is nothing to aim and
 * nothing to dodge, which is what keeps the feed a dumb log.
 */
/* The odds when neither side has a gun between them: a boarding
 * scuffle, and the attacker's advantage is only that they chose the
 * moment. Everything above this is decided by what the ships are. */
#define INTERCEPT_ATTACKER_ODDS   55   /* percent, out of 100          */

/* Nothing at sea is ever certain, however lopsided. A convoy that
 * could not possibly be taken would make escorting a solved problem
 * rather than a judgement, and a warship that could not possibly lose
 * would make attacking one. */
/* Ticks in port, at an island with a Shipyard, per point of hull
 * restored. A fight costs a warship several points, so a refit is a
 * real absence from the water rather than a formality — which is what
 * makes wear a pressure and a Shipyard near contested water worth
 * building. */
#define SHIP_REFIT_TICKS_PER_HULL 60

#define INTERCEPT_MIN_ODDS         8
#define INTERCEPT_MAX_ODDS        92

/* Does the attacker prevail? Pure and seeded, like the piracy roll —
 * the same voyage always resolves the same way, which is what lets
 * both players' clients and the server agree without exchanging a
 * shot.
 *
 * `attacker_guns` and `defender_guns` are the strengths brought to it;
 * the defender's includes every escort sailing with them
 * (MARITIME_PLAN Phase 5). Odds are the attacker's share of the total,
 * clamped, so a warship against an unescorted merchantman is nearly
 * certain and two cutters are nearly a coin flip. */
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
