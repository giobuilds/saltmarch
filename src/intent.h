#ifndef INTENT_H
#define INTENT_H

/* =========================================================
 * intent.h  --  What the player did, and what they were
 *               looking at when they did it (UI_PLAN M1)
 *
 * A Command records a decision. An Intent records the CLICK that
 * produced it: where the cursor was, which overlay was open, and —
 * the load-bearing field — the exact sim tick whose snapshot the frame
 * was showing.
 *
 * That last one is the whole format decision. Intents happen at frame
 * times; commands apply at tick boundaries. If a recorded click does
 * not carry the tick its frame was drawn from, a replay rebuilds a
 * DIFFERENT snapshot than the player saw — different prices, a
 * different page, possibly a different number of rows — and hit-tests
 * a click against a screen that never existed. The plan calls this out
 * as a thing to get right once, because a log recorded wrongly is not
 * repairable later.
 *
 * What this buys: CI can re-simulate to each intent's tick, take the
 * snapshot, drive the REAL builders and hit-tests, and assert that the
 * command that falls out is the one in the log. A click-through UI
 * regression suite on three platforms, in an environment with no
 * display and no input automation.
 *
 * Intents are cosmetic to the sim. They are never applied, never
 * hashed, and a log with none replays exactly as before.
 * ========================================================= */

#include <stdint.h>

typedef enum {
    INTENT_NONE = 0,
    INTENT_LEFT_CLICK,
    INTENT_RIGHT_CLICK,
    INTENT_KEY            /* `key` holds an SDL scancode-ish id */
} IntentKind;

/* The client view state a frame was rendered with. Recorded rather than
 * re-derived: the harness folds its own copy from the intent stream and
 * asserts the two agree, which is how "UiState is a pure fold over the
 * input stream" stops being an aspiration. */
typedef struct {
    uint8_t  overlay;        /* GameOverlay at the moment of the click */
    uint8_t  hud_category;
    uint16_t exchange_page;
    uint16_t inventory_page;

    /* The order book's page and the draft order on it (UI_PLAN N3).
     * The draft is recorded for the same reason the pages are: it is
     * part of the screen the click landed on, and a replay that
     * rebuilt it from scratch would hit-test a composer showing
     * different numbers than the one the player was looking at. */
    uint16_t book_page;
    uint8_t  book_side;
    uint8_t  book_res;
    int32_t  book_qty;
    int32_t  book_limit;

    /* The passages overlay's page (UI_PLAN N4). A page index, like the
     * three above it: which page a click landed on decides which rows
     * were under the cursor, so a replay that guessed would hit-test a
     * screen the player never saw. */
    uint16_t chart_page;

    /* And the shipyard's (UI_PLAN N6), for the same reason. */
    uint16_t yard_page;

    /* The tile under the cursor, as the frame computed it. Recorded
     * rather than re-derived because it comes from the camera, and the
     * camera is client state that never enters the log — a replay has
     * no idea where the view was scrolled to. */
    int16_t  hovered_row, hovered_col;
    int16_t  current_island;
} IntentUiState;

typedef struct {
    uint64_t      tick;      /* sim_tick_no the frame's snapshot showed */
    int32_t       x, y;      /* logical (1920x1080) cursor position     */
    uint8_t       kind;      /* IntentKind                              */
    uint8_t       key;       /* INTENT_KEY only                         */
    IntentUiState ui;
    uint32_t      seq;       /* the command this click produced, 0 if
                              * it produced none (a tab, a page turn,
                              * a click on empty water)                 */
} Intent;

#endif /* INTENT_H */
