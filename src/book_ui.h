#ifndef BOOK_UI_H
#define BOOK_UI_H

/* =========================================================
 * book_ui.h  --  Drawing the order book (UI_PLAN N3)
 *
 * Paint only, on the same division of labour as trade_ui.c: book_view.c
 * decides where everything is, which buttons are pressable and why not,
 * and this turns the resulting UiList into pixels. If a rect looks
 * wrong the bug is in book_view.c, where there is a headless test that
 * can prove it.
 *
 * The one thing worth stating here, because it is a drawing decision
 * and not a layout one: an order that has left the book is drawn struck
 * through rather than removed. book_view.c keeps the row; this is what
 * makes "gone" legible without it moving anything.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "book_view.h"
#include "ui_kit.h"

void book_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                  const UiList *list, const BookView *view,
                  int mouse_x, int mouse_y);

#endif /* BOOK_UI_H */
