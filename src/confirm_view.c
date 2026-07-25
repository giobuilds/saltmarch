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

    case CONFIRM_UPGRADE: {
        int            idx  = cs->cmd.b;
        BuildingType   from = BUILDING_NONE, to = BUILDING_NONE;
        const TierDef *tier, *next;
        RejectReason   why;
        int            k, prereq_type, prereq_present;

        if (idx >= 0 && idx < isl->building_count)
            from = (BuildingType)isl->buildings[idx].type;

        tier        = tier_def_for(from);
        prereq_type = (int)tier_upgrade_requires(from);
        prereq_present = snapshot_has_building(isl,
                                               (BuildingType)prereq_type);

        why  = snapshot_upgrade_check(isl, idx, &to);
        next = tier_def_for(to != BUILDING_NONE
                            ? to
                            : (tier ? tier->next_tier : BUILDING_NONE));

        snprintf(out->title, sizeof(out->title), "Upgrade to %s",
                 next ? BUILDING_DEFS[next->house_type].name : "nothing");
        snprintf(out->lines[out->line_count++], CONFIRM_LINE_LEN,
                 "They will want all of this, every time:");

        /* One row per good the tier being entered will need. This is
         * the whole point of the popup — the upgrade is not a price,
         * it is a question about whether you can keep supplying them. */
        if (next) {
            for (k = 0; k < MAX_TIER_GOODS; k++) {
                if (next->needs[k] == RES_COUNT) continue;
                snprintf(out->needs[out->need_count].label,
                         sizeof(out->needs[0].label), "%s",
                         RESOURCE_NAMES[next->needs[k]]);
                out->needs[out->need_count].met =
                    (uint8_t)(isl->stock[next->needs[k]] > 0);
                out->need_count++;
            }
        }
        if (prereq_type != BUILDING_NONE && !prereq_present &&
            out->need_count < MAX_TIER_GOODS) {
            snprintf(out->needs[out->need_count].label,
                     sizeof(out->needs[0].label), "%s",
                     BUILDING_DEFS[prereq_type].name);
            out->needs[out->need_count].met = 0;
            out->need_count++;
        }

        out->refusal = (int32_t)why;
        snprintf(out->options[0].label, sizeof(out->options[0].label),
                 "Pay %d Gold", tier ? tier->upgrade_gold : 0);
        /* Affordability is the SHARED rule's verdict, not a separate
         * gold check: the button a player can press and the command
         * sim_apply will accept are decided by one function. */
        out->options[0].affordable = (uint8_t)(why == REJ_OK);
        out->option_count = 1;
        break;
    }

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
           (float)v->need_count * CONFIRM_NEED_H +
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

    /* The needs checklist. Pushed as widgets rather than drawn from
     * the view directly so the drawer reads its rects from the same
     * list as everything else — UI_W_HEADER keeps them out of the
     * hit-test, which is what "label only" means here: a good you are
     * missing is information, not a button. */
    for (i = 0; i < view->need_count; i++) {
        UiRect r = ui_row(&l, CONFIRM_NEED_H);
        ui_list_push(out, ui_id(UI_GROUP_NONE, (uint16_t)i), r,
                     view->needs[i].label, view->needs[i].met,
                     (uint8_t)(UI_W_HEADER |
                               (view->needs[i].met ? 0u : UI_W_MUTED)));
    }

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
