/* people_view.c  --  The people overlay (LIFE_PLAN Phase 9) */

#include "people_view.h"
#include "island_bar.h"
#include <stdio.h>
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

/* A 0..1 factor as per mille, for a widget's int payload. */
static int32_t permille(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (int32_t)(v * 1000.0f + 0.5f);
}

void people_view_build(PeopleView *v, const UiSnapshot *snap, int island)
{
    const UiIsland *isl;
    int             i;

    memset(v, 0, sizeof(*v));
    v->island = island;

    if (!snap || island < 0 || island >= MAX_ISLANDS) {
        copy_str(v->title, sizeof(v->title), "The people");
        return;
    }
    isl = &snap->islands[island];

    snprintf(v->title, sizeof(v->title), "The people — %s", isl->name);
    island_hue(island, &v->hue_r, &v->hue_g, &v->hue_b);
    v->settled      = isl->settled;
    v->detail_known = isl->detail_known;

    v->residents         = isl->residents;
    v->reserve           = isl->reserve;
    v->homes_empty       = isl->homes_empty;
    v->founder_allowance = isl->founder_allowance;
    v->left_last_month   = isl->left_last_month;

    /* An island you have not surveyed has a population you were not
     * told, and scoring it would be scoring zeroes (UI_PLAN N2). */
    if (!isl->detail_known) return;

    wellbeing_island(&v->island_wb, snap, island);
    v->scored = (uint8_t)(isl->residents > 0 || isl->reserve > 0);

    for (i = 0; i < isl->cast_count && i < UI_CAST_MAX; i++) {
        const UiResident *r   = &isl->cast[i];
        PeopleRow        *row = &v->rows[v->row_count++];

        row->id = r->id;
        wellbeing_describe(r, row->line, sizeof(row->line));
        wellbeing_resident(&row->wb, snap, island, r);
    }
}

/* ---- geometry --------------------------------------------- */

static float wanted_height(const PeopleView *v)
{
    return PEOPLE_MARGIN * 2.0f + PEOPLE_TITLE_H + PEOPLE_SCORE_H +
           (float)WB_FACTOR_COUNT * PEOPLE_FACTOR_H + PEOPLE_HEAD_H +
           (float)v->row_count * (PEOPLE_ROW_H + PEOPLE_ROW_GAP) +
           PEOPLE_FOOTER_H;
}

void people_build(UiList *out, const PeopleView *view,
                  float screen_w, float screen_h)
{
    UiRect   panel;
    UiRect   body;
    UiLayout l;
    float    max_h = PEOPLE_MAX_H;
    int      i;

    if (max_h > screen_h - 80.0f) max_h = screen_h - 80.0f;
    panel = ui_panel_centered(screen_w, screen_h, PEOPLE_W,
                              wanted_height(view), max_h);

    ui_list_reset(out);
    ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), panel,
                 view->title, 0, 0);

    body = ui_inset(panel, PEOPLE_MARGIN);
    l    = ui_layout(body, 0.0f);

    (void)ui_row(&l, PEOPLE_TITLE_H);

    /* The island's score, on its own line above the factors that make
     * it. The value is the score in hundredths of a rung. */
    {
        UiRect r = ui_row(&l, PEOPLE_SCORE_H);
        ui_list_push(out, ui_id(UI_GROUP_FACTOR, PEOPLE_FACTOR_TOTAL), r,
                     "Wellbeing",
                     (int32_t)(view->island_wb.score * 100.0f + 0.5f),
                     UI_W_HEADER);
    }

    /* One row per factor, modelled or not: an unmodelled factor is
     * listed and marked rather than omitted, so the six the projection
     * names are all on screen and two of them say why they are blank. */
    for (i = 0; i < WB_FACTOR_COUNT; i++) {
        UiRect r = ui_row(&l, PEOPLE_FACTOR_H);
        ui_list_push(out, ui_id(UI_GROUP_FACTOR, (uint16_t)i), r,
                     wellbeing_factor_name(i),
                     permille(view->island_wb.factor[i]), UI_W_HEADER);
    }

    {
        UiRect r = ui_row(&l, PEOPLE_HEAD_H);
        /* No heading at all on ground nobody stands on: the score block
         * has already said so, and saying it twice reads as a fault. */
        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_NONE), r,
                     !view->scored     ? NULL
                     : view->row_count ? "Who lives here"
                                       : "Nobody here is worth naming yet",
                     view->row_count, UI_W_HEADER);
    }

    /* A cast row carries its index and its score; the sentence itself
     * stays on the view, because it is longer than UI_LABEL_LEN and a
     * description truncated mid-word is worse than none. */
    for (i = 0; i < view->row_count; i++) {
        UiRect r = ui_row(&l, PEOPLE_ROW_H);
        l.cursor += PEOPLE_ROW_GAP;
        ui_list_push(out, ui_id(UI_GROUP_RESIDENT, (uint16_t)i), r, NULL,
                     (int32_t)(view->rows[i].wb.score * 100.0f + 0.5f),
                     UI_W_HEADER);
    }

    {
        UiRect footer = ui_row(&l, PEOPLE_FOOTER_H);
        UiRect close  = { footer.x + footer.w - 110.0f, footer.y + 4.0f,
                          110.0f, 30.0f };
        ui_list_push(out, ui_id(UI_GROUP_ACTION, UI_ACTION_CLOSE), close,
                     "Close", 0, 0);
    }
}

PeopleHit people_hit(const UiList *list, float x, float y)
{
    PeopleHit       hit;
    const UiWidget *w;

    memset(&hit, 0, sizeof(hit));
    hit.kind = PEOPLE_HIT_OUTSIDE;

    w = ui_list_hit(list, x, y);
    if (!w) return hit;
    hit.rect = w->rect;

    if (ui_id_group(w->id) == UI_GROUP_ACTION &&
        (UiAction)ui_id_value(w->id) == UI_ACTION_CLOSE)
        hit.kind = PEOPLE_HIT_CLOSE;
    else
        hit.kind = PEOPLE_HIT_NONE;

    return hit;
}
