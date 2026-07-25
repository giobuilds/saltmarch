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
#include "world_ui.h"        /* archipelago overview */
#include "ship.h"
#include "fonts.h"    /* Phase 5 */
#include "feed.h"     /* MMO Phase 4: shared voyage feed */
#include "net.h"      /* MMO Phase 5: lockstep co-op */
#include "escrow_ui.h" /* MMO Phase 5: harbor escrow panel */
#include "inventory_ui.h"  /* UI_PLAN Phase 4: stores + vitals */
#include "confirm_ui.h"    /* UI_PLAN Phase 6: the one confirmation */
#include "client.h"   /* MMO Phase 6: the client half of the frame */
#include "ui_kit.h"       /* UI_PLAN Phase 0: widget kit, reject text   */
#include "ui_snapshot.h"  /* UI_PLAN Phase 0: what the UI may see       */
#include "exchange_view.h"/* UI_PLAN Phase 1: the exchange surface      */
#include "replay.h"   /* MMO Phase 6: the headless record/replay harness */

/* Feed and NetSession live here, beside the window — NOT in GameState.
 * They are client chrome: ghosts never enter sim_hash, the net session
 * is referenced from GameState only as an opaque routing pointer, and
 * the CLI record/replay path constructs neither. */
typedef struct {
    SDL_Window   *w;
    SDL_Renderer *r;
    GameState    *g;
    Feed          feed;
    NetSession   *net;   /* NULL when playing offline */

    /* UI_PLAN Phase 0/1. The snapshot is rebuilt once per frame after
     * the tick loop; overlays read it instead of GameState. `ui` is
     * view state (which page you are on), never world state. The
     * exchange view and its widget list are rebuilt each frame so the
     * list that is hit-tested is the list that was drawn. */
    UiSnapshot    snap;
    UiState       ui;
    ExchangeView  exchange;
    UiList        exchange_list;
    HudView       hud;
    UiList        hud_list;
    InventoryView inventory;
    UiList        inventory_list;
    VitalsView    vitals;
    UiList        island_list;
    ConfirmView   confirm;
    UiList        confirm_list;
} App;

/* Wall-clock unix milliseconds, for feed timestamps and ghost lerp. */
static uint64_t wall_unix_ms(void)
{
    SDL_Time t = 0;
    if (!SDL_GetCurrentTime(&t)) return 0;
    return (uint64_t)t / 1000000ULL;
}

/* ---- Headless CLI: record / replay (MMO_PLAN Phase 1d) ----
 * The harness itself lives in the sim library (replay.c) so the
 * standalone saltmarch_replay tool and this binary run identical code;
 * all that is left here is the short-circuit before a window exists. */
