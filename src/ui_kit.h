#ifndef UI_KIT_H
#define UI_KIT_H

/* =========================================================
 * ui_kit.h  --  Layout, widget lists and hit-testing
 *               (UI_PLAN Phase 0)
 *
 * Pure geometry over plain structs. No SDL, no SDL_ttf, no GameState,
 * no drawing: every function here is a function of its arguments, so
 * an overlay's layout and hit-testing can be exercised headlessly, in
 * CI, on a machine with no display — which is the only regression net
 * this project can have for UI (there is no xdotool here).
 *
 * That purity is enforced structurally, not by discipline:
 * libsaltmarch_ui links no SDL, so a layout that reaches for a font
 * metric or a renderer fails to link. That link failure IS the test.
 *
 * HARD RULE, from the plan: no layout decision may consult text
 * measurement. Rows are fixed-height, columns fixed-width. Text is
 * drawn into rectangles that were sized without asking how wide it is.
 * Layout that depends on TTF metrics cannot be replayed headlessly and
 * silently reflows when a font changes.
 *
 * The division of labour an overlay follows:
 *
 *     *_build(UiList *, const UiSnapshot *, const UiState *)   here
 *     *_draw (renderer, const UiList *)                        client
 *
 * — one builder producing the widget list, one drawer consuming it,
 * and hit-testing performed against the same list that was drawn.
 * ========================================================= */

#include <stdint.h>
#include "command.h"   /* RejectReason */

/* ---- rectangles ------------------------------------------ */
typedef struct {
    float x, y, w, h;
} UiRect;

/* The canonical containment test. Every overlay uses this one rather
 * than open-coding the comparison: half-open on the far edges, so two
 * abutting rects never both claim the same pixel. */
int ui_point_in(UiRect r, float x, float y);

/* Shrink a rect on all sides (negative grows it). */
UiRect ui_inset(UiRect r, float by);

/* ---- widget identity --------------------------------------
 * Ids encode WHAT a widget is, never WHERE it sits: a resource enum
 * value, a building type, an entity id — never "row 3 on page 2".
 *
 * The reason is replay, not tidiness. Pagination plus a growing
 * RES_COUNT means a positional id silently changes meaning between
 * versions, so an old recorded click replays as a command against a
 * different resource — a desync that is invisible until it happens.
 *
 * An id is (group << 16) | value. Group 0 is reserved so that a
 * zeroed struct reads as UI_ID_NONE. */
#define UI_ID_NONE 0u

uint32_t ui_id(uint16_t group, uint16_t value);
uint16_t ui_id_group(uint32_t id);
uint16_t ui_id_value(uint32_t id);

/* Groups in use. New overlays add a value here rather than inventing
 * a literal, so that two overlays cannot collide on an id. */
enum {
    UI_GROUP_NONE = 0,
    UI_GROUP_BUILDING,      /* value = BuildingType                  */
    UI_GROUP_RESOURCE,      /* value = ResourceType                  */
    UI_GROUP_ACTION,        /* value = UiAction below                */
    UI_GROUP_SELL,          /* value = ResourceType; widget value=qty */
    UI_GROUP_BUY,           /* value = ResourceType; widget value=qty */
    UI_GROUP_ISLAND,        /* value = island index                  */
    UI_GROUP_SHIP           /* value = ship index                    */
};

/* Fixed actions — the non-entity buttons (Close, Prev, ...). These are
 * identities too: the enum value is stable, its position is not. */
typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_CLOSE,
    UI_ACTION_PREV,
    UI_ACTION_NEXT,
    UI_ACTION_ACCEPT,
    UI_ACTION_REJECT
} UiAction;

/* ---- the layout cursor ------------------------------------
 * A cursor that walks down a rect handing out rows. Deliberately
 * one-directional and arithmetic-only: no constraint solving, no
 * retained tree, nothing to invalidate. */
typedef struct {
    UiRect bounds;
    float  cursor;   /* y of the next row                            */
    float  pad;      /* vertical gap inserted between rows           */
} UiLayout;

UiLayout ui_layout(UiRect bounds, float pad);

/* Take the next row of height `h`. Rows past the bottom of the bounds
 * are still returned (correctly positioned, so a caller that measures
 * before clamping sees the true height it wanted) — use
 * ui_layout_overflowed() to ask whether that happened. */
UiRect ui_row(UiLayout *l, float h);

/* Everything from the cursor to the bottom edge. */
UiRect ui_layout_rest(const UiLayout *l);

/* 1 once the cursor has passed the bottom of the bounds. */
int ui_layout_overflowed(const UiLayout *l);

