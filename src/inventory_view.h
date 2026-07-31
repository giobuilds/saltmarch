#ifndef INVENTORY_VIEW_H
#define INVENTORY_VIEW_H

/* =========================================================
 * inventory_view.h  --  Everything this island holds
 *                       (UI_PLAN Phase 4)
 *
 * The corner panel shows every resource, which works at seven and is
 * the third capacity cliff v1 measured: it runs off the top of the
 * screen somewhere around forty-three. This overlay is where the full
 * list lives — paged, grouped by category, with the storage pressure
 * per good made visible rather than inferred from a number.
 *
 * The corner panel stays: it is the glance. This is the look.
 *
 * Same shape as the other Phase 0+ surfaces: a value view, a pure
 * builder, a drawer in the client.
 * ========================================================= */

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

    /* Whether these numbers are knowledge or absence (UI_PLAN N2). The
     * stores of an island you do not hold arrive as zeroes meaning "you
     * were not told", and a warehouse list full of 0 is a much more
     * confident lie than a single field: it reads as a surveyed, empty
     * colony. The rows are still built — the drawer marks them — so
     * that layout, scrolling and hit-testing do not acquire a second
     * shape that only foreign islands ever take. */
    uint8_t      detail_known;

    /* ---- what the harbour can put to sea (UI_PLAN N8) -----
     * Goods are only half of what an island holds. Merchants, hulls,
     * scholars and research boats are the other half — capital rather
     * than stock — and until now the only way to discover that every
     * merchant was committed was to watch an order sit unfilled.
     *
     * Committed AND capacity, always as a pair: one number alone
     * ("2 merchants") cannot say whether that is comfortable or the
     * whole of what you have, and which of those it is decides whether
     * to post another order.
     *
     * The standing insurance policy sits with them because it is the
     * same kind of fact — a property of the harbour rather than of any
     * one voyage — and because the premium it pays is per ROUTE, so
     * turning it on is a decision about everything this port sends. */
    int32_t      merchants_out, merchant_capacity;
    int32_t      hulls_out, hull_capacity;
    int32_t      scholars_out, scholar_capacity;
    int32_t      research_boats;
    uint8_t      insured;         /* the standing policy is on         */
    uint8_t      yours;           /* you may throw the lever at all    */
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
    INVENTORY_HIT_INSURANCE   /* `on` is what to set it to (N8)       */
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
