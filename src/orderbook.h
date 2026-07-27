#ifndef ORDERBOOK_H
#define ORDERBOOK_H

/* =========================================================
 * orderbook.h  --  Players trade with each other
 *                  (MARITIME_PLAN Phase 2: the order book)
 *
 * Trading was a player and the NPC faction, at a price the faction
 * quoted. This is players posting buy and sell orders that the sim
 * matches against each other, and a match becoming a shipment that
 * takes as long as the water between the two harbours.
 *
 * WHAT IS BEING TRADED IS A KIND AND AN ID, not a ResourceType.
 *
 * Every tradeable thing used to be an index into one flat enum. A
 * route chart is not: a chart for the passage by the Coffin Race is a
 * different object from one for the Silt Narrows, so they cannot share
 * an enum slot, and giving each generated route its own would grow
 * RES_COUNT — which sizes every stockpile, price table, faction
 * inventory and snapshot field in the game — with the size of the
 * world.
 *
 * So a TradeId is (kind, id): (TRADE_RESOURCE, RES_PLANKS) or
 * (TRADE_ROUTE_CHART, 17). The book compares the pair. This costs one
 * packed payload slot now and saves a retrofit later, which is the
 * whole argument for deciding it before the matcher exists rather than
 * after.
 *
 * MATCHING IS SIM STATE AND MUST BE REPRODUCIBLE. Orders, the book and
 * open bookings are hashed and replayed like everything else, so
 * matching runs at tick boundaries and its order is fully determined:
 * best price first, then earliest placed, then lowest order id. Ids are
 * assigned in command-log order, so a replay fills exactly the trades
 * the original run filled.
 *
 * GOODS AND GOLD ARE RESERVED WHEN AN ORDER IS POSTED, not when it
 * fills. A sell takes the goods out of the stockpile and a buy takes
 * the gold, both held against the order and returned on cancellation.
 * Without that a player could post the same cargo into ten books and
 * fill all ten.
 * ========================================================= */

#include <stdint.h>
#include "resource.h"

/* What a TradeId can name. Appending here is the supported way to make
 * something new tradeable; the matcher compares (kind, id) and needs no
 * knowledge of what either means. */
typedef enum {
    TRADE_RESOURCE    = 0,   /* id is a ResourceType                   */
    TRADE_ROUTE_CHART = 1,   /* id is a route index (Phase 3)          */
    TRADE_KIND_COUNT
} TradeKind;

typedef struct {
    uint16_t kind;
    uint16_t id;
} TradeId;

/* A TradeId packs into one int32 payload slot, which is what lets an
 * order fit the four Command carries: island, packed identity,
 * quantity (sign is the side) and limit price. */
#define TRADE_PACK(kind, id)  ((int32_t)(((uint32_t)(kind) << 16) | \
                                         ((uint32_t)(id) & 0xFFFFu)))
#define TRADE_KIND_OF(packed)  ((uint16_t)(((uint32_t)(packed) >> 16) & 0xFFFFu))
#define TRADE_ID_OF(packed)    ((uint16_t)((uint32_t)(packed) & 0xFFFFu))

#define ORDERBOOK_MAX_ORDERS       256
#define ORDERBOOK_MAX_BOOKINGS      64

/* One player may not fill the book by themselves. The transport already
 * rate-limits commands per peer per tick; this bounds the standing
 * cost of a player who posts steadily and never cancels. */
#define ORDERBOOK_MAX_PER_PLAYER    24

typedef enum { ORDER_BUY = 0, ORDER_SELL = 1 } OrderSide;

typedef struct {
    int32_t  active;
    uint32_t id;             /* monotonic; assigned in command order   */
    uint32_t owner;          /* player id                              */
    int32_t  island;         /* the harbour it was posted at           */
    TradeId  what;
    int32_t  side;           /* OrderSide                              */
    int32_t  qty;            /* units still unfilled                   */
    int32_t  limit;          /* price per unit                         */
    int32_t  reserved_gold;  /* held for a buy; returned on cancel     */
    uint64_t placed_tick;    /* time priority                          */
} Order;

/* A matched trade, in transit. The goods are already out of the
 * seller's hands and the gold already out of the buyer's; this is the
 * crossing.
 *
 * A booking outlives its own delivery. It holds a merchant and a hull
 * from `from_island` for the ROUND TRIP: the cargo lands at
 * `arrive_tick`, and the two of them are only free again at
 * `return_tick`, having sailed home. Releasing them on delivery would
 * make a one-way trip the unit of trade capacity and quietly double
 * what an island can run — and it would contradict the thing that makes
 * merchants capital rather than fuel, which is that they come back. */
typedef struct {
    int32_t  active;
    TradeId  what;
    int32_t  qty;
    int32_t  price;          /* per unit, at which it filled           */
    int32_t  from_island;
    int32_t  to_island;
    uint32_t buyer;
    uint32_t seller;
    uint64_t arrive_tick;
    uint64_t return_tick;    /* when the merchant and hull are free    */
    int32_t  delivered;      /* the cargo has landed; still sailing home */
} Booking;

typedef struct {
    Order    order[ORDERBOOK_MAX_ORDERS];
    int32_t  order_count;    /* high-water slot count, like buildings  */
    uint32_t next_order_id;
    Booking  booking[ORDERBOOK_MAX_BOOKINGS];
    int32_t  booking_count;
} OrderBook;

void orderbook_init(OrderBook *b);

/* How many live orders `player` holds. */
int  orderbook_open_count(const OrderBook *b, uint32_t player);

/* How many live orders and bookings the book holds in total.
 *
 * These are not conveniences: they are what sim_hash and the snapshot
 * both count, and they must agree. The book is compacted when it is
 * checkpointed — an order is addressed by id, never by slot — so the
 * hash may not depend on where in the array a live entry sits, only on
 * how many there are and what they say. */
int  orderbook_open_live(const OrderBook *b);
int  orderbook_booking_live(const OrderBook *b);

/* Find a live order by id, or NULL. */
Order *orderbook_find(OrderBook *b, uint32_t id);

#endif /* ORDERBOOK_H */
