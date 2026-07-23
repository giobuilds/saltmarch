/*  test_faction.c  --  headless verification of MMO_PLAN Phase 3
 *
 * The elastic NPC market. Uses sim_apply() directly to isolate a single
 * trade from the per-tick mean reversion, then sim_run_one_tick() to
 * exercise reversion. Checks the properties the plan calls out:
 *
 *   - Day-one neutrality: at baseline inventory the quotes equal the old
 *     fixed SELL_PRICE / BUY_PRICE.
 *   - Conservation: player gold + faction gold is unchanged by a trade.
 *   - Elasticity: selling raises faction inventory and lowers its bid.
 *   - Mean reversion: leaving it alone drifts inventory back to baseline
 *     and the bid recovers.
 *   - Finite gold: a near-broke faction buys only what it can pay for.
 *   - Finite inventory: the faction sells only what it holds.
 *
 * Built and run by tests/run.sh.
 */

#include "game.h"
#include "faction.h"
#include "resource.h"
#include <SDL3/SDL.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* player_id 1: island 0 is owned by player 1, and sim_apply's Phase 5
 * ownership gate rejects identity-less commands. */
static void apply_sell(GameState *gs, ResourceType res, int qty)
{
    Command c = {0};
    c.kind = CMD_SELL_RESOURCE; c.player_id = 1;
    c.a = 0; c.b = (int32_t)res; c.c = qty;
    sim_apply(gs, &c);
}
static void apply_buy(GameState *gs, ResourceType res, int qty)
{
    Command c = {0};
    c.kind = CMD_BUY_RESOURCE; c.player_id = 1;
    c.a = 0; c.b = (int32_t)res; c.c = qty;
    sim_apply(gs, &c);
}

int main(void)
{
    GameState *gs = game_init();
    if (!gs) { printf("game_init failed\n"); return 1; }
    game_new_seeded(gs, 55u);

    Stockpile *sp = &gs->islands[0].stockpile;
    sp->capacity = 2000;   /* room to trade freely in the test */

    /* ---- Day-one neutrality ---- */
    CHECK(faction_bid(&gs->faction, RES_FISH) == SELL_PRICE[RES_FISH],
          "bid equals SELL_PRICE at baseline inventory");
    CHECK(faction_ask(&gs->faction, RES_FISH) == BUY_PRICE[RES_FISH],
          "ask equals BUY_PRICE at baseline inventory");

    /* ---- Conservation + elasticity on a sell ---- */
    sp->amount[RES_FISH] = 400;
    long total_before = (long)sp->amount[RES_GOLD] + gs->faction.gold;
    int  inv_before   = gs->faction.inventory[RES_FISH];
    int  bid_before   = faction_bid(&gs->faction, RES_FISH);

    apply_sell(gs, RES_FISH, 100);

    long total_after = (long)sp->amount[RES_GOLD] + gs->faction.gold;
    CHECK(total_after == total_before,
          "player gold + faction gold conserved across a sell");
    CHECK(gs->faction.inventory[RES_FISH] == inv_before + 100,
          "faction inventory rose by the quantity sold");
    CHECK(faction_bid(&gs->faction, RES_FISH) < bid_before,
          "bid stepped down after selling");

    /* ---- Mean reversion recovers the price ---- */
    int inv_hi = gs->faction.inventory[RES_FISH];
    int bid_lo = faction_bid(&gs->faction, RES_FISH);
    for (int i = 0; i < 1000; i++) sim_run_one_tick(gs);
    CHECK(gs->faction.inventory[RES_FISH] < inv_hi,
          "inventory reverts toward baseline over time");
    CHECK(faction_bid(&gs->faction, RES_FISH) > bid_lo,
          "bid recovers as inventory reverts");

    /* ---- Conservation on a buy ---- */
    sp->amount[RES_GOLD] = 10000;
    long tb = (long)sp->amount[RES_GOLD] + gs->faction.gold;
    apply_buy(gs, RES_WOOD, 20);
    long ta = (long)sp->amount[RES_GOLD] + gs->faction.gold;
    CHECK(ta == tb, "player gold + faction gold conserved across a buy");

    /* ---- Finite faction gold: it refuses beyond what it can pay ---- */
    gs->faction.gold      = 5;
    sp->amount[RES_FISH]  = 400;
    int fish_price        = faction_bid(&gs->faction, RES_FISH);
    int gold_before_broke = sp->amount[RES_GOLD];
    apply_sell(gs, RES_FISH, 400);
    int paid = sp->amount[RES_GOLD] - gold_before_broke;
    CHECK(gs->faction.gold >= 0, "faction never pays into gold debt");
    CHECK(paid <= 5, "near-broke faction buys only what its gold covers");
    (void)fish_price;

    /* ---- Finite faction inventory: it sells only what it holds ---- */
    gs->faction.inventory[RES_MALT] = 3;
    sp->amount[RES_GOLD] = 100000;
    int room_before = sp->amount[RES_MALT];
    apply_buy(gs, RES_MALT, 50);      /* ask for 50, only 3 available */
    CHECK(sp->amount[RES_MALT] - room_before == 3,
          "buy clamps to the faction's available inventory");
    CHECK(gs->faction.inventory[RES_MALT] == 0,
          "faction inventory drained to zero, not negative");

    game_free(gs);
    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
