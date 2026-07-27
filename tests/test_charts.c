/*  test_charts.c  --  route knowledge and the maps that buy it
 *                     (MARITIME_PLAN Phase 3b)
 *
 * Phase 3a put three routes between every pair and left all of them
 * usable. This is the half that makes two of them secret, and the
 * properties worth asserting are the ones a player would notice going
 * wrong:
 *
 *   - a passage you have no map for is not a passage you can sail,
 *     however fast it is;
 *   - a map you hold gets spent, so the advantage is bought and not
 *     granted;
 *   - knowing outlives holding, because a trader who has sailed a
 *     strait does not forget where it was;
 *   - and a chart is a THING — it can be bought and sold like cargo,
 *     which is what the (kind, id) trade identity was built for two
 *     phases ago.
 *
 * Built and run by tests/run.sh. Linked without SDL: this is sim.
 */

#include "game.h"
#include "knowledge.h"
#include "orderbook.h"
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

/* Two traders, as in test_orderbook, with the market maker aimed at
 * goods and routes these tests do not use. */
static GameState *two_traders(uint32_t seed)
{
    GameState *gs = game_init();
    if (!gs) return NULL;

    game_new_seeded(gs, seed);
    gs->islands[1].settled = 1;
    gs->islands[1].owner   = 2u;
    stockpile_init(&gs->islands[1].stockpile);

    gs->islands[0].stockpile.amount[RES_GOLD]   = 1000000;
    gs->islands[1].stockpile.amount[RES_GOLD]   = 1000000;
    gs->islands[0].stockpile.amount[RES_PLANKS] = 1000;
    return gs;
}

static void place(GameState *gs, uint32_t who, int island,
                  TradeKind kind, uint16_t id, int qty, int limit)
{
    Command c;
    memset(&c, 0, sizeof(c));
    c.kind = CMD_PLACE_ORDER;
    c.a    = island;
    c.b    = TRADE_PACK(kind, id);
    c.c    = qty;
    c.d    = limit;
    AS(gs, who, command_submit(gs, &c));
}

/* The fastest private passage between two islands, and its id. */
static const Route *fastest_private(const Sea *sea, int a, int b, int *out_id)
{
    const Route *best = NULL;
    int          v;

    *out_id = -1;
    for (v = 0; v < SEA_ROUTES_PER_PAIR; v++) {
        const Route *r = sea_route_variant(sea, a, b, v);
        if (!r || !r->is_private) continue;
        if (!best || r->total_ticks < best->total_ticks) {
            best    = r;
            *out_id = sea_route_id(sea, r);
        }
    }
    return best;
}

