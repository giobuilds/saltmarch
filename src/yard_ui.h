#ifndef YARD_UI_H
#define YARD_UI_H

/* =========================================================
 * yard_ui.h  --  Drawing the yard and the fleet (UI_PLAN N6)
 *
 * Paint only, on the same division of labour as book_ui.c and
 * chart_ui.c: yard_view.c decides where everything is and which buttons
 * are pressable, and this turns the resulting UiList into pixels.
 *
 * The one drawing decision worth stating here: condition is a BAR as
 * well as a number. A hull is the only thing about a ship that moves
 * and it only ever moves down, so "38 of 60" wants to be seen at a
 * glance across a fleet, and the eye reads a shortening bar faster than
 * it reads two numbers. It colours to a warning below a third, which is
 * the point at which sending it anywhere is a decision rather than a
 * habit.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "yard_view.h"
#include "ui_kit.h"

void yard_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                  const UiList *list, const YardView *view,
                  int mouse_x, int mouse_y);

#endif /* YARD_UI_H */
