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
#include "sea.h"
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

/* ---- home ports and market making (MARITIME_PLAN Phase 2) ----
 * The faction is a trader with a location. It holds the last few
 * islands as home ports — settled, owned by PLAYER_FACTION, and not
 * colonisable — and posts standing orders there like any player, which
 * ship, take time, and tie up its merchants and hulls.
 *
 * That is the whole point of giving it ports rather than letting it
 * trade from nowhere: distance to the market becomes a real cost, its
 * liquidity is finite in THROUGHPUT and not just in stock, and a
 * blockade of its harbour is a thing a player could attempt.
 *
 * ONE COMPANY STOCK, SEVERAL HARBOURS. Its inventory and gold stay
 * global — it is one company with warehouses, not two rival branches —
 * and its quotes stay global with them, so every existing caller of
 * faction_bid/faction_ask is unchanged. That is safe only because
 * posting RESERVES: an order at each port draws down the same stock
 * when it is posted, exactly as a player's several orders draw down one
 * stockpile. (Per-port inventory, and with it price differences between
 * the faction's own harbours, is a separate and larger change.)
 */
#define FACTION_PORT_COUNT 2

/* It cannot quote everything: two sides times every good times every
 * port would exhaust the book by itself. It quotes the goods it is
 * furthest from baseline on — where it most wants to trade — which
 * makes the selection economic rather than a blind rotation, and means
 * the market leans against its own imbalance. */
#define FACTION_QUOTE_GOODS   6
#define FACTION_QUOTE_LOT    20   /* units per standing order          */
#define FACTION_QUOTE_INTERVAL_TICKS 100  /* re-quote every 10 seconds */

/* ---- charts (MARITIME_PLAN Phase 3b) ----------------------
 * The market draws maps as well as moving cargo, so it is where a
 * player gets their first private passage. It offers charts for a few
 * routes at a time, priced by what the passage is WORTH — the ticks it
 * saves over the public lane — rather than by a flat number, so a
 * shortcut that barely helps is cheap and one that halves a crossing
 * is not.
 *
 * That the market sells them at all is the interim answer to "where do
 * charts come from". Survey and research are the intended sources and
 * are a phase away, blocked on what a failed survey costs; without
 * some source the whole mechanic would be unreachable. Looting pirates
 * is the third, and is Phase 5. */
#define FACTION_CHART_ROUTES   4    /* routes quoted at once           */
#define FACTION_CHART_LOT      1    /* charts per standing order       */
#define FACTION_CHART_GOLD_PER_TICK_SAVED 3
#define FACTION_CHART_MIN_PRICE 40

/* Whether island `idx` is one of the faction's home ports. A pure
 * function of the index, so it needs no state and every client agrees
 * without being told. */
int  faction_is_home_port(int idx);

typedef struct {
    int32_t  gold;
    int32_t  inventory[RES_COUNT];   /* GOLD slot unused                 */
    uint32_t revert_timer;           /* ticks toward the next nudge      */

    /* Per-ROUTE insurance premium, in tenths of a percent of declared
     * cargo value. Moves as an EMA on every insured shipment's outcome,
     * so the table is a map of where cargo has been lost.
     *
     * Indexed by sea route id (MARITIME_PLAN Phase 3c). It used to be
     * [from][to] — one number for the water between two islands — which
     * stopped being enough the moment there were three ways across it.
     * A private passage is faster BECAUSE it runs outside patrolled
     * water, and a premium that could not tell the two apart priced
     * that risk at zero. Insurance is where "faster but unsafe" stops
     * being a sentence in a design document.
     *
     * Sim state: hashed and replayed. */
    int16_t  route_premium[SEA_MAX_ROUTES];

        /* Where the faction's standing orders are in the book
     * (MARITIME_PLAN Phase 2). One id per (port, good, side) it is
     * currently quoting; 0 for a slot it is not. Kept so a refresh can
     * withdraw the stale quote instead of posting a second one beside
     * it, which is how a market maker fills a book with its own
     * history in about a minute. */
    uint32_t quote_order[FACTION_PORT_COUNT][FACTION_QUOTE_GOODS][2];
    uint32_t quote_timer;

    /* The chart offers it currently has standing, one id each, and
     * which route each is for. Same reason as quote_order: a refresh
     * has to withdraw its own stale offers rather than stack new ones
     * beside them. */
    uint32_t chart_order[FACTION_CHART_ROUTES];
    uint32_t chart_cursor;   /* which routes it is offering this cycle */

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

/* The premium this route currently charges, in tenths of a percent.
 * Out-of-range ids get the starting premium rather than nothing, so a
 * caller holding a stale id is overcharged rather than insured free. */
int  faction_route_premium(const Faction *f, int route_id);

/* Fold one insured shipment's outcome into the route's premium:
 * `raided` pushes it up, a safe arrival pulls it down. An EMA rather
 * than a counter, so a passage that was dangerous last month stops
 * being priced as though it still is. */
void faction_route_experience(Faction *f, int route_id, int raided);

/* Set every route's starting premium from what kind of water it is:
 * the patrolled lane at the base rate, a private passage dearer,
 * because it is dearer. Called once when a world is built, after the
 * sea exists — faction_init runs before it and cannot know. */
void faction_init_routes(Faction *f, const Sea *sea);

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
