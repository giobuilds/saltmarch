/* people_ui.c  --  Painting the people overlay (LIFE_PLAN Phase 9) */

#include "people_ui.h"
#include "fonts.h"
#include "wellbeing.h"

static SDL_FRect to_sdl(UiRect r)
{
    SDL_FRect s;
    s.x = r.x; s.y = r.y; s.w = r.w; s.h = r.h;
    return s;
}

static void fill(SDL_Renderer *r, UiRect rect,
                 Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    SDL_FRect f = to_sdl(rect);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderFillRect(r, &f);
}

static void outline(SDL_Renderer *r, UiRect rect,
                    Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    SDL_FRect f = to_sdl(rect);
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderRect(r, &f);
}

/* Green at the top of the ladder, amber in the middle, red at the
 * bottom. `frac` is 0..1 of WB_SCALE_MAX. */
static SDL_Color ladder_colour(float frac)
{
    SDL_Color c;
    if (frac >= 0.66f)      { c.r = 130; c.g = 185; c.b = 110; }
    else if (frac >= 0.40f) { c.r = 215; c.g = 180; c.b =  95; }
    else                    { c.r = 205; c.g = 110; c.b =  85; }
    c.a = 255;
    return c;
}

/* The island's score as a number and a full-width track. */
static void draw_score(SDL_Renderer *r, UiRect row, float score)
{
    const SDL_Color DIM   = { 130, 122, 105, 255 };
    float           frac  = score / WB_SCALE_MAX;
    SDL_Color       col   = ladder_colour(frac);
    UiRect          track = { row.x + PEOPLE_BAR_X, row.y + 18.0f,
                              PEOPLE_BAR_W, 16.0f };
    UiRect          bar   = track;
    char            buf[32];

    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    SDL_snprintf(buf, sizeof(buf), "%.1f", (double)score);
    font_draw_text(r, FONT_NORMAL, buf, (int)(row.x + 6.0f),
                   (int)(row.y + 14.0f), col);
    font_draw_text(r, FONT_SMALL, "out of 10", (int)(row.x + 60.0f),
                   (int)(row.y + 18.0f), DIM);

    fill(r, track, 28, 24, 18, 255);
    bar.w = track.w * frac;
    fill(r, bar, col.r, col.g, col.b, 255);
    outline(r, track, 80, 72, 55, 255);
}

/* One factor: its name, its weight, and a track showing how much of it
 * this island has. An unmodelled factor gets an empty track and says so
 * — its weight is excluded from the score rather than contributed. A
 * negative `permille` means there is nobody here to measure. */
static void draw_factor(SDL_Renderer *r, UiRect row, int factor, int permille)
{
    const SDL_Color TEXT = { 220, 210, 185, 255 };
    const SDL_Color DIM  = { 130, 122, 105, 255 };
    const WellbeingFactorDef *def = &WELLBEING_FACTORS[factor];
    UiRect          track, bar;
    char            buf[32];

    font_draw_text(r, FONT_SMALL, def->name, (int)(row.x + 4.0f),
                   (int)(row.y + 5.0f), def->modelled ? TEXT : DIM);

    SDL_snprintf(buf, sizeof(buf), "x%.1f", (double)def->weight);
    font_draw_text(r, FONT_SMALL, buf, (int)(row.x + 160.0f),
                   (int)(row.y + 5.0f), DIM);

    track    = row;
    track.x  = row.x + PEOPLE_BAR_X;
    track.w  = PEOPLE_BAR_W;
    track.y  = row.y + 6.0f;
    track.h  = 12.0f;
    fill(r, track, 28, 24, 18, 255);
    outline(r, track, 80, 72, 55, 255);

    if (!def->modelled) {
        font_draw_text(r, FONT_SMALL, "not modelled",
                       (int)(track.x + 6.0f), (int)(row.y + 5.0f), DIM);
        return;
    }
    if (permille < 0) return;

    {
        float     frac = (float)permille / 1000.0f;
        SDL_Color col  = ladder_colour(frac);

        bar   = track;
        bar.w = track.w * frac;
        fill(r, bar, col.r, col.g, col.b, 255);

        SDL_snprintf(buf, sizeof(buf), "%d%%", permille / 10);
        font_draw_text(r, FONT_SMALL, buf,
                       (int)(track.x + track.w + 10.0f),
                       (int)(row.y + 5.0f), TEXT);
    }
}

/* A resident's six factors as one small bar per factor, so ten people
 * can be compared down a column without opening any of them. */
static void draw_sparks(SDL_Renderer *r, UiRect row, const Wellbeing *wb)
{
    const float seg = PEOPLE_ROW_BAR_W / (float)WB_FACTOR_COUNT;
    int         i;

    for (i = 0; i < WB_FACTOR_COUNT; i++) {
        UiRect cell = { row.x + seg * (float)i, row.y + 5.0f,
                        seg - 3.0f, 18.0f };

        fill(r, cell, 28, 24, 18, 255);

        if (!WELLBEING_FACTORS[i].modelled) {
            UiRect line = { cell.x, cell.y + cell.h - 2.0f, cell.w, 2.0f };
            fill(r, line, 70, 64, 52, 255);
            continue;
        }

        {
            float     frac = wb->factor[i];
            SDL_Color col  = ladder_colour(frac);
            UiRect    bar  = cell;

            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            bar.h = cell.h * frac;
            bar.y = cell.y + cell.h - bar.h;
            fill(r, bar, col.r, col.g, col.b, 255);
        }
    }
}

