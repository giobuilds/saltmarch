/* ui_kit.c  --  Layout, widget lists, hit-testing (UI_PLAN Phase 0) */

#include "ui_kit.h"
#include <string.h>
#include <stdio.h>

/* ---- rectangles ------------------------------------------ */

int ui_point_in(UiRect r, float x, float y)
{
    /* Half-open on the far edges: a point on the boundary between two
     * abutting rects belongs to exactly one of them, so a click never
     * hits two widgets and the answer never depends on iteration
     * order. */
    return x >= r.x && x < r.x + r.w &&
           y >= r.y && y < r.y + r.h;
}

UiRect ui_inset(UiRect r, float by)
{
    r.x += by;
    r.y += by;
    r.w -= by * 2.0f;
    r.h -= by * 2.0f;
    if (r.w < 0.0f) r.w = 0.0f;
    if (r.h < 0.0f) r.h = 0.0f;
    return r;
}

/* ---- widget identity -------------------------------------- */

uint32_t ui_id(uint16_t group, uint16_t value)
{
    if (group == UI_GROUP_NONE) return UI_ID_NONE;
    return ((uint32_t)group << 16) | (uint32_t)value;
}

uint16_t ui_id_group(uint32_t id) { return (uint16_t)(id >> 16); }
uint16_t ui_id_value(uint32_t id) { return (uint16_t)(id & 0xFFFFu); }

/* ---- the layout cursor ------------------------------------ */

UiLayout ui_layout(UiRect bounds, float pad)
{
    UiLayout l;
    l.bounds = bounds;
    l.cursor = bounds.y;
    l.pad    = pad;
    return l;
}

UiRect ui_row(UiLayout *l, float h)
{
    UiRect r;
    r.x = l->bounds.x;
    r.y = l->cursor;
    r.w = l->bounds.w;
    r.h = h;
    /* The cursor advances past the bottom rather than clamping: a
     * caller measuring its content's true height (before deciding how
     * much of it fits) needs the honest number, and
     * ui_layout_overflowed() is how it asks. */
    l->cursor += h + l->pad;
    return r;
}

UiRect ui_layout_rest(const UiLayout *l)
{
    UiRect r;
    r.x = l->bounds.x;
    r.y = l->cursor;
    r.w = l->bounds.w;
    r.h = (l->bounds.y + l->bounds.h) - l->cursor;
    if (r.h < 0.0f) r.h = 0.0f;
    return r;
}

int ui_layout_overflowed(const UiLayout *l)
{
    return l->cursor > l->bounds.y + l->bounds.h;
}

UiRect ui_split_h(UiRect row, int n, int index, float gap)
{
    UiRect r;
    float  each;

    if (n <= 0 || index < 0 || index >= n) {
        r.x = r.y = r.w = r.h = 0.0f;
        return r;
    }

    each = (row.w - gap * (float)(n - 1)) / (float)n;
    if (each < 0.0f) each = 0.0f;

    r.x = row.x + ((float)index * (each + gap));
    r.y = row.y;
    r.w = each;
    r.h = row.h;
    return r;
}

UiRect ui_col_from_right(UiRect row, float w, float gap, int index)
{
    UiRect r;

    if (index < 0) {
        r.x = r.y = r.w = r.h = 0.0f;
        return r;
    }

    /* Anchored right so that adding a column to a cluster does not
     * move the columns already there — muscle memory is a feature and
     * recorded intent replays depend on click targets not drifting. */
    r.x = row.x + row.w - (w + ((float)index * (w + gap)));
    r.y = row.y;
    r.w = w;
    r.h = row.h;
    return r;
}

/* ---- measured, then clamped ------------------------------- */

UiRect ui_panel_centered(float screen_w, float screen_h,
                         float w, float wanted_h, float max_h)
{
    UiRect r;
    float  h = wanted_h < max_h ? wanted_h : max_h;

    if (h < 0.0f) h = 0.0f;
    if (w < 0.0f) w = 0.0f;

    r.w = w;
    r.h = h;
    r.x = (screen_w - w) * 0.5f;
    r.y = (screen_h - h) * 0.5f;
    return r;
}

