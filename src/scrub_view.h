#ifndef SCRUB_VIEW_H
#define SCRUB_VIEW_H

/* =========================================================
 * scrub_view.h  --  The time-travel slider
 *                   (MMO_PLAN later phases)
 *
 * A world is (seed, ordered log), which means every past tick is still
 * reachable: re-simulate to it. This is the widget for that — a bar
 * across the bottom of the screen, click anywhere to travel.
 *
 * Layout and hit-testing here, SDL-free and tested, like every other
 * surface since UI_PLAN Phase 0. The drawer is in the client.
 *
 * Why it is worth having at all: a bug that only shows up after twenty
 * minutes of play is otherwise twenty minutes per attempt to look at.
 * With this it is a click, and the sim is frozen while you look.
 * ========================================================= */

#include <stdint.h>
#include "ui_kit.h"

#define SCRUB_H        44.0f
#define SCRUB_MARGIN   40.0f
#define SCRUB_TRACK_H  10.0f
#define SCRUB_HANDLE_W 10.0f

/* Build the bar for `tick` out of `max_tick`. The track is pushed
 * first, then the handle, then the "back to now" button. */
void scrub_build(UiList *out, uint64_t tick, uint64_t max_tick,
                 float screen_w, float screen_h);

typedef enum {
    SCRUB_HIT_NONE = 0,
    SCRUB_HIT_SEEK,        /* `tick` is where the player pointed */
    SCRUB_HIT_LIVE         /* leave the past, return to now      */
} ScrubHitKind;

typedef struct {
    ScrubHitKind kind;
    uint64_t     tick;
} ScrubHit;

/* Decode a click. Seeking is proportional across the track, so the
 * answer is a tick rather than a pixel — the caller never has to know
 * how wide the bar was. */
ScrubHit scrub_hit(const UiList *list, uint64_t max_tick, float x, float y);

#endif /* SCRUB_VIEW_H */
