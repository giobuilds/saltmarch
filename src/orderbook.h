#ifndef ORDERBOOK_H
#define ORDERBOOK_H

/* orderbook.h  --  Players trade with each other
 * (MARITIME_PLAN Phase 2: the order book) */

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
 * crossing. */
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
    int32_t  route_id;       /* which of the pair's three it sails     */
    int32_t  raided;         /* pirates took it; nothing will arrive   */
    int32_t  insured_value;  /* 0 if uninsured; what the policy pays   */
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

/* How many live orders and bookings the book holds in total. */
int  orderbook_open_live(const OrderBook *b);
int  orderbook_booking_live(const OrderBook *b);

/* Find a live order by id, or NULL. */
Order *orderbook_find(OrderBook *b, uint32_t id);

#endif /* ORDERBOOK_H */
