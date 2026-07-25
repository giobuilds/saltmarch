/*  confirm_view.c  --  The one confirmation (UI_PLAN Phase 6)  */

#include "confirm_view.h"
#include "building.h"
#include "island_bar.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>

/* "20 Wood, 150 Gold" — the cost line, in the goods it is actually
 * paid in. */
static void cost_text(char *out, size_t n, const int cost[RES_COUNT])
{
    size_t used = 0;
    int    r, first = 1;

    out[0] = '\0';
    for (r = 0; r < RES_COUNT; r++) {
        int w;
        if (cost[r] <= 0) continue;
        w = snprintf(out + used, n - used, "%s%d %s", first ? "" : ", ",
                     cost[r], RESOURCE_NAMES[r]);
        if (w < 0 || (size_t)w >= n - used) break;
        used += (size_t)w;
        first = 0;
    }
    if (first) snprintf(out, n, "free");
}

static int can_afford(const UiIsland *isl, const int cost[RES_COUNT])
{
    int r;
    for (r = 0; r < RES_COUNT; r++)
        if (cost[r] > 0 && isl->stock[r] < cost[r]) return 0;
    return 1;
}

void confirm_view_build(ConfirmView *out, const UiSnapshot *snap)
{
    const ConfirmState *cs = &snap->confirm;
    const UiIsland     *isl;
    int                 island;

    memset(out, 0, sizeof(*out));
    if (!cs->open) return;

    island = cs->cmd.a;
    if (island < 0 || island >= MAX_ISLANDS) island = snap->current_island;
    isl = &snap->islands[island];

    island_hue(island, &out->hue_r, &out->hue_g, &out->hue_b);
    out->chosen = cs->chosen;

    /* The apply tick: commands are stamped for the tick that has not
     * run yet. Under lockstep the host adds its delay on top, which the
     * client cannot know here — so this is stated as the earliest, not
     * promised as the exact. */
    out->apply_tick = snap->tick;

    switch (cs->kind) {
    case CONFIRM_BUILD: {
        BuildingType       type = (BuildingType)(cs->cmd.d / 2);
        const BuildingDef *def;
        char               goods[28];   /* fits "Pay %s" in a label */

        if (type >= BUILDING_TYPE_COUNT) type = BUILDING_NONE;
        def = &BUILDING_DEFS[type];

        snprintf(out->title, sizeof(out->title), "Build %s", def->name);
        snprintf(out->lines[out->line_count++], CONFIRM_LINE_LEN,
                 "at (%d, %d) on %s", cs->cmd.b, cs->cmd.c, isl->name);

        cost_text(goods, sizeof(goods), def->cost);
        snprintf(out->options[0].label, sizeof(out->options[0].label),
                 "Pay %s", goods);
        out->options[0].affordable = (uint8_t)can_afford(isl, def->cost);

        /* The Gold-only option's price is the faction's ask on each
         * good, which the snapshot already resolved. */
        {
            int gold = def->cost[RES_GOLD], r;
            for (r = 0; r < RES_COUNT; r++)
                if (r != (int)RES_GOLD && def->cost[r] > 0)
                    gold += def->cost[r] * snap->ask[r];
            snprintf(out->options[1].label, sizeof(out->options[1].label),
                     "Pay %d Gold", gold);
            out->options[1].affordable =
                (uint8_t)(isl->stock[RES_GOLD] >= gold);
        }
        out->option_count = 2;
        break;
    }

    case CONFIRM_DEMOLISH: {
        const char *name = "building";
        int         idx  = cs->cmd.b;

        if (idx >= 0 && idx < isl->building_count) {
            int t = isl->buildings[idx].type;
            if (t >= 0 && t < BUILDING_TYPE_COUNT) name = BUILDING_DEFS[t].name;
        }
        snprintf(out->title, sizeof(out->title), "Demolish %s?", name);
        snprintf(out->lines[out->line_count++], CONFIRM_LINE_LEN,
                 "Nothing is refunded. This cannot be undone.");
        snprintf(out->options[0].label, sizeof(out->options[0].label),
                 "Demolish");
        out->options[0].affordable = 1;
        out->option_count = 1;
        out->destructive  = 1;
        break;
    }

    case CONFIRM_UPGRADE:
        snprintf(out->title, sizeof(out->title), "Upgrade to Worker's House");
        snprintf(out->lines[out->line_count++], CONFIRM_LINE_LEN,
                 "Workers need Beer as well as food.");
        snprintf(out->options[0].label, sizeof(out->options[0].label),
                 "Pay %d Gold", TIER_UPGRADE_COST_GOLD);
        out->options[0].affordable =
            (uint8_t)(isl->stock[RES_GOLD] >= TIER_UPGRADE_COST_GOLD);
        out->option_count = 1;
        break;

    case CONFIRM_SHIP:
        snprintf(out->title, sizeof(out->title), "Lay down a ship");
        snprintf(out->lines[out->line_count++], CONFIRM_LINE_LEN,
                 "It will be docked at %s.", isl->name);
        snprintf(out->options[0].label, sizeof(out->options[0].label),
                 "Pay %d Gold", SHIP_BUILD_COST_GOLD);
        out->options[0].affordable =
            (uint8_t)(isl->stock[RES_GOLD] >= SHIP_BUILD_COST_GOLD);
        out->option_count = 1;
        break;

    default:
        return;
    }

    /* The literal command behind each option. This is the phase's
     * point: not a description assembled from the same inputs, but the
     * struct itself, decoded by the same code that documents the
     * payload encoding. */
    command_describe(&cs->cmd, out->options[0].preview, CONFIRM_LINE_LEN);
    if (out->option_count > 1)
        command_describe(&cs->alt, out->options[1].preview, CONFIRM_LINE_LEN);
}

