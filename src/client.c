/*  client.c  --  Per-frame client update (MMO_PLAN Phase 6)
 *
 *  Was game.c's game_update(). It moved out unchanged in behaviour when
 *  the sim became an SDL-free library: this function reads SDL's clock,
 *  asks the renderer to map window pixels to logical ones, and drives
 *  the tick pump — three things the headless sim must not do.
 */

#include "client.h"
#include "net.h"        /* the lockstep tick gate */
#include "render.h"     /* screen_to_iso */
#include "ui.h"         /* HUD_HEIGHT */
#include "building.h"
#include "island.h"
#include "simclock.h"

void client_update(GameState *gs, SDL_Renderer *renderer)
{
    Island *isl = game_cur_island(gs);
    float   lx, ly;

    uint64_t now = SDL_GetTicksNS();
    uint64_t frame_ns;
    float    dt;

    /* game_init() cannot read SDL's clock (it is sim-side code), so the
     * first frame seeds last_tick here. Without this the first frame
     * would measure its delta against 0 — the whole uptime — and spend
     * the accumulator's entire clamped budget in one go. */
    if (gs->last_tick == 0) gs->last_tick = now;

    frame_ns = now - gs->last_tick;
    dt       = (float)frame_ns / 1000000000.0f;
    if (dt > 0.1f) dt = 0.1f;   /* cosmetic clamp for camera/hover only */
    gs->last_tick  = now;
    gs->delta_time = dt;

    if (gs->input.pan_left)  isl->camera.offset_x += CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_right) isl->camera.offset_x -= CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_up)    isl->camera.offset_y += CAMERA_PAN_SPEED * dt;
    if (gs->input.pan_down)  isl->camera.offset_y -= CAMERA_PAN_SPEED * dt;

    /* Zoom toward cursor on mouse wheel scroll. Keeps the tile under
     * the cursor stationary while zooming — the same behaviour as
     * Google Maps.
     *
     * Gated on no overlay being open (UI_PLAN Phase 4). Scrolling over
     * an open modal used to zoom the world behind it, which is
     * README's long-standing "mouse wheel is not overlay-aware" bug:
     * the wheel handler simply never asked. game_topmost_overlay() is
     * now the one place that question is answered. */
    if (gs->input.scroll_y != 0.0f && !game_overlay_open(gs)) {
        float old_zoom = isl->camera.zoom;
        float new_zoom = old_zoom + gs->input.scroll_y * ZOOM_STEP;
        if (new_zoom < ZOOM_MIN) new_zoom = ZOOM_MIN;
        if (new_zoom > ZOOM_MAX) new_zoom = ZOOM_MAX;
        if (new_zoom != old_zoom) {
            float cx    = (float)gs->input.logical_x;
            float cy    = (float)gs->input.logical_y;
            float dx    = cx - isl->camera.offset_x;
            float dy    = cy - isl->camera.offset_y;
            float ratio = new_zoom / old_zoom;
            isl->camera.offset_x = cx - dx * ratio;
            isl->camera.offset_y = cy - dy * ratio;
            isl->camera.zoom     = new_zoom;
        }
    }

    SDL_RenderCoordinatesFromWindow(renderer,
        (float)gs->input.mouse_x, (float)gs->input.mouse_y, &lx, &ly);
    gs->input.logical_x = (int)lx;
    gs->input.logical_y = (int)ly;

    if (gs->input.logical_y < SCREEN_H - HUD_HEIGHT && !game_overlay_open(gs)) {
        screen_to_iso(gs->input.logical_x, gs->input.logical_y,
                      &isl->camera, &gs->hovered_row, &gs->hovered_col);
        if (gs->hovered_row < 0 || gs->hovered_row >= MAP_ROWS ||
            gs->hovered_col < 0 || gs->hovered_col >= MAP_COLS) {
            gs->hovered_row = -1;
            gs->hovered_col = -1;
        }
    } else {
        gs->hovered_row = -1;
        gs->hovered_col = -1;
    }

    /* Road drag-placement: while the button is held and Road is
     * selected, place at each newly-hovered tile as the cursor
     * crosses it (no confirm popup — see game_try_place_road()'s doc
     * comment on why Road is exempt). Reset drag_last_row/col to -1
     * whenever the button isn't held so the next drag's first tile
     * is never skipped as "unchanged". */
    if (!gs->input.left_down) {
        gs->drag_last_row = -1;
        gs->drag_last_col = -1;
    } else if (gs->selected_building == BUILDING_ROAD &&
              !game_overlay_open(gs) &&
              gs->hovered_row >= 0 &&
              (gs->hovered_row != gs->drag_last_row ||
               gs->hovered_col != gs->drag_last_col)) {
        game_try_place_road(gs, gs->hovered_row, gs->hovered_col);
        gs->drag_last_row = gs->hovered_row;
        gs->drag_last_col = gs->hovered_col;
    }

    /* placement_valid reflects only "does this tile structurally
     * work" plus "is this island even settled" — affordability is a
     * per-payment-method question the build-confirmation popup
     * resolves, so the player can always open it and see both options
     * even sitting at 0 Gold. */
    gs->placement_valid  = 0;
    gs->placement_reason = REJ_OK;
    if (isl->settled &&
        gs->selected_building != BUILDING_NONE && gs->hovered_row >= 0) {
        RejectReason why = building_place_check(&isl->map,
            gs->selected_building, gs->hovered_row, gs->hovered_col);
        gs->placement_valid  = (why == REJ_OK);
        gs->placement_reason = (int)why;
    } else if (!isl->settled && gs->selected_building != BUILDING_NONE) {
        /* Looking at an island you have not colonised: the ghost is
         * red either way, but "not your island" is a more useful thing
         * to read than silence. */
        gs->placement_reason = (int)REJ_NOT_OWNER;
    }

    /* Fixed-timestep simulation. Everything above this point is
     * cosmetic and per-frame (camera, hover, the drag-placement input);
     * everything the sim owns advances only here, in whole ticks, so
     * frame rate cannot change the world. Accumulate the real elapsed
     * time and spend it one tick at a time.
     *
     * The accumulator is clamped so a long stall (a breakpoint, a
     * dragged window) spends at most a bounded number of ticks catching
     * up instead of freezing in a spiral; the world simply advances a
     * little less during that stall, which is invisible in single
     * player and is what the future server's continuous ticking exists
     * to make authoritative anyway. */
    /* Time does not pass in the past (MMO_PLAN's scrubber). The
     * accumulator is left alone rather than zeroed, so leaving scrub
     * mode does not spend a backlog of stored-up ticks in one frame. */
    if (game_scrubbing(gs)) return;

    gs->sim_acc_ns += frame_ns;
    if (gs->sim_acc_ns > SIM_TICK_NS * 8)
        gs->sim_acc_ns = SIM_TICK_NS * 8;
    while (gs->sim_acc_ns >= SIM_TICK_NS) {
        /* Lockstep gate (Phase 5): a co-op guest may only simulate
         * ticks the host has authorised — an authorised tick is a
         * complete tick (every command for it has arrived). When the
         * gate closes, real time keeps accumulating (clamped above) and
         * the sim catches up in a burst when authorisation arrives,
         * staying in step rather than drifting. Hosts and offline play
         * are never gated. */
        if (gs->net && !net_tick_allowed(gs->net, gs->sim_tick_no))
            break;
        sim_run_one_tick(gs);
        gs->sim_acc_ns -= SIM_TICK_NS;
    }
}
