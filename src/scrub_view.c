/*  scrub_view.c  --  The time slider (MMO_PLAN later phases)  */

#include "scrub_view.h"
#include <stdio.h>

void scrub_build(UiList *out, uint64_t tick,
                 uint64_t min_tick, uint64_t max_tick,
                 float screen_w, float screen_h)
{
    UiRect bar, track, handle, live;
    float  frac;
    char   label[UI_LABEL_LEN];

    ui_list_reset(out);

    bar.x = 0.0f;
    bar.y = screen_h - SCRUB_H;
    bar.w = screen_w;
    bar.h = SCRUB_H;
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), bar, NULL, 0, 0);

    track    = bar;
    track.x += SCRUB_MARGIN;
    track.w -= SCRUB_MARGIN * 2.0f + 130.0f;   /* room for the button */
    track.y += (SCRUB_H - SCRUB_TRACK_H) * 0.5f;
    track.h  = SCRUB_TRACK_H;
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_PREV), track,
                 NULL, 0, 0);

    /* The handle is where we are, proportionally ACROSS THE RETAINED */
    frac = (max_tick > min_tick)
         ? (float)(tick - min_tick) / (float)(max_tick - min_tick)
         : 1.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    handle    = track;
    handle.x  = track.x + track.w * frac - SCRUB_HANDLE_W * 0.5f;
    handle.w  = SCRUB_HANDLE_W;
    handle.y -= 6.0f;
    handle.h += 12.0f;
    snprintf(label, sizeof(label), "%llu", (unsigned long long)tick);
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), handle,
                 label, (int32_t)tick, UI_W_HEADER);

    live    = bar;
    live.x  = bar.x + bar.w - SCRUB_MARGIN - 110.0f;
    live.w  = 110.0f;
    live.y += 8.0f;
    live.h  = SCRUB_H - 16.0f;
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_ACCEPT), live,
                 "Back to now", 0, 0);
}

ScrubHit scrub_hit(const UiList *list, uint64_t min_tick,
                   uint64_t max_tick, float x, float y)
{
    ScrubHit        hit;
    const UiWidget *w;

    hit.kind = SCRUB_HIT_NONE;
    hit.tick = 0;

    w = ui_list_hit(list, x, y);
    if (!w) return hit;

    if (ui_id_value(w->id) == UI_ACTION_ACCEPT) {
        hit.kind = SCRUB_HIT_LIVE;
        return hit;
    }

    if (ui_id_value(w->id) == UI_ACTION_PREV) {
        /* Proportional across the track: the caller gets a tick, and
         * never has to know how wide the bar was. */
        float frac = (w->rect.w > 0.0f) ? (x - w->rect.x) / w->rect.w : 0.0f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        hit.kind = SCRUB_HIT_SEEK;
        hit.tick = min_tick +
            (uint64_t)((double)(max_tick - min_tick) * (double)frac);
        return hit;
    }
    return hit;
}
