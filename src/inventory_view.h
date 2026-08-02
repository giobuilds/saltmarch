#ifndef INVENTORY_VIEW_H
#define INVENTORY_VIEW_H

/* inventory_view.h  --  Everything this island holds
 * (UI_PLAN Phase 4) */

#include <stdint.h>
#include "ui_kit.h"
#include "ui_snapshot.h"

#define INVENTORY_MAX_ROWS 96
#define INVENTORY_NAME_LEN 16

typedef struct {
    uint16_t ident;                     /* ResourceType — identity    */
    uint8_t  category;                  /* ResourceCategory           */
    uint8_t  full;                      /* at or over capacity        */
    char     name[INVENTORY_NAME_LEN];
    int32_t  amount;
    int32_t  capacity;                  /* 0 = uncapped (Gold)        */
    int32_t  ship_cargo;                /* the same good, at sea      */
    int32_t  escrow;                    /* ...and sitting in escrow   */
} InventoryRow;

typedef struct {
    char         title[32];
    uint8_t      hue_r, hue_g, hue_b;   /* whose island this is (Phase 5) */
    InventoryRow rows[INVENTORY_MAX_ROWS];
    int32_t      row_count;
    int32_t      residents;

    /* Whether these numbers are knowledge or absence (UI_PLAN N2). */
    uint8_t      detail_known;

    /* ---- what the harbour can put to sea (UI_PLAN N8) ----- */
    int32_t      merchants_out, merchant_capacity;
    int32_t      hulls_out, hull_capacity;
    int32_t      scholars_out, scholar_capacity;
    int32_t      research_boats;
    uint8_t      insured;         /* the standing policy is on         */
    uint8_t      yours;           /* you may throw the lever at all    */

    /* ---- the treasury (LIFE_PLAN Phase 7) ----------------- */
    int32_t      tax_rate_permille;
    int32_t      tax_last_month;
    int32_t      compliance_permille;
} InventoryView;

/* Build from one island's snapshot. Ship cargo is counted per good
 * across every ship of this player: "where did my Wood go" is a
 * question the corner panel cannot answer, and a warehouse that looks
 * empty because it is all at sea is a confusing thing to stare at. */
void inventory_view_build(InventoryView *out, const UiSnapshot *snap,
                          int island);

/* ---- geometry ------------------------------------------- */
#define INVENTORY_W        620.0f
#define INVENTORY_MARGIN    16.0f
#define INVENTORY_TITLE_H   40.0f
#define INVENTORY_HEAD_H    22.0f
#define INVENTORY_ROW_H     30.0f
#define INVENTORY_ROW_GAP    4.0f
#define INVENTORY_FOOTER_H  40.0f
/* The harbour block: four capacity lines and the policy lever, between
 * the goods and the pager (UI_PLAN N8). */
#define INVENTORY_HARBOUR_H 76.0f
/* The treasury line: the rate with its two steppers, what it brought in
 * and what share is being paid. */
#define INVENTORY_TAX_H     34.0f
#define INVENTORY_MAX_H    820.0f

typedef enum {
    INV_COL_NAME = 0,
    INV_COL_AMOUNT,
    INV_COL_CAPACITY,
    INV_COL_ELSEWHERE,
    INV_COL_COUNT
} InventoryCol;

UiRect inventory_col_rect(UiRect row, InventoryCol col);

void inventory_build(UiList *out, const InventoryView *view,
                     const UiState *st, float screen_w, float screen_h);

typedef enum {
    INVENTORY_HIT_NONE = 0,
    INVENTORY_HIT_OUTSIDE,
    INVENTORY_HIT_CLOSE,
    INVENTORY_HIT_PAGE,
    INVENTORY_HIT_INSURANCE,  /* `on` is what to set it to (N8)       */
    INVENTORY_HIT_TAX         /* `on` is the rate to set, per mille   */
} InventoryHitKind;

typedef struct {
    InventoryHitKind kind;
    int              page;
    int              on;
    UiRect           rect;
} InventoryHit;

InventoryHit inventory_hit(const UiList *list, const UiState *st,
                           float x, float y);

#endif /* INVENTORY_VIEW_H */
