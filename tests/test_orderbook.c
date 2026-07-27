/*  test_orderbook.c  --  players trading with each other
 *                        (MARITIME_PLAN Phase 2)
 *
 * The order book is the first mechanic where one player's command
 * moves another player's goods, so the properties worth asserting are
 * the ones that make that safe:
 *
 *   - posting RESERVES. A sell that did not take the goods out of the
 *     stockpile could be posted ten times over and filled ten times.
 *   - cancelling returns exactly what is left, not what was posted.
 *   - a fill is not a transfer: goods cross the water and arrive when
 *     the route says they do.
 *   - matching is reproducible, because it is sim state and a replay
 *     must fill the same trades in the same order.
 *
 * Built and run by tests/run.sh.
 */

#include "game.h"
#include "orderbook.h"
#include "resource.h"
#include "sea.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* Submit as `who`, the way test_ownership does: the funnel stamps the
 * local player id, so becoming somebody else is how a second trader is
 * simulated in one process. */
#define AS(gs, who, call) do {                                         \
        uint32_t saved__ = (gs)->local_player_id;                      \
        (gs)->local_player_id = (who);                                 \
        (call);                                                        \
        (gs)->local_player_id = saved__;                               \
    } while (0)

static void run_ticks(GameState *gs, int n)
{
    while (n-- > 0) sim_run_one_tick(gs);
}

/* Two islands, two owners, both stocked. Island 0 belongs to player 1
 * from world creation; island 1 is granted to player 2. */
static GameState *two_traders(uint32_t seed)
{
    GameState *gs = game_init();
    if (!gs) return NULL;

    game_new_seeded(gs, seed);
    gs->islands[1].settled = 1;
    gs->islands[1].owner   = 2u;
    stockpile_init(&gs->islands[1].stockpile);

    gs->islands[0].stockpile.amount[RES_GOLD]  = 100000;
    gs->islands[1].stockpile.amount[RES_GOLD]  = 100000;
    gs->islands[0].stockpile.amount[RES_PLANKS] = 100;

    /* These tests are about the book, not about the market maker, so
     * point it somewhere else. It quotes the six goods it is furthest
     * from baseline on, so overstocking six the tests never touch
     * guarantees Planks is never one of them and the faction's orders
     * can never fill against the test's. Zeroing its stock instead does
     * NOT work: mean reversion walks it back to baseline and the quotes
     * reappear halfway through a long test. The market maker gets its
     * own section at the end. */
    {
        static const ResourceType DECOY[FACTION_QUOTE_GOODS] = {
            RES_WOOD, RES_FISH, RES_GRAIN, RES_WOOL, RES_CLOTH, RES_FISH_OIL
        };
        int i;
        for (i = 0; i < FACTION_QUOTE_GOODS; i++)
            gs->faction.inventory[DECOY[i]] = 100000;
    }
    gs->faction.gold = 0;      /* and it bids for nothing at all */
    return gs;
}

/* The orders and bookings belonging to the players a test drives, as
 * opposed to the market maker's. */
static int player_orders(const OrderBook *b)
{
    return orderbook_open_count(b, 1u) + orderbook_open_count(b, 2u) +
           orderbook_open_count(b, 3u);
}

static int bookings_from(const OrderBook *b, int island)
{
    int i, n = 0;

    for (i = 0; i < b->booking_count; i++)
        if (b->booking[i].active && b->booking[i].from_island == island) n++;
    return n;
}

static void place(GameState *gs, uint32_t who, int island,
                  ResourceType res, int qty, int limit)
{
    Command c;
    memset(&c, 0, sizeof(c));
    c.kind = CMD_PLACE_ORDER;
    c.a    = island;
    c.b    = TRADE_PACK(TRADE_RESOURCE, (uint16_t)res);
    c.c    = qty;               /* sign is the side */
    c.d    = limit;
    AS(gs, who, command_submit(gs, &c));
}

