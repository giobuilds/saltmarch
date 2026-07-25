#ifndef EXCHANGE_VIEW_H
#define EXCHANGE_VIEW_H

/* =========================================================
 * exchange_view.h  --  One exchange surface, parameterised by
 *                      counterparty (UI_PLAN Phase 1, decision 4)
 *
 * MMO_PLAN's thesis is that a bot is indistinguishable from a slow
 * player. Carried into the UI, that means the marketplace (trading with
 * the NPC faction) and the harbour escrow panel (trading with another
 * player) are not two screens: they are one screen handed a different
 * value struct.
 *
 * ExchangeView is that struct — a pure snapshot of one side of a trade,
 * copied, never pointers into live sim state, so the whole layout and
 * hit-test path stays a function of its arguments and runs headlessly.
 *
 * WHAT THIS REPLACES. The old trade overlay laid out a 92px block per
 * good and derived its panel height from the number of goods. At six
 * goods it was 722px tall; at ten it would have been 1130px on a 1080px
 * screen — the capacity cliff UI_PLAN was written around, reached by
 * adding four goods. Here the panel measures what it wants, clamps to
 * what the screen has, and pages the remainder.
 *
 * ROWS ARE A LIST, NOT A PARALLEL ARRAY PER RESOURCE. The plan sketched
 * `int32_t bid[RES_COUNT], ask[RES_COUNT], ...`; rows carry their own
 * identity instead. Two reasons: an escrow offer's rows are cargo lines
 * rather than one-per-resource, and a test can build a 40-row view
 * without RES_COUNT having to grow to 40 first — which is exactly the
 * cliff this phase exists to prove is gone.
 * ========================================================= */

#include <stdint.h>
#include "ui_kit.h"
#include "ui_snapshot.h"

/* Enough for any plausible goods table; a view is a value type, so this
 * is a stack-sized bound rather than a design limit. */
#define EXCHANGE_MAX_ROWS  64
#define EXCHANGE_NAME_LEN  16
#define EXCHANGE_TITLE_LEN 32

/* A counterparty with no meaningful limit (the pre-faction market, and
 * anything that should never render a "they are out of it" state). */
#define EXCHANGE_INFINITE  INT32_MAX

typedef enum {
    EXCHANGE_QUOTES = 0,   /* they quote bid/ask; you buy and sell     */
    EXCHANGE_OFFER         /* they offer cargo; you accept or reject   */
} ExchangeKind;

typedef struct {
    uint16_t ident;                    /* ResourceType — the identity  */
    uint8_t  category;                 /* ResourceCategory, for grouping*/
    char     name[EXCHANGE_NAME_LEN];
    int32_t  yours;                    /* your stock                   */
    int32_t  theirs;                   /* theirs (EXCHANGE_INFINITE ok)*/
    int32_t  bid;                      /* they buy at                  */
    int32_t  ask;                      /* they sell at                 */
    uint8_t  refuse;                   /* RejectReason from THEIR side */
} ExchangeRow;

typedef struct {
    char         title[EXCHANGE_TITLE_LEN];
    uint8_t      hue_r, hue_g, hue_b;  /* whose island this is (Phase 5) */
    ExchangeKind kind;
    int32_t      your_gold;
    int32_t      their_gold;           /* EXCHANGE_INFINITE ok         */
    int32_t      capacity;             /* your per-resource storage cap*/
    ExchangeRow  rows[EXCHANGE_MAX_ROWS];
    int32_t      row_count;
} ExchangeView;

/* Build the marketplace view: your island's stock against the NPC
 * faction's live quotes, both already resolved in the snapshot. Gold is
 * not a row — you cannot trade currency for itself. */
void exchange_view_market(ExchangeView *out, const UiSnapshot *snap,
                          int island);

/* ---- geometry ---------------------------------------------
 * Public so the drawer can size its columns identically without
 * recomputing them, and so tests can assert against them. */
#define EXCHANGE_W          860.0f
#define EXCHANGE_MARGIN      16.0f
#define EXCHANGE_TITLE_H     40.0f
#define EXCHANGE_HEAD_H      22.0f    /* column headings              */
#define EXCHANGE_ROW_H       34.0f    /* one good                     */
#define EXCHANGE_ROW_GAP      4.0f
#define EXCHANGE_FOOTER_H    40.0f    /* pager + close                */
#define EXCHANGE_BTN_W       54.0f
#define EXCHANGE_BTN_GAP      6.0f
#define EXCHANGE_MAX_H      900.0f    /* never taller than this        */

/* Column widths, left to right, before the right-anchored buttons. */
#define EXCHANGE_COL_NAME   150.0f
#define EXCHANGE_COL_YOURS   90.0f
#define EXCHANGE_COL_THEIRS  90.0f
#define EXCHANGE_COL_BID     70.0f
#define EXCHANGE_COL_ASK     70.0f

/* The action cluster, right to left. Sell quantities are negative, buy
 * quantities positive; the "all"/"max" ends are the sentinel -1/-1
 * resolved against live state by the sim, exactly as before. */
#define EXCHANGE_ACTIONS 6

/* Build the widget list for `view` at page `st->exchange_page`.
 * Everything the drawer needs is in the list; everything the hit-test
 * needs is in the same list, which is what stops the two drifting.
 *
 * Widget ids are identities: UI_GROUP_SELL/UI_GROUP_BUY carry the
 * ResourceType, and the QUANTITY rides in the widget's value — so the
 * buttons of one row share an id and differ by payload, and no id
 * anywhere encodes "row 3 of page 2". */
void exchange_build(UiList *out, const ExchangeView *view,
                    const UiState *st, float screen_w, float screen_h);

/* Row rects are pushed as headers (never clickable) so the drawer can
 * find them; this is the id it should look for. */
uint32_t exchange_row_id(uint16_t ident);

/* The columns of a row, left to right. The drawer asks for these rather
 * than re-deriving them from the widths above, so headings and cells
 * cannot drift a pixel apart from each other. */
typedef enum {
    EX_COL_NAME = 0,
    EX_COL_YOURS,
    EX_COL_THEIRS,
    EX_COL_BID,
    EX_COL_ASK,
    EX_COL_COUNT
} ExchangeCol;

UiRect exchange_col_rect(UiRect row, ExchangeCol col);

typedef enum {
    EXCHANGE_HIT_NONE = 0,   /* inside the panel, on nothing          */
    EXCHANGE_HIT_OUTSIDE,    /* outside the panel entirely — dismiss  */
    EXCHANGE_HIT_CLOSE,
    EXCHANGE_HIT_PAGE,       /* `page` is the new page index          */
    EXCHANGE_HIT_SELL,       /* `res` and `qty` are set               */
    EXCHANGE_HIT_BUY
} ExchangeHitKind;

typedef struct {
    ExchangeHitKind kind;
    int             res;
    int             qty;     /* < 0 means "all"/"max", as before      */
    int             page;
} ExchangeHit;

/* Decode a click against the list that was drawn. */
ExchangeHit exchange_hit(const UiList *list, const UiState *st,
                         float x, float y);

/* How many pages the view needs at this screen size — the drawer shows
 * it, the test asserts it. */
int exchange_page_count(const ExchangeView *view, float screen_h);

#endif /* EXCHANGE_VIEW_H */
