/*  knowledge.c  --  What each player knows, and what they hold
 *                   (MARITIME_PLAN Phase 3b)
 *
 *  See knowledge.h for why knowing a route and holding a chart for it
 *  are separate, and why both are world state rather than something
 *  the client keeps to itself.
 *
 *  Every accessor tolerates a player id this does not track. That is
 *  not defensiveness: PLAYER_FACTION genuinely is the exception —
 *  the market sells the maps, so it knows every passage and is never
 *  short of one — and writing that here once is better than every
 *  caller remembering it.
 */

#include "knowledge.h"
#include "island.h"      /* PLAYER_FACTION */

#include <string.h>

void knowledge_init(Knowledge *k)
{
    /* Zero the lot: every byte is hashed, and a field this function
     * forgets is uninitialised memory entering sim_hash — the mistake
     * faction_init's comment already records being made once. */
    memset(k, 0, sizeof(*k));
}

int knowledge_slot(uint32_t player)
{
    if (player == 0u || player == PLAYER_FACTION) return -1;
    if (player > (uint32_t)MAX_PLAYERS) return -1;
    return (int)player - 1;
}

int knowledge_knows(const Knowledge *k, uint32_t player, int route_id,
                    int is_private)
{
    int slot;

    if (!is_private) return 1;              /* public is what public means */
    if (player == PLAYER_FACTION) return 1; /* it draws the maps           */
    if (route_id < 0 || route_id >= SEA_MAX_ROUTES) return 0;

    slot = knowledge_slot(player);
    if (slot < 0) return 0;

    return (k->player[slot].known[route_id / 8] >> (route_id % 8)) & 1;
}

int knowledge_charts(const Knowledge *k, uint32_t player, int route_id)
{
    int slot;

    if (route_id < 0 || route_id >= SEA_MAX_ROUTES) return 0;
    if (player == PLAYER_FACTION) return MAX_CHARTS_PER_ROUTE;

    slot = knowledge_slot(player);
    if (slot < 0) return 0;

    return k->player[slot].charts[route_id];
}

int knowledge_add_charts(Knowledge *k, uint32_t player, int route_id,
                         int qty)
{
    int slot, had, now;

    if (route_id < 0 || route_id >= SEA_MAX_ROUTES) return 0;
    if (player == PLAYER_FACTION) return qty;   /* bottomless, by design */

    slot = knowledge_slot(player);
    if (slot < 0) return 0;

    had = k->player[slot].charts[route_id];
    now = had + qty;
    if (now < 0) now = 0;
    if (now > MAX_CHARTS_PER_ROUTE) now = MAX_CHARTS_PER_ROUTE;
    k->player[slot].charts[route_id] = (uint8_t)now;

    /* Holding one teaches you the passage. Losing your last does not
     * make you forget it — that asymmetry is the whole point of
     * keeping the bit separate from the count. */
    if (now > had)
        k->player[slot].known[route_id / 8] |= (uint8_t)(1u << (route_id % 8));

    return now - had;
}