static int run_cli_mode(int argc, char *argv[], SDL_AppResult *out)
{
    if (!replay_cli_requested(argc, argv)) return 0;

    *out = replay_cli_run(argc, argv) == 0 ? SDL_APP_SUCCESS
                                           : SDL_APP_FAILURE;
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

    /* App is SDL_malloc'd, so the UI blocks start as garbage. Zero them
     * before the first frame: a stray page index would only be clamped,
     * but a widget list with a nonsense count would be read as real. */
    SDL_memset(&app->snap,          0, sizeof(app->snap));
    SDL_memset(&app->ui,            0, sizeof(app->ui));
    SDL_memset(&app->exchange,      0, sizeof(app->exchange));
    SDL_memset(&app->exchange_list, 0, sizeof(app->exchange_list));
    SDL_memset(&app->hud,           0, sizeof(app->hud));
    SDL_memset(&app->hud_list,      0, sizeof(app->hud_list));
    SDL_memset(&app->inventory,     0, sizeof(app->inventory));
    SDL_memset(&app->inventory_list,0, sizeof(app->inventory_list));
    SDL_memset(&app->vitals,        0, sizeof(app->vitals));
    SDL_memset(&app->island_list,   0, sizeof(app->island_list));
    SDL_memset(&app->confirm,       0, sizeof(app->confirm));
    SDL_memset(&app->confirm_list,  0, sizeof(app->confirm_list));
    app->ui.hud_category = BCAT_GATHERING;

    /* Display name for the shared feed: SALTMARCH_PLAYER, or a default.
     * Cosmetic identity only — the sim's player_id comes from the co-op
     * session (or defaults to player 1 offline). */
    feed_init(&app->feed, SDL_getenv("SALTMARCH_PLAYER"));

    /* Co-op (Phase 5): --host [port] listens for players; --join
     * host[:port] connects to a host or to saltmarch_host, the dedicated
     * server (Phase 6) — the client cannot tell the two apart, which is
     * the point. --as N asks to resume the identity a previous session
     * was given, so your island is still yours after a reconnect; the id
     * to pass is the one the join logged and the HUD shows. The session
     * lives in App; gs->net is the routing pointer command_submit and
     * the tick gate consult. */
    app->net = NULL;
    {
        int      i;
        uint32_t resume_id = PLAYER_NONE;
        char     hostbuf[128] = {0};
        uint16_t join_port = NET_DEFAULT_PORT;
        int      want_join = 0;

        /* Two passes: --as may appear on either side of --join. */
        for (i = 1; i < argc; i++)
            if (SDL_strcmp(argv[i], "--as") == 0 && i + 1 < argc)
                resume_id = (uint32_t)SDL_strtoul(argv[++i], NULL, 10);

        for (i = 1; i < argc; i++) {
            if (SDL_strcmp(argv[i], "--host") == 0) {
                uint16_t port = NET_DEFAULT_PORT;
                if (i + 1 < argc && argv[i + 1][0] != '-')
                    port = (uint16_t)SDL_strtoul(argv[++i], NULL, 10);
                app->net = net_host(port);
            } else if (SDL_strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
                char *colon;
                SDL_strlcpy(hostbuf, argv[++i], sizeof(hostbuf));
                colon = SDL_strchr(hostbuf, ':');
                if (colon) {
                    *colon = '\0';
                    join_port = (uint16_t)SDL_strtoul(colon + 1, NULL, 10);
                }
                want_join = 1;
            }
        }
        if (want_join)
            app->net = net_join(hostbuf, join_port, resume_id);
        if (app->net) net_attach(gs, app->net);
    }

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

    /* Co-op session (Phase 5): drain the socket BEFORE the sim runs so
     * this frame's ticks see every command and authorisation that has
     * arrived; a dead session tears down to single-player continuation. */
    if (app->net) {
        if (!net_pump(app->net, gs)) {
            net_close(app->net);
            app->net = NULL;
            net_detach(gs);
        }
    }

    client_update(gs, app->r);

    if (app->net)
        net_after_update(app->net, gs);

    /* UI_PLAN Phase 0: take the picture the UI will work from. After
     * the tick loop, so every overlay in this frame sees one tick and
     * none of them can observe the world mid-tick. */
    ui_snapshot_build(&app->snap, gs);

    /* The half of the health readings the sim cannot know: how stale
     * the shared feed is, and whether anyone is connected. */
    app->snap.health.feed_age_s    = feed_age_seconds(&app->feed,
                                                      wall_unix_ms());
    app->snap.health.net_connected = app->net ? net_peer_count(app->net) : -1;

    vitals_build(&app->vitals, &app->snap, gs->current_island);
    island_bar_build(&app->island_list, &app->snap, (float)SCREEN_W);

    if (gs->confirm.open) {
        confirm_view_build(&app->confirm, &app->snap);
        confirm_build(&app->confirm_list, &app->confirm,
                      (float)SCREEN_W, (float)SCREEN_H);
    }

    if (gs->inventory_open) {
        inventory_view_build(&app->inventory, &app->snap, gs->current_island);
        inventory_build(&app->inventory_list, &app->inventory, &app->ui,
                        (float)SCREEN_W, (float)SCREEN_H);
    }

    hud_view_build(&app->hud, &app->snap, gs->current_island);
    app->hud.selected      = gs->selected_building;
    app->hud.demolish_mode = gs->demolish_mode;
    app->hud.world_open    = gs->world_open;
    app->hud.menu_open     = gs->menu_open;
    hud_build(&app->hud_list, &app->hud, &app->ui,
              (float)SCREEN_W, (float)SCREEN_H);

    if (gs->trade_open) {
        exchange_view_market(&app->exchange, &app->snap, gs->current_island);
        exchange_build(&app->exchange_list, &app->exchange, &app->ui,
                       (float)SCREEN_W, (float)SCREEN_H);
    }

    /* Shared feed (Phase 4): publish any departures the ticks above
     * just caused, and re-read the inbound feed on its poll interval.
     * Wall-clock, cosmetic, outside the sim — see feed.h. */
    feed_track_departures(&app->feed, gs->ships, gs->ship_count,
                          wall_unix_ms());
    feed_poll(&app->feed, SDL_GetTicksNS());

    /* Everything below acts on the island currently being viewed;
     * client_update() has already simulated every settled one. Fetched
     * after client_update() because a menu action there (New Game /
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

    /* I: the stores overlay (UI_PLAN Phase 4). Opening resets to the
     * first page — a page index left over from last time is a small
     * surprise for no benefit. */
    if (gs->input.inventory_toggle) {
        gs->inventory_open = !gs->inventory_open;
        app->ui.inventory_page = 0;
    }

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

        /* The one confirmation (UI_PLAN Phase 6). Four popups —
         * build, demolish, tier upgrade, ship build — used to appear
         * here as four near-identical branches; they are one action
         * over one stored Command now. */
        } else if (gs->confirm.open) {
            ConfirmHit ch = confirm_hit(&app->confirm_list,
                                        (float)gs->input.logical_x,
                                        (float)gs->input.logical_y);
            switch (ch.kind) {
            case CONFIRM_HIT_CHOOSE:
                game_confirm_choose(gs, ch.option);
                break;
            case CONFIRM_HIT_ACCEPT:
                game_confirm_accept(gs);
                break;
            case CONFIRM_HIT_CANCEL:
            case CONFIRM_HIT_OUTSIDE:
                game_confirm_cancel(gs);
                break;
            case CONFIRM_HIT_NONE:
            default:
                break;      /* the panel: absorb it */
            }

        /* Phase 4: if the trade screen is open, only its buttons
         * respond (mirrors the menu_open branch below). */
        } else if (gs->inventory_open) {
            InventoryHit ihit = inventory_hit(&app->inventory_list, &app->ui,
                                              (float)gs->input.logical_x,
                                              (float)gs->input.logical_y);
            if (ihit.kind == INVENTORY_HIT_PAGE)
                app->ui.inventory_page = ihit.page;
            else if (ihit.kind == INVENTORY_HIT_CLOSE ||
                     ihit.kind == INVENTORY_HIT_OUTSIDE)
                gs->inventory_open = 0;

        } else if (gs->trade_open) {
            ExchangeHit hit = exchange_hit(&app->exchange_list, &app->ui,
                                           (float)gs->input.logical_x,
                                           (float)gs->input.logical_y);
            switch (hit.kind) {
            case EXCHANGE_HIT_SELL: {
                int qty = hit.qty;
                /* "All" is resolved here rather than in the sim: the
                 * player meant "the amount I could see", which is the
                 * snapshot's number, not whatever production has added
                 * since. */
                if (qty < 0)
                    qty = app->snap.islands[gs->current_island].stock[hit.res];
                game_sell_resource(gs, (ResourceType)hit.res, qty);
                break;
            }
            case EXCHANGE_HIT_BUY:
                /* qty < 0 ("Max") is resolved inside game_buy_resource
                 * itself, since it needs both storage headroom and
                 * Gold on hand to know what "max" means. */
                game_buy_resource(gs, (ResourceType)hit.res, hit.qty);
                break;
            case EXCHANGE_HIT_PAGE:
                app->ui.exchange_page = hit.page;
                break;
            case EXCHANGE_HIT_NONE:
                break;                      /* the panel: absorb it     */
            case EXCHANGE_HIT_CLOSE:
            case EXCHANGE_HIT_OUTSIDE:
            default:
                gs->trade_open = 0;
                break;
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

        } else if (island_bar_hit(&app->island_list,
                                  (float)gs->input.logical_x,
                                  (float)gs->input.logical_y).kind
                   == ISLAND_BAR_HIT_SWITCH) {
            /* The ‹ name › header (UI_PLAN Phase 5). Checked before the
             * map so a chevron over water switches island rather than
             * dropping a building in the sea. */
            IslandBarHit ih = island_bar_hit(&app->island_list,
                                             (float)gs->input.logical_x,
                                             (float)gs->input.logical_y);
            game_set_current_island(gs, ih.island);

        } else {
            /* One hit-test against the bar's own widget list (UI_PLAN
             * Phase 3), replacing the four separate ui_*_hit_test calls
             * this cascade used to make. */
            HudHit hh = hud_hit(&app->hud_list,
                                (float)gs->input.logical_x,
                                (float)gs->input.logical_y);

            if (hh.kind == HUD_HIT_MENU) {
                gs->menu_open = 1;
                gs->selected_building = BUILDING_NONE; /* deselect on menu open */
                gs->demolish_mode = 0;

            } else if (hh.kind == HUD_HIT_WORLD) {
                gs->world_open        = 1;
                gs->selected_building = BUILDING_NONE;
                gs->demolish_mode     = 0;

            } else if (hh.kind == HUD_HIT_DEMOLISH) {
                gs->demolish_mode = !gs->demolish_mode;
                gs->selected_building = BUILDING_NONE;

            } else if (hh.kind == HUD_HIT_TAB) {
                /* Sticky: the tab changes here and nowhere else. */
                app->ui.hud_category = hh.category;

            } else if (hh.kind == HUD_HIT_NONE) {
                /* The bar itself. Absorb it — a click on empty bar is
                 * not a click on the world behind it. */

            } else {
                BuildingType hud_sel = (hh.kind == HUD_HIT_BUILDING)
                                       ? (BuildingType)hh.type
                                       : BUILDING_NONE;
                if (hud_sel != BUILDING_NONE) {
                    gs->selected_building =
                        (gs->selected_building == hud_sel)
                        ? BUILDING_NONE : hud_sel;
                    gs->demolish_mode = 0;
                } else if (gs->demolish_mode) {
                    /* Fix pass: clicking a building while the demolish
                     * tool is active opens a confirmation popup rather
                     * than destroying immediately (roads included). */
                    int found = game_find_building_at(gs, gs->hovered_row,
                                                      gs->hovered_col);
                    if (found >= 0) game_confirm_demolish(gs, found);
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
                            app->ui.exchange_page  = 0;
                            break;
                        case BUILDING_HOUSE:
                            game_confirm_upgrade(gs, found);
                            break;
                        case BUILDING_SHIPYARD:
                            game_confirm_ship(gs);
                            break;
                        case BUILDING_HARBOR:
                            /* The escrow panel is the OWNER's desk;
                             * a visitor viewing the island gets no
                             * controls (their side of the airlock is
                             * their ship's transfer buttons). */
                            if (isl->owner == gs->local_player_id)
                                gs->escrow_open = 1;
                            break;
                        default:
                            break;   /* not an interactive building */
                        }
                    }
                } else if (gs->selected_building == BUILDING_ROAD) {
                    /* Roads are exempt from the confirm popup — also
                     * placeable by dragging (client_update()'s per-frame
                     * drag check), and a per-tile confirmation would
                     * make that gesture unusable. A single click
                     * behaves the same way a 1-tile drag does. */
                    game_try_place_road(gs, gs->hovered_row, gs->hovered_col);
                } else if (gs->selected_building != BUILDING_NONE &&
                          gs->hovered_row >= 0 &&
                          building_can_place(&isl->map, gs->selected_building,
                                            gs->hovered_row, gs->hovered_col)) {
                    game_confirm_build(gs, gs->hovered_row, gs->hovered_col,
                                       gs->selected_building);
                }
            }
        }
    }

    /* Right click closes the topmost overlay, else deselects. The
     * order comes from game_topmost_overlay() rather than a second
     * hand-maintained list of flags (UI_PLAN Phase 4) — the two used to
     * be written out separately here and in the click cascade, which is
     * how they drift. */
    if (gs->input.right_click) {
        switch (game_topmost_overlay(gs)) {
        case UI_OVERLAY_MENU:      gs->menu_open      = 0; break;
        case UI_OVERLAY_CONFIRM:   game_confirm_cancel(gs); break;
        case UI_OVERLAY_TRADE:     gs->trade_open     = 0; break;
        case UI_OVERLAY_ESCROW:    gs->escrow_open    = 0; break;
        case UI_OVERLAY_INVENTORY: gs->inventory_open = 0; break;
        case UI_OVERLAY_WORLD:     gs->world_open     = 0; break;
        case UI_OVERLAY_NONE:
        default:
            gs->selected_building = BUILDING_NONE;
            gs->demolish_mode     = 0;
            break;
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

    if (gs->selected_building != BUILDING_NONE && gs->hovered_row >= 0) {
        render_ghost(app->r, &isl->camera,
                     gs->selected_building,
                     gs->hovered_row, gs->hovered_col,
                     gs->placement_valid);

        /* Why the ghost is red, said at the cursor (UI_PLAN Phase 0.5).
         * Localized rather than a corner toast: the answer belongs to
         * the tile being pointed at, and a player scanning for a legal
         * spot reads it without moving their eyes. The string comes
         * from the rejection vocabulary the sim itself uses, so it
         * cannot drift from the actual verdict. */
        if (!gs->placement_valid &&
            gs->placement_reason != (int)REJ_OK) {
            SDL_Color warn = { 235, 120, 110, 255 };
            font_draw_text(app->r, FONT_SMALL,
                ui_reject_text((RejectReason)gs->placement_reason),
                gs->input.logical_x + 18, gs->input.logical_y + 18, warn);
        }
    }

    render_hovered_tile(app->r, &isl->camera,
                        gs->hovered_row, gs->hovered_col);

    /* Phase 4: resource stockpile panel */
    render_resources(app->r, &isl->stockpile);

    /* Phase 5: population counter */
    render_population(app->r,
                      pop_total(isl->pop_data, isl->building_count),
                      SCREEN_W);

    ui_draw(app->r, &app->hud_list, &app->hud,
            gs->input.logical_x, gs->input.logical_y);

    /* CHANGED: draw menu overlay on top of everything when open */
    if (gs->menu_open)
        ui_menu_draw(app->r, SCREEN_W, SCREEN_H,
                     gs->input.logical_x,
                     gs->input.logical_y);

    /* Phase 4: draw the trade screen on top when open */
    island_bar_draw(app->r, &app->island_list, &app->snap,
                    gs->input.logical_x, gs->input.logical_y);

    vitals_ui_draw(app->r, SCREEN_W, &app->vitals);

    if (gs->inventory_open)
        inventory_ui_draw(app->r, SCREEN_W, SCREEN_H, &app->inventory_list,
                          &app->inventory, gs->input.logical_x,
                          gs->input.logical_y);

    if (gs->trade_open)
        trade_ui_draw(app->r, SCREEN_W, SCREEN_H, &app->exchange_list,
                      &app->exchange, gs->input.logical_x,
                      gs->input.logical_y);

    /* Phase 5: harbor escrow panel on top when open */
    if (gs->escrow_open)
        escrow_ui_draw(app->r, SCREEN_W, SCREEN_H, isl,
                       gs->input.logical_x, gs->input.logical_y);

    /* The one confirmation, on top when open (UI_PLAN Phase 6). It
     * shows the literal Command it will submit; four popups used to be
     * drawn here from four different files. */
    if (gs->confirm.open)
        confirm_ui_draw(app->r, SCREEN_W, SCREEN_H, &app->confirm_list,
                        &app->confirm, gs->input.logical_x,
                        gs->input.logical_y);

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

    /* Co-op / server status line, top-left, whenever a session exists.
     * The identity goes on the same line because it is the one thing a
     * player needs to write down: reconnecting to a persistent server
     * with --as N is how you get your island back (SERVER.md). */
    if (app->net) {
        SDL_Color net_col = { 160, 210, 250, 255 };
        char      line[128];
        uint32_t  id = net_resume_id(app->net);

        if (id != PLAYER_NONE)
            SDL_snprintf(line, sizeof(line), "%s  |  player %u (--as %u)",
                         net_status(app->net), id, id);
        else
            SDL_strlcpy(line, net_status(app->net), sizeof(line));

        font_draw_text(app->r, FONT_SMALL, line, 16, 8, net_col);
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
    if (app) {
        if (app->net) net_close(app->net);   /* sends BYE to the peer */
        game_free(app->g);
        SDL_free(app);
    }
}