/* ---- geometry --------------------------------------------- */

static float wanted_height(const ConfirmView *v)
{
    return CONFIRM_MARGIN * 2.0f + CONFIRM_TITLE_H +
           (float)v->line_count * CONFIRM_LINE_H +
           (float)v->option_count * (CONFIRM_OPTION_H + 20.0f) +
           CONFIRM_BTN_H + 12.0f;
}

void confirm_build(UiList *out, const ConfirmView *view,
                   float screen_w, float screen_h)
{
    UiRect   panel, body;
    UiLayout l;
    int      i;

    ui_list_reset(out);
    if (view->option_count == 0) return;

    panel = ui_panel_centered(screen_w, screen_h, CONFIRM_W,
                              wanted_height(view), screen_h - 80.0f);
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), panel,
                 view->title, 0, 0);

    body = ui_inset(panel, CONFIRM_MARGIN);
    l    = ui_layout(body, 0.0f);

    (void)ui_row(&l, CONFIRM_TITLE_H);
    for (i = 0; i < view->line_count; i++)
        (void)ui_row(&l, CONFIRM_LINE_H);

    /* One row per option: the offer, with the command it submits drawn
     * under it. Options are selectable when there are two; with one
     * they are still pushed (the drawer needs the rect) but the
     * selection is meaningless. */
    for (i = 0; i < view->option_count; i++) {
        UiRect  r     = ui_row(&l, CONFIRM_OPTION_H);
        uint8_t flags = 0;

        l.cursor += 20.0f;   /* room for the preview line beneath */

        if (i == view->chosen && view->option_count > 1)
            flags |= UI_W_SELECTED;
        if (!view->options[i].affordable)
            flags |= UI_W_MUTED;

        ui_list_push(out, ui_id(UI_GROUP_RESOURCE, (uint16_t)i), r,
                     view->options[i].label, i, flags);
        if (!view->options[i].affordable)
            out->items[out->count - 1].reason = (uint8_t)REJ_CANT_AFFORD;
    }

    {
        UiRect row    = ui_row(&l, CONFIRM_BTN_H);
        UiRect cancel = row, accept = row;

        cancel.w = 150.0f;
        accept.w = 150.0f;
        accept.x = row.x + row.w - accept.w;

        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_REJECT), cancel,
                     "Cancel", 0, 0);
        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_ACCEPT), accept,
                     view->destructive ? "Demolish" : "Confirm", 0, 0);
    }
}

ConfirmHit confirm_hit(const UiList *list, float x, float y)
{
    ConfirmHit      hit;
    const UiWidget *w;

    hit.kind   = CONFIRM_HIT_OUTSIDE;
    hit.option = -1;

    w = ui_list_hit(list, x, y);
    if (!w) return hit;

    if (ui_id_group(w->id) == UI_GROUP_RESOURCE) {
        hit.kind   = CONFIRM_HIT_CHOOSE;
        hit.option = w->value;
        return hit;
    }
    if (ui_id_group(w->id) == UI_GROUP_ACTION) {
        switch ((UiAction)ui_id_value(w->id)) {
        case UI_ACTION_ACCEPT: hit.kind = CONFIRM_HIT_ACCEPT; break;
        case UI_ACTION_REJECT: hit.kind = CONFIRM_HIT_CANCEL; break;
        default:               hit.kind = CONFIRM_HIT_NONE;   break;
        }
    }
    return hit;
}
