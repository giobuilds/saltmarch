/*  input.c  --  Input handling  (Phase 3)  */

#include "input.h"
#include <string.h>

void input_init(InputState *input)
{
    memset(input, 0, sizeof(InputState));
}

SDL_AppResult input_handle_event(InputState *input,
                                 const SDL_Event *event)
{
    int down;

    switch (event->type) {

    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        down = (event->type == SDL_EVENT_KEY_DOWN) ? 1 : 0;
        switch (event->key.scancode) {
        case SDL_SCANCODE_LEFT:  case SDL_SCANCODE_A:
            input->pan_left  = down; break;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D:
            input->pan_right = down; break;
        case SDL_SCANCODE_UP:    case SDL_SCANCODE_W:
            input->pan_up    = down; break;
        case SDL_SCANCODE_DOWN:  case SDL_SCANCODE_S:
            input->pan_down  = down; break;
        case SDL_SCANCODE_ESCAPE:
            return SDL_APP_SUCCESS;
        case SDL_SCANCODE_F9:
            /* Edge on press only, so holding the key fires one check. */
            if (down) input->replay_check = 1;
            break;
        case SDL_SCANCODE_F10:
            if (down) input->faction_debug_toggle = 1;
            break;
        default: break;
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        /* CHANGED: store raw window coords — converted to logical
         * coords in game_update() via SDL_RenderCoordinatesFromWindow() */
        input->mouse_x = (int)event->motion.x;
        input->mouse_y = (int)event->motion.y;
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        /* scroll_y: positive = up (zoom in), negative = down (zoom out) */
        input->scroll_y += event->wheel.y;
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        input->mouse_x = (int)event->button.x;
        input->mouse_y = (int)event->button.y;
        if (event->button.button == SDL_BUTTON_LEFT)
            input->left_down = 1;
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        /* CHANGED: store click position in raw window coords too */
        input->mouse_x = (int)event->button.x;  /* update pos on click */
        input->mouse_y = (int)event->button.y;
        if (event->button.button == SDL_BUTTON_LEFT) {
            input->left_click = 1;
            input->left_down  = 0;
        }
        if (event->button.button == SDL_BUTTON_RIGHT)
            input->right_click = 1;
        break;

    default:
        break;
    }

    return SDL_APP_CONTINUE;
}

void input_clear_clicks(InputState *input)
{
    input->left_click           = 0;
    input->right_click          = 0;
    input->replay_check         = 0;
    input->faction_debug_toggle = 0;
    input->scroll_y             = 0.0f;   /* CHANGED: reset scroll each frame */
}
