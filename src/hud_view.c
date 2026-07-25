/*  hud_view.c  --  The build bar (UI_PLAN Phase 3)
 *
 *  Layout and hit-testing only; ui.c paints the result. See hud_view.h
 *  for the two rules this obeys (sticky tab, greyed-not-hidden).
 */

#include "hud_view.h"
#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ---- building the view ------------------------------------ */

void hud_view_build(HudView *out, const UiSnapshot *snap, int island)
{
    const UiIsland *isl;
    int             t, r;

    memset(out, 0, sizeof(*out));
    out->selected = BUILDING_NONE;

    if (island < 0 || island >= MAX_ISLANDS) return;
    isl = &snap->islands[island];

    for (t = 0; t < BUILDING_TYPE_COUNT &&
                out->entry_count < HUD_MAX_ENTRIES; t++) {
        const BuildingDef *def = &BUILDING_DEFS[t];
        HudEntry          *e;
        int                affordable = 1;

        /* Not everything is reachable from the bar: a Worker's House
         * exists only as an upgrade of a placed House. */
        if (!def->hud_placeable) continue;

        e = &out->entries[out->entry_count++];
        e->type      = (uint16_t)t;
        e->category  = (uint8_t)def->category;
        e->cost_gold = def->cost[RES_GOLD];
        copy_str(e->name, sizeof(e->name), def->name);

        /* The same rule building_can_afford() applies, read off the
         * snapshot: every non-zero line of the cost must be in stock.
         * Being short only means the resource-payment option is out —
         * the confirm popup can still offer to pay in Gold — so this
         * greys the slot rather than disabling it. */
        for (r = 0; r < RES_COUNT; r++)
            if (def->cost[r] > 0 && isl->stock[r] < def->cost[r])
                affordable = 0;

        e->affordable = (uint8_t)affordable;
        e->refuse     = (uint8_t)(affordable ? REJ_OK : REJ_CANT_AFFORD);
    }
}

/* ---- geometry --------------------------------------------- */

/* The right-hand cluster (world, demolish, cog) is anchored to the
 * right edge; slots may not run into it. */
static float right_cluster_left(float screen_w)
{
    const float slot = (float)HUD_SLOT_SIZE;
    const float pad  = (float)HUD_SLOT_PAD;
    return screen_w - (float)HUD_MARGIN_LEFT - (slot * 3.0f + pad * 2.0f);
}

int hud_slots_that_fit(float screen_w)
{
    float avail = right_cluster_left(screen_w) - (float)HUD_MARGIN_LEFT
                  - (float)HUD_SLOT_PAD;
    int   n     = (int)((avail + (float)HUD_SLOT_PAD) /
                        ((float)HUD_SLOT_SIZE + (float)HUD_SLOT_PAD));
    return n < 0 ? 0 : n;
}

static UiRect slot_rect(float screen_h, int index)
{
    UiRect r;
    r.x = (float)HUD_MARGIN_LEFT +
          (float)index * ((float)HUD_SLOT_SIZE + (float)HUD_SLOT_PAD);
    r.y = screen_h - (float)HUD_HEIGHT + HUD_TAB_TOP + HUD_TAB_H +
          HUD_SLOT_TOP;
    r.w = (float)HUD_SLOT_SIZE;
    r.h = (float)HUD_SLOT_SIZE;
    return r;
}

/* ---- the builder ------------------------------------------ */

