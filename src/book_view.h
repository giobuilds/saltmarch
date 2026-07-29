#ifndef BOOK_VIEW_H
#define BOOK_VIEW_H

/* =========================================================
 * book_view.h  --  Your side of the order book (UI_PLAN N3)
 *
 * The sim has had a matching order book since MARITIME_PLAN Phase 2 and
 * no way to reach it. This is the screen: post a buy or a sell with a
 * limit price, see what is resting, cancel one.
 *
 * WHY THIS IS NOT A THIRD ExchangeKind. Decision 4 unified the
 * marketplace and the harbour quay into one surface and predicted its
 * own failure mode — "if per-kind branches start appearing per-column,
 * the unification has failed". They appear here before the first line:
 * the columns are disjoint (side, resting quantity, limit, age against
 * yours, theirs, bid, ask, trend), the action cluster is one Cancel
 * against six quantities, the draft composer exists only here, and a row
 * is identified by an ORDER ID rather than a ResourceType. So the plan's
 * own mitigation applies: split rather than defend.
 *
 * ROWS ARE RETAINED, NOT REBUILT. This is the load-bearing decision and
 * the reason book_view_update() takes its own output as an input.
 *
 * The snapshot carries live orders only. Build the rows from it alone
 * and a filled order is simply absent next frame, every row below it
 * slides up, and the click that was already travelling toward Cancel
 * lands on somebody else's order. The order book is the first screen in
 * this project that ANOTHER PLAYER changes mid-read, and a row that
 * disappears between the frame that drew it and the click that hits it
 * is the one failure it must not have.
 *
 * So an order that leaves the book stays where it was, struck through,
 * with a dead Cancel. It is still a pure function — of (previous view,
 * snapshot) rather than of the snapshot alone — so it still replays
 * headlessly, which is what matters. Ordering is by ascending order id,
 * never by position in the snapshot's array: the snapshot compacts
 * around inactive slots, so array position is exactly the thing that
 * moves under a cursor.
 *
 * Stale rows are cleared when the panel closes (book_view_reset), and
 * the oldest is dropped if the view fills — a stale row is a note about
 * something that just happened, not a permanent record.
 * ========================================================= */

#include <stdint.h>
#include "ui_kit.h"
#include "ui_snapshot.h"

/* Your live orders are capped at ORDERBOOK_MAX_PER_PLAYER (24); the
 * slack is for the stale rows kept alongside them. A static assert in
 * book_view.c keeps this honest against the sim's cap. */
#define BOOK_MAX_ROWS    40
#define BOOK_NAME_LEN    20
#define BOOK_TITLE_LEN   40

typedef struct {
    uint32_t id;                 /* the order's identity, never a slot  */
    uint16_t kind;               /* TradeKind                           */
    uint16_t what;               /* ResourceType, or a route id         */
    int32_t  side;               /* OrderSide: 0 buy, 1 sell            */
    int32_t  qty;                /* units still unfilled, last seen     */
    int32_t  limit;
    uint64_t placed_tick;
    uint8_t  gone;               /* it left the book while we watched   */
    char     name[BOOK_NAME_LEN];
} BookRow;

typedef struct {
    char     title[BOOK_TITLE_LEN];
    uint8_t  hue_r, hue_g, hue_b;    /* whose island this is (Phase 5)  */
    int32_t  island;
    uint64_t tick;                   /* the snapshot these rows are of  */

    int32_t  your_gold;
    int32_t  open_count;             /* live rows: what the cap counts  */
    int32_t  cap;                    /* ORDERBOOK_MAX_PER_PLAYER        */
    uint8_t  yours;                  /* you may post here at all        */

    /* The draft being composed, resolved against the world. The draft
     * ITSELF lives in UiState (it is client view state and a fold over
     * clicks); these are the numbers it needs the sim to answer. */
    int32_t  draft_quote;            /* the market's ask (buy) or bid   */
    int32_t  draft_stock;            /* your stock of the drafted good  */
    char     draft_name[BOOK_NAME_LEN];

    BookRow  rows[BOOK_MAX_ROWS];
    int32_t  row_count;
} BookView;

/* Forget every row. Called when the panel opens: a stale row is a note
 * about what happened while you were looking, and you were not. */
void book_view_reset(BookView *v);

