#ifndef KNOWLEDGE_H
#define KNOWLEDGE_H

/* knowledge.h  --  What each player knows, and what they hold
 * (MARITIME_PLAN Phase 3b: routes and charts) */

#include <stdint.h>
#include "sea.h"

/* Players one world tracks knowledge for. Ids run from 1, so player N
 * lives at index N-1; PLAYER_FACTION is not one of these (the market
 * knows every route by definition — it sells the maps). */
#define MAX_PLAYERS 8

/* Charts of one route one player may hold. A byte each, and the cap. */
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

/* Discard every player's charts of `route_id` — the passage has gone */
void knowledge_void_charts(Knowledge *k, int route_id);

#endif /* KNOWLEDGE_H */