void people_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                    const UiList *list, const PeopleView *view,
                    int mouse_x, int mouse_y)
{
    const SDL_Color TITLE = { 200, 175, 110, 255 };
    const SDL_Color HEAD  = { 150, 135,  95, 255 };
    const SDL_Color TEXT  = { 220, 210, 185, 255 };
    const SDL_Color DIM   = { 130, 122, 105, 255 };
    const SDL_Color WARN  = { 225, 150, 110, 255 };
    UiRect          panel;
    int             i;
    char            buf[96];

    if (list->count == 0) return;
    panel = list->items[0].rect;

    {
        UiRect all = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        fill(renderer, all, 0, 0, 0, 160);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    fill(renderer, panel, 35, 28, 18, 255);
    outline(renderer, panel, 120, 100, 60, 255);

    {
        UiRect bar    = { panel.x, panel.y, panel.w, PEOPLE_TITLE_H };
        UiRect stripe = { panel.x, panel.y, 5.0f, PEOPLE_TITLE_H };

        fill(renderer, bar, 55, 44, 28, 255);
        SDL_SetRenderDrawColor(renderer, 120, 100, 60, 255);
        SDL_RenderLine(renderer, panel.x, panel.y + PEOPLE_TITLE_H,
                       panel.x + panel.w, panel.y + PEOPLE_TITLE_H);
        fill(renderer, stripe, view->hue_r, view->hue_g, view->hue_b, 255);

        font_draw_text(renderer, FONT_NORMAL, view->title,
                       (int)(panel.x + 14.0f), (int)(panel.y + 10.0f), TITLE);

        ui_fmt_known(buf, sizeof(buf), view->detail_known, "%d people",
                     view->residents);
        font_draw_text(renderer, FONT_SMALL, buf,
                       (int)(panel.x + panel.w - 150.0f),
                       (int)(panel.y + 14.0f), TEXT);
    }

    if (!view->detail_known) {
        font_draw_text(renderer, FONT_NORMAL, ui_unknown_label(),
                       (int)(panel.x + 20.0f),
                       (int)(panel.y + PEOPLE_TITLE_H + 24.0f), DIM);
    }

    for (i = 1; i < list->count; i++) {
        const UiWidget *w     = &list->items[i];
        int             hover = ui_point_in(w->rect, (float)mouse_x,
                                            (float)mouse_y);

        if (ui_id_group(w->id) == UI_GROUP_FACTOR) {
            if (!view->detail_known) continue;
            if (ui_id_value(w->id) == PEOPLE_FACTOR_TOTAL) {
                if (view->scored)
                    draw_score(renderer, w->rect, (float)w->value / 100.0f);
                else
                    font_draw_text(renderer, FONT_SMALL,
                                   "Nobody lives here to score",
                                   (int)(w->rect.x + 6.0f),
                                   (int)(w->rect.y + 18.0f), DIM);
            } else {
                draw_factor(renderer, w->rect, ui_id_value(w->id),
                            view->scored ? w->value : -1);
            }
            continue;
        }

        if (ui_id_group(w->id) == UI_GROUP_RESIDENT) {
            int               idx = ui_id_value(w->id);
            const PeopleRow  *row;
            UiRect            sparks;
            float             frac;

            if (idx >= view->row_count) continue;
            row = &view->rows[idx];

            if ((idx % 2) == 0) fill(renderer, w->rect, 44, 36, 24, 255);

            font_draw_text(renderer, FONT_SMALL, row->line,
                           (int)(w->rect.x + 6.0f), (int)(w->rect.y + 6.0f),
                           TEXT);

            sparks   = w->rect;
            sparks.x = w->rect.x + w->rect.w - PEOPLE_ROW_BAR_W
                     - PEOPLE_ROW_BAR_PAD;
            draw_sparks(renderer, sparks, &row->wb);

            frac = row->wb.score / WB_SCALE_MAX;
            SDL_snprintf(buf, sizeof(buf), "%.1f", (double)row->wb.score);
            font_draw_text(renderer, FONT_SMALL, buf,
                           (int)(w->rect.x + w->rect.w - 40.0f),
                           (int)(w->rect.y + 6.0f), ladder_colour(frac));
            continue;
        }

        if (w->flags & UI_W_HEADER) {
            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 6.0f),
                           (int)(w->rect.y + 6.0f), HEAD);
            continue;
        }

        {
            int disabled = (w->flags & UI_W_DISABLED) != 0;
            if (disabled)   fill(renderer, w->rect, 42, 38, 30, 255);
            else if (hover) fill(renderer, w->rect, 96, 80, 46, 255);
            else            fill(renderer, w->rect, 68, 56, 34, 255);
            outline(renderer, w->rect, disabled ? 60 : 130,
                    disabled ? 54 : 110, disabled ? 44 : 66, 255);
            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 8.0f), (int)(w->rect.y + 5.0f),
                           disabled ? DIM : TEXT);
        }
    }

    /* The counts the projection does not score but the player has to
     * act on: who has no roof, which roofs are empty, how many more
     * households may still be founded, and who gave up waiting. */
    if (view->detail_known) {
        UiRect line = { panel.x + PEOPLE_MARGIN,
                        panel.y + panel.h - PEOPLE_MARGIN - 30.0f,
                        panel.w, 24.0f };

        SDL_snprintf(buf, sizeof(buf),
                     "%d waiting for a roof   %d homes empty   "
                     "%d households left to found",
                     view->reserve, view->homes_empty,
                     view->founder_allowance);
        font_draw_text(renderer, FONT_SMALL, buf, (int)(line.x + 4.0f),
                       (int)(line.y + 4.0f),
                       view->reserve > 0 ? WARN : HEAD);

        if (view->left_last_month > 0) {
            SDL_snprintf(buf, sizeof(buf), "%d left last month",
                         view->left_last_month);
            font_draw_text(renderer, FONT_SMALL, buf, (int)(line.x + 4.0f),
                           (int)(line.y - 14.0f), WARN);
        }
    }
}
