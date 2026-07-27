#ifndef KNOWLEDGE_H
#define KNOWLEDGE_H

/* =========================================================
 * knowledge.h  --  What each player knows, and what they hold
 *                  (MARITIME_PLAN Phase 3b: routes and charts)
 *
 * Phase 3a generated three routes between every island pair — one
 * public lane and two private passages — and left all three visible,
 * because a route existing and a player knowing it are different
 * things. This is the second half: the private ones are concealed, and
 * a player opens them for themselves.
 *
 * THE FIRST PER-PLAYER STATE IN THE WORLD. Everything before this was
 * owned by a place or a thing: an island has an owner, a ship has an
 * owner, an order has an owner. Nothing was ever true *of a player*.
 * Route knowledge is, and it cannot be attached to an island — a
 * player who loses their colony does not forget the sea.
 *
 * KNOWING AND HOLDING ARE SEPARATE, and the design says so twice in
 * different words: a private route is "unlocked with charts and
 * consumed when travel is done", and a player "unlocks that knowledge
 * for themselves". Those are two mechanics, not one:
 *
 *   known[]  -- a permanent bit. Once you have learned a passage
 *               exists you never unlearn it; you can see it on the map
 *               and you know what a chart for it is worth. Set the
 *               first time a chart for it reaches you.
 *   charts[] -- how many maps of it you hold. Spent, one per round
 *               trip, when a booking sails it.
 *
 * So knowing without holding is the normal state of an experienced
 * trader: you know four fast passages and can currently afford to sail
 * one. That is the pressure the Chart House exists to relieve.
 *
 * ALL OF IT IS WORLD STATE — hashed, snapshotted, replayed. It has to
 * be: which route a booking takes depends on what its seller knows, so
 * two clients that disagreed about knowledge would disagree about when
 * a cargo arrives. Concealment from the PLAYER is a presentation
 * question (and, properly, a server-authority one — see
 * SERVER_AUTHORITY.md); concealment from the SIM is not a thing.
 * ========================================================= */

#include <stdint.h>
#include "sea.h"

/* Players one world tracks knowledge for. Ids run from 1, so player N
 * lives at index N-1; PLAYER_FACTION is not one of these (the market
 * knows every route by definition — it sells the maps). */
#define MAX_PLAYERS 8

/* Charts of one route one player may hold. A byte each, and the cap is
 * what keeps that honest — an unbounded stock would make a surveyed
 * passage permanently open, which is one of the open questions this
 * phase deliberately does not answer yet. 255 is "effectively
 * unlimited, but it fits". */
#define MAX_CHARTS_PER_ROUTE 255

typedef struct {
    /* One bit per route, indexed by sea route id. */
    uint8_t known[(SEA_MAX_ROUTES + 7) / 8];
    uint8_t charts[SEA_MAX_ROUTES];
} PlayerKnowledge;

typedef struct {
    PlayerKnowledge player[MAX_PLAYERS];
} Knowledge;

void knowledge_init(Knowledge *k);

/* Index for a player id, or -1 if it is not one this tracks (0,
 * PLAYER_FACTION, or beyond MAX_PLAYERS). Every accessor below tolerates
 * an untracked player: the market and unowned things simply know
 * everything and hold nothing, which is what the callers want. */
int  knowledge_slot(uint32_t player);

/* Does `player` know this route exists? Always true for the public
 * lane — that is what public means — and always true for the faction,
 * which is where the maps come from. */
int  knowledge_knows(const Knowledge *k, uint32_t player, int route_id,
                     int is_private);

/* Charts of `route_id` held. The faction is not chart-limited: it
 * trades in maps rather than sailing on them. */
int  knowledge_charts(const Knowledge *k, uint32_t player, int route_id);

/* Add or remove charts, clamped to [0, MAX_CHARTS_PER_ROUTE]. Adding
 * also sets the known bit — a map in your hands teaches you the
 * passage, whether you bought it, were given it, or took it off a
 * pirate. Returns how many actually moved. */
int  knowledge_add_charts(Knowledge *k, uint32_t player, int route_id,
                          int qty);

#endif /* KNOWLEDGE_H */
