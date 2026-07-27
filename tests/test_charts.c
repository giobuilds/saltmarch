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
#include "ship.h"
#include "survey.h"
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

    /* Aim the market maker at goods these tests never trade, as
     * test_orderbook does: it quotes the six it is furthest from
     * baseline on, so overstocking six others keeps Planks out of its
     * book and the counts below are the test's own. Its chart offers
     * are harmless here — nobody bids on them. */
    {
        static const ResourceType DECOY[FACTION_QUOTE_GOODS] = {
            RES_WOOD, RES_FISH, RES_GRAIN, RES_WOOL, RES_CLOTH, RES_FISH_OIL
        };
        int i;
        for (i = 0; i < FACTION_QUOTE_GOODS; i++)
            gs->faction.inventory[DECOY[i]] = 100000;
    }
    gs->faction.gold = 0;
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

    printf("\n=== the fast water is dangerous water ===\n");
    {
        GameState *gs = two_traders(4242u);
        int        rid, raided = 0, safe = 0, i;

        if (!gs) { printf("game_init failed\n"); return 1; }
        fastest_private(&gs->sea, 0, 1, &rid);

        /* A raid is derived from the shipment's identity, not rolled,
         * so the honest test is that the derivation is BIASED the way
         * the design says — a private passage loses cargo more often
         * than the lane — rather than that any one crossing is taken. */
        for (i = 0; i < 400; i++) {
            if (shipment_is_raided(gs->world_seed, rid, (uint64_t)i, 1u,
                                   PIRACY_CHANCE_PRIVATE)) raided++;
            if (shipment_is_raided(gs->world_seed, 0, (uint64_t)i, 1u,
                                   PIRACY_CHANCE_PER_MILLE)) safe++;
        }
        CHECK(raided > safe,
              "pirates take more off the passages than off the lane");
        CHECK(raided > 0 && raided < 400,
              "but not everything, and not nothing");

        /* And it is a function, not a roll: the same shipment always
         * has the same fate, which is what lets a replay agree. */
        CHECK(shipment_is_raided(gs->world_seed, rid, 99u, 1u, 240) ==
              shipment_is_raided(gs->world_seed, rid, 99u, 1u, 240),
              "and the same crossing always has the same fate");

        game_free(gs);
    }

    printf("\n=== a raided cargo costs the seller, not the buyer ===\n");
    {
        GameState   *gs = two_traders(4242u);
        const Route *priv;
        int          rid, tries, raided_at = -1;
        int          buyer_gold_before = 0, seller_planks_before = 0;

        if (!gs) { printf("game_init failed\n"); return 1; }
        priv = fastest_private(&gs->sea, 0, 1, &rid);

        /* Rather than compute which tick produces a taken crossing and
         * hope the booking lands on it, sail the passage repeatedly
         * until one is taken. Each round trip is a real dispatch, so
         * the flag under test is the one the sim actually derived. */
        for (tries = 0; tries < 12 && raided_at < 0; tries++) {
            int i;

            knowledge_add_charts(&gs->knowledge, 1u, rid, 1);
            place(gs, 1u, 0, TRADE_RESOURCE, RES_PLANKS, -10, 5);
            place(gs, 2u, 1, TRADE_RESOURCE, RES_PLANKS,  10, 9);
            run_ticks(gs, 2);

            for (i = 0; i < gs->book.booking_count; i++) {
                Booking *bk = &gs->book.booking[i];
                if (!bk->active || bk->route_id != rid) continue;

                /* The wiring, asserted against the shipment's OWN
                 * dispatch tick: the flag must be what the derivation
                 * says, not merely plausible. */
                {
                    uint64_t booked = bk->arrive_tick - priv->total_ticks;
                    CHECK(bk->raided == shipment_is_raided(gs->world_seed,
                              rid, booked, 1u, PIRACY_CHANCE_PRIVATE),
                          "a shipment's fate is what its identity says");
                }
                if (bk->raided) {
                    raided_at = i;
                    buyer_gold_before =
                        gs->islands[1].stockpile.amount[RES_GOLD];
                    seller_planks_before =
                        gs->islands[0].stockpile.amount[RES_PLANKS];
                }
                break;
            }
            if (raided_at < 0) run_ticks(gs, (int)priv->total_ticks * 2 + 4);
        }

        CHECK(raided_at >= 0, "the passage takes a cargo sooner or later");
        if (raided_at >= 0) {
            int landed_before = gs->islands[1].stockpile.amount[RES_PLANKS];

            run_ticks(gs, (int)priv->total_ticks * 2 + 6);
            CHECK(gs->islands[1].stockpile.amount[RES_PLANKS] ==
                  landed_before,
                  "nothing lands");
            CHECK(gs->islands[1].stockpile.amount[RES_GOLD] >
                  buyer_gold_before,
                  "the buyer, who did not choose the passage, is made whole");
            CHECK(gs->islands[0].stockpile.amount[RES_PLANKS] ==
                  seller_planks_before,
                  "and the seller is out the goods");
        }

        game_free(gs);
    }

    printf("\n=== a policy pays, and the passage costs more to cover ===\n");
    {
        GameState *gs = two_traders(4242u);
        int        rid, i, found = -1, lane_id;
        int        seller_gold;

        if (!gs) { printf("game_init failed\n"); return 1; }
        fastest_private(&gs->sea, 0, 1, &rid);
        lane_id = sea_route_id(&gs->sea, sea_route_between(&gs->sea, 0, 1));

        CHECK(faction_route_premium(&gs->faction, rid) >
              faction_route_premium(&gs->faction, lane_id),
              "covering the passage costs more than covering the lane");

        for (i = 5; i < 600 && found < 0; i++)
            if (shipment_is_raided(gs->world_seed, rid, (uint64_t)i, 1u,
                                   PIRACY_CHANCE_PRIVATE)) found = i;
        CHECK(found > 4, "some crossing on this passage is taken");

        AS(gs, 1u, game_set_insurance(gs, 0, 1));
        knowledge_add_charts(&gs->knowledge, 1u, rid, 1);
        run_ticks(gs, found - 1);
        CHECK(gs->islands[0].insure_shipments, "the port carries a policy");

        place(gs, 1u, 0, TRADE_RESOURCE, RES_PLANKS, -10, 5);
        place(gs, 2u, 1, TRADE_RESOURCE, RES_PLANKS,  10, 9);
        run_ticks(gs, 2);
        CHECK(gs->book.booking[0].insured_value > 0,
              "and the shipment goes out covered");

        seller_gold = gs->islands[0].stockpile.amount[RES_GOLD];
        run_ticks(gs, 900);
        CHECK(gs->islands[0].stockpile.amount[RES_GOLD] > seller_gold,
              "so the loss is paid out rather than simply borne");

        game_free(gs);
    }

    printf("\n=== an expedition needs a scholar, a boat and paper ===\n");
    {
        GameState *gs = two_traders(4242u);
        Island    *isl;
        int        rid;

        if (!gs) { printf("game_init failed\n"); return 1; }
        isl = &gs->islands[0];
        fastest_private(&gs->sea, 0, 1, &rid);

        CHECK(island_scholar_capacity(isl) == 0,
              "an island with no Scholars' House can send nobody");

        AS(gs, 1u, game_survey(gs, 0, 1));
        run_ticks(gs, 2);
        CHECK(survey_active_count(&gs->surveys, 1u) == 0,
              "and a survey from it does not sail");

        /* Give it a scholar. Still no boat. */
        {
            int i = isl->building_count++;
            isl->buildings[i].active   = 1;
            isl->buildings[i].type     = BUILDING_HOUSE_SCHOLAR;
            isl->pop_data[i].active    = 1;
            isl->pop_data[i].residents = 6;
        }
        CHECK(island_scholar_capacity(isl) == 1, "a lived-in one can");
        AS(gs, 1u, game_survey(gs, 0, 1));
        run_ticks(gs, 2);
        CHECK(survey_active_count(&gs->surveys, 1u) == 0,
              "but not without a hull to put them in");

        /* A research boat needs a Shipyard to build. */
        AS(gs, 1u, game_build_research_boat(gs, 0));
        run_ticks(gs, 2);
        CHECK(isl->research_boats == 0, "no Shipyard, no research boat");

        {
            int i = isl->building_count++;
            isl->buildings[i].active = 1;
            isl->buildings[i].type   = BUILDING_SHIPYARD;
        }
        AS(gs, 1u, game_build_research_boat(gs, 0));
        run_ticks(gs, 2);
        CHECK(isl->research_boats == 1, "with one, the yard lays one down");

        /* Still no blank chart. */
        isl->stockpile.amount[RES_CHARTS] = 0;
        AS(gs, 1u, game_survey(gs, 0, 1));
        run_ticks(gs, 2);
        CHECK(survey_active_count(&gs->surveys, 1u) == 0,
              "and nothing sails without paper to draw on");

        isl->stockpile.amount[RES_CHARTS] = 1;
        AS(gs, 1u, game_survey(gs, 0, 1));
        run_ticks(gs, 2);
        CHECK(survey_active_count(&gs->surveys, 1u) == 1,
              "with all three, the expedition sails");
        CHECK(isl->stockpile.amount[RES_CHARTS] == 0,
              "the blank chart is spent on departure, not on return");
        CHECK(isl->scholars_out == 1 && isl->research_boats_out == 1,
              "and the scholar and the boat are committed");

        /* One scholar means one expedition. */
        isl->stockpile.amount[RES_CHARTS] = 1;
        AS(gs, 1u, game_survey(gs, 0, 2));
        run_ticks(gs, 2);
        CHECK(survey_active_count(&gs->surveys, 1u) == 1,
              "a second cannot sail while the first is out");

        game_free(gs);
    }

    printf("\n=== what an expedition comes back with ===\n");
    {
        /* The outcome is a function of the mission's identity, so the
         * honest test is that both outcomes occur and that each has
         * the consequences it should — not that any one mission
         * succeeds. */
        int i, wins = 0, losses = 0, sunk = 0;

        for (i = 0; i < 300; i++) {
            if (survey_succeeds(4242u, 7, (uint64_t)i, 1u)) wins++;
            else if (survey_is_lost(4242u, 7, (uint64_t)i, 1u)) sunk++;
            else losses++;
        }
        CHECK(wins > 0 && losses > 0 && sunk > 0,
              "expeditions succeed, fail, and are lost");
        CHECK(wins > sunk, "most that sail come home");
        CHECK(survey_is_lost(4242u, 7, 3u, 1u) == 0 ||
              survey_succeeds(4242u, 7, 3u, 1u) == 0,
              "and one that found the passage is never also lost");
        CHECK(survey_succeeds(4242u, 7, 11u, 1u) ==
              survey_succeeds(4242u, 7, 11u, 1u),
              "the same expedition always has the same fate");

        /* And the odds must hold for EVERY passage over the SPAN THE
         * GAME ACTUALLY ASKS ABOUT — low route ids, early ticks — not
         * merely on average across a large sample. The first version
         * of this derivation passed every other test here while giving
         * one route a 0% loss rate over two hundred consecutive ticks.
         * Widen the sample and it looked fine; that is exactly why the
         * sample here is narrow.
         *
         * A derived outcome that is deterministic but visibly lumpy is
         * worse than a random one: it reads to a player as the game
         * having decided something about them. */
        {
            int r, worst = 100000, best = 0;

            for (r = 0; r < 12; r++) {
                int t, lost = 0;
                for (t = 0; t < 200; t++)
                    if (survey_is_lost(4242u, r, (uint64_t)t, 1u)) lost++;
                if (lost < worst) worst = lost;
                if (lost > best)  best  = lost;
            }
            /* Expected 14% of 200 = 28. The band is wide enough not to
             * fail on the constants being retuned, and narrow enough
             * that a passage which is quietly never dangerous, or
             * always is, does not get through. */
            CHECK(worst >= 10 && best <= 50,
                  "and no passage is quietly safe or quietly lethal");
        }
    }

    printf("\n=== a survey that finds it hands over the passage ===\n");
    {
        GameState *gs = two_traders(4242u);
        Island    *isl;
        int        rid, tries, charted = 0;

        if (!gs) { printf("game_init failed\n"); return 1; }
        isl = &gs->islands[0];
        fastest_private(&gs->sea, 0, 1, &rid);

        {
            int i = isl->building_count++;
            isl->buildings[i].active   = 1;
            isl->buildings[i].type     = BUILDING_HOUSE_SCHOLAR;
            isl->pop_data[i].active    = 1;
            isl->pop_data[i].residents = 8;
            i = isl->building_count++;
            isl->buildings[i].active = 1;
            isl->buildings[i].type   = BUILDING_SHIPYARD;
        }
        isl->research_boats = 4;

        for (tries = 0; tries < 8 && !charted; tries++) {
            isl->stockpile.amount[RES_CHARTS] = 1;
            AS(gs, 1u, game_survey(gs, 0, 1));
            run_ticks(gs, SURVEY_TICKS + 4);
            if (knowledge_charts(&gs->knowledge, 1u, rid) > 0) charted = 1;
        }

        CHECK(charted, "sooner or later an expedition charts the passage");
        CHECK(knowledge_knows(&gs->knowledge, 1u, rid, 1),
              "and the player knows it thereafter");
        CHECK(isl->scholars_out == 0 && isl->research_boats_out == 0,
              "the crew is released either way");

        /* A pair has TWO private passages, so charting one leaves
         * something still worth looking for — the next expedition aims
         * at the other. Only when both are known is there nothing left
         * to find, and then the survey is refused rather than quietly
         * burning the paper. */
        {
            int v, remaining = 0;
            for (v = 0; v < SEA_ROUTES_PER_PAIR; v++) {
                const Route *r = sea_route_variant(&gs->sea, 0, 1, v);
                int          id;
                if (!r || !r->is_private) continue;
                id = sea_route_id(&gs->sea, r);
                if (!knowledge_knows(&gs->knowledge, 1u, id, 1)) remaining++;
            }
            CHECK(remaining >= 1, "the pair still has an unknown passage");

            /* Learn every private passage between the two, and the
             * expedition has nowhere left to go. */
            for (v = 0; v < SEA_ROUTES_PER_PAIR; v++) {
                const Route *r = sea_route_variant(&gs->sea, 0, 1, v);
                if (r && r->is_private)
                    knowledge_add_charts(&gs->knowledge, 1u,
                                         sea_route_id(&gs->sea, r), 1);
            }
        }
        isl->stockpile.amount[RES_CHARTS] = 1;
        AS(gs, 1u, game_survey(gs, 0, 1));
        run_ticks(gs, 2);
        CHECK(isl->stockpile.amount[RES_CHARTS] == 1,
              "a survey for a crossing you have fully charted is refused, "
              "rather than quietly burning the paper");

        game_free(gs);
    }

    printf("\n=== an expedition lost takes the scholar with it ===\n");
    {
        int attempt, seen = 0;

        /* A fresh world per attempt, each dispatching at a different
         * tick. One world cannot supply enough samples: an expedition
         * that succeeds charts its passage, a pair has only two, and
         * after fourteen the player knows every private route out of
         * their island and nothing further can sail at all. */
        for (attempt = 0; attempt < 60 && !seen; attempt++) {
            GameState *gs = two_traders(4242u);
            Island    *isl;
            int        house, idx = -1, j;

            if (!gs) { printf("game_init failed\n"); return 1; }
            isl = &gs->islands[0];

            house = isl->building_count++;
            isl->buildings[house].active   = 1;
            isl->buildings[house].type     = BUILDING_HOUSE_SCHOLAR;
            isl->pop_data[house].active    = 1;
            isl->pop_data[house].residents = 40;
            j = isl->building_count++;
            isl->buildings[j].active = 1;
            isl->buildings[j].type   = BUILDING_SHIPYARD;
            isl->research_boats = 4;

            run_ticks(gs, attempt);
            isl->stockpile.amount[RES_CHARTS] = 1;
            AS(gs, 1u, game_survey(gs, 0, 1));
            run_ticks(gs, 2);

            for (j = 0; j < gs->surveys.count; j++)
                if (gs->surveys.mission[j].active) idx = j;

            if (idx >= 0 && gs->surveys.mission[idx].lost) {
                uint64_t finish = gs->surveys.mission[idx].finish_tick;
                int      boats_before, residents_before;

                seen = 1;

                /* Measure across the tick that RESOLVES the mission,
                 * not across the whole voyage: residents rise and fall
                 * on their own over nine hundred ticks, and a
                 * before-and-after spanning all of it would be
                 * measuring the food supply instead. */
                while (gs->sim_tick_no + 1 < finish) sim_run_one_tick(gs);
                boats_before     = isl->research_boats;
                residents_before = isl->pop_data[house].residents;
                run_ticks(gs, 2);

                CHECK(isl->research_boats == boats_before - 1,
                      "a lost expedition does not give the boat back");
                CHECK(isl->pop_data[house].residents == residents_before - 1,
                      "and the house that sent them is one smaller");
                CHECK(isl->scholars_out == 0,
                      "the commitment ends even though the scholar did not "
                      "come home");
            }
            game_free(gs);
        }
        CHECK(seen, "an expedition is lost sooner or later");
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
