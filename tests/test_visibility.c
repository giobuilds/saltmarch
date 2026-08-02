/* test_visibility.c  --  what a client is not told
 * (SERVER_AUTHORITY.md Phase 3) */

#include "game.h"
#include "net.h"
#include "knowledge.h"
#include "orderbook.h"
#include "resource.h"
#include "sea.h"
#include "snapshot.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* Encode `gs` as player `viewer` is entitled to see it, then decode it
 * into `out` — exactly what the wire does, so what this inspects is
 * what a client would actually hold. */
static int view_as(const GameState *gs, uint32_t viewer, GameState *out)
{
    unsigned char *buf = NULL;
    size_t         len = 0;
    int            ok;

    if (!snapshot_encode_for(gs, viewer, &buf, &len)) return 0;
    ok = snapshot_decode(out, buf, len);
    free(buf);
    return ok;
}

int main(void)
{
    GameState *w    = game_init();
    GameState *view = game_init();
    int        rid, mine = 0, theirs = 1;

    if (!w || !view) { printf("game_init failed\n"); return 1; }
    game_new_seeded(w, 4242u);

    /* Two holdings: island 0 is player 1's from world creation, island
     * 1 goes to player 2. We will look at the world as player 2. */
    w->islands[theirs].settled = 1;
    w->islands[theirs].owner   = 2u;
    stockpile_init(&w->islands[theirs].stockpile);

    /* Give player 1 something worth hiding. */
    w->islands[mine].stockpile.amount[RES_PLANKS] = 4321;
    w->islands[mine].stockpile.amount[RES_GOLD]   = 98765;
    w->islands[mine].escrow[RES_FISH]             = 60;
    w->islands[mine].research_boats               = 3;
    w->islands[mine].insure_shipments             = 1;
    w->islands[mine].docking_allowed              = 1;
    {
        int b = w->islands[mine].building_count++;
        w->islands[mine].buildings[b].active   = 1;
        w->islands[mine].buildings[b].type     = BUILDING_HOUSE_SCHOLAR;
        w->islands[mine].pop_data[b].active    = 1;
        w->islands[mine].pop_data[b].residents = 12;
    }

    /* A passage player 1 has charted, and one player 2 has. */
    {
        const Route *r;
        int          v;
        rid = -1;
        for (v = 1; v < SEA_ROUTES_PER_PAIR; v++) {
            r = sea_route_variant(&w->sea, 0, 1, v);
            if (r && r->is_private) { rid = sea_route_id(&w->sea, r); break; }
        }
    }
    knowledge_add_charts(&w->knowledge, 1u, rid, 5);
    knowledge_add_charts(&w->knowledge, 2u, rid, 2);

    /* An expedition of player 1's, in progress. */
    {
        Survey *m = &w->surveys.mission[w->surveys.count++];
        m->active      = 1;
        m->owner       = 1u;
        m->from_island = mine;
        m->to_island   = theirs;
        m->route_id    = rid;
        m->finish_tick = 5000;
    }

    printf("=== a rival's books are not in your memory ===\n");

    if (!view_as(w, 2u, view)) {
        printf("  FAIL: could not build the view\n");
        return 1;
    }

    CHECK(view->islands[mine].stockpile.amount[RES_PLANKS] == 0 &&
          view->islands[mine].stockpile.amount[RES_GOLD] == 0,
          "a rival's stockpile is not there to read");
    CHECK(view->islands[mine].building_count == 0,
          "nor what they have built");
    CHECK(view->islands[mine].research_boats == 0,
          "nor whether they are outfitting expeditions");
    CHECK(view->islands[mine].insure_shipments == 0,
          "nor how they insure their cargo");

    printf("\n=== nor their charts, which are the whole advantage ===\n");
    CHECK(knowledge_charts(&view->knowledge, 1u, rid) == 0,
          "a rival's charts are not there");
    CHECK(!knowledge_knows(&view->knowledge, 1u, rid, 1),
          "nor even that they know the passage exists");
    CHECK(knowledge_charts(&view->knowledge, 2u, rid) == 2,
          "but your own are exactly where you left them");

    printf("\n=== nor what they are out looking for ===\n");
    {
        int i, theirs_visible = 0;
        for (i = 0; i < view->surveys.count; i++)
            if (view->surveys.mission[i].active &&
                view->surveys.mission[i].owner == 1u) theirs_visible++;
        CHECK(theirs_visible == 0, "a rival's expedition is not reported");
    }

    printf("\n=== what you must still be told ===\n");
    CHECK(view->islands[mine].settled &&
          view->islands[mine].owner == 1u,
          "the island exists and you know who holds it");
    CHECK(view->islands[mine].name[0] != '\0', "and what it is called");
    CHECK(view->islands[mine].docking_allowed == 1,
          "and whether it will let you dock — you cannot sail on a "
          "question you are not allowed to ask");
    CHECK(view->islands[mine].escrow[RES_FISH] == 60,
          "and the harbour escrow survives, because it is the one "
          "exchange strangers are allowed to have");

    printf("\n=== a hold is not a manifest ===\n");
    {
        GameState *v2 = game_init();
        int        s;

        if (!v2) { printf("game_init failed\n"); return 1; }

        s = w->ship_count++;
        w->ships[s].active      = 1;
        w->ships[s].owner       = 1u;
        w->ships[s].at_island   = mine;
        w->ships[s].from_island = mine;
        w->ships[s].to_island   = theirs;
        w->ships[s].cargo[RES_PLANKS] = 40;

        if (!view_as(w, 2u, v2)) { printf("  FAIL: view\n"); return 1; }

        CHECK(v2->ships[s].active && v2->ships[s].owner == 1u,
              "you can see a rival's ship");
        CHECK(v2->ships[s].to_island == theirs,
              "and where it is going, which is what you would attack it for");
        CHECK(v2->ships[s].cargo[RES_PLANKS] == 0,
              "but not what is in it — interception is a bet, not shopping");

        game_free(v2);
    }

    printf("\n=== a chart buys concealment as well as speed ===\n");
    {
        GameState *v3 = game_init();
        GameState *v1 = game_init();
        int        b, lane_id;

        if (!v3 || !v1) { printf("game_init failed\n"); return 1; }
        lane_id = sea_route_id(&w->sea, sea_route_between(&w->sea, 0, 1));

        b = w->book.booking_count++;
        w->book.booking[b].active      = 1;
        w->book.booking[b].seller      = 1u;
        w->book.booking[b].buyer       = 3u;
        w->book.booking[b].from_island = mine;
        w->book.booking[b].to_island   = theirs;
        w->book.booking[b].qty         = 10;
        w->book.booking[b].price       = 5;
        w->book.booking[b].route_id    = rid;      /* the private one */
        w->book.booking[b].arrive_tick = 900;
        w->book.booking[b].return_tick = 1800;

        if (!view_as(w, 2u, v3)) { printf("  FAIL: view\n"); return 1; }

        CHECK(v3->book.booking[b].active,
              "a rival's shipment is visible — the market is public");
        CHECK(v3->book.booking[b].route_id == lane_id,
              "but it is reported on the public lane, whatever it really "
              "took");
        CHECK(v3->book.booking[b].route_id != rid,
              "so finding a faster way is itself concealed");

        /* And the seller still sees their own route truthfully, or the
         * filter would be lying to the person it is protecting. */
        if (!view_as(w, 1u, v1)) { printf("  FAIL: view\n"); return 1; }
        CHECK(v1->book.booking[b].route_id == rid,
              "while its owner sees the passage they actually paid for");

        game_free(v3);
        game_free(v1);
    }

    printf("\n=== a checkpoint is not a view ===\n");
    {
        GameState *full = game_init();
        unsigned char *buf = NULL;
        size_t         len = 0;

        if (!full) { printf("game_init failed\n"); return 1; }

        /* The server's own record of the world must stay complete, or
         * a restart would quietly lose everybody's charts. PLAYER_NONE
         * is what says "this is not anybody's view". */
        CHECK(snapshot_encode_for(w, PLAYER_NONE, &buf, &len),
              "a checkpoint encodes");
        if (!buf) { printf("\nFAILED\n"); return 1; }
        CHECK(snapshot_decode(full, buf, len), "and restores");
        CHECK(knowledge_charts(&full->knowledge, 1u, rid) == 5,
              "with every player's charts intact");
        CHECK(sim_hash(full) == sim_hash(w),
              "and is the whole world, byte for byte");

        free(buf);
        game_free(full);
    }

    game_free(view);
    game_free(w);

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