/* Column `index` of `n` equal columns across `row`, separated by
 * `gap`. Columns are fixed-width by construction — see the hard rule
 * above. Out-of-range indices return a zero rect. */
UiRect ui_split_h(UiRect row, int n, int index, float gap);

/* Column `index` of `n` where each column is `w` wide, laid out from
 * the RIGHT edge of `row` (index 0 is the rightmost). Button clusters
 * anchor right so that adding a column does not move the others. */
UiRect ui_col_from_right(UiRect row, float w, float gap, int index);

/* ---- measured, then clamped -------------------------------
 * Panels compute the height they want from their content, then clamp
 * to what the screen allows; the leftover is what pagination is for.
 * This ordering is the fix for the whole class of "it looked fine at
 * six goods" bugs. */
UiRect ui_panel_centered(float screen_w, float screen_h,
                         float w, float wanted_h, float max_h);

/* How many `row_h` rows (separated by `gap`) fit in `avail_h`.
 * Never negative. */
int ui_rows_that_fit(float avail_h, float row_h, float gap);

/* ---- pagination -------------------------------------------
 * Pagination, not scrolling: a scroll offset is continuous state that
 * a hit-test must agree with mid-gesture, while a page index is a
 * small integer that a recorded intent can carry. */
typedef struct {
    int page;    /* clamped into [0, pages-1]                        */
    int pages;   /* at least 1, even when total == 0                 */
    int first;   /* index of this page's first item                  */
    int count;   /* items on this page                               */
} UiPage;

UiPage ui_paginate(int total, int per_page, int page);

/* ---- the widget list --------------------------------------
 * A builder fills one of these; the drawer renders it and the
 * hit-test queries it. Because both consume the same list, a widget
 * that is drawn is by construction clickable exactly where it appears
 * — the two cannot drift apart the way parallel draw/hit-test code
 * does.
 *
 * ORDERING IS FROZEN WHILE AN OVERLAY IS OPEN. Rebuilding may not
 * re-sort a list (alert ordering, pagination) between the frame that
 * drew a row and the click on it, or the click lands on a row that
 * moved under the cursor. Re-sorts happen on open/close only. */
#define UI_MAX_WIDGETS 192
#define UI_LABEL_LEN    32

/* Widget flags — presentation states the drawer honours and the
 * hit-test respects (a disabled widget is never returned). */
#define UI_W_DISABLED  0x01u
#define UI_W_SELECTED  0x02u
#define UI_W_HEADER    0x04u   /* label only, never hit-tested       */

typedef struct {
    uint32_t id;
    UiRect   rect;
    char     label[UI_LABEL_LEN];
    int32_t  value;      /* payload: quantity, count, price, ...     */
    uint8_t  flags;
    uint8_t  reason;     /* RejectReason for a disabled widget       */
} UiWidget;

typedef struct {
    UiWidget items[UI_MAX_WIDGETS];
    int      count;
    int      dropped;    /* pushes refused for want of room          */
} UiList;

void ui_list_reset(UiList *l);

/* Append a widget. `label` may be NULL (empty) and is copied, never
 * borrowed — a UiList outlives whatever built it (it is serialised to
 * text for golden diffs, and may be compared across frames), so a
 * pointer into a caller's stack buffer would be a dangling read.
 * Returns 1 if it was stored, 0 if the list is full (counted in
 * `dropped`, so a silent truncation is visible in tests). */
int ui_list_push(UiList *l, uint32_t id, UiRect rect,
                 const char *label, int32_t value, uint8_t flags);

/* Set the reason a widget is unavailable. Applies to the last pushed
 * widget; also sets UI_W_DISABLED. No-op on an empty list. */
void ui_list_disable_last(UiList *l, RejectReason reason);

/* The widget containing (x, y), or NULL. Later widgets win, so a
 * builder that pushes a panel before its buttons gets the intuitive
 * result. Headers and disabled widgets are skipped. */
const UiWidget *ui_list_hit(const UiList *l, float x, float y);

/* Convenience: the hit widget's id, or UI_ID_NONE. */
uint32_t ui_list_hit_id(const UiList *l, float x, float y);

/* Find a widget by id (NULL if absent). Used by drawers that need one
 * specific widget, and by tests. */
const UiWidget *ui_list_find(const UiList *l, uint32_t id);

/* ---- rejection vocabulary ---------------------------------
 * The sim decides why something is impossible; the UI decides how to
 * say it. This is the whole table — one string per RejectReason — so
 * that "the message shown is definitionally the reason the sim
 * refused" holds by construction. Never returns NULL. */
const char *ui_reject_text(RejectReason reason);

#endif /* UI_KIT_H */
