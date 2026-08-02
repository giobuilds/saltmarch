#ifndef CHART_VIEW_H
#define CHART_VIEW_H

/* chart_view.h  --  The passages, and the maps of them
 * (UI_PLAN N4) */

#include <stdint.h>
#include "ui_kit.h"
#include "ui_snapshot.h"
#include "sea.h"

/* Seven destinations at MAX_ISLANDS, each with a header and three
 * routes, plus room for passages that rotate out while the panel is
 * open. A static assert in chart_view.c keeps this honest against the
 * sim's island count. */
#define CHART_MAX_ROWS    64
#define CHART_NAME_LEN    SEA_ROUTE_NAME_LEN
#define CHART_TITLE_LEN   48

typedef struct {
    int32_t  island;         /* the destination this row is about       */
    int32_t  route_id;       /* -1 on a destination header              */
    int32_t  variant;        /* SEA_ROUTE_PUBLIC, or 1..2               */
    uint8_t  header;         /* a destination heading, not a passage    */
    uint8_t  is_private;
    uint8_t  known;          /* this player has learned the passage     */
    uint8_t  gone;           /* it left play while we were looking      */
    uint8_t  surveying;      /* header rows: an expedition is out       */
    uint64_t survey_back;    /* header rows: when it is due back        */
    uint8_t  survey_reason;  /* header rows: RejectReason, or REJ_OK    */

    int32_t  charts;         /* maps of it in hand                      */
    uint32_t ticks;          /* the crossing                            */
    int32_t  saves;          /* ticks fewer than the patrolled lane     */
    int32_t  ask;            /* best resting sell of a chart, 0 if none */
    int32_t  bid;            /* best resting buy, 0 if none             */
    uint64_t expires_tick;   /* when this water goes out of use, 0 never */

    char     name[CHART_NAME_LEN];
} ChartRow;

typedef struct {
    char     title[CHART_TITLE_LEN];
    uint8_t  hue_r, hue_g, hue_b;
    int32_t  island;         /* the harbour we are looking out from     */
    uint64_t tick;

    int32_t  your_gold;
    int32_t  blank_charts;   /* RES_CHARTS in store: the Chart House's
                              * output, and what a survey spends        */

    /* What an expedition would commit, and what is left to commit
     * (UI_PLAN N7). A survey is a scholar, a research boat and a blank
     * chart; all three are shown because the one you are short of is
     * the one you need to know about. */
    int32_t  scholars_free, boats_free;
    uint8_t  yours;          /* you may post from this harbour at all   */

    ChartRow rows[CHART_MAX_ROWS];
    int32_t  row_count;
} ChartView;

/* Forget every row. Called when the panel opens: a row marked "out of
 * use" is a note about something that happened while you were looking,
 * and you were not. */
void chart_view_reset(ChartView *v);

/* Fold this frame into `v`: refresh the passages still in play, mark the
 * ones that have left, append the ones that replaced them. Resets itself
 * if the harbour changed under it. */
void chart_view_update(ChartView *v, const UiSnapshot *snap, const Sea *sea,
                       int island);

/* ---- geometry ---------------------------------------------- */
#define CHART_W           880.0f
#define CHART_MARGIN       16.0f
#define CHART_TITLE_H      40.0f
#define CHART_HEAD_H       22.0f
#define CHART_ROW_H        30.0f
#define CHART_ROW_GAP       4.0f
#define CHART_FOOTER_H     40.0f
#define CHART_BTN_W        54.0f
#define CHART_BTN_GAP       6.0f
#define CHART_MAX_H       880.0f

/* Columns, left to right, before the right-anchored action cluster. */
#define CHART_COL_NAME    240.0f
#define CHART_COL_CROSS    90.0f
#define CHART_COL_SAVES    90.0f
#define CHART_COL_HELD     70.0f
#define CHART_COL_EXPIRY  100.0f
#define CHART_COL_PRICE    90.0f

typedef enum {
    CH_COL_NAME = 0,
    CH_COL_CROSS,      /* how long the crossing takes                   */
    CH_COL_SAVES,      /* against the patrolled lane                    */
    CH_COL_HELD,       /* charts of it in hand                          */
    CH_COL_EXPIRY,     /* how long this water stays in use              */
    CH_COL_PRICE,      /* what a map of it costs on the book            */
    CH_COL_COUNT
} ChartCol;

UiRect chart_col_rect(UiRect row, ChartCol col);

/* A route id fits a widget id's sixteen bits with room to spare
 * (SEA_MAX_ROUTES is 512), so unlike an order id it needs no second
 * home in the widget's value — which the Buy and Sell buttons use for
 * the PRICE the row was showing instead. */
void chart_build(UiList *out, const ChartView *view, const UiState *st,
                 float screen_w, float screen_h);

int  chart_page_count(const ChartView *view, float screen_h);

typedef enum {
    CHART_HIT_NONE = 0,     /* inside the panel, on nothing             */
    CHART_HIT_OUTSIDE,      /* outside it entirely — dismiss            */
    CHART_HIT_CLOSE,
    CHART_HIT_PAGE,         /* `page` is the new page                   */
    CHART_HIT_BUY,          /* post a buy for one chart of `route_id`   */
    CHART_HIT_SELL,
    CHART_HIT_SURVEY        /* send an expedition to `island`           */
} ChartHitKind;

typedef struct {
    ChartHitKind kind;
    int32_t      island;    /* CHART_HIT_SURVEY: where to look          */
    int32_t      route_id;
    int32_t      limit;     /* the price the row was displaying         */
    int32_t      page;
    UiRect       rect;      /* where to flash a rejection (UI_PLAN M1)  */
} ChartHit;

ChartHit chart_hit(const UiList *list, const ChartView *view,
                   const UiState *st, float x, float y);

/* One chart per click. A map is not a commodity you buy ten of — each
 * crossing spends exactly one, and the quantity a player actually wants
 * is "one more than I have". Anything cleverer wants the composer, and
 * the composer is the book's. */
#define CHART_LOT 1

#endif /* CHART_VIEW_H */