void hud_build(UiList *out, const HudView *view, const UiState *st,
               float screen_w, float screen_h)
{
    UiRect bar;
    int    cat, chosen, i, slot = 0, capacity, shown = 0, hidden = 0;

    ui_list_reset(out);

    bar.x = 0.0f;
    bar.y = screen_h - (float)HUD_HEIGHT;
    bar.w = screen_w;
    bar.h = (float)HUD_HEIGHT;
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), bar, NULL, 0, 0);

    /* Which tab. Clamped rather than trusted: hud_category is client
     * state that outlives a def table being re-categorised. */
    chosen = st ? (int)st->hud_category : BCAT_FARMING;
    if (chosen <= BCAT_NONE || chosen >= BCAT_COUNT) chosen = BCAT_FARMING;

    /* ---- the tab strip ---------------------------------- */
    {
        int  drawn = 0;
        for (cat = BCAT_NONE + 1; cat < BCAT_COUNT; cat++) {
            UiRect  tab;
            int     count = 0;
            uint8_t flags = 0;

            for (i = 0; i < view->entry_count; i++)
                if (view->entries[i].category == cat) count++;

            /* A category with nothing in it gets no tab. Phase 2's
             * test asserts that never happens for the real def table;
             * this is for a synthetic one. */
            if (count == 0) continue;

            tab.x = (float)HUD_MARGIN_LEFT +
                    (float)drawn * (HUD_TAB_W + HUD_TAB_GAP);
            tab.y = bar.y + HUD_TAB_TOP;
            tab.w = HUD_TAB_W;
            tab.h = HUD_TAB_H;
            drawn++;

            if (cat == chosen) flags |= UI_W_SELECTED;
            ui_list_push(out, ui_id(UI_GROUP_CATEGORY, (uint16_t)cat), tab,
                         building_category_name((BuildingCategory)cat),
                         count, flags);
        }
    }

    /* ---- the slots of the chosen tab --------------------- */
    capacity = hud_slots_that_fit(screen_w);

    for (i = 0; i < view->entry_count; i++) {
        const HudEntry *e = &view->entries[i];
        UiRect          r;
        uint8_t         flags = 0;

        if (e->category != chosen) continue;

        if (shown >= capacity) { hidden++; continue; }

        r = slot_rect(screen_h, slot++);
        shown++;

        if ((int)e->type == view->selected) flags |= UI_W_SELECTED;
        if (!e->affordable)                 flags |= UI_W_MUTED;

        ui_list_push(out, ui_id(UI_GROUP_BUILDING, e->type), r, e->name,
                     e->cost_gold, flags);
        out->items[out->count - 1].reason = e->refuse;
    }

    /* Overflow is stated, not silent — the same "+k more" pattern the
     * vitals strip uses. A tab this full wants splitting, and saying so
     * on screen is how that gets noticed. */
    if (hidden > 0) {
        UiRect r = slot_rect(screen_h, slot);
        char   label[UI_LABEL_LEN];
        label[0] = '+';
        label[1] = (char)('0' + (hidden < 9 ? hidden : 9));
        label[2] = '\0';
        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), r, label,
                     hidden, UI_W_HEADER);
    }

    /* ---- the right-hand cluster -------------------------- */
    {
        UiRect r;
        float  x = right_cluster_left(screen_w);
        int    k;
        static const struct { UiAction action; const char *label; } BTN[3] = {
            { UI_ACTION_WORLD,    "Islands" },
            { UI_ACTION_DEMOLISH, "Demolish" },
            { UI_ACTION_MENU,     "Menu" }
        };

        for (k = 0; k < 3; k++) {
            uint8_t flags = 0;

            r    = slot_rect(screen_h, 0);
            r.x  = x + (float)k * ((float)HUD_SLOT_SIZE + (float)HUD_SLOT_PAD);

            if (BTN[k].action == UI_ACTION_WORLD    && view->world_open)
                flags |= UI_W_SELECTED;
            if (BTN[k].action == UI_ACTION_DEMOLISH && view->demolish_mode)
                flags |= UI_W_SELECTED;
            if (BTN[k].action == UI_ACTION_MENU     && view->menu_open)
                flags |= UI_W_SELECTED;

            ui_list_push(out, ui_id(UI_GROUP_ACTION, (uint16_t)BTN[k].action),
                         r, BTN[k].label, 0, flags);
        }
    }
}

/* ---- decoding a click -------------------------------------- */

HudHit hud_hit(const UiList *list, float x, float y)
{
    HudHit          hit;
    const UiWidget *w;

    hit.kind     = HUD_HIT_OUTSIDE;
    hit.type     = BUILDING_NONE;
    hit.category = BCAT_NONE;
    hit.refuse   = (uint8_t)REJ_OK;

    w = ui_list_hit(list, x, y);
    if (!w) return hit;

    switch (ui_id_group(w->id)) {
    case UI_GROUP_CATEGORY:
        hit.kind     = HUD_HIT_TAB;
        hit.category = (int)ui_id_value(w->id);
        break;

    case UI_GROUP_BUILDING:
        hit.kind   = HUD_HIT_BUILDING;
        hit.type   = (int)ui_id_value(w->id);
        hit.refuse = w->reason;   /* greyed slots still answer */
        break;

    case UI_GROUP_ACTION:
        switch ((UiAction)ui_id_value(w->id)) {
        case UI_ACTION_MENU:     hit.kind = HUD_HIT_MENU;     break;
        case UI_ACTION_DEMOLISH: hit.kind = HUD_HIT_DEMOLISH; break;
        case UI_ACTION_WORLD:    hit.kind = HUD_HIT_WORLD;    break;
        default:                 hit.kind = HUD_HIT_NONE;     break;
        }
        break;

    default:
        hit.kind = HUD_HIT_NONE;
        break;
    }
    return hit;
}
