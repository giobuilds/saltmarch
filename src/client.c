/* client.c  --  Per-frame client update (MMO_PLAN Phase 6) */

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
     * Google Maps. */
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

    /* Road drag-placement: while the button is held and Road. */
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

    /* placement_valid reflects only "does this tile structurally */
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

    /* Fixed-timestep simulation. Everything above this point. */
    /* Time does not pass in the past (MMO_PLAN's scrubber). The
     * accumulator is left alone rather than zeroed, so leaving scrub
     * mode does not spend a backlog of stored-up ticks in one frame. */
    if (game_scrubbing(gs)) return;

    /* Who is computing the world (SERVER_AUTHORITY.md Phase 2). Set */
    gs->predict_only = net_server_authoritative(gs->net)
                     ? gs->local_player_id : 0u;

    gs->sim_acc_ns += frame_ns;
    if (gs->sim_acc_ns > SIM_TICK_NS * 8)
        gs->sim_acc_ns = SIM_TICK_NS * 8;
    while (gs->sim_acc_ns >= SIM_TICK_NS) {
        /* Lockstep gate (Phase 5): a co-op guest may only simulate */
        if (gs->net && !net_tick_allowed(gs->net, gs->sim_tick_no))
            break;
        sim_run_one_tick(gs);
        /* Per COMPLETED tick, not per frame: the desync hash has to be
         * taken while the world is AT the boundary, and a frame that
         * runs two ticks would otherwise step over one. */
        net_on_tick(gs->net, gs);
        gs->sim_acc_ns -= SIM_TICK_NS;
    }
}