int main(void)
{
    printf("=== posting reserves ===\n");
    {
        GameState *gs = two_traders(4242u);
        int        planks_before, gold_before;

        if (!gs) { printf("game_init failed\n"); return 1; }
        planks_before = gs->islands[0].stockpile.amount[RES_PLANKS];
        gold_before   = gs->islands[1].stockpile.amount[RES_GOLD];

        place(gs, 1u, 0, RES_PLANKS, -20, 9);   /* player 1 sells 20 */
        place(gs, 2u, 1, RES_PLANKS,  20, 4);   /* player 2 bids low */
        run_ticks(gs, 1);

        CHECK(gs->islands[0].stockpile.amount[RES_PLANKS] ==
              planks_before - 20,
              "a sell takes the goods out of the seller's stockpile");
        CHECK(gs->islands[1].stockpile.amount[RES_GOLD] ==
              gold_before - 20 * 4,
              "and a buy takes the gold out of the buyer's");
        CHECK(orderbook_open_count(&gs->book, 1u) == 1 &&
              orderbook_open_count(&gs->book, 2u) == 1,
              "both orders are resting");

        /* The bid is below the ask, so nothing should have crossed. */
        run_ticks(gs, 20);
        CHECK(orderbook_open_count(&gs->book, 1u) == 1,
              "a bid under the ask does not fill");

        game_free(gs);
    }

    printf("\n=== cancelling returns what is left ===\n");
    {
        GameState *gs = two_traders(4242u);
        Command    c;
        uint32_t   id;

        if (!gs) { printf("game_init failed\n"); return 1; }
        place(gs, 1u, 0, RES_PLANKS, -20, 9);
        run_ticks(gs, 1);
        id = gs->book.order[0].id;

        memset(&c, 0, sizeof(c));
        c.kind = CMD_CANCEL_ORDER;
        c.a    = (int32_t)id;

        /* Somebody else's order is not yours to withdraw. */
        AS(gs, 2u, command_submit(gs, &c));
        run_ticks(gs, 1);
        CHECK(orderbook_open_count(&gs->book, 1u) == 1,
              "another player cannot cancel your order");

        AS(gs, 1u, command_submit(gs, &c));
        run_ticks(gs, 1);
        CHECK(orderbook_open_count(&gs->book, 1u) == 0, "the owner can");
        CHECK(gs->islands[0].stockpile.amount[RES_PLANKS] == 100,
              "and the goods come back");

        game_free(gs);
    }

    printf("\n=== a fill crosses the water ===\n");
    {
        GameState *gs = two_traders(4242u);
        uint32_t   crossing;
        int        seller_gold;

        if (!gs) { printf("game_init failed\n"); return 1; }
        crossing    = sea_crossing_ticks(&gs->sea, 0, 1);
        seller_gold = gs->islands[0].stockpile.amount[RES_GOLD];

        place(gs, 1u, 0, RES_PLANKS, -20, 9);   /* ask 9 */
        run_ticks(gs, 1);
        place(gs, 2u, 1, RES_PLANKS,  20, 12);  /* bid 12: crosses */
        run_ticks(gs, 1);

        CHECK(gs->book.booking[0].active, "the match becomes a booking");
        CHECK(gs->book.booking[0].price == 9,
              "and fills at the resting order's price, not the taker's");
        CHECK(gs->islands[1].stockpile.amount[RES_PLANKS] == 0,
              "the goods are not there yet");

        /* Halfway across: still nothing. */
        run_ticks(gs, (int)crossing / 2);
        CHECK(gs->islands[1].stockpile.amount[RES_PLANKS] == 0,
              "nor halfway across");

        run_ticks(gs, (int)crossing / 2 + 2);
        CHECK(gs->islands[1].stockpile.amount[RES_PLANKS] == 20,
              "they arrive after the crossing the route says");
        CHECK(gs->islands[0].stockpile.amount[RES_GOLD] ==
              seller_gold + 20 * 9,
              "and the seller is paid on delivery");
        CHECK(gs->book.booking[0].active && gs->book.booking[0].delivered,
              "the booking outlives the delivery — the crew is sailing home");

        run_ticks(gs, (int)crossing + 2);
        CHECK(!gs->book.booking[0].active, "and closes when they arrive");

        game_free(gs);
    }

    printf("\n=== a trade costs a merchant and a hull, and gives them back ===\n");
    {
        GameState *gs = two_traders(4242u);
        uint32_t   crossing;

        if (!gs) { printf("game_init failed\n"); return 1; }
        crossing = sea_crossing_ticks(&gs->sea, 0, 1);

        CHECK(island_merchant_capacity(&gs->islands[0]) == TRADE_BASE_MERCHANTS
              && island_hull_capacity(&gs->islands[0]) == TRADE_BASE_HULLS,
              "a bare island can run one trade at a time");

        place(gs, 1u, 0, RES_PLANKS, -10, 5);
        place(gs, 2u, 1, RES_PLANKS,  10, 9);
        run_ticks(gs, 2);
        CHECK(gs->islands[0].merchants_out == 1 &&
              gs->islands[0].hulls_out == 1,
              "a booking takes one of each from the selling island");

        /* Delivery is not the release: the crew is still at sea. */
        run_ticks(gs, (int)crossing + 2);
        CHECK(gs->islands[0].merchants_out == 1,
              "delivering does not free them");

        run_ticks(gs, (int)crossing + 2);
        CHECK(gs->islands[0].merchants_out == 0 &&
              gs->islands[0].hulls_out == 0,
              "coming home does");

        game_free(gs);
    }

    printf("\n=== a trade waits for a hull rather than being refused ===\n");
    {
        GameState *gs = two_traders(4242u);
        uint32_t   crossing;

        if (!gs) { printf("game_init failed\n"); return 1; }
        crossing = sea_crossing_ticks(&gs->sea, 0, 1);

        place(gs, 1u, 0, RES_PLANKS, -10, 5);
        place(gs, 2u, 1, RES_PLANKS,  10, 9);
        run_ticks(gs, 2);

        /* A second crossing pair, posted while the island's one hull is
         * already at sea. The orders are good and they cross; only the
         * capacity is missing. */
        place(gs, 1u, 0, RES_PLANKS, -10, 5);
        place(gs, 2u, 1, RES_PLANKS,  10, 9);
        run_ticks(gs, 2);
        CHECK(bookings_from(&gs->book, 0) == 1 && player_orders(&gs->book) == 2,
              "a crossing pair with no hull to carry it rests, not rejected");

        /* The round trip completes; the waiting pair sails on the same
         * tick the hull is released. */
        run_ticks(gs, (int)crossing * 2 + 4);
        CHECK(player_orders(&gs->book) == 0 && gs->islands[0].hulls_out == 1,
              "and sails the moment the hull is free again");

        game_free(gs);
    }

    printf("\n=== a seller with no hull does not wedge the good ===\n");
    {
        GameState *gs = two_traders(4242u);

        if (!gs) { printf("game_init failed\n"); return 1; }

        /* Island 2 is a third port, owned by player 3. */
        gs->islands[2].settled = 1;
        gs->islands[2].owner   = 3u;
        stockpile_init(&gs->islands[2].stockpile);
        gs->islands[2].stockpile.amount[RES_PLANKS] = 100;

        /* Player 1 sells cheapest, and uses up their one hull. */
        place(gs, 1u, 0, RES_PLANKS, -10, 3);
        place(gs, 2u, 1, RES_PLANKS,  10, 20);
        run_ticks(gs, 2);
        CHECK(gs->islands[0].hulls_out == 1, "the cheapest seller ships first");

        /* Player 1 is now out of hulls but still has the best price.
         * A matcher that stopped at the top of book would strand every
         * other seller behind an ask that cannot move — the same free
         * denial of service as the self-crossing case. */
        place(gs, 1u, 0, RES_PLANKS, -10, 3);
        place(gs, 3u, 2, RES_PLANKS, -10, 8);
        place(gs, 2u, 1, RES_PLANKS,  10, 20);
        run_ticks(gs, 2);

        CHECK(gs->islands[2].hulls_out == 1,
              "a seller who still has a hull trades past one who does not");

        game_free(gs);
    }

    printf("\n=== buildings raise the ceiling ===\n");
    {
        GameState *gs = two_traders(4242u);
        int        base_m, base_h;

        if (!gs) { printf("game_init failed\n"); return 1; }
        base_m = island_merchant_capacity(&gs->islands[0]);
        base_h = island_hull_capacity(&gs->islands[0]);

        /* Hand-place rather than going through the funnel: this is a
         * test of the capacity rule, not of placement. */
        {
            Island *isl = &gs->islands[0];
            int     i   = isl->building_count++;
            isl->buildings[i].active = 1;
            isl->buildings[i].type   = BUILDING_HOUSE_MERCHANT;
            isl->pop_data[i].active    = 1;
            isl->pop_data[i].residents = 0;
        }
        CHECK(island_merchant_capacity(&gs->islands[0]) == base_m,
              "an empty Merchant House supplies no merchant");

        gs->islands[0].pop_data[gs->islands[0].building_count - 1].residents = 4;
        CHECK(island_merchant_capacity(&gs->islands[0]) ==
              base_m + TRADE_MERCHANTS_PER_HOUSE,
              "a lived-in one does");

        {
            Island *isl = &gs->islands[0];
            int     i   = isl->building_count++;
            isl->buildings[i].active = 1;
            isl->buildings[i].type   = BUILDING_SHIPYARD;
        }
        CHECK(island_hull_capacity(&gs->islands[0]) ==
              base_h + TRADE_HULLS_PER_SHIPYARD,
              "and a shipyard raises the hulls");

        game_free(gs);
    }

    printf("\n=== a partial fill leaves the right reserve ===\n");
    {
        GameState *gs = two_traders(4242u);
        Command    c;
        uint32_t   id;

        if (!gs) { printf("game_init failed\n"); return 1; }

        /* An ask for 5 at 9 is resting, so it sets the price. A bid for
         * 20 at 12 arrives: 5 fill at 9, and the 15 the buyer was
         * willing to overpay comes straight back.
         *
         * The buyer reserved 240. Cancelling the unfilled 15 must
         * return exactly the 180 those units still hold — not the 195
         * a reserve decremented by the FILL price rather than the LIMIT
         * price would hand back, which pays the 15 out twice and mints
         * gold by part-filling an order and withdrawing it. */
        place(gs, 1u, 0, RES_PLANKS,  -5,  9);
        run_ticks(gs, 1);
        place(gs, 2u, 1, RES_PLANKS,  20, 12);
        run_ticks(gs, 1);

        id = 0u;
        for (int i = 0; i < gs->book.order_count; i++)
            if (gs->book.order[i].active && gs->book.order[i].owner == 2u)
                id = gs->book.order[i].id;
        CHECK(id != 0u, "the unfilled remainder is still resting");

        memset(&c, 0, sizeof(c));
        c.kind = CMD_CANCEL_ORDER;
        c.a    = (int32_t)id;
        AS(gs, 2u, command_submit(gs, &c));
        run_ticks(gs, 1);

        CHECK(gs->islands[1].stockpile.amount[RES_GOLD] == 100000 - 5 * 9,
              "a part-filled order that is cancelled returns exactly what "
              "it still held");

        game_free(gs);
    }

    printf("\n=== you cannot trade with yourself, and it costs no one else ===\n");
    {
        GameState *gs = two_traders(4242u);

        if (!gs) { printf("game_init failed\n"); return 1; }

        /* Player 1 crosses their own book: best bid and best ask are
         * both theirs. That pair must not fill — but it also must not
         * WEDGE the book. A matcher that gave up on the resource the
         * moment the top of book was self-crossing would let one player
         * lock every other player out of a good for free. */
        gs->islands[0].stockpile.amount[RES_GOLD] = 100000;
        place(gs, 1u, 0, RES_PLANKS,  10, 50);   /* own bid, very high */
        place(gs, 1u, 0, RES_PLANKS, -10,  1);   /* own ask, very low  */
        run_ticks(gs, 2);

        CHECK(player_orders(&gs->book) == 2 &&
              bookings_from(&gs->book, 0) == 0,
              "a self-crossing pair does not fill");

        /* Now player 2 offers to sell into that standing bid at a price
         * player 1 is plainly willing to pay. */
        gs->islands[1].stockpile.amount[RES_PLANKS] = 10;
        place(gs, 2u, 1, RES_PLANKS, -10, 20);
        run_ticks(gs, 2);

        CHECK(bookings_from(&gs->book, 1) == 1,
              "and another player can still trade against it");

        game_free(gs);
    }

    printf("\n=== one player cannot fill the book ===\n");
    {
        GameState *gs = two_traders(4242u);
        int        i;

        if (!gs) { printf("game_init failed\n"); return 1; }

        for (i = 0; i < ORDERBOOK_MAX_PER_PLAYER + 6; i++)
            place(gs, 1u, 0, RES_PLANKS, -1, 900 + i);   /* far above any bid */
        run_ticks(gs, 2);

        CHECK(orderbook_open_count(&gs->book, 1u) == ORDERBOOK_MAX_PER_PLAYER,
              "posting stops at the per-player cap");
        CHECK(gs->islands[0].stockpile.amount[RES_PLANKS] ==
              100 - ORDERBOOK_MAX_PER_PLAYER,
              "and a refused order reserves nothing");

        game_free(gs);
    }

    printf("\n=== the book is world state ===\n");
    {
        GameState *a = two_traders(777u);
        GameState *b = two_traders(777u);

        if (!a || !b) { printf("game_init failed\n"); return 1; }

        /* The same commands in the same order must produce the same
         * fills — the book is hashed, so a matcher that depended on
         * anything but price, time and id would show up here. */
        place(a, 1u, 0, RES_PLANKS, -20, 9);
        place(b, 1u, 0, RES_PLANKS, -20, 9);
        run_ticks(a, 1); run_ticks(b, 1);
        place(a, 2u, 1, RES_PLANKS, 20, 12);
        place(b, 2u, 1, RES_PLANKS, 20, 12);
        run_ticks(a, 40); run_ticks(b, 40);

        CHECK(sim_hash(a) == sim_hash(b),
              "two identical runs hash identically through a fill");

        game_free(a);
        game_free(b);
    }

    printf("\n=== a checkpoint keeps open orders ===\n");
    {
        GameState *gs = two_traders(4242u);
        GameState *rs = game_init();
        unsigned char *buf = NULL;
        size_t         len = 0;

        if (!gs || !rs) { printf("game_init failed\n"); return 1; }

        place(gs, 1u, 0, RES_PLANKS, -20, 9);
        place(gs, 2u, 1, RES_PLANKS,  20, 12);
        run_ticks(gs, 2);           /* matched, in transit */

        CHECK(snapshot_encode(gs, &buf, &len), "the world snapshots");
        if (!buf) { printf("\nFAILED\n"); return 1; }
        CHECK(snapshot_decode(rs, buf, len), "and restores");
        CHECK(sim_hash(rs) == sim_hash(gs),
              "with the book intact — a checkpoint that dropped an open "
              "booking would steal goods already paid for");

        /* And the shipment still lands on the far side. */
        run_ticks(gs, 400);
        run_ticks(rs, 400);
        CHECK(sim_hash(rs) == sim_hash(gs),
              "and both deliver it identically afterwards");

        free(buf);
        game_free(gs);
        game_free(rs);
    }

    printf("\n=== the market maker is a trader with a location ===\n");
    {
        /* Deliberately NOT two_traders: this section wants the faction
         * quoting normally. */
        GameState *gs = game_init();
        int        p, quoted, mispriced, off_port;

        if (!gs) { printf("game_init failed\n"); return 1; }
        game_new_seeded(gs, 4242u);

        for (p = 0; p < FACTION_PORT_COUNT; p++) {
            int idx = MAX_ISLANDS - FACTION_PORT_COUNT + p;
            CHECK(gs->islands[idx].settled &&
                  gs->islands[idx].owner == PLAYER_FACTION,
                  "the market holds a home port from tick 0");
        }
        CHECK(!faction_is_home_port(0),
              "and the first player's island is not one of them");

        run_ticks(gs, FACTION_QUOTE_INTERVAL_TICKS + 2);

        quoted = orderbook_open_count(&gs->book, PLAYER_FACTION);
        CHECK(quoted > 0, "it posts standing orders into the book");
        CHECK(quoted <= FACTION_PORT_COUNT * FACTION_QUOTE_GOODS * 2,
              "and never more than its own ports and goods allow");

        /* Its orders live at its harbours, and its ask is above its
         * bid — so its two sides never cross each other, and a player
         * can trade inside the spread with somebody else. */
        mispriced = off_port = 0;
        {
            int i, j;
            for (i = 0; i < gs->book.order_count; i++) {
                const Order *o = &gs->book.order[i];
                if (!o->active || o->owner != PLAYER_FACTION) continue;
                if (!faction_is_home_port(o->island)) off_port++;
                for (j = 0; j < gs->book.order_count; j++) {
                    const Order *q = &gs->book.order[j];
                    if (!q->active || q->owner != PLAYER_FACTION) continue;
                    if (q->what.id != o->what.id) continue;
                    if (o->side == ORDER_BUY && q->side == ORDER_SELL &&
                        o->limit >= q->limit) mispriced++;
                }
            }
        }
        CHECK(off_port == 0, "every quote sits at one of its harbours");
        CHECK(mispriced == 0, "and its bid never crosses its own ask");

        /* Re-quoting replaces, it does not accumulate. A market maker
         * that left its stale orders behind would fill the book with
         * its own history inside a minute. */
        run_ticks(gs, FACTION_QUOTE_INTERVAL_TICKS * 3);
        CHECK(orderbook_open_count(&gs->book, PLAYER_FACTION) <=
              FACTION_PORT_COUNT * FACTION_QUOTE_GOODS * 2,
              "three refreshes later it still holds only one book");

        /* Its harbour is a real place with finite throughput. */
        CHECK(island_hull_capacity(&gs->islands[MAX_ISLANDS - 1]) ==
              FACTION_PORT_HULLS,
              "a home port has a trading house's capacity, not a colony's");

        game_free(gs);
    }

    printf("\n=== a player can buy from the market, and it takes time ===\n");
    {
        GameState *gs = game_init();
        int        i, res = -1, ask = 0, before;
        uint32_t   crossing;

        if (!gs) { printf("game_init failed\n"); return 1; }
        game_new_seeded(gs, 4242u);
        run_ticks(gs, FACTION_QUOTE_INTERVAL_TICKS + 2);

        /* Find something the market is actually offering. */
        for (i = 0; i < gs->book.order_count; i++) {
            const Order *o = &gs->book.order[i];
            if (o->active && o->owner == PLAYER_FACTION &&
                o->side == ORDER_SELL) { res = o->what.id; ask = o->limit; break; }
        }
        CHECK(res >= 0, "the market is offering something");

        if (res >= 0) {
            gs->islands[0].stockpile.amount[RES_GOLD] = 100000;
            before = gs->islands[0].stockpile.amount[res];

            place(gs, 1u, 0, (ResourceType)res, 10, ask + 5);
            run_ticks(gs, 2);
            CHECK(gs->islands[0].stockpile.amount[res] == before,
                  "buying from the market does not teleport the goods");

            /* Which of its harbours served the order is the matcher's
             * business, so take the crossing off the booking rather
             * than assuming a port. */
            crossing = 0;
            for (i = 0; i < gs->book.booking_count; i++)
                if (gs->book.booking[i].active &&
                    gs->book.booking[i].buyer == 1u)
                    crossing = (uint32_t)(gs->book.booking[i].arrive_tick -
                                          gs->sim_tick_no);
            run_ticks(gs, (int)crossing + 4);
            CHECK(gs->islands[0].stockpile.amount[res] > before,
                  "they arrive after a crossing, like anyone else's cargo");
            CHECK(gs->islands[MAX_ISLANDS - 1].hulls_out > 0 ||
                  gs->islands[MAX_ISLANDS - 2].hulls_out > 0,
                  "and the market's own hull is at sea carrying them");
        }

        game_free(gs);
    }

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