int ui_rows_that_fit(float avail_h, float row_h, float gap)
{
    int n;

    if (row_h <= 0.0f || avail_h <= 0.0f) return 0;

    /* n rows occupy n*row_h + (n-1)*gap. Solve for the largest n whose
     * total fits, by construction rather than by a loop with a
     * floating-point comparison at the boundary. */
    n = (int)((avail_h + gap) / (row_h + gap));
    return n < 0 ? 0 : n;
}

/* ---- tooltips --------------------------------------------- */

UiRect ui_tooltip_rect(float cx, float above_y, float w, float h,
                       UiRect bounds)
{
    const float MARGIN = 4.0f;
    UiRect      r;

    r.w = w;
    r.h = h;
    r.x = cx - w * 0.5f;
    r.y = above_y - h - MARGIN;

    /* Horizontal: clamp, do not centre-and-hope. The leftmost HUD slot
     * is 20px from the edge and its label is wider than that. */
    if (r.x + r.w > bounds.x + bounds.w - MARGIN)
        r.x = bounds.x + bounds.w - MARGIN - r.w;
    if (r.x < bounds.x + MARGIN)
        r.x = bounds.x + MARGIN;

    /* Vertical: prefer above, flip below when there is no room — a tip
     * clipped by the top of the window is no more readable than one
     * clipped by the side. */
    if (r.y < bounds.y + MARGIN)
        r.y = above_y + MARGIN;

    return r;
}

/* ---- pagination ------------------------------------------- */

UiPage ui_paginate(int total, int per_page, int page)
{
    UiPage p;

    if (per_page < 1) per_page = 1;
    if (total < 0)    total    = 0;

    p.pages = (total + per_page - 1) / per_page;
    if (p.pages < 1) p.pages = 1;   /* an empty list still has page 1 */

    /* Clamping rather than wrapping: a page index arriving from a
     * stale UiState (a resource list that shrank) must land somewhere
     * sane, and wrapping would teleport the player to the far end. */
    if (page < 0)         page = 0;
    if (page >= p.pages)  page = p.pages - 1;

    p.page  = page;
    p.first = page * per_page;
    p.count = total - p.first;
    if (p.count < 0)         p.count = 0;
    if (p.count > per_page)  p.count = per_page;
    return p;
}

/* ---- the widget list -------------------------------------- */

void ui_list_reset(UiList *l)
{
    l->count   = 0;
    l->dropped = 0;
}

int ui_list_push(UiList *l, uint32_t id, UiRect rect,
                 const char *label, int32_t value, uint8_t flags)
{
    UiWidget *w;

    if (l->count >= UI_MAX_WIDGETS) {
        l->dropped++;
        return 0;
    }

    w = &l->items[l->count++];
    w->id     = id;
    w->rect   = rect;
    w->value  = value;
    w->flags  = flags;
    w->reason = (uint8_t)REJ_OK;

    /* Copied, not borrowed: lists outlive their builders (golden diffs
     * serialise them; tests compare them across frames), so a pointer
     * into a caller's scratch buffer would dangle. */
    if (label) {
        size_t n = strlen(label);
        if (n >= UI_LABEL_LEN) n = UI_LABEL_LEN - 1;
        memcpy(w->label, label, n);
        w->label[n] = '\0';
    } else {
        w->label[0] = '\0';
    }
    return 1;
}

void ui_list_disable_last(UiList *l, RejectReason reason)
{
    UiWidget *w;

    if (l->count <= 0) return;

    w = &l->items[l->count - 1];
    w->flags |= UI_W_DISABLED;
    w->reason = (uint8_t)reason;
}

const UiWidget *ui_list_hit(const UiList *l, float x, float y)
{
    int i;

    /* Back to front: later pushes draw on top, so they claim the click
     * on top too. */
    for (i = l->count - 1; i >= 0; i--) {
        const UiWidget *w = &l->items[i];
        if (w->flags & (UI_W_HEADER | UI_W_DISABLED)) continue;
        if (w->id == UI_ID_NONE) continue;
        if (ui_point_in(w->rect, x, y)) return w;
    }
    return NULL;
}

