/*  test_hud.c  --  the build bar in categories (UI_PLAN Phase 3)
 *
 * Linked WITHOUT SDL, against libsaltmarch_ui: this drives the real
 * layout and the real hit-test.
 *
 * The plan's verification is "synthetic 40-entry def table, per-tab
 * slot fit and hit-test" — the HUD's own capacity cliff. The real def
 * table has twelve placeable buildings, and the bar visibly runs into
 * the right-hand buttons somewhere past twenty; a test that only ever
 * saw twelve would not notice until a future phase added the thirteenth
 * through twenty-fifth.
 *
 * Also checked:
 *   - tabs are the categories that actually have buildings in them;
 *   - the sticky tab: what is shown follows UiState and nothing else;
 *   - greyed-not-hidden: an unaffordable building keeps its slot, keeps
 *     its position, stays clickable, and carries its reason;
 *   - the real def table produces a bar that fits, with every placeable
 *     building reachable from exactly one tab.
 */

#include "hud_view.h"
#include "ui_kit.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* camera.h (via ui_snapshot.h) already defines these as ints — the bar
 * is laid out against the same logical screen the game uses. */
#define SCR_W ((float)SCREEN_W)
#define SCR_H ((float)SCREEN_H)

/* `n` synthetic buildings spread evenly across the real categories. */
static void synth(HudView *v, int n, int affordable_every)
{
    int i;

    memset(v, 0, sizeof(*v));
    v->selected    = BUILDING_NONE;
    v->entry_count = n > HUD_MAX_ENTRIES ? HUD_MAX_ENTRIES : n;

    for (i = 0; i < v->entry_count; i++) {
        HudEntry *e = &v->entries[i];
        e->type       = (uint16_t)i;
        e->category   = (uint8_t)(BCAT_FARMING +
                                  (i % (BCAT_COUNT - BCAT_FARMING)));
        e->affordable = (uint8_t)(affordable_every == 0 ||
                                  (i % affordable_every) == 0);
        e->refuse     = (uint8_t)(e->affordable ? REJ_OK : REJ_CANT_AFFORD);
        e->cost_gold  = 50 + i;
        snprintf(e->name, sizeof(e->name), "Bldg%d", i);
    }
}

static int count_group(const UiList *l, int group)
{
    int i, n = 0;
    for (i = 0; i < l->count; i++)
        if (ui_id_group(l->items[i].id) == group) n++;
    return n;
}

/* ---- 1. the bar fits, at any table size ------------------ */
static void test_fits(void)
{
    const int SIZES[] = { 12, 25, 40 };
    int       s;

    for (s = 0; s < (int)(sizeof(SIZES) / sizeof(SIZES[0])); s++) {
        HudView v;
        UiState st;
        UiList  list;
        UiRect  bar;
        int     i, inside = 1, in_bar = 1, overlap = 0;
        char    msg[96];

        synth(&v, SIZES[s], 0);
        memset(&st, 0, sizeof(st));
        st.hud_category = BCAT_FARMING;
        hud_build(&list, &v, &st, SCR_W, SCR_H);

        bar = list.items[0].rect;

        for (i = 1; i < list.count; i++) {
            UiRect r = list.items[i].rect;
            if (r.x < 0.0f || r.x + r.w > SCR_W ||
                r.y < 0.0f || r.y + r.h > SCR_H) inside = 0;
            if (r.y < bar.y - 0.01f ||
                r.y + r.h > bar.y + bar.h + 0.01f) in_bar = 0;
        }

        /* Slots must not run into the right-hand cluster — the exact
         * failure this phase exists to prevent. */
        {
            float rightmost_slot = -1.0f, leftmost_button = SCR_W;
            for (i = 1; i < list.count; i++) {
                const UiWidget *w = &list.items[i];
                if (ui_id_group(w->id) == UI_GROUP_BUILDING &&
                    w->rect.x + w->rect.w > rightmost_slot)
                    rightmost_slot = w->rect.x + w->rect.w;
                if (ui_id_group(w->id) == UI_GROUP_ACTION &&
                    ui_id_value(w->id) != UI_ACTION_NONE &&
                    w->rect.x < leftmost_button)
                    leftmost_button = w->rect.x;
            }
            if (rightmost_slot > leftmost_button + 0.01f) overlap = 1;
        }

        snprintf(msg, sizeof(msg), "%2d buildings: bar is on screen", SIZES[s]);
        CHECK(inside, msg);
        snprintf(msg, sizeof(msg), "%2d buildings: everything inside the bar",
                 SIZES[s]);
        CHECK(in_bar, msg);
        snprintf(msg, sizeof(msg),
                 "%2d buildings: slots never reach the right-hand buttons",
                 SIZES[s]);
        CHECK(!overlap, msg);
    }
}