int main(void)
{
    printf("=== the sea keeps its secrets ===\n");
    {
        GameState   *gs = two_traders(4242u);
        const Route *pub, *priv;
        int          rid;

        if (!gs) { printf("game_init failed\n"); return 1; }

        pub  = sea_route_between(&gs->sea, 0, 1);
        priv = fastest_private(&gs->sea, 0, 1, &rid);
        CHECK(pub && priv && rid >= 0, "the pair has a lane and a passage");
        CHECK(priv->total_ticks < pub->total_ticks,
              "and the passage is the faster water");

        CHECK(knowledge_knows(&gs->knowledge, 1u,
                              sea_route_id(&gs->sea, pub), 0),
              "everyone knows the public lane");
        CHECK(!knowledge_knows(&gs->knowledge, 1u, rid, 1),
              "and nobody starts knowing a private one");
        CHECK(knowledge_knows(&gs->knowledge, PLAYER_FACTION, rid, 1),
              "except the market, which draws the maps");

        game_free(gs);
    }

    printf("\n=== without a chart, the fast water is not yours ===\n");
    {
        GameState   *gs = two_traders(4242u);
        const Route *pub;
        int          rid;

        if (!gs) { printf("game_init failed\n"); return 1; }
        pub = sea_route_between(&gs->sea, 0, 1);
        fastest_private(&gs->sea, 0, 1, &rid);

        place(gs, 1u, 0, TRADE_RESOURCE, RES_PLANKS, -10, 5);
        place(gs, 2u, 1, TRADE_RESOURCE, RES_PLANKS,  10, 9);
        run_ticks(gs, 2);

        CHECK(gs->book.booking[0].active, "the trade books");
        CHECK(gs->book.booking[0].route_id == sea_route_id(&gs->sea, pub),
              "and sails the public lane, because that is all it has");
        CHECK(gs->book.booking[0].arrive_tick - gs->sim_tick_no + 2 >=
              pub->total_ticks,
              "at the lane's pace, not the passage's");

        game_free(gs);
    }

    printf("\n=== a chart buys the passage, once ===\n");
    {
        GameState   *gs = two_traders(4242u);
        const Route *priv;
        int          rid;

        if (!gs) { printf("game_init failed\n"); return 1; }
        priv = fastest_private(&gs->sea, 0, 1, &rid);

        /* Player 1 acquires one map, however they came by it. */
        knowledge_add_charts(&gs->knowledge, 1u, rid, 1);
        CHECK(knowledge_knows(&gs->knowledge, 1u, rid, 1),
              "holding a map teaches you the passage");
        CHECK(knowledge_charts(&gs->knowledge, 1u, rid) == 1,
              "and you have one of them");

        place(gs, 1u, 0, TRADE_RESOURCE, RES_PLANKS, -10, 5);
        place(gs, 2u, 1, TRADE_RESOURCE, RES_PLANKS,  10, 9);
        run_ticks(gs, 2);

        CHECK(gs->book.booking[0].route_id == rid,
              "the cargo takes the passage the seller can sail");
        CHECK(gs->book.booking[0].arrive_tick - (gs->sim_tick_no - 1) <=
              priv->total_ticks + 1,
              "and arrives at the passage's pace");
        CHECK(knowledge_charts(&gs->knowledge, 1u, rid) == 0,
              "the map is spent on the voyage — bought, not granted");
        CHECK(knowledge_knows(&gs->knowledge, 1u, rid, 1),
              "but knowing it outlives holding it");

        /* The next cargo has no map, so it goes the long way round.
         * Wait for the first crew to get home first — the island has
         * one hull, and an order with nothing to carry it just rests
         * (which test_orderbook already covers). */
        run_ticks(gs, (int)priv->total_ticks * 2 + 4);
        CHECK(gs->islands[0].hulls_out == 0, "the first crew is home");

        place(gs, 1u, 0, TRADE_RESOURCE, RES_PLANKS, -10, 5);
        place(gs, 2u, 1, TRADE_RESOURCE, RES_PLANKS,  10, 9);
        run_ticks(gs, 2);
        {
            int i, latest = -1;
            for (i = 0; i < gs->book.booking_count; i++)
                if (gs->book.booking[i].active)
                    latest = gs->book.booking[i].route_id;
            CHECK(latest >= 0 && latest != rid,
                  "and the next one is back on the lane");
        }

        game_free(gs);
    }

    printf("\n=== a chart is a thing, and things are traded ===\n");
    {
        GameState *gs = two_traders(4242u);
        int        rid;

        if (!gs) { printf("game_init failed\n"); return 1; }
        fastest_private(&gs->sea, 0, 1, &rid);

        /* Player 1 has two maps and sells one to player 2. This is the
         * whole reason a trade identity is a (kind, id) pair rather
         * than a ResourceType: a chart for THIS passage is a different
         * object from a chart for any other, and neither could ever
         * have had an enum slot. */
        knowledge_add_charts(&gs->knowledge, 1u, rid, 2);

        place(gs, 1u, 0, TRADE_ROUTE_CHART, (uint16_t)rid, -1, 100);
        run_ticks(gs, 1);
        CHECK(knowledge_charts(&gs->knowledge, 1u, rid) == 1,
              "posting a chart for sale takes it out of your hands");

        place(gs, 2u, 1, TRADE_ROUTE_CHART, (uint16_t)rid, 1, 150);
        run_ticks(gs, 2);
        CHECK(orderbook_booking_live(&gs->book) >= 1,
              "and it matches like any other cargo");
        CHECK(knowledge_charts(&gs->knowledge, 2u, rid) == 0,
              "a map at sea is not yet a map in hand");

        run_ticks(gs, 900);
        CHECK(knowledge_charts(&gs->knowledge, 2u, rid) == 1,
              "it arrives by ship, like everything else");
        CHECK(knowledge_knows(&gs->knowledge, 2u, rid, 1),
              "and the buyer now knows the passage");

        game_free(gs);
    }

    printf("\n=== a withdrawn chart comes back ===\n");
    {
        GameState *gs = two_traders(4242u);
        Command    c;
        int        rid;
        uint32_t   id = 0u;
        int        i;

        if (!gs) { printf("game_init failed\n"); return 1; }
        fastest_private(&gs->sea, 0, 1, &rid);
        knowledge_add_charts(&gs->knowledge, 1u, rid, 1);

        place(gs, 1u, 0, TRADE_ROUTE_CHART, (uint16_t)rid, -1, 100);
        run_ticks(gs, 1);
        for (i = 0; i < gs->book.order_count; i++)
            if (gs->book.order[i].active && gs->book.order[i].owner == 1u)
                id = gs->book.order[i].id;

        memset(&c, 0, sizeof(c));
        c.kind = CMD_CANCEL_ORDER;
        c.a    = (int32_t)id;
        AS(gs, 1u, command_submit(gs, &c));
        run_ticks(gs, 1);

        CHECK(knowledge_charts(&gs->knowledge, 1u, rid) == 1,
              "cancelling returns the map, not a resource of the same id");

        game_free(gs);
    }

    printf("\n=== the market sells maps ===\n");
    {
        GameState *gs = game_init();
        int        i, offers = 0, on_public = 0;

        if (!gs) { printf("game_init failed\n"); return 1; }
        game_new_seeded(gs, 4242u);
        run_ticks(gs, FACTION_QUOTE_INTERVAL_TICKS + 2);

        for (i = 0; i < gs->book.order_count; i++) {
            const Order *o = &gs->book.order[i];
            if (!o->active || o->owner != PLAYER_FACTION) continue;
            if (o->what.kind != TRADE_ROUTE_CHART) continue;
            offers++;
            if (!gs->sea.route[o->what.id].is_private) on_public++;
        }
        CHECK(offers > 0, "the market has charts on the counter");
        CHECK(offers <= FACTION_CHART_ROUTES, "a few of them, not an atlas");
        CHECK(on_public == 0,
              "and never a map of the lane everyone already knows");

        game_free(gs);
    }

    printf("\n=== knowledge survives a checkpoint ===\n");
    {
        GameState *gs = two_traders(4242u);
        GameState *rs = game_init();
        unsigned char *buf = NULL;
        size_t         len = 0;
        int            rid;

        if (!gs || !rs) { printf("game_init failed\n"); return 1; }
        fastest_private(&gs->sea, 0, 1, &rid);
        knowledge_add_charts(&gs->knowledge, 1u, rid, 3);
        run_ticks(gs, 2);

        CHECK(snapshot_encode(gs, &buf, &len), "the world snapshots");
        if (!buf) { printf("\nFAILED\n"); return 1; }
        CHECK(snapshot_decode(rs, buf, len), "and restores");
        CHECK(knowledge_charts(&rs->knowledge, 1u, rid) == 3,
              "with the charts still in hand");
        CHECK(sim_hash(rs) == sim_hash(gs),
              "and hashes identically — a restore that forgot a passage "
              "would route the next cargo differently and desync");

        free(buf);
        game_free(gs);
        game_free(rs);
    }

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
