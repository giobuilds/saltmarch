/*  island_bar.c  --  The island header (UI_PLAN Phase 5)  */

#include "island_bar.h"
#include <string.h>

/* One fixed colour per island slot. Indexed, not generated, so the
 * colours are chosen rather than whatever a hash produced — and so
 * they stay distinguishable from each other and from the terrain. */
void island_hue(int island, uint8_t *r, uint8_t *g, uint8_t *b)
{
    static const uint8_t HUE[][3] = {
        { 225, 195, 120 },   /* Saltford  — wheat      */
        { 150, 180, 225 },   /* Brinehold — cold blue  */
        { 150, 205, 150 },   /* Tidefast  — green      */
        { 220, 160, 150 }    /* Marrowbay — clay       */
    };
    int n = (int)(sizeof(HUE) / sizeof(HUE[0]));
    int i = (island >= 0 && island < n) ? island : 0;

    if (r) *r = HUE[i][0];
    if (g) *g = HUE[i][1];
    if (b) *b = HUE[i][2];
}

/* The settled island before/after `from`, wrapping. Returns `from`
 * itself when there is nowhere else to go. */
static int step_settled(const UiSnapshot *snap, int from, int dir)
{
    int i, idx;

    for (i = 1; i <= MAX_ISLANDS; i++) {
        idx = ((from + dir * i) % MAX_ISLANDS + MAX_ISLANDS) % MAX_ISLANDS;
        if (snap->islands[idx].settled) return idx;
    }
    return from;
}

void island_bar_build(UiList *out, const UiSnapshot *snap, float screen_w)
{
    UiRect bar, left, right, label;
    int    cur   = snap->current_island;
    int    prev, next;
    char   name[UI_LABEL_LEN];

    ui_list_reset(out);

    if (cur < 0 || cur >= MAX_ISLANDS) return;

    bar.w = ISLAND_BAR_W;
    bar.h = ISLAND_BAR_H_PX;
    bar.x = (screen_w - bar.w) * 0.5f;
    bar.y = ISLAND_BAR_TOP;

    /* The bar itself absorbs clicks so the header is not a hole in the
     * UI that drops a building on the tile behind it. */
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), bar,
                 NULL, 0, 0);

    prev = step_settled(snap, cur, -1);
    next = step_settled(snap, cur, +1);

    left      = bar;
    left.w    = ISLAND_BAR_CHEVRON;
    right     = bar;
    right.w   = ISLAND_BAR_CHEVRON;
    right.x   = bar.x + bar.w - ISLAND_BAR_CHEVRON;
    label     = bar;
    label.x  += ISLAND_BAR_CHEVRON;
    label.w  -= ISLAND_BAR_CHEVRON * 2.0f;

    ui_list_push(out, ui_id(UI_GROUP_ISLAND, (uint16_t)prev), left,
                 "<", prev, 0);
    if (prev == cur) ui_list_disable_last(out, REJ_UNAVAILABLE);

    memcpy(name, snap->islands[cur].name, sizeof(name) < ISLAND_NAME_LEN
                                          ? sizeof(name) : ISLAND_NAME_LEN);
    name[UI_LABEL_LEN - 1] = '\0';
    ui_list_push(out, ui_id(UI_GROUP_ISLAND, (uint16_t)cur), label,
                 name, cur, UI_W_HEADER);

    ui_list_push(out, ui_id(UI_GROUP_ISLAND, (uint16_t)next), right,
                 ">", next, 0);
    if (next == cur) ui_list_disable_last(out, REJ_UNAVAILABLE);
}

IslandBarHit island_bar_hit(const UiList *list, float x, float y)
{
    IslandBarHit    hit;
    const UiWidget *w;

    hit.kind   = ISLAND_BAR_HIT_NONE;
    hit.island = -1;

    w = ui_list_hit(list, x, y);
    if (!w) return hit;

    if (ui_id_group(w->id) == UI_GROUP_ISLAND) {
        hit.kind   = ISLAND_BAR_HIT_SWITCH;
        hit.island = w->value;
    }
    return hit;
}