/* ---- 2. tabs and stickiness ------------------------------ */
static void test_tabs(void)
{
    HudView v;
    UiState st;
    UiList  list;
    int     tabs, slots_gathering, slots_maritime, i;

    synth(&v, 20, 0);
    memset(&st, 0, sizeof(st));
    st.hud_category = BCAT_FARMING;
    hud_build(&list, &v, &st, SCR_W, SCR_H);

    tabs = count_group(&list, UI_GROUP_CATEGORY);
    CHECK(tabs == BCAT_COUNT - 1,
          "one tab per category that has buildings in it");

    slots_gathering = count_group(&list, UI_GROUP_BUILDING);
    CHECK(slots_gathering > 0, "the chosen tab shows its buildings");

    /* Everything on screen belongs to the chosen tab — that is what a
     * tab IS, and the assertion that catches a filter bug. */
    {
        int wrong = 0;
        for (i = 1; i < list.count; i++) {
            const UiWidget *w = &list.items[i];
            int             j;
            if (ui_id_group(w->id) != UI_GROUP_BUILDING) continue;
            for (j = 0; j < v.entry_count; j++)
                if (v.entries[j].type == ui_id_value(w->id) &&
                    v.entries[j].category != BCAT_FARMING) wrong++;
        }
        CHECK(wrong == 0, "no slot from another category leaks in");
    }

    st.hud_category = BCAT_MARITIME;
    hud_build(&list, &v, &st, SCR_W, SCR_H);
    slots_maritime = count_group(&list, UI_GROUP_BUILDING);
    CHECK(slots_maritime > 0, "switching tab shows a different set");

    /* Sticky: the view changing (a building gets selected, gold moves)
     * must not move the tab. Only UiState decides. */
    v.selected = 3;
    hud_build(&list, &v, &st, SCR_W, SCR_H);
    CHECK(count_group(&list, UI_GROUP_BUILDING) == slots_maritime,
          "selecting a building does not switch tab");

    {
        const UiWidget *tab = ui_list_find(&list,
                                  ui_id(UI_GROUP_CATEGORY, BCAT_MARITIME));
        CHECK(tab && (tab->flags & UI_W_SELECTED),
              "the chosen tab is the one drawn as selected");
    }

    /* A nonsense category (client state outliving a re-categorised def
     * table) falls back rather than showing an empty bar. */
    st.hud_category = 999;
    hud_build(&list, &v, &st, SCR_W, SCR_H);
    CHECK(count_group(&list, UI_GROUP_BUILDING) > 0,
          "an out-of-range tab index falls back to a real tab");
}

