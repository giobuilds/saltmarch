#ifndef INPUT_H
#define INPUT_H

#include <SDL3/SDL.h>

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

    /* Level-triggered: 1 while the left button is physically held,
     * 0 otherwise. Unlike left_click (an edge fired once on release,
     * cleared every frame by input_clear_clicks()), this persists
     * across frames for as long as the button is actually down — it's
     * what drives road drag-placement (see game_update()'s per-frame
     * drag check in game.c). Set by SDL_EVENT_MOUSE_BUTTON_DOWN/UP,
     * NOT reset by input_clear_clicks(). */
    int left_down;
} InputState;

void           input_init(InputState *input);
SDL_AppResult  input_handle_event(InputState *input,
                                  const SDL_Event *event);
void           input_clear_clicks(InputState *input);

#endif /* INPUT_H */
