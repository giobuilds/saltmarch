/*  confirm_ui.c  --  Painting the confirmation (UI_PLAN Phase 6)  */

#include "confirm_ui.h"
#include "fonts.h"

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

void confirm_ui_draw(SDL_Renderer *renderer, int screen_w, int screen_h,
                     const UiList *list, const ConfirmView *view,
                     int mouse_x, int mouse_y)
{
    const SDL_Color TITLE   = { 205, 180, 115, 255 };
    const SDL_Color TEXT    = { 220, 210, 185, 255 };
    const SDL_Color DIM     = { 135, 128, 112, 255 };
    const SDL_Color PREVIEW = { 120, 150, 160, 255 };
    const SDL_Color DANGER  = { 240, 140, 120, 255 };
    const SDL_Color HAVE    = { 140, 205, 140, 255 };
    UiRect          panel;
    float           y;
    int             i;
    char            buf[80];

    if (list->count == 0) return;
    panel = list->items[0].rect;

    {
        UiRect all = { 0.0f, 0.0f, (float)screen_w, (float)screen_h };
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        fill(renderer, all, 0, 0, 0, 170);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    fill(renderer, panel, 35, 28, 18, 255);
    outline(renderer, panel, view->destructive ? 150 : 120,
            view->destructive ? 80 : 100, view->destructive ? 60 : 60, 255);

    /* Title bar, with the island's colour (Phase 5) — a confirmation
     * always belongs to somewhere. */
    {
        UiRect bar    = { panel.x, panel.y, panel.w, CONFIRM_TITLE_H };
        UiRect stripe = { panel.x, panel.y, 5.0f, CONFIRM_TITLE_H };
        fill(renderer, bar, 55, 44, 28, 255);
        fill(renderer, stripe, view->hue_r, view->hue_g, view->hue_b, 255);
        font_draw_text(renderer, FONT_NORMAL, view->title,
                       (int)(panel.x + 14.0f), (int)(panel.y + 9.0f), TITLE);
    }

    y = panel.y + CONFIRM_TITLE_H + 8.0f;
    for (i = 0; i < view->line_count; i++) {
        font_draw_text(renderer, FONT_SMALL, view->lines[i],
                       (int)(panel.x + CONFIRM_MARGIN), (int)y, TEXT);
        y += CONFIRM_LINE_H;
    }

    for (i = 1; i < list->count; i++) {
        const UiWidget *w        = &list->items[i];
        int             hover    = ui_point_in(w->rect, (float)mouse_x,
                                               (float)mouse_y);
        int             selected = (w->flags & UI_W_SELECTED) != 0;
        int             muted    = (w->flags & UI_W_MUTED) != 0;

        /* A needs row: a tick or a cross, then the good. Never a
         * button — the flag that keeps it out of the hit-test is the
         * same one that says so here. */
        if (w->flags & UI_W_HEADER) {
            int met = w->value;
            font_draw_text(renderer, FONT_SMALL, met ? "+" : "-",
                           (int)(w->rect.x + 10.0f), (int)(w->rect.y + 2.0f),
                           met ? HAVE : DANGER);
            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 26.0f), (int)(w->rect.y + 2.0f),
                           met ? TEXT : DIM);
            continue;
        }

        /* Payment options carry the command they would submit. */
        if (ui_id_group(w->id) == UI_GROUP_RESOURCE) {
            int opt = w->value;

            fill(renderer, w->rect, selected ? 62 : (hover ? 52 : 42),
                 selected ? 52 : (hover ? 44 : 36),
                 selected ? 32 : (hover ? 30 : 24), 255);
            outline(renderer, w->rect, selected ? 230 : 80,
                    selected ? 195 : 72, selected ? 110 : 54, 255);

            font_draw_text(renderer, FONT_SMALL, w->label,
                           (int)(w->rect.x + 10.0f), (int)(w->rect.y + 8.0f),
                           muted ? DIM : TEXT);

            if (muted)
                font_draw_text(renderer, FONT_SMALL,
                               ui_reject_text((RejectReason)w->reason),
                               (int)(w->rect.x + w->rect.w - 130.0f),
                               (int)(w->rect.y + 8.0f), DANGER);

            /* The command itself, under its option. Small and dim: it
             * is for the curious and for screenshots, not the sentence
             * a player reads to decide. */
            if (opt >= 0 && opt < 2)
                font_draw_text(renderer, FONT_SMALL, view->options[opt].preview,
                               (int)(w->rect.x + 10.0f),
                               (int)(w->rect.y + w->rect.h + 3.0f), PREVIEW);
            continue;
        }

        {
            int accept = ui_id_value(w->id) == UI_ACTION_ACCEPT;
            int danger = accept && view->destructive;

            fill(renderer, w->rect, danger ? (hover ? 130 : 95) : (hover ? 96 : 68),
                 danger ? (hover ? 45 : 32) : (hover ? 80 : 56),
                 danger ? (hover ? 38 : 26) : (hover ? 46 : 34), 255);
            outline(renderer, w->rect, danger ? 220 : 130,
                    danger ? 110 : 110, danger ? 90 : 66, 255);
            font_draw_text(renderer, FONT_NORMAL, w->label,
                           (int)(w->rect.x + 16.0f), (int)(w->rect.y + 7.0f),
                           danger ? DANGER : TEXT);
        }
    }

    /* When it lands. Commands apply at a tick boundary, and under
     * lockstep several ticks later — saying so makes the delay a stated
     * rule rather than a suspicion that the click was eaten. */
    SDL_snprintf(buf, sizeof(buf), "applies at tick %llu or later",
                 (unsigned long long)view->apply_tick);
    font_draw_text(renderer, FONT_SMALL, buf,
                   (int)(panel.x + CONFIRM_MARGIN),
                   (int)(panel.y + panel.h - 20.0f), DIM);
}