/* ---- 3. greyed, not hidden ------------------------------- */
static void test_greyed_not_hidden(void)
{
    HudView v;
    UiState st;
    UiList  all, some;
    int     n_all, n_some, i, muted = 0, reasoned = 0;
    HudHit  hit;

    memset(&st, 0, sizeof(st));
    st.hud_category = BCAT_FARMING;

    synth(&v, 20, 0);                 /* everything affordable */
    hud_build(&all, &v, &st, SCR_W, SCR_H);
    n_all = count_group(&all, UI_GROUP_BUILDING);

    synth(&v, 20, 3);                 /* most of it not */
    hud_build(&some, &v, &st, SCR_W, SCR_H);
    n_some = count_group(&some, UI_GROUP_BUILDING);

    CHECK(n_all == n_some,
          "an unaffordable building keeps its slot — nothing is hidden");

    for (i = 1; i < some.count; i++) {
        const UiWidget *w = &some.items[i];
        if (ui_id_group(w->id) != UI_GROUP_BUILDING) continue;
        if (w->flags & UI_W_MUTED) {
            muted++;
            if (w->reason == (uint8_t)REJ_CANT_AFFORD) reasoned++;
        }
    }
    CHECK(muted > 0, "unaffordable slots are greyed");
    CHECK(muted == reasoned, "...and every one of them carries its reason");

    /* Still clickable: the build-confirm popup can offer to pay in
     * Gold, so refusing the click would remove a real option. */
    {
        const UiWidget *m = NULL;
        for (i = 1; i < some.count; i++)
            if (ui_id_group(some.items[i].id) == UI_GROUP_BUILDING &&
                (some.items[i].flags & UI_W_MUTED)) {
                m = &some.items[i];
                break;
            }
        CHECK(m != NULL, "found a greyed slot to click");
        if (m) {
            hit = hud_hit(&some, m->rect.x + m->rect.w * 0.5f,
                          m->rect.y + m->rect.h * 0.5f);
            CHECK(hit.kind == HUD_HIT_BUILDING &&
                  hit.type == (int)ui_id_value(m->id),
                  "a greyed slot still selects its building");
            CHECK(hit.refuse == (uint8_t)REJ_CANT_AFFORD,
                  "...and the click reports why it was greyed");
        }
    }

    /* Slot ORDER does not depend on affordability: a slot must not
     * move because Gold crossed a threshold. */
    {
        int k = 0, same = 1;
        for (i = 1; i < all.count && same; i++) {
            if (ui_id_group(all.items[i].id) != UI_GROUP_BUILDING) continue;
            for (; k < some.count; k++)
                if (ui_id_group(some.items[k].id) == UI_GROUP_BUILDING) break;
            if (k >= some.count) { same = 0; break; }
            if (all.items[i].id != some.items[k].id) same = 0;
            if (all.items[i].rect.x != some.items[k].rect.x) same = 0;
            k++;
        }
        CHECK(same, "slots keep their positions when affordability changes");
    }
}

/* ---- 4. hit-testing every element ------------------------ */
static void test_hits(void)
{
    HudView v;
    UiState st;
    UiList  list;
    HudHit  hit;
    int     i, ok = 1;

    synth(&v, 20, 0);
    memset(&st, 0, sizeof(st));
    st.hud_category = BCAT_FARMING;
    hud_build(&list, &v, &st, SCR_W, SCR_H);

    for (i = 1; i < list.count; i++) {
        const UiWidget *w = &list.items[i];
        float           x = w->rect.x + w->rect.w * 0.5f;
        float           y = w->rect.y + w->rect.h * 0.5f;

        if (w->flags & UI_W_HEADER) continue;
        hit = hud_hit(&list, x, y);

        switch (ui_id_group(w->id)) {
        case UI_GROUP_BUILDING:
            if (hit.kind != HUD_HIT_BUILDING ||
                hit.type != (int)ui_id_value(w->id)) ok = 0;
            break;
        case UI_GROUP_CATEGORY:
            if (hit.kind != HUD_HIT_TAB ||
                hit.category != (int)ui_id_value(w->id)) ok = 0;
            break;
        case UI_GROUP_ACTION:
            switch ((UiAction)ui_id_value(w->id)) {
            case UI_ACTION_MENU:     if (hit.kind != HUD_HIT_MENU)     ok = 0; break;
            case UI_ACTION_DEMOLISH: if (hit.kind != HUD_HIT_DEMOLISH) ok = 0; break;
            case UI_ACTION_WORLD:    if (hit.kind != HUD_HIT_WORLD)    ok = 0; break;
            default: break;
            }
            break;
        default: break;
        }
    }
    CHECK(ok, "every widget on the bar hit-tests back to itself");

    hit = hud_hit(&list, 900.0f, 40.0f);
    CHECK(hit.kind == HUD_HIT_OUTSIDE,
          "a click on the world above the bar is not a bar click");

    /* Empty bar background: absorbed, not passed through to the map. */
    hit = hud_hit(&list, SCR_W * 0.5f, SCR_H - 4.0f);
    CHECK(hit.kind == HUD_HIT_NONE || hit.kind == HUD_HIT_BUILDING,
          "the bar itself absorbs clicks that hit no control");
}

