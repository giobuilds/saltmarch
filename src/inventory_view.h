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
    INVENTORY_HIT_PAGE
} InventoryHitKind;

typedef struct {
    InventoryHitKind kind;
    int              page;
} InventoryHit;

InventoryHit inventory_hit(const UiList *list, const UiState *st,
                           float x, float y);

#endif /* INVENTORY_VIEW_H */
