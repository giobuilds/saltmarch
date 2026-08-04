#ifndef PEOPLE_UI_H
#define PEOPLE_UI_H

/* people_ui.h  --  Painting the people overlay (LIFE_PLAN Phase 9) */

#include <SDL3/SDL.h>
#include "people_view.h"
#include "ui_kit.h"

void people_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                    const UiList *list, const PeopleView *view,
                    int mouse_x, int mouse_y);

#endif /* PEOPLE_UI_H */
