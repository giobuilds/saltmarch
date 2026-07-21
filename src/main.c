/*  main.c  --  Anno Clone  (Phase 3 - menu update)
 *  by Giovanni Dick  -  2026
 */

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "game.h"
#include "render.h"
#include "ui.h"
#include "trade_ui.h"  /* Phase 4 */
#include "build_confirm_ui.h" /* fix pass: gold/resource payment choice */
#include "demolish_confirm_ui.h" /* bulldozer confirmation */
#include "tier_upgrade_ui.h" /* production chains: population tier upgrade */
#include "world_ui.h"        /* archipelago overview */
#include "fonts.h"    /* Phase 5 */

typedef struct { SDL_Window *w; SDL_Renderer *r; GameState *g; } App;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_Window   *window   = NULL;
    SDL_Renderer *renderer = NULL;
    GameState    *gs       = NULL;
    App          *app      = NULL;

    (void)argc; (void)argv;

    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING,    "Anno Clone");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, "0.3.0");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING,    "game");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("Anno Clone",
                                     SCREEN_W, SCREEN_H,
                                     SDL_WINDOW_FULLSCREEN,   /* CHANGED */
                                     &window, &renderer)) {
        SDL_Log("Window/renderer failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetRenderLogicalPresentation(renderer, SCREEN_W, SCREEN_H,
                                     SDL_LOGICAL_PRESENTATION_STRETCH); /* CHANGED */

    gs = game_init();
    if (!gs) { SDL_Log("game_init failed"); return SDL_APP_FAILURE; }

    app = (App *)SDL_malloc(sizeof(App));
    if (!app) { game_free(gs); return SDL_APP_FAILURE; }

    app->w = window;
    app->r = renderer;
    app->g = gs;
    *appstate = app;

    /* Phase 5: initialise SDL_ttf font rendering */
    if (!fonts_init())
        SDL_Log("Warning: fonts unavailable, text will not render");

    SDL_Log("Phase 5 ready. ESC or menu Quit button to exit.");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    App *app = (App *)appstate;
    return input_handle_event(&app->g->input, event);
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    App       *app = (App *)appstate;
    GameState *gs  = app->g;
    Island    *isl;

    game_update(gs, app->r);

    /* Everything below acts on the island currently being viewed;
     * game_update() has already simulated every settled one. Fetched
     * after game_update() because a menu action there (New Game /
     * Load) can change which island is current. */
    isl = game_cur_island(gs);

    /* --- Handle clicks ---------------------------------- */
    if (gs->input.left_click) {

        /* Archipelago overview: checked before the confirm popups
         * only in the sense that it cannot coexist with them —
         * opening it is a HUD action, and the confirm popups are all
         * closed by then. */
        if (gs->world_open) {
            int      target = -1;
            WorldHit hit    = world_ui_hit_test(SCREEN_W, SCREEN_H, MAX_ISLANDS,
                                                gs->input.logical_x,
                                                gs->input.logical_y, &target);
            if (hit == WORLD_HIT_ISLAND && target >= 0) {
                game_set_current_island(gs, target);
                isl = game_cur_island(gs);   /* the view just moved */
            } else if (hit == WORLD_HIT_CLOSE) {
                gs->world_open = 0;
            }
            /* WORLD_HIT_NONE: a click on open sea does nothing, so a
             * misclick can't dismiss the map. Close or right-click. */

        /* Tier-upgrade confirmation: same top priority as the other
         * confirm popups — mutually exclusive with them in practice. */
        } else if (gs->tier_upgrade_open) {
            TierUpgradeHit hit = tier_upgrade_ui_hit_test(SCREEN_W, SCREEN_H,
                                                          gs->input.logical_x,
                                                          gs->input.logical_y);
            if (hit == TU_HIT_OK)
                game_upgrade_house(gs, gs->tier_upgrade_idx);
            /* TU_HIT_CANCEL or TU_HIT_NONE (click outside the panel)
             * both dismiss with no upgrade. */
            gs->tier_upgrade_open = 0;

        /* Bulldozer confirmation: highest priority of all — mutually
         * exclusive with build_confirm_open in practice (selecting a
         * building type already clears demolish_mode and vice versa),
         * but checked first regardless. */
        } else if (gs->demolish_confirm_open) {
            DemolishConfirmHit hit = demolish_confirm_ui_hit_test(SCREEN_W, SCREEN_H,
                                                                  gs->input.logical_x,
                                                                  gs->input.logical_y);
            if (hit == DC_HIT_OK)
                game_demolish_building(gs, gs->demolish_confirm_idx);
            /* DC_HIT_CANCEL or DC_HIT_NONE (click outside the panel)
             * both dismiss with no destruction. */
            gs->demolish_confirm_open = 0;

        /* Fix pass: if the build-confirmation popup is open, only its
         * buttons respond (highest priority — mirrors trade_open/
         * menu_open below). */
        } else if (gs->build_confirm_open) {
            BuildConfirmHit hit = build_confirm_ui_hit_test(SCREEN_W, SCREEN_H,
                                                            gs->input.logical_x,
                                                            gs->input.logical_y);
            switch (hit) {
            case BC_HIT_PAY_RESOURCES:
                gs->build_confirm_payment = 0;
                break;
            case BC_HIT_PAY_GOLD:
                gs->build_confirm_payment = 1;
                break;
            case BC_HIT_OK:
                game_place_building_confirmed(gs, gs->build_confirm_payment);
                gs->build_confirm_open = 0;
                break;
            case BC_HIT_CANCEL:
            case BC_HIT_NONE:
                /* Cancel, or a click outside the panel: close with no
                 * placement and no payment deducted. */
                gs->build_confirm_open = 0;
                break;
            }

        /* Phase 4: if the trade screen is open, only its buttons
         * respond (mirrors the menu_open branch below). */
        } else if (gs->trade_open) {
            ResourceType res;
            int          qty;
            TradeHit     hit = trade_ui_hit_test(SCREEN_W, SCREEN_H,
                                                 gs->input.logical_x,
                                                 gs->input.logical_y,
                                                 &res, &qty);
            if (hit == TRADE_HIT_SELL) {
                if (qty < 0) qty = isl->stockpile.amount[res]; /* Sell All */
                game_sell_resource(gs, res, qty);
            } else if (hit == TRADE_HIT_BUY) {
                /* qty < 0 ("Max") is resolved inside game_buy_resource
                 * itself, since it needs both storage headroom and
                 * Gold on hand to know what "max" means. */
                game_buy_resource(gs, res, qty);
            } else {
                /* TRADE_HIT_CLOSE or TRADE_HIT_NONE (click outside
                 * the panel) both dismiss the screen. */
                gs->trade_open = 0;
            }

        /* CHANGED: if menu is open, only menu buttons respond */
        } else if (gs->menu_open) {
            MenuHit hit = ui_menu_hit_test(SCREEN_W, SCREEN_H,
                                           gs->input.logical_x,
                                           gs->input.logical_y);
            switch (hit) {
            case MENU_HIT_QUIT:
                return SDL_APP_SUCCESS;   /* clean exit */

            case MENU_HIT_NEWGAME:
                game_new(gs);
                gs->menu_open = 0;
                break;

            case MENU_HIT_SAVE:
                game_save(gs, SAVE_FILE_PATH);
                gs->menu_open = 0;
                break;

            case MENU_HIT_LOAD:
                game_load(gs, SAVE_FILE_PATH);
                gs->menu_open = 0;
                break;

            case MENU_HIT_NONE:
                /* Click outside buttons — close the menu */
                gs->menu_open = 0;
                break;
            }

        } else {
            /* CHANGED: check cog button first, then demolish button,
             * then HUD slots, then map */
            if (ui_cog_hit_test(SCREEN_W, SCREEN_H,
                                gs->input.logical_x,
                                gs->input.logical_y)) {
                gs->menu_open = 1;
                gs->selected_building = BUILDING_NONE; /* deselect on menu open */
                gs->demolish_mode = 0;

            } else if (ui_world_hit_test(SCREEN_W, SCREEN_H,
                                         gs->input.logical_x,
                                         gs->input.logical_y)) {
                gs->world_open        = 1;
                gs->selected_building = BUILDING_NONE;
                gs->demolish_mode     = 0;

            } else if (ui_demolish_hit_test(SCREEN_W, SCREEN_H,
                                            gs->input.logical_x,
                                            gs->input.logical_y)) {
                gs->demolish_mode = !gs->demolish_mode;
                gs->selected_building = BUILDING_NONE;

            } else {
                BuildingType hud_hit = ui_hit_test(SCREEN_W, SCREEN_H,
                                                   gs->input.logical_x,
                                                   gs->input.logical_y);
                if (hud_hit != BUILDING_NONE) {
                    gs->selected_building =
                        (gs->selected_building == hud_hit)
                        ? BUILDING_NONE : hud_hit;
                    gs->demolish_mode = 0;
                } else if (gs->demolish_mode) {
                    /* Fix pass: clicking a building while the demolish
                     * tool is active opens a confirmation popup rather
                     * than destroying immediately (roads included). */
                    int found = game_find_building_at(gs, gs->hovered_row,
                                                      gs->hovered_col);
                    if (found >= 0) {
                        gs->demolish_confirm_open = 1;
                        gs->demolish_confirm_idx  = found;
                    }
                } else if (gs->selected_building == BUILDING_NONE) {
                    /* Phase 4: nothing selected — clicking a placed,
                     * connected Marketplace opens the trade screen;
                     * production chains: clicking a placed, connected
                     * Farmers' House opens the tier-upgrade popup
                     * instead of doing nothing. */
                    int found = game_find_building_at(gs, gs->hovered_row,
                                                      gs->hovered_col);
                    if (found >= 0 &&
                        isl->buildings[found].type == BUILDING_MARKETPLACE &&
                        isl->buildings[found].connected) {
                        gs->trade_open        = 1;
                        gs->trade_building_idx = found;
                    } else if (found >= 0 &&
                              isl->buildings[found].type == BUILDING_HOUSE &&
                              isl->buildings[found].connected) {
                        gs->tier_upgrade_open = 1;
                        gs->tier_upgrade_idx  = found;
                    }
                } else if (gs->selected_building == BUILDING_ROAD) {
                    /* Roads are exempt from the confirm popup — also
                     * placeable by dragging (game_update()'s per-frame
                     * drag check), and a per-tile confirmation would
                     * make that gesture unusable. A single click
                     * behaves the same way a 1-tile drag does. */
                    game_try_place_road(gs, gs->hovered_row, gs->hovered_col);
                } else if (gs->selected_building != BUILDING_NONE &&
                          gs->hovered_row >= 0 &&
                          building_can_place(&isl->map, gs->selected_building,
                                            gs->hovered_row, gs->hovered_col,
                                            NULL, 0)) {
                    gs->build_confirm_open    = 1;
                    gs->build_confirm_row     = gs->hovered_row;
                    gs->build_confirm_col     = gs->hovered_col;
                    gs->build_confirm_payment = 0;
                }
            }
        }
    }

    /* Right click: close confirm popup, trade screen, or menu if
     * open (highest priority first), else deselect */
    if (gs->input.right_click) {
        if (gs->world_open)
            gs->world_open = 0;
        else if (gs->tier_upgrade_open)
            gs->tier_upgrade_open = 0;       /* cancel, no upgrade */
        else if (gs->demolish_confirm_open)
            gs->demolish_confirm_open = 0;   /* cancel, no destruction */
        else if (gs->build_confirm_open)
            gs->build_confirm_open = 0;
        else if (gs->trade_open)
            gs->trade_open = 0;       /* Phase 4 */
        else if (gs->menu_open)
            gs->menu_open = 0;        /* CHANGED: right click closes menu */
        else {
            gs->selected_building = BUILDING_NONE;
            gs->demolish_mode     = 0;
        }
    }

    input_clear_clicks(&gs->input);

    /* --- Render ---------------------------------------- */
    render_clear(app->r);
    render_map(app->r, &isl->map, &isl->camera);
    render_buildings(app->r, isl->buildings,
                     isl->building_count, &isl->camera);

    /* Phase 5: walking population agents */
    render_agents(app->r, isl->agents, isl->agent_count, &isl->camera);

    if (gs->selected_building != BUILDING_NONE && gs->hovered_row >= 0)
        render_ghost(app->r, &isl->camera,
                     gs->selected_building,
                     gs->hovered_row, gs->hovered_col,
                     gs->placement_valid);

    render_hovered_tile(app->r, &isl->camera,
                        gs->hovered_row, gs->hovered_col);

    /* Phase 4: resource stockpile panel */
    render_resources(app->r, &isl->stockpile);

    /* Phase 5: population counter */
    render_population(app->r,
                      pop_total(isl->pop_data, isl->building_count),
                      SCREEN_W);

    /* CHANGED: pass menu_open flag to ui_draw */
    ui_draw(app->r, SCREEN_W, SCREEN_H,
            gs->selected_building,
            gs->input.logical_x,
            gs->input.logical_y,
            gs->menu_open,
            gs->demolish_mode,
            gs->world_open);

    /* CHANGED: draw menu overlay on top of everything when open */
    if (gs->menu_open)
        ui_menu_draw(app->r, SCREEN_W, SCREEN_H,
                     gs->input.logical_x,
                     gs->input.logical_y);

    /* Phase 4: draw the trade screen on top when open */
    if (gs->trade_open)
        trade_ui_draw(app->r, SCREEN_W, SCREEN_H, &isl->stockpile,
                     gs->input.logical_x, gs->input.logical_y);

    /* Fix pass: draw the build-confirmation popup on top when open */
    if (gs->build_confirm_open)
        build_confirm_ui_draw(app->r, SCREEN_W, SCREEN_H,
                              gs->selected_building, &isl->stockpile,
                              gs->build_confirm_payment,
                              gs->input.logical_x, gs->input.logical_y);

    /* Bulldozer confirmation popup, drawn on top when open */
    if (gs->demolish_confirm_open) {
        BuildingType t = isl->buildings[gs->demolish_confirm_idx].type;
        demolish_confirm_ui_draw(app->r, SCREEN_W, SCREEN_H,
                                 BUILDING_DEFS[t].name,
                                 gs->input.logical_x, gs->input.logical_y);
    }

    /* Tier-upgrade confirmation popup, drawn on top when open */
    if (gs->tier_upgrade_open)
        tier_upgrade_ui_draw(app->r, SCREEN_W, SCREEN_H,
                             TIER_UPGRADE_COST_GOLD,
                             isl->stockpile.amount[RES_GOLD] >= TIER_UPGRADE_COST_GOLD,
                             gs->input.logical_x, gs->input.logical_y);

    /* Archipelago overview on top of everything when open */
    if (gs->world_open)
        world_ui_draw(app->r, SCREEN_W, SCREEN_H,
                      gs->islands, MAX_ISLANDS, gs->current_island,
                      gs->input.logical_x, gs->input.logical_y);

    SDL_RenderPresent(app->r);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    App *app = (App *)appstate;
    (void)result;
    fonts_quit();   /* Phase 5: release SDL_ttf resources */
    if (app) { game_free(app->g); SDL_free(app); }
}
