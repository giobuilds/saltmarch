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

/* island.h includes this header, so the island count cannot come from
 * there without a cycle. Kept in step by a compile-time assert in
 * faction.c. */
#define MAX_ISLANDS_FOR_LANES 8

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

/* ---- price history (UI_PLAN M3) ---------------------------
 * A short ring of past mid-prices per good, sampled on a fixed tick
 * interval. It exists to answer a question the numbers alone cannot:
 * "is this price normal?" A bid of 2 means nothing; a bid of 2 after a
 * week at 3 means you just flooded the market.
 *
 * MMO_PLAN's risk register lists "elastic market reads as a rigged slot
 * machine at tiny scale" — the mitigation is making the elasticity
 * VISIBLE. Sell-Max leaves a scar on the line; mean reversion visibly
 * heals it. The F10 tuning overlay and the player's trade screen both
 * draw this same buffer, so the debug view and the game can never
 * disagree about what the price did.
 *
 * It is sim state: hashed, replayed, integer-only. That costs 6 bytes
 * per good per sample and buys a history that a replayed session
 * reproduces exactly. */
#define FACTION_HIST_LEN            24
#define FACTION_HIST_INTERVAL_TICKS 50   /* one sample per 5 seconds */

typedef struct {
    int32_t  gold;
    int32_t  inventory[RES_COUNT];   /* GOLD slot unused                 */
    uint32_t revert_timer;           /* ticks toward the next nudge      */

    /* Per-lane insurance premium, in tenths of a percent of declared
     * cargo value (MMO_PLAN later phases). Indexed [from][to]; the
     * diagonal is unused. Moves as an EMA on every insured voyage's
     * outcome, so the table is a map of where ships have been lost.
     * Sim state: hashed and replayed. */
    int16_t  lane_premium[MAX_ISLANDS_FOR_LANES][MAX_ISLANDS_FOR_LANES];

    /* Ring of mid-prices ((bid+ask)/2), oldest-to-newest by index once
     * hist_count reaches FACTION_HIST_LEN. hist_head is where the next
     * sample goes. */
    int16_t  hist[RES_COUNT][FACTION_HIST_LEN];
    uint16_t hist_head;
    uint16_t hist_count;
    uint32_t hist_timer;
} Faction;

/* Baseline: full gold reserve, every tradeable good at baseline stock,
 * every lane at the starting premium. */
void faction_init(Faction *f);

/* The premium this lane currently charges, in tenths of a percent. */
int  faction_lane_premium(const Faction *f, int from, int to);

/* Fold one insured voyage's outcome into the lane's premium: `raided`
 * pushes it up, a safe arrival pulls it down. An EMA rather than a
 * counter, so a lane that was dangerous last month stops being priced
 * as though it still is. */
void faction_lane_experience(Faction *f, int from, int to, int raided);

/* Price the faction will PAY the player for one unit of `r` (bid), and
 * price it CHARGES the player for one unit (ask). ask > bid always (the
 * spread is BUY_PRICE/SELL_PRICE, preserved proportionally). Both are 0
 * for RES_GOLD or an out-of-range resource. */
int  faction_bid(const Faction *f, ResourceType r);
int  faction_ask(const Faction *f, ResourceType r);

/* One tick of slow mean reversion of inventory toward baseline, plus
 * the periodic price-history sample. Called once per sim tick from
 * sim_run_one_tick. Gold is deliberately left alone (see
 * FACTION_START_GOLD). */
void faction_tick(Faction *f);

/* Read the history for `r` oldest-first into `out` (at most
 * FACTION_HIST_LEN entries). Returns how many were written — 0 before
 * the first sample. The ring's internal order is not something callers
 * should have to think about. */
int  faction_history(const Faction *f, ResourceType r, int16_t *out, int max);

#endif /* FACTION_H */
