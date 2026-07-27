/*  orderbook.c  --  Posting, cancelling and matching
 *                   (MARITIME_PLAN Phase 2: the order book)
 *
 *  See orderbook.h for why a trade identity is a kind and an id, and
 *  why goods are reserved at posting rather than at filling.
 *
 *  This file holds only the container operations. The rules that move
 *  goods and gold live in game.c beside the other sim_* mutators,
 *  because they touch stockpiles and must run inside the same funnel
 *  as everything else that changes the world.
 */

#include "orderbook.h"

#include <stddef.h>
#include <string.h>

void orderbook_init(OrderBook *b)
{
    /* Zero the whole thing: every byte is hashed, and a field this
     * function forgets is uninitialised memory entering sim_hash —
     * which is the mistake faction_init's comment already records
     * being made once. */
    memset(b, 0, sizeof(*b));
    b->next_order_id = 1u;   /* 0 means "no order", as elsewhere */
}

int orderbook_open_count(const OrderBook *b, uint32_t player)
{
    int i, n = 0;

    for (i = 0; i < b->order_count; i++)
        if (b->order[i].active && b->order[i].owner == player) n++;
    return n;
}

int orderbook_open_live(const OrderBook *b)
{
    int i, n = 0;

    for (i = 0; i < b->order_count; i++) if (b->order[i].active) n++;
    return n;
}

int orderbook_booking_live(const OrderBook *b)
{
    int i, n = 0;

    for (i = 0; i < b->booking_count; i++) if (b->booking[i].active) n++;
    return n;
}

Order *orderbook_find(OrderBook *b, uint32_t id)
{
    int i;

    if (id == 0u) return NULL;
    for (i = 0; i < b->order_count; i++)
        if (b->order[i].active && b->order[i].id == id)
            return &b->order[i];
    return NULL;
}
