#ifndef CLIENT_H
#define CLIENT_H

/* ========================================================= */

#include <SDL3/SDL.h>
#include "game.h"
#include "input.h"

/* Called once per frame, after net_pump and before rendering. Moves. */
void client_update(GameState *gs, SDL_Renderer *renderer);

/* Fold one SDL event into `input` (input.c). Returns SDL_APP_SUCCESS to
 * quit (window close, Escape), otherwise SDL_APP_CONTINUE. */
SDL_AppResult input_handle_event(InputState *input, const SDL_Event *event);

#endif /* CLIENT_H */
