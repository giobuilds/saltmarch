#ifndef BOOK_UI_H
#define BOOK_UI_H

/* book_ui.h  --  Drawing the order book (UI_PLAN N3) */

#include <SDL3/SDL.h>
#include "book_view.h"
#include "ui_kit.h"

void book_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                  const UiList *list, const BookView *view,
                  int mouse_x, int mouse_y);

#endif /* BOOK_UI_H */
