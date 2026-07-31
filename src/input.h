#ifndef INPUT_H
#define INPUT_H

/* InputState is embedded in GameState, which belongs to the SDL-free sim
 * library (MMO_PLAN Phase 6) — so this header holds no SDL. The one
 * SDL-shaped entry point, input_handle_event(), is declared in client.h
 * and still implemented in input.c. */

typedef struct {
    int pan_left, pan_right, pan_up, pan_down;

    int mouse_x, mouse_y;
    int logical_x, logical_y;

    /* CHANGED: mouse wheel scroll accumulated this frame.
     * Positive = scroll up (zoom in), negative = scroll down (zoom out).
     * Reset to 0 by input_clear_clicks() each frame. */
    float scroll_y;

    int left_click;
    int right_click;

    /* Edge-triggered like the clicks: 1 on the frame F9 was pressed,
     * cleared by input_clear_clicks(). Requests a determinism self-check
     * (MMO_PLAN Phase 1c); acted on in SDL_AppIterate. */
    int replay_check;

    /* Edge-triggered on F10: toggles the market debug overlay
     * (MMO_PLAN Phase 3 — the economy test harness). */
    int faction_debug_toggle;

    /* Edge-triggered on I: opens/closes the stores overlay
     * (UI_PLAN Phase 4). */
    int inventory_toggle;

    /* Edge-triggered on B: opens/closes the order book (UI_PLAN N3).
     * A key rather than a building, because posting an order is gated
     * on owning the island and nothing else — there is no counter to
     * walk up to. */
    int book_toggle;

    /* Edge-triggered on C: opens/closes the passages overlay (UI_PLAN
     * N4). Beside the book on purpose — charts are bought and sold
     * through it, and the two screens answer halves of one question. */
    int chart_toggle;

    /* Edge-triggered on Y: opens/closes the shipyard (UI_PLAN N6). */
    int yard_toggle;

    /* Edge-triggered on F8: the time-travel scrubber (MMO_PLAN later
     * phases). */
    int scrub_toggle;

    /* Level-triggered: 1 while the left button is physically held,
     * 0 otherwise. Unlike left_click (an edge fired once on release,
     * cleared every frame by input_clear_clicks()), this persists
     * across frames for as long as the button is actually down — it's
     * what drives road drag-placement (see client_update()'s per-frame
     * drag check in client.c). Set by SDL_EVENT_MOUSE_BUTTON_DOWN/UP,
     * NOT reset by input_clear_clicks(). */
    int left_down;
} InputState;

/* No input_init(): InputState is plain data embedded in GameState, and
 * game_init() zeroes it along with the rest of the struct. Calling into
 * the client from the sim library to memset a few ints is the kind of
 * dependency the Phase 6 split exists to remove. */
void           input_clear_clicks(InputState *input);

#endif /* INPUT_H */