/* Fold this frame's snapshot into `v`. New orders of yours at `island`
 * are appended in id order, ones still present are refreshed (a partial
 * fill moves `qty`), and ones that have left are marked `gone` in place.
 * Resets itself if the island changed under it. */
void book_view_update(BookView *v, const UiSnapshot *snap, int island,
                      const UiState *st);

/* ---- the draft ---------------------------------------------
 * There is no text input in this game — InputState carries clicks, a
 * wheel and a few function keys — so a limit price is entered with
 * stepper buttons, starting from the market's own quote. Two clicks for
 * the ordinary case, and the steppers reach the limits a quote screen
 * could never express, which is the entire reason a book exists.
 *
 * The draft lives in UiState because it is a pure fold over the input
 * stream: every hit below returns the value the click produces, and the
 * caller assigns it. No overlay computes a draft of its own. */
void book_draft_default(UiState *st);

/* What the draft's limit currently means: the explicit price if one was
 * set, otherwise the market quote it is following. Zero when there is no
 * quote to follow (an unpriced good), which disables Post. */
int32_t book_draft_limit(const BookView *v, const UiState *st);

/* ---- geometry ---------------------------------------------- */
#define BOOK_W            820.0f
#define BOOK_MARGIN        16.0f
#define BOOK_TITLE_H       40.0f
#define BOOK_HEAD_H        22.0f
#define BOOK_ROW_H         30.0f
#define BOOK_ROW_GAP        4.0f
#define BOOK_COMPOSER_H    76.0f   /* the draft strip                  */
#define BOOK_FOOTER_H      40.0f
#define BOOK_BTN_W         54.0f
#define BOOK_BTN_GAP        6.0f
#define BOOK_MAX_H        860.0f

/* Columns, left to right, before the right-anchored Cancel. */
#define BOOK_COL_SIDE      60.0f
#define BOOK_COL_NAME     150.0f
#define BOOK_COL_QTY       80.0f
#define BOOK_COL_LIMIT     80.0f
#define BOOK_COL_VALUE     90.0f
#define BOOK_COL_AGE       80.0f

typedef enum {
    BK_COL_SIDE = 0,
    BK_COL_NAME,
    BK_COL_QTY,
    BK_COL_LIMIT,
    BK_COL_VALUE,     /* qty x limit: what it is holding of yours      */
    BK_COL_AGE,
    BK_COL_COUNT
} BookCol;

UiRect book_col_rect(UiRect row, BookCol col);

/* The id a row header carries. The order id is 32 bits and a widget id
 * has 16 for its value, so the identity keeps the low half and the FULL
 * id rides in the widget's value — which is what the cancel hit reads
 * and what game_cancel_order() is given. Content-derived either way, so
 * decision 2 (ids are identities, never positions) still holds. */
uint32_t book_row_id(uint32_t order_id);

void book_build(UiList *out, const BookView *view, const UiState *st,
                float screen_w, float screen_h);

int  book_page_count(const BookView *view, float screen_h);

typedef enum {
    BOOK_HIT_NONE = 0,     /* inside the panel, on nothing             */
    BOOK_HIT_OUTSIDE,      /* outside it entirely — dismiss            */
    BOOK_HIT_CLOSE,
    BOOK_HIT_PAGE,         /* `page` is the new page                   */
    BOOK_HIT_CANCEL,       /* `order_id` is the full 32-bit id         */
    BOOK_HIT_SIDE,         /* the draft fields below are the new draft */
    BOOK_HIT_GOOD,
    BOOK_HIT_QTY,
    BOOK_HIT_LIMIT,
    BOOK_HIT_POST          /* side/res/qty/limit are what to submit    */
} BookHitKind;

typedef struct {
    BookHitKind kind;
    uint32_t    order_id;
    int32_t     side;      /* the draft AFTER this click               */
    int32_t     res;
    int32_t     qty;
    int32_t     limit;     /* resolved: never "follow the market"      */
    int32_t     follow;    /* 1 if the limit is still following the
                            * quote — what to store back in UiState    */
    int32_t     page;
    UiRect      rect;      /* where to flash a rejection (UI_PLAN M1)  */
} BookHit;

BookHit book_hit(const UiList *list, const BookView *view,
                 const UiState *st, float x, float y);

#endif /* BOOK_VIEW_H */