/* ---- 5. the real def table ------------------------------- */
static void test_real_defs(void)
{
    HudView    v;
    UiState    st;
    UiList     list;
    UiSnapshot snap;
    int        i, t, total = 0, placeable = 0;

    memset(&snap, 0, sizeof(snap));
    snap.islands[0].settled = 1;
    for (i = 0; i < RES_COUNT; i++) snap.islands[0].stock[i] = 10000;

    hud_view_build(&v, &snap, 0);

    for (t = 0; t < BUILDING_TYPE_COUNT; t++)
        if (BUILDING_DEFS[t].hud_placeable) placeable++;
    CHECK(v.entry_count == placeable,
          "every placeable building, and only those, reaches the bar");

    /* Each one appears in exactly one tab. */
    memset(&st, 0, sizeof(st));
    for (i = BCAT_NONE + 1; i < BCAT_COUNT; i++) {
        st.hud_category = i;
        hud_build(&list, &v, &st, SCR_W, SCR_H);
        total += count_group(&list, UI_GROUP_BUILDING);
    }
    CHECK(total == placeable,
          "the tabs between them show every building exactly once");

    /* Why that can fail, and why it is worth a second, louder check.
     * The bar draws `hud_slots_that_fit` slots per tab and marks the
     * remainder with a "+N" badge — which tells a player that
     * buildings exist without giving them any way to reach one. It is
     * a dead end, not a scrollbar.
     *
     * SUPPLY_CHAIN Phase 6 hit it for the first time: Workshops
     * reached 22 against 21 slots, so exactly one building became
     * unbuildable. Three furnace-driven rows moved to Factories, where
     * they always belonged. The next phase to overflow a tab will fail
     * HERE, naming the category, rather than being discovered as a
     * building somebody could not find. At that point the answer is
     * paging in the bar, not another recategorisation. */
    {
        int cap = hud_slots_that_fit(SCR_W), cat_i, over = 0;
        for (cat_i = BCAT_NONE + 1; cat_i < BCAT_COUNT; cat_i++) {
            int n = 0, t2;
            for (t2 = 0; t2 < BUILDING_TYPE_COUNT; t2++)
                if ((int)BUILDING_DEFS[t2].category == cat_i &&
                    BUILDING_DEFS[t2].hud_placeable) n++;
            if (n > cap) {
                printf("  FAIL: '%s' holds %d buildings but only %d fit — "
                       "%d cannot be built at all\n",
                       building_category_name((BuildingCategory)cat_i),
                       n, cap, n - cap);
                over++;
            }
        }
        CHECK(over == 0, "no category holds more buildings than the bar "
                         "can show");
    }

    /* With a rich island nothing is greyed; with a poor one, plenty is,
     * and the count of slots is unchanged. */
    {
        int rich_muted = 0, poor_muted = 0, rich_n, poor_n;

        st.hud_category = BCAT_FARMING;
        hud_build(&list, &v, &st, SCR_W, SCR_H);
        rich_n = count_group(&list, UI_GROUP_BUILDING);
        for (i = 1; i < list.count; i++)
            if (list.items[i].flags & UI_W_MUTED) rich_muted++;

        memset(&snap.islands[0].stock, 0, sizeof(snap.islands[0].stock));
        hud_view_build(&v, &snap, 0);
        hud_build(&list, &v, &st, SCR_W, SCR_H);
        poor_n = count_group(&list, UI_GROUP_BUILDING);
        for (i = 1; i < list.count; i++)
            if (list.items[i].flags & UI_W_MUTED) poor_muted++;

        CHECK(rich_muted == 0, "a rich island greys nothing");
        CHECK(poor_muted > 0, "a broke island greys what it cannot pay for");
        CHECK(rich_n == poor_n, "and the bar keeps the same number of slots");
    }
}

int main(void)
{
    printf("== hud bar (no SDL linked) ==\n");
    test_fits();
    test_tabs();
    test_greyed_not_hidden();
    test_hits();
    test_real_defs();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
