#ifndef FACTION_H
#define FACTION_H

/* =========================================================
 * faction.h  --  The NPC market as a real counterparty
 *                (MMO_PLAN Phase 3)
 *
 * Replaces the infinite-liquidity fixed price tables (SELL_PRICE /
 * BUY_PRICE) with one faction that has finite gold, real inventory, and
 * elastic quotes. Selling a good TO the faction raises its inventory of
 * that good, which lowers what it will pay next time; buying drains its
 * inventory and lifts the price. Inventory slowly mean-reverts toward a
 * baseline, so prices recover when left alone.
 *
 * DETERMINISM: this is world sim state. It lives in GameState, is hashed
 * by sim_hash, mutates only inside sim_apply (trades) and
 * sim_run_one_tick (faction_tick), and every value here is an integer —
 * no float can drift across platforms. The quotes are computed by
 * integer multiply-then-divide so low-priced goods still move.
 *
 * DAY-ONE NEUTRALITY: at the baseline inventory the quotes reproduce
 * exactly today's SELL_PRICE / BUY_PRICE, so introducing the faction
 * changes no prices until the player actually trades.
 *
 * SDL-free (destined for the headless sim library).
 * ========================================================= */

#include "resource.h"
#include <stdint.h>

/* Baseline inventory the faction holds of each tradeable good, and the
 * elasticity scale (kept equal, which makes the quote curve exactly
 * "2x price when empty, base price at baseline, 0 at twice baseline"). */
#define FACTION_BASE_INVENTORY  200

/* Starting (and only) gold reserve. Finite: heavy net-selling can drain
 * it, at which point the faction stops buying until trade flows gold
 * back. It does NOT mean-revert — that keeps player+faction gold exactly
 * conserved across every trade (see the conservation assert in game.c). */
#define FACTION_START_GOLD      20000

/* Inventory nudges one unit toward baseline every this many ticks —
 * "slow mean reversion", the thing that makes prices recover over time. */
#define FACTION_REVERT_INTERVAL_TICKS  4

typedef struct {
    int32_t  gold;
    int32_t  inventory[RES_COUNT];   /* GOLD slot unused                 */
    uint32_t revert_timer;           /* ticks toward the next nudge      */
} Faction;

/* Baseline: full gold reserve, every tradeable good at baseline stock. */
void faction_init(Faction *f);

/* Price the faction will PAY the player for one unit of `r` (bid), and
 * price it CHARGES the player for one unit (ask). ask > bid always (the
 * spread is BUY_PRICE/SELL_PRICE, preserved proportionally). Both are 0
 * for RES_GOLD or an out-of-range resource. */
int  faction_bid(const Faction *f, ResourceType r);
int  faction_ask(const Faction *f, ResourceType r);

/* One tick of slow mean reversion of inventory toward baseline. Called
 * once per sim tick from sim_run_one_tick. Gold is deliberately left
 * alone (see FACTION_START_GOLD). */
void faction_tick(Faction *f);

#endif /* FACTION_H */
