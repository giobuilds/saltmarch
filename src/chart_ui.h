#ifndef CHART_UI_H
#define CHART_UI_H

/* =========================================================
 * chart_ui.h  --  Drawing the passages (UI_PLAN N4)
 *
 * Paint only, on the same division of labour as book_ui.c: chart_view.c
 * decides where everything is, which buttons are pressable and why not,
 * and this turns the resulting UiList into pixels. If a rect looks wrong
 * the bug is in chart_view.c, where there is a headless test that can
 * prove it.
 *
 * Two things are drawing decisions rather than layout ones, and so live
 * here. A passage this player has not learned draws its name and its
 * numbers as the unknown mark (UI_PLAN N2) — the row is still built, laid
 * out and hit-tested exactly like any other, because a layout that
 * changes shape with what you know is a layout with two versions and one
 * of them is rarely exercised. And a passage that has gone out of use is
 * struck through where it stood, which is what makes "your charts of this
 * are waste paper now" legible without anything moving.
 * ========================================================= */

#include <SDL3/SDL.h>
#include "chart_view.h"
#include "ui_kit.h"

void chart_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                   const UiList *list, const ChartView *view,
                   int mouse_x, int mouse_y);

#endif /* CHART_UI_H */
