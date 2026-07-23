/*  main.c  --  Saltmarch
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
#include "ship.h"
#include "fonts.h"    /* Phase 5 */
#include "feed.h"     /* MMO Phase 4: shared voyage feed */

/* Feed lives here, beside the window — NOT in GameState. It is client
 * chrome: ghosts never enter sim_hash, and the CLI record/replay path
 * never constructs one (see feed.h's cosmetic-boundary note). */
typedef struct { SDL_Window *w; SDL_Renderer *r; GameState *g; Feed feed; } App;

/* Wall-clock unix milliseconds, for feed timestamps and ghost lerp. */
static uint64_t wall_unix_ms(void)
{
    SDL_Time t = 0;
    if (!SDL_GetCurrentTime(&t)) return 0;
    return (uint64_t)t / 1000000ULL;
}

/* ---- Headless CLI: record / replay (MMO_PLAN Phase 1d) ----
 * A deterministic scripted session used by --record to produce a .smlog
 * fixture. It touches the float-sensitive paths on purpose (a house, so
 * population and agents run; a voyage, so ship progress accumulates), so
 * that replaying it is a meaningful determinism check rather than a
 * trivial one. */
static void record_demo_session(GameState *gs, Uint32 seed)
{
    Island *isl;
    int     r, c, t, placed = 0;

    game_new_seeded(gs, seed);
    isl = game_cur_island(gs);

    for (r = 0; r < MAP_ROWS && !placed; r++)
        for (c = 0; c < MAP_COLS && !placed; c++)
            if (building_can_place(&isl->map, BUILDING_HOUSE, r, c, NULL, 0)) {
                gs->selected_building = BUILDING_HOUSE;
                gs->build_confirm_row = r;
                gs->build_confirm_col = c;
                game_place_building_confirmed(gs, 0);
                placed = 1;
            }
    gs->selected_building = BUILDING_NONE;

    game_buy_resource(gs, (ResourceType)0, 8);
    game_build_ship(gs);
    game_ship_transfer(gs, 0, (ResourceType)0, 5);
    game_ship_depart(gs, 0, 1);

    for (t = 0; t < 500; t++)
        sim_run_one_tick(gs);
}

/* Handle --record / --replay. Returns 1 if a CLI mode ran (with the
 * process result in *out), 0 to fall through to the normal game. */
