#ifndef BOOK_VIEW_H
#define BOOK_VIEW_H

/* book_view.h  --  Your side of the order book (UI_PLAN N3) */

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

/* ---- the draft --------------------------------------------- */
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

/* The id a row header carries. The order id is 32 bits and a widget id */
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
