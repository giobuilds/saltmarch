/*  main.c  --  Anno Clone  (Phase 3 - menu update)
 *  by Giovanni Dick  -  2026
 */

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "game.h"
#include "render.h"
#include "ui.h"
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

    game_update(gs, app->r);

    /* --- Handle clicks ---------------------------------- */
    if (gs->input.left_click) {

        /* CHANGED: if menu is open, only menu buttons respond */
        if (gs->menu_open) {
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
            /* CHANGED: check cog button first, then HUD slots, then map */
            if (ui_cog_hit_test(SCREEN_W, SCREEN_H,
                                gs->input.logical_x,
                                gs->input.logical_y)) {
                gs->menu_open = 1;
                gs->selected_building = BUILDING_NONE; /* deselect on menu open */

            } else {
                BuildingType hud_hit = ui_hit_test(SCREEN_W, SCREEN_H,
                                                   gs->input.logical_x,
                                                   gs->input.logical_y);
                if (hud_hit != BUILDING_NONE) {
                    gs->selected_building =
                        (gs->selected_building == hud_hit)
                        ? BUILDING_NONE : hud_hit;
                } else {
                    game_place_building(gs);
                }
            }
        }
    }

    /* Right click: close menu if open, else deselect building */
    if (gs->input.right_click) {
        if (gs->menu_open)
            gs->menu_open = 0;        /* CHANGED: right click closes menu */
        else
            gs->selected_building = BUILDING_NONE;
    }

    input_clear_clicks(&gs->input);

    /* --- Render ---------------------------------------- */
    render_clear(app->r);
    render_map(app->r, &gs->map, &gs->camera);
    render_buildings(app->r, gs->buildings,
                     gs->building_count, &gs->camera);

    if (gs->selected_building != BUILDING_NONE && gs->hovered_row >= 0)
        render_ghost(app->r, &gs->camera,
                     gs->selected_building,
                     gs->hovered_row, gs->hovered_col,
                     gs->placement_valid);

    render_hovered_tile(app->r, &gs->camera,
                        gs->hovered_row, gs->hovered_col);

    /* Phase 4: resource stockpile panel */
    render_resources(app->r, &gs->stockpile);

    /* Phase 5: population counter */
    render_population(app->r,
                      pop_total(gs->pop_data, gs->building_count),
                      SCREEN_W);

    /* CHANGED: pass menu_open flag to ui_draw */
    ui_draw(app->r, SCREEN_W, SCREEN_H,
            gs->selected_building,
            gs->input.logical_x,
            gs->input.logical_y,
            gs->menu_open);

    /* CHANGED: draw menu overlay on top of everything when open */
    if (gs->menu_open)
        ui_menu_draw(app->r, SCREEN_W, SCREEN_H,
                     gs->input.logical_x,
                     gs->input.logical_y);

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
