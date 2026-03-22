/*  main.c  --  A city-builder/economy sim like Anno 1800
 *              built in C and SDL3
 *
 *  by Giovanni Dick
 *  14 Mar 2026
 *
 *  SDL3 "main callbacks" model:
 *    SDL_AppInit     – called once at startup
 *    SDL_AppEvent    – called for every OS/input event
 *    SDL_AppIterate  – called once per frame
 *    SDL_AppQuit     – called once at shutdown
 *
 *  We store all game state in a heap-allocated GameState
 *  and hand a pointer to SDL via the appstate parameter.
 *  This avoids global variables and keeps each callback
 *  self-contained.
 */

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "game.h"
#include "render.h"
#include "ui.h"
#include "fonts.h"    /* Phase 5 */
#include "sprite.h"    /* Phase 6 */

typedef struct { SDL_Window *w; SDL_Renderer *r; GameState *g; } App;

/* ---- SDL_AppInit ---------------------------------------
 * One-time setup: create the window and renderer, then
 * initialise game state and store it in *appstate so the
 * other callbacks can retrieve it.
 * -------------------------------------------------------- */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_Window   *window   = NULL;
    SDL_Renderer *renderer = NULL;
    App          *app      = NULL;
    GameState    *gs       = NULL;

    /* Suppress unused-parameter warning for argv/argc
     * (we don't parse command-line args in Phase 1). */
    (void)argc;
    (void)argv;

    /* --- App metadata ---------------------------------- */
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, "Anno Clone");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, "0.3.0");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING, "com.giovannidick.annoclone");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, "Giovanni Dick");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_COPYRIGHT_STRING, "Copyright (c) 2026 Giovanni Dick");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "game");

    /* --- SDL init -------------------------------------- */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialise SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    /* --- Window & renderer ----------------------------- */
    if (!SDL_CreateWindowAndRenderer("Anno Clone - Phase 3",
                                     SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN,
                                     &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    /* Logical presentation keeps the coordinate system fixed at
     * 1920×1080 even if the OS scales the window. */
    SDL_SetRenderLogicalPresentation(renderer,
                                     SCREEN_W, SCREEN_H,
                                     SDL_LOGICAL_PRESENTATION_STRETCH);

    /* --- Game state ------------------------------------ */
    gs = game_init();
    if (!gs) {
        SDL_Log("Couldn't allocate game state");
        return SDL_APP_FAILURE;
    }

    /* Pack both pointers into appstate.
     * We use a small heap struct so AppQuit can free them. */
    app = (App *)SDL_malloc(sizeof(App));
    if (!app) {
        game_free(gs);
        SDL_Log("Couldn't allocate app struct");
        return SDL_APP_FAILURE;
    }
    app->w = window;
    app->r = renderer;
    app->g = gs;

    *appstate = app;

    /* Phase 5: initialise SDL_ttf font rendering */
    if (!fonts_init())
        SDL_Log("Warning: fonts unavailable, text will not render");

    /* Phase 6: load spritesheets from assets/tiles/ */
    if (!sprites_load(renderer, "assets/tiles"))
        SDL_Log("Warning: sprites unavailable, using coloured fallback");
 
    SDL_Log("Phase 5 ready. ESC or menu Quit button to exit.");
    return SDL_APP_CONTINUE;
}

/* ---- SDL_AppEvent --------------------------------------
 * Forward every event to the input handler.
 * -------------------------------------------------------- */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    App *app = (App *)appstate;

    return input_handle_event(&app->g->input, event);
}

/* ---- SDL_AppIterate ------------------------------------
 * Called every frame.  Order: update → clear → draw → present.
 * -------------------------------------------------------- */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    App *app = (App *)appstate;
    GameState *gs = app->g;
 
    /* --- Update ----------------------------------------- */
    game_update(gs, app->r);
    
    /* --- Handle clicks ---------------------------------- */
 
    /* Left click on HUD → select building type */
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
            case MENU_HIT_SAVE:
                SDL_Log("Stub: %s",
                    hit == MENU_HIT_NEWGAME ? "New Game" : "Save");
                gs->menu_open = 0;        /* close menu after stub action */
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
 
    /* Right click → deselect */
    if (gs->input.right_click) {
        if (gs->menu_open)
            gs->menu_open = 0;        /* CHANGED: right click closes menu */
        else
            gs->selected_building = BUILDING_NONE;
    }
 
    
 
    input_clear_clicks(&gs->input);
 
    /* --- Render ----------------------------------------- */
    render_clear(app->r);
    render_map(app->r, &gs->map, &gs->camera);
    render_buildings(app->r, gs->buildings,
                     gs->building_count, &gs->camera);
 
    /* Ghost: only when a building is selected and cursor on map */
    if (gs->selected_building != BUILDING_NONE && gs->hovered_row >= 0)
        render_ghost(app->r, &gs->camera,
                     gs->selected_building,
                     gs->hovered_row, gs->hovered_col,
                     gs->placement_valid);
 
    /* Hover outline — draw on top of ghost */
    render_hovered_tile(app->r, &gs->camera,
                        gs->hovered_row, gs->hovered_col);
 
    /* CHANGED Phase 4: draw resource stockpile panel */
    render_resources(app->r, &gs->stockpile);

    /* Phase 5: population counter */
    render_population(app->r,
                      pop_total(gs->pop_data, gs->building_count),
                      SCREEN_W);

    /* HUD on top of everything */
    ui_draw(app->r, SCREEN_W, SCREEN_H, gs->selected_building, 
            gs->input.logical_x, gs->input.logical_y, gs->menu_open);
 
    /* CHANGED: draw menu overlay on top of everything when open */
    if (gs->menu_open)
        ui_menu_draw(app->r, SCREEN_W, SCREEN_H,
                     gs->input.logical_x,
                     gs->input.logical_y);

    SDL_RenderPresent(app->r);
    return SDL_APP_CONTINUE;
}

/* ---- SDL_AppQuit ---------------------------------------
 * Free everything we allocated.  SDL cleans up the window
 * and renderer automatically after this returns.
 * -------------------------------------------------------- */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    App *app = (App *)appstate;

    (void)result;   /* not checking exit code in Phase 1 */

    sprites_free(); /* Phase 6: release sprite textures */
    fonts_quit();   /* Phase 5: release SDL_ttf resources */

    if (app) {
        game_free(app->g);
        SDL_free(app);
    }
}