static int run_cli_mode(int argc, char *argv[], SDL_AppResult *out)
{
    const char *replay_file = NULL, *record_file = NULL, *expect = NULL;
    Uint32      seed = 1u;
    GameState  *gs;
    SDL_AppResult res = SDL_APP_SUCCESS;
    int         i;

    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--replay") == 0 && i + 1 < argc)
            replay_file = argv[++i];
        else if (SDL_strcmp(argv[i], "--record") == 0 && i + 1 < argc)
            record_file = argv[++i];
        else if (SDL_strcmp(argv[i], "--expect-hash") == 0 && i + 1 < argc)
            expect = argv[++i];
        else if (SDL_strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = (Uint32)SDL_strtoul(argv[++i], NULL, 10);
    }
    if (!replay_file && !record_file) return 0;

    SDL_Init(0);   /* base only — no video/window for headless CLI */

    gs = game_init();
    if (!gs) { SDL_Log("cli: game_init failed"); *out = SDL_APP_FAILURE;
               SDL_Quit(); return 1; }

    if (record_file) {
        record_demo_session(gs, seed);
        if (!game_save(gs, record_file)) res = SDL_APP_FAILURE;
        else SDL_Log("record: %s seed=%u tick=%llu hash=%016llx",
                     record_file, seed, (unsigned long long)gs->sim_tick_no,
                     (unsigned long long)sim_hash(gs));
    } else {
        if (!game_load(gs, replay_file)) {
            res = SDL_APP_FAILURE;
        } else {
            uint64_t h = sim_hash(gs);
            SDL_Log("replay: %s tick=%llu hash=%016llx", replay_file,
                    (unsigned long long)gs->sim_tick_no, (unsigned long long)h);

            /* Self-check: rebuild the world a SECOND time from seed+log
             * and confirm it lands on the same hash. This makes plain
             * `--replay <file>` a determinism gate needing no expected
             * hash — the form CI runs on every platform. */
            if (!game_verify_determinism(gs)) {
                SDL_Log("replay SELF-CHECK FAILED: world is nondeterministic");
                res = SDL_APP_FAILURE;
            }

            /* Optional pin to a known hash (e.g. a committed fixture's
             * cross-platform value). */
            if (res == SDL_APP_SUCCESS && expect) {
                uint64_t want = (uint64_t)SDL_strtoull(expect, NULL, 16);
                if (want != h) {
                    SDL_Log("replay MISMATCH: expected %016llx got %016llx",
                            (unsigned long long)want, (unsigned long long)h);
                    res = SDL_APP_FAILURE;
                } else {
                    SDL_Log("replay OK: hash matches");
                }
            }
        }
    }

    game_free(gs);
    SDL_Quit();
    *out = res;
    return 1;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_Window   *window   = NULL;
    SDL_Renderer *renderer = NULL;
    GameState    *gs       = NULL;
    App          *app      = NULL;
    SDL_AppResult cli_result;

    *appstate = NULL;   /* defined for the CLI and failure paths */

    /* Headless record/replay short-circuits before any window exists. */
    if (run_cli_mode(argc, argv, &cli_result))
        return cli_result;

    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING,    "Saltmarch");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, "0.3.0");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING,    "game");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_CreateWindowAndRenderer("Saltmarch",
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

    /* Display name for the shared feed: SALTMARCH_PLAYER, or a default.
     * Cosmetic identity only — sim player_id stays 0 until Phase 5. */
    feed_init(&app->feed, SDL_getenv("SALTMARCH_PLAYER"));

    *appstate = app;

    /* A missing font is not cosmetic: every resource count, price and
     * menu label is text, so without it the game renders but cannot be
     * played. We still start (so the map is at least inspectable) but
     * log at ERROR severity, and the CI smoke test asserts the
     * "Fonts loaded:" line — otherwise this fails silently and green,
     * which is exactly how it went unnoticed that the font path only
     * ever existed on Fedora. */
    if (!fonts_init())
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Fonts unavailable — no text will render, and the game "
                     "is not usable in this state. See BUILD.md.");

    SDL_Log("Ready. ESC or the menu's Quit button to exit.");
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

    /* Shared feed (Phase 4): publish any departures the ticks above
     * just caused, and re-read the inbound feed on its poll interval.
     * Wall-clock, cosmetic, outside the sim — see feed.h. */
    feed_track_departures(&app->feed, gs->ships, gs->ship_count,
                          wall_unix_ms());
    feed_poll(&app->feed, SDL_GetTicksNS());

    /* Everything below acts on the island currently being viewed;
     * game_update() has already simulated every settled one. Fetched
     * after game_update() because a menu action there (New Game /
     * Load) can change which island is current. */
    isl = game_cur_island(gs);

    /* F9: determinism self-check (Phase 1c). Rebuilds the world from the
     * seed + command log and compares; the result is shown briefly by
     * the render block below. */
    if (gs->input.replay_check) {
        game_verify_determinism(gs);
        gs->replay_show_until_ns = SDL_GetTicksNS() + 5000000000ULL;
    }

    /* F10: toggle the market debug overlay. */
    if (gs->input.faction_debug_toggle)
        gs->faction_debug = !gs->faction_debug;

    /* --- Handle clicks ---------------------------------- */
    if (gs->input.left_click) {

        /* Archipelago overview: checked before the confirm popups
         * only in the sense that it cannot coexist with them —
         * opening it is a HUD action, and the confirm popups are all
         * closed by then. */
        if (gs->world_open) {
            int          target = -1, tship = -1;
            ResourceType tres   = RES_WOOD;
            WorldHit     hit    = world_ui_hit_test(SCREEN_W, SCREEN_H, MAX_ISLANDS,
                                                gs->ships, gs->ship_count,
                                                gs->world_selected_ship,
                                                gs->input.logical_x,
                                                gs->input.logical_y,
                                                &target, &tship, &tres);
            switch (hit) {
            case WORLD_HIT_SHIP:
                /* Click a ship to select it, again to deselect. */
                gs->world_selected_ship =
                    (gs->world_selected_ship == tship) ? -1 : tship;
                break;

            case WORLD_HIT_ISLAND:
                if (gs->world_selected_ship >= 0) {
                    /* A ship is selected, so an island click is an
                     * order to sail there rather than a view change —
                     * the same select-then-click grammar the HUD uses
                     * for placing buildings. Routed through the command
                     * funnel like every other mutation (Phase 1a); the
                     * depart's own validation handles "not docked" and
                     * "already there". */
                    game_ship_depart(gs, gs->world_selected_ship, target);
                } else if (target >= 0) {
                    game_set_current_island(gs, target);
                    isl = game_cur_island(gs);   /* the view just moved */
                }
                break;

            case WORLD_HIT_LOAD:
                game_ship_transfer(gs, gs->world_selected_ship, tres, 10);
                break;

            case WORLD_HIT_UNLOAD:
                game_ship_transfer(gs, gs->world_selected_ship, tres, -10);
                break;

            case WORLD_HIT_COLONISE:
                if (gs->world_selected_ship >= 0) {
                    int at = gs->ships[gs->world_selected_ship].at_island;
                    game_colonise(gs, gs->world_selected_ship, at);
                    /* Colonisation applies at the next tick boundary, so
                     * its result is not known here. Optimistically show
                     * the target island: the world map only offers this
                     * action for a ship docked at an unsettled island
                     * with the founding gold aboard, and nothing can
                     * change that before the next tick. */
                    if (at >= 0) {
                        game_set_current_island(gs, at);
                        isl = game_cur_island(gs);
                    }
                }
                break;

            case WORLD_HIT_ROUTE_OUT:
            case WORLD_HIT_ROUTE_BACK:
                /* Cycle the carried good for one route leg. One button
                 * covers the whole choice (through every good and back
                 * to "nothing"); the cycle itself lives in the sim so
                 * it is recorded like every other mutation (Phase 1b). */
                if (gs->world_selected_ship >= 0)
                    game_ship_set_route_res(gs, gs->world_selected_ship,
                                            hit == WORLD_HIT_ROUTE_OUT ? 0 : 1);
                break;

            case WORLD_HIT_ROUTE_TOGGLE:
                if (gs->world_selected_ship >= 0)
                    game_ship_toggle_route(gs, gs->world_selected_ship);
                break;

            case WORLD_HIT_CLOSE:
                gs->world_open = 0;
                break;

            case WORLD_HIT_NONE:
            default:
                /* A click on open sea does nothing, so a misclick
                 * can't dismiss the map. Close or right-click. */
                break;
            }

        /* Ship-build confirmation, from clicking a Shipyard. */
        } else if (gs->ship_build_open) {
            TierUpgradeHit hit = tier_upgrade_ui_hit_test(SCREEN_W, SCREEN_H,
                                                          gs->input.logical_x,
                                                          gs->input.logical_y);
            if (hit == TU_HIT_OK)
                game_build_ship(gs);
            gs->ship_build_open = 0;

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
                    /* Nothing selected, so a map click means "interact
                     * with whatever building is here". Every case needs
                     * the building to be road-connected, so that check
                     * is hoisted out of the switch rather than repeated
                     * per branch. */
                    int found = game_find_building_at(gs, gs->hovered_row,
                                                      gs->hovered_col);
                    if (found >= 0 && isl->buildings[found].connected) {
                        switch (isl->buildings[found].type) {
                        case BUILDING_MARKETPLACE:
                            gs->trade_open         = 1;
                            gs->trade_building_idx = found;
                            break;
                        case BUILDING_HOUSE:
                            gs->tier_upgrade_open = 1;
                            gs->tier_upgrade_idx  = found;
                            break;
                        case BUILDING_SHIPYARD:
                            gs->ship_build_open = 1;
                            gs->ship_build_idx  = found;
                            break;
                        default:
                            break;   /* not an interactive building */
                        }
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
        else if (gs->ship_build_open)
            gs->ship_build_open = 0;
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
        trade_ui_draw(app->r, SCREEN_W, SCREEN_H, &isl->stockpile, &gs->faction,
                     gs->input.logical_x, gs->input.logical_y);

    /* Fix pass: draw the build-confirmation popup on top when open */
    if (gs->build_confirm_open)
        build_confirm_ui_draw(app->r, SCREEN_W, SCREEN_H,
                              gs->selected_building, &isl->stockpile, &gs->faction,
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
                             "Upgrade to Worker's House?",
                             "Workers also need Beer.",
                             TIER_UPGRADE_COST_GOLD,
                             isl->stockpile.amount[RES_GOLD] >= TIER_UPGRADE_COST_GOLD,
                             gs->input.logical_x, gs->input.logical_y);

    /* Ship-build confirmation reuses the tier-upgrade popup's shape:
     * both are "spend Gold to change this building's situation",
     * so a second near-identical *_ui file would be pure duplication. */
    if (gs->ship_build_open)
        tier_upgrade_ui_draw(app->r, SCREEN_W, SCREEN_H,
                             "Build a Ship?",
                             "Lay down a ship at this Shipyard.",
                             SHIP_BUILD_COST_GOLD,
                             isl->stockpile.amount[RES_GOLD] >= SHIP_BUILD_COST_GOLD,
                             gs->input.logical_x, gs->input.logical_y);

    /* Archipelago overview on top of everything when open */
    if (gs->world_open)
        world_ui_draw(app->r, SCREEN_W, SCREEN_H,
                      gs->islands, MAX_ISLANDS, gs->current_island,
                      gs->ships, gs->ship_count, gs->world_selected_ship,
                      app->feed.ghosts, app->feed.ghost_count, wall_unix_ms(),
                      gs->input.logical_x, gs->input.logical_y);

    /* F10 market debug overlay: the economy test harness. Shows the
     * faction's gold and, per tradeable good, its inventory and live
     * bid/ask — watch these move as you trade or wait. */
    if (gs->faction_debug) {
        const Faction *fac = &gs->faction;
        int       x = SCREEN_W - 300, y = 60, line = 18, r;
        SDL_FRect bg = { (float)(x - 10), (float)(y - 8),
                         290.0f, (float)(line * (RES_GOLD + 2) + 12) };
        SDL_Color hdr = { 210, 190, 120, 255 };
        SDL_Color txt = { 200, 200, 200, 255 };
        char buf[96];

        SDL_SetRenderDrawBlendMode(app->r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(app->r, 0, 0, 0, 190);
        SDL_RenderFillRect(app->r, &bg);
        SDL_SetRenderDrawBlendMode(app->r, SDL_BLENDMODE_NONE);

        SDL_snprintf(buf, sizeof(buf), "MARKET (F10)   gold %d", fac->gold);
        font_draw_text(app->r, FONT_SMALL, buf, x, y, hdr);
        y += line;
        for (r = 0; r < (int)RES_GOLD; r++) {
            SDL_snprintf(buf, sizeof(buf), "%-6s inv %4d  bid %3d  ask %3d",
                         RESOURCE_NAMES[r], fac->inventory[r],
                         faction_bid(fac, (ResourceType)r),
                         faction_ask(fac, (ResourceType)r));
            font_draw_text(app->r, FONT_SMALL, buf, x, y, txt);
            y += line;
        }
    }

    /* F9 determinism result, shown top-centre for a few seconds. */
    if (gs->replay_state != 0 &&
        SDL_GetTicksNS() < gs->replay_show_until_ns) {
        char      msg[160];
        SDL_Color col;
        switch (gs->replay_state) {
        case 1:
            SDL_snprintf(msg, sizeof(msg),
                "REPLAY OK  tick %llu  hash %016llx",
                (unsigned long long)gs->replay_tick,
                (unsigned long long)gs->replay_live_hash);
            col = (SDL_Color){ 90, 200, 90, 255 };
            break;
        case 2:
            SDL_snprintf(msg, sizeof(msg),
                "REPLAY DESYNC @ tick %llu  live %016llx  replay %016llx",
                (unsigned long long)gs->replay_tick,
                (unsigned long long)gs->replay_live_hash,
                (unsigned long long)gs->replay_replay_hash);
            col = (SDL_Color){ 230, 70, 70, 255 };
            break;
        default:
            SDL_snprintf(msg, sizeof(msg), "REPLAY N/A (loaded save)");
            col = (SDL_Color){ 170, 170, 170, 255 };
            break;
        }
        font_draw_text(app->r, FONT_NORMAL, msg, SCREEN_W / 2 - 300, 8, col);
    }

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