uint32_t ui_list_hit_id(const UiList *l, float x, float y)
{
    const UiWidget *w = ui_list_hit(l, x, y);
    return w ? w->id : UI_ID_NONE;
}

const UiWidget *ui_list_find(const UiList *l, uint32_t id)
{
    int i;
    for (i = 0; i < l->count; i++)
        if (l->items[i].id == id) return &l->items[i];
    return NULL;
}

/* ---- untrusted text (UI_PLAN M4) -------------------------- */

size_t ui_clean_label(char *dst, size_t cap, const char *src)
{
    size_t n = 0;

    if (!dst || cap == 0) return 0;
    if (!src) { dst[0] = '\0'; return 0; }

    while (src[n] && n + 1 < cap) {
        unsigned char c = (unsigned char)src[n];
        /* Printable ASCII only. Anything else — control codes, a
         * newline, the high half of a UTF-8 sequence — becomes '?'. */
        dst[n] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        n++;
    }
    dst[n] = '\0';
    return n;
}

/* ---- rejection vocabulary --------------------------------- */

const char *ui_reject_text(RejectReason reason)
{
    /* Designated initialisers, deliberately: this table is indexed by
     * an enum that will grow, and the RES_COL lesson in this codebase
     * (a positional table silently misaligning when the enum gained a
     * member) is the reason nothing here is written positionally. */
    static const char *const TEXT[REJ_COUNT] = {
        [REJ_OK]                   = "",
        [REJ_OUT_OF_BOUNDS]        = "Outside the island",
        [REJ_NOT_BUILDABLE]        = "Can't build on this tile",
        [REJ_NEEDS_FERTILE]        = "Soil isn't fertile",
        [REJ_NEEDS_CROP]           = "Wrong soil for this crop",
        [REJ_NEEDS_DEPOSIT]        = "Nothing to work here",
        [REJ_NEEDS_GOODS]          = "Can't supply them yet",
        [REJ_NEEDS_BUILDING]       = "Needs a building you don't have",
        [REJ_NEEDS_COAST]          = "Needs water alongside",
        [REJ_NEEDS_FOREST]         = "Needs forest alongside",
        [REJ_OCCUPIED]             = "Something is already here",
        [REJ_CANT_AFFORD]          = "Can't afford it",
        [REJ_NO_STOCK]             = "Nothing in stock",
        [REJ_NO_STORAGE]           = "No room to store it",
        [REJ_COUNTERPARTY_NO_GOLD] = "They're out of Gold",
        [REJ_PRICE_MOVED]          = "Price moved",
        [REJ_NOT_OWNER]            = "Not your island",
        [REJ_ESCROW_REFUSED]       = "Harbour closed to you",
        [REJ_OFFER_CHANGED]        = "The quay changed — look again",
        [REJ_NO_TARGET]            = "That voyage is gone",
        [REJ_UNAVAILABLE]          = "Not possible right now",
        [REJ_ORDER_GONE]           = "That order is gone",
        [REJ_NO_CREW]              = "No scholar free to sail",
        [REJ_NO_BOAT]              = "No research boat free",
        [REJ_NOTHING_TO_FIND]      = "You have charted this crossing"
    };

    if (reason < 0 || reason >= REJ_COUNT || !TEXT[reason])
        return "Not possible right now";
    return TEXT[reason];
}

/* ---- absence has a look (UI_PLAN N2) ---------------------- */

void ui_fmt_known(char *out, size_t n, int known, const char *fmt, int value)
{
    if (!out || n == 0) return;

    if (!known) {
        /* The mark, not a formatted zero. This function exists so that
         * the decision is made once: a caller that had to remember to
         * check `known` before every snprintf would eventually forget,
         * and forgetting fails silently as a plausible number. */
        snprintf(out, n, "%s", UI_UNKNOWN_MARK);
        return;
    }
    snprintf(out, n, fmt, value);
}

const char *ui_unknown_label(void)
{
    return "Not known";
}
