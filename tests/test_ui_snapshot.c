/* test_ui_snapshot.c  --  what the screens are allowed to know
 * (UI_PLAN N1) */

#include "game.h"
#include "ui_snapshot.h"
#include "orderbook.h"
#include "knowledge.h"
#include "sea.h"
#include "ui_kit.h"
#include "inventory_view.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

static UiSnapshot snap;

int main(void)
{
    GameState *gs = game_init();
    int        mine = 0, theirs = 1, rid = -1, v;

    if (!gs) { printf("game_init failed\n"); return 1; }
    game_new_seeded(gs, 4242u);
    gs->local_player_id = 1u;

    gs->islands[theirs].settled = 1;
    gs->islands[theirs].owner   = 2u;
    stockpile_init(&gs->islands[theirs].stockpile);

    printf("=== absence is not zero ===\n");
    {
        ui_snapshot_build(&snap, gs);

        CHECK(snap.islands[mine].detail_known,
              "your own island's numbers are knowledge");
        CHECK(!snap.islands[theirs].detail_known,
              "a rival's are not — and the snapshot says so, because a "
              "drawing function cannot tell an empty stockpile from an "
              "unseen one and will render both as 0");

        /* The public face still comes through. A flag that suppressed
         * everything would be as wrong in the other direction: you have
         * to know the island exists, who holds it, and whether it will
         * let you dock. */
        CHECK(snap.islands[theirs].settled &&
              snap.islands[theirs].owner == 2u,
              "you are still told the island is there and who holds it");
        CHECK(snap.islands[theirs].name[0] != '\0',
              "and what it is called");
    }

    printf("\n=== the harbour's capacity, resolved once ===\n");
    {
        CHECK(snap.islands[mine].merchant_capacity > 0 &&
              snap.islands[mine].hull_capacity > 0,
              "a settled harbour can put something to sea");
        CHECK(snap.islands[mine].scholar_capacity == 0,
              "but cannot send an expedition without a Scholars' House");
        CHECK(snap.islands[mine].merchants_out == 0,
              "and has nothing out yet");
    }

    printf("\n=== the book reaches the screen ===\n");
    {
        Command c;

        memset(&c, 0, sizeof(c));
        c.kind = CMD_PLACE_ORDER;
        c.a    = mine;
        c.b    = TRADE_PACK(TRADE_RESOURCE, (uint16_t)RES_PLANKS);
        c.c    = -10;
        c.d    = 7;
        gs->islands[mine].stockpile.amount[RES_PLANKS] = 100;
        command_submit(gs, &c);
        sim_run_one_tick(gs);
        ui_snapshot_build(&snap, gs);

        CHECK(snap.order_count >= 1, "a posted order is visible");
        {
            int i, found = -1;
            for (i = 0; i < snap.order_count; i++)
                if (snap.order[i].mine) found = i;
            CHECK(found >= 0, "and marked as the local player's");
            if (found >= 0) {
                CHECK(snap.order[found].id != 0,
                      "carrying its own id — a row index would be exactly "
                      "the positional identity UI_PLAN decision 2 bans, "
                      "and the book reorders under you");
                CHECK(snap.order[found].limit == 7 &&
                      snap.order[found].qty == 10,
                      "with the price and quantity it was posted at");
            }
        }
    }

    printf("\n=== charts, which are the whole advantage ===\n");
    {
        for (v = 1; v < SEA_ROUTES_PER_PAIR; v++) {
            const Route *r = sea_route_variant(&gs->sea, 0, 1, v);
            if (r && r->is_private) { rid = sea_route_id(&gs->sea, r); break; }
        }
        CHECK(rid >= 0, "the pair has a private passage");

        ui_snapshot_build(&snap, gs);
        CHECK(!snap.route_known[rid] && snap.chart_held[rid] == 0,
              "which you have not found");

        knowledge_add_charts(&gs->knowledge, 1u, rid, 2);
        ui_snapshot_build(&snap, gs);
        CHECK(snap.route_known[rid] && snap.chart_held[rid] == 2,
              "and now have two maps of");

        /* Somebody else's charts are not in this process to copy, so
         * there is nothing here to leak — which is the point of the
         * snapshot being built from an already-redacted world rather
         * than being trusted to filter. */
        knowledge_add_charts(&gs->knowledge, 2u, rid, 5);
        ui_snapshot_build(&snap, gs);
        CHECK(snap.chart_held[rid] == 2,
              "a rival's charts do not appear in your snapshot even when "
              "they are in the same process");
    }

    printf("\n=== which passages are in play ===\n");
    {
        int pair = sea_pair_index(&gs->sea, 0, 1);
        CHECK(pair >= 0 && pair < UI_MAX_PAIRS, "the pair has an index");
        CHECK(snap.pair_cursor[pair] == gs->sea.pair_cursor[pair],
              "and the snapshot carries where its rotation has got to — "
              "the one mutable byte of a Sea, so the UI can read the rest "
              "of it directly and still be looking at the same world");
    }

    printf("\n=== the fleets ===\n");
    {
        CHECK(snap.pirate_count > 0, "the sea's fleets reach the screen");
        CHECK(snap.pirate[0].waypoint >= 0 &&
              snap.pirate[0].waypoint < gs->sea.waypoint_count,
              "at a waypoint the map can name");
    }

    printf("\n=== absence has a look ===\n");
    {
        char buf[32];

        /* The vocabulary itself (UI_PLAN N2). It lives in ui_kit rather
         * than in each drawer so every surface says it the same way —
         * a player should learn the mark once. */
        ui_fmt_known(buf, sizeof(buf), 1, "%d", 0);
        CHECK(strcmp(buf, "0") == 0,
              "a zero you were told is a zero");

        ui_fmt_known(buf, sizeof(buf), 0, "%d", 0);
        CHECK(strcmp(buf, "0") != 0,
              "a zero you were NOT told is not a zero — which is the "
              "whole of N2, and the difference a drawing function cannot "
              "recover on its own");
        CHECK(buf[0] != '\0', "and it is something rather than a gap: "
              "the eye skips a gap, so absence has to be present to be "
              "information");

        ui_fmt_known(buf, sizeof(buf), 0, "%d", 4321);
        CHECK(strcmp(buf, "4321") != 0,
              "and a number you were not told never leaks through it");
    }

    printf("\n=== the stores of an island you do not hold ===\n");
    {
        InventoryView v;

        ui_snapshot_build(&snap, gs);

        inventory_view_build(&v, &snap, mine);
        CHECK(v.detail_known, "your own warehouse is knowledge");

        inventory_view_build(&v, &snap, theirs);
        CHECK(!v.detail_known,
              "a rival's is not — and the list is still built, so layout "
              "and hit-testing do not grow a second shape that only "
              "foreign islands ever take");
        CHECK(v.row_count > 0,
              "the rows are there to be marked, not removed");
    }

    printf("\n=== the snapshot is a copy, not a window ===\n");
    {
        /* UI_PLAN decision 1 in one assertion: a screen holding a
         * snapshot cannot reach the world through it. If this ever
         * stops being true the whole seam is decorative. */
        int32_t before = snap.islands[mine].stock[RES_PLANKS];

        gs->islands[mine].stockpile.amount[RES_PLANKS] = 12345;
        CHECK(snap.islands[mine].stock[RES_PLANKS] == before,
              "changing the world does not change a snapshot already taken");
    }

    game_free(gs);
    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
