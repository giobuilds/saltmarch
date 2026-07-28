#ifndef PIRATE_H
#define PIRATE_H

/* =========================================================
 * pirate.h  --  Something to hunt
 *               (MARITIME_PLAN Phase 5b: pirates as entities)
 *
 * Piracy has been a pure function since it existed:
 * `shipment_is_raided()` hashed a voyage's identity and returned a
 * boolean. That is fine for a hazard and useless for an enemy. Nothing
 * was there. You could not find it, could not fight it, could not
 * decide to go and deal with it, and the loss it caused went nowhere —
 * the goods left the world.
 *
 * A pirate is now a thing in the water. It sits somewhere, it takes
 * what passes near it, it KEEPS what it takes, and it can be hunted
 * down and emptied. Three consequences fall out of that, and they are
 * why the change is worth the state it costs:
 *
 *   A raid becomes CAUSED rather than rolled. Your cargo was taken
 *   because it sailed past the Coffin Race and somebody was waiting
 *   there — not because a hash said so. A route is dangerous for a
 *   reason a player can learn, and learning it is what the named
 *   waypoints were always for.
 *
 *   Loss becomes RECOVERABLE. Plunder sits in a hold at a place, so
 *   hunting the fleet that has been bleeding your lane is a way to get
 *   it back — possibly somebody else's, which makes a successful hunt
 *   a service to every trader on that water.
 *
 *   And a warship gains a use that is not PvP. Phase 5a made hulls a
 *   choice but gave the guns only one thing to shoot at: other
 *   players. A game where the only reason to arm is to raid your
 *   neighbours is a game about raiding your neighbours.
 *
 * WHERE THEY ARE IS GENERATED; WHAT THEY HOLD IS WORLD STATE. Their
 * lairs come from the world seed like everything else in the sea, so
 * every client agrees where the dangerous water is without being told.
 * Their plunder, their damage and whether they are still alive are
 * hashed, snapshotted and replayed.
 * ========================================================= */

#include <stdint.h>
#include "sea.h"
#include "resource.h"

#define MAX_PIRATES 6

/* How near a shipment has to pass to be taken. In sea units, against a
 * sea 10000 across and island separations of 1500 — so a lair threatens
 * the water around one waypoint rather than a whole crossing. */
#define PIRATE_STRIKE_RADIUS 900

/* They do not sit still for ever. Every this many ticks a fleet moves
 * to another waypoint, chosen deterministically — so a lane that has
 * been safe for an hour is not safe by right, and a chart of where the
 * danger is goes stale the way everything else in this sea does. */
#define PIRATE_MOVE_INTERVAL_TICKS 4000

/* What they are, in the terms Phase 5a established. Meaner than a
 * cutter and lighter than a warship: a merchantman alone loses, a
 * merchantman with a cutter is a fair fight, a warship wins. */
#define PIRATE_GUNS 5
#define PIRATE_HULL 8

/* Of the goods a raid takes, how much the fleet keeps rather than
 * spoiling or selling on. All of it: the point is that loss is
 * recoverable, and plunder that evaporated would just be the old
 * derived boolean with extra steps. */

typedef struct {
    int32_t  active;
    int32_t  waypoint;              /* the water it works, by index    */
    int32_t  guns;
    int32_t  hull;
    int32_t  plunder[RES_COUNT];
    int32_t  chart;                 /* a route chart it carries, or -1 */
    uint64_t last_move_tick;
} Pirate;

typedef struct {
    Pirate fleet[MAX_PIRATES];
    int32_t count;
} PirateSea;

/* Seed the fleets into the sea. Deterministic: same seed, same lairs,
 * so "the Teeth are bad water" is a fact about the world rather than a
 * thing one client believes. */
void pirate_init(PirateSea *ps, const Sea *sea, uint32_t seed);

/* Where fleet `i` currently is, or a zero position if it is dead. */
SeaPos pirate_pos(const PirateSea *ps, const Sea *sea, int i);

/* The fleet lying in wait within strike range of `p`, or -1. Lowest
 * index wins a tie, so two lairs covering one strait rob a cargo in an
 * order every client agrees on. */
int pirate_at(const PirateSea *ps, const Sea *sea, SeaPos p);

/* Move fleets that are due to move. Called once per tick; does nothing
 * on all but one tick in PIRATE_MOVE_INTERVAL_TICKS. */
void pirate_update(PirateSea *ps, const Sea *sea, uint64_t tick,
                   uint32_t seed);

#endif /* PIRATE_H */
