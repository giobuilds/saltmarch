#ifndef CHART_UI_H
#define CHART_UI_H

/* chart_ui.h  --  Drawing the passages (UI_PLAN N4) */

#include <SDL3/SDL.h>
#include "chart_view.h"
#include "ui_kit.h"

void chart_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const UiList *list, const ChartView *view,
                   int mouse_x, int mouse_y);

#endif /* CHART_UI_H */
