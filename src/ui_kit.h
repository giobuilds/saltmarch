#ifndef UI_KIT_H
#define UI_KIT_H

/* ui_kit.h  --  Layout, widget lists and hit-testing
 * (UI_PLAN Phase 0) */

#include <stddef.h>
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
 * value, a building type, an entity id — never "row 3 on page 2". */
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
    UI_GROUP_SHIP,          /* value = ship index                    */
    UI_GROUP_CATEGORY,      /* value = BuildingCategory (HUD tabs)   */
    /* An order id is 32 bits and this field is 16, so these two carry */
    UI_GROUP_ORDER,         /* value = order id, low 16 (the row)    */
    UI_GROUP_CANCEL,        /* value = order id, low 16 (the button) */
    /* A sea route id, which is what a chart names (UI_PLAN N4). It
     * needs no second home in the widget's value the way an order id
     * does — SEA_MAX_ROUTES is 512 — so the value carries the price the
     * row was showing instead. */
    UI_GROUP_ROUTE,         /* value = route id (the row)            */
    UI_GROUP_CHART_BUY,     /* value = route id; widget value=price  */
    UI_GROUP_CHART_SELL,    /* value = route id; widget value=price  */
    /* The yard and the fleet (UI_PLAN N6). A hull row is named by its
     * ShipClass and a fleet row by its ship index; the escort button's
     * widget value is the ship it would guard NEXT, decoded at hit
     * time so the label and the click cannot disagree. */
    UI_GROUP_HULL,          /* value = ShipClass (the row)           */
    UI_GROUP_BUILD_HULL,    /* value = ShipClass (the button)        */
    UI_GROUP_ESCORT,        /* value = ship index; widget value=target */
    UI_GROUP_SURVEY,        /* value = the island to look for a way to */
    UI_GROUP_CAPACITY,      /* value = which harbour capacity (N8)   */
    /* The people screen (LIFE_PLAN Phase 9). A factor row is named by
     * its WellbeingFactor; a cast row by its index into the view, since
     * a resident id is 32 bits and this field is 16. */
    UI_GROUP_FACTOR,        /* value = WellbeingFactor               */
    UI_GROUP_RESIDENT       /* value = index into PeopleView.rows    */
};

/* Fixed actions — the non-entity buttons (Close, Prev, ...). These are
 * identities too: the enum value is stable, its position is not. */
typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_CLOSE,
    UI_ACTION_PREV,
    UI_ACTION_NEXT,
    UI_ACTION_ACCEPT,
    UI_ACTION_REJECT,
    UI_ACTION_MENU,       /* the cog                                 */
    UI_ACTION_DEMOLISH,   /* the bulldozer tool                      */
    UI_ACTION_WORLD,      /* the archipelago overview                */
    UI_ACTION_DOCKING,    /* the harbour's open/closed lever          */

    /* The order book's draft composer (UI_PLAN N3). There is no text
     * input in this game, so a price is entered with steppers: these
     * four name the FIELD a button moves, and the widget's value is the
     * step, decoded against the current draft at hit time. */
    UI_ACTION_SIDE,       /* buy <-> sell                            */
    UI_ACTION_QTY,        /* value = step                            */
    UI_ACTION_LIMIT,      /* value = step                            */
    UI_ACTION_MARKET,     /* drop the explicit limit, follow the quote*/
    UI_ACTION_POST,       /* submit the draft                        */

    /* The harbour's standing marine policy (UI_PLAN N8). The widget's
     * value is what the lever will set it to, not what it is. */
    UI_ACTION_INSURE,

    /* What the treasury takes, in per mille (LIFE_PLAN Phase 7). */
    UI_ACTION_TAX
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
 * to what the screen allows; the leftover is what pagination is for. */
UiRect ui_panel_centered(float screen_w, float screen_h,
                         float w, float wanted_h, float max_h);

/* How many `row_h` rows (separated by `gap`) fit in `avail_h`.
 * Never negative. */
int ui_rows_that_fit(float avail_h, float row_h, float gap);

/* ---- tooltips --------------------------------------------- */
UiRect ui_tooltip_rect(float cx, float above_y, float w, float h,
                       UiRect bounds);

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

/* ---- the widget list -------------------------------------- */
#define UI_MAX_WIDGETS 192
#define UI_LABEL_LEN    32

/* Widget flags — presentation states the drawer honours and the
 * hit-test respects (a disabled widget is never returned). */
#define UI_W_DISABLED  0x01u
#define UI_W_SELECTED  0x02u
#define UI_W_HEADER    0x04u   /* label only, never hit-tested       */
/* Greyed but still clickable: "you cannot afford this right now" is
 * information, not a prohibition — the build-confirm popup can still
 * offer to pay in Gold, so refusing the click would remove a real
 * option. UI_W_DISABLED is for things that genuinely cannot be done. */
#define UI_W_MUTED     0x08u

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

/* Append a widget. `label` may be NULL (empty) and is copied, never */
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

/* ---- untrusted text (UI_PLAN M4) --------------------------
 * Copy `src` into `dst` as a label safe to hand to a font renderer: */
size_t ui_clean_label(char *dst, size_t cap, const char *src);

/* ---- rejection vocabulary --------------------------------- */
const char *ui_reject_text(RejectReason reason);


/* ---- absence has a look (UI_PLAN N2) ---------------------- */
#define UI_UNKNOWN_MARK "\u2014"          /* em dash */

/* Format `value` into `out`, or the unknown mark if `known` is false.
 * `fmt` is applied only in the known case and must consume exactly one
 * int. Always NUL-terminates. */
void ui_fmt_known(char *out, size_t n, int known, const char *fmt, int value);

/* The label for a whole line that cannot be shown at all — a stores
 * list, a building count — as opposed to a single number. Kept
 * separate because "we are not told this" reads differently as a
 * sentence than as a cell. */
const char *ui_unknown_label(void);

#endif /* UI_KIT_H */
