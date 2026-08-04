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
#include "inventory_ui.h"  /* UI_PLAN Phase 4: stores + vitals */
#include "confirm_ui.h"    /* UI_PLAN Phase 6: the one confirmation */
#include "fx_reject.h"     /* UI_PLAN M1: what happened to my click */
#include "intent.h"        /* UI_PLAN M1: recording the input stream */
#include "scrub_view.h"    /* MMO later phases: the time scrubber   */
#include "client.h"   /* MMO Phase 6: the client half of the frame */
#include "ui_kit.h"       /* UI_PLAN Phase 0: widget kit, reject text   */
#include "ui_snapshot.h"  /* UI_PLAN Phase 0: what the UI may see       */
#include "exchange_view.h"/* UI_PLAN Phase 1: the exchange surface      */
#include "book_ui.h"      /* UI_PLAN N3: the order book (pulls its view)*/
#include "chart_ui.h"     /* UI_PLAN N4: the passages (pulls its view)  */
#include "yard_ui.h"      /* UI_PLAN N6: the yard and the fleet         */
#include "people_ui.h"    /* LIFE_PLAN Phase 9: the people (pulls view) */
#include "config.h"       /* AUTH_PLAN Phase 2: where a token lives      */
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

    /* UI_PLAN Phase 0/1. The snapshot is rebuilt once per frame. */
    UiSnapshot    snap;
    UiState       ui;
    ExchangeView  exchange;
    UiList        exchange_list;
    /* The book's view is the one overlay state that PERSISTS between */
    BookView      book;
    UiList        book_list;
    /* And the passages, retained for the same reason: a route that goes
     * out of use stays where it stood rather than the next one sliding
     * into its place under the cursor (UI_PLAN N4). */
    ChartView     charts;
    UiList        chart_list;
    /* Where everything spatial goes on the world map (UI_PLAN N5).
     * Rebuilt each frame the map is open, like the other views. */
    SeaView       sea_view;
    YardView      yard;
    UiList        yard_list;
    PeopleView    people;
    UiList        people_list;
    HudView       hud;
    UiList        hud_list;
    InventoryView inventory;
    UiList        inventory_list;
    VitalsView    vitals;
    UiList        island_list;
    UiList        scrub_list;
    ConfirmView   confirm;
    UiList        confirm_list;
    FxReject      fx;

    /* The intent being assembled this frame (UI_PLAN M1), and the
     * command sequence before the click was handled — the difference is
     * how we learn what the click produced. */
    Intent        intent;
    uint32_t      intent_seq_before;

    /* Client preferences, which since AUTH_PLAN Phase 2 means the
     * tokens servers have issued us. Loaded once at startup; written
     * back the moment a server issues a new one, because a token told
     * once and not written down is a token lost. */
    Config        cfg;
    char          join_host[CONFIG_HOST_LEN];
    uint16_t      join_port;
    int           joined;

    /* --screenshot: draw N frames, save the last one, exit. */
    char          shot_path[512];
    int           shot_frames;      /* frames to draw before saving     */
    int           shot_overlay;     /* GameOverlay to open, or 0        */
} App;

/* Which overlay --screenshot-overlay names. Kept beside the flag rather
 * than in game.h: this is a debugging convenience, not a concept the
 * game has. */
static int overlay_by_name(const char *name)
{
    if (SDL_strcmp(name, "book")   == 0) return UI_OVERLAY_BOOK;
    if (SDL_strcmp(name, "charts") == 0) return UI_OVERLAY_CHARTS;
    if (SDL_strcmp(name, "yard")   == 0) return UI_OVERLAY_YARD;
    if (SDL_strcmp(name, "people") == 0) return UI_OVERLAY_PEOPLE;
    if (SDL_strcmp(name, "stores") == 0) return UI_OVERLAY_INVENTORY;
    if (SDL_strcmp(name, "world")  == 0) return UI_OVERLAY_WORLD;
    if (SDL_strcmp(name, "trade")  == 0) return UI_OVERLAY_TRADE;
    return 0;
}

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
    char          shot_path[512];
    int           shot_frames  = 0;
    int           shot_overlay = 0;
    int           i;

    shot_path[0] = '\0';

    *appstate = NULL;   /* defined for the CLI and failure paths */

    /* Said before anything can fail, because "it started and printed
     * nothing" and "it never started" are different problems and the
     * logs could not tell them apart. ci/smoke-test.sh treats an empty
     * log as its own failure for the same reason. */
    SDL_Log("Saltmarch starting");

    /* Headless record/replay short-circuits before any window exists. */
    if (run_cli_mode(argc, argv, &cli_result))
        return cli_result;

    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING,    "Saltmarch");
    /* Keep this in step with the release tag. It had drifted three
     * releases behind — 0.3.0 against a v0.5.0-alpha tag — because the
     * tag is the thing anybody looks at and this string is the thing
     * nothing reads back. */
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, "0.6.0");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING,    "game");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    /* Parsed before the window exists, because --screenshot decides */
    for (i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            SDL_strlcpy(shot_path, argv[++i], sizeof(shot_path));
            if (shot_frames <= 0) shot_frames = 30;
        } else if (SDL_strcmp(argv[i], "--screenshot-frames") == 0 &&
                   i + 1 < argc) {
            shot_frames = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--screenshot-overlay") == 0 &&
                   i + 1 < argc) {
            shot_overlay = overlay_by_name(argv[++i]);
        }
    }

    if (!SDL_CreateWindowAndRenderer("Saltmarch",
                                     SCREEN_W, SCREEN_H,
                                     shot_path[0] ? 0 : SDL_WINDOW_FULLSCREEN,
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
    SDL_memset(&app->scrub_list,    0, sizeof(app->scrub_list));
    SDL_memset(&app->confirm,       0, sizeof(app->confirm));
    SDL_memset(&app->confirm_list,  0, sizeof(app->confirm_list));
    fx_reject_init(&app->fx);
    SDL_memset(&app->intent, 0, sizeof(app->intent));
    SDL_strlcpy(app->shot_path, shot_path, sizeof(app->shot_path));
    app->shot_frames  = shot_frames;
    app->shot_overlay = shot_overlay;

    config_load(&app->cfg);
    SDL_Log("Config: %s (%d server%s remembered)",
            app->cfg.path[0] ? app->cfg.path : "(none)",
            app->cfg.count, app->cfg.count == 1 ? "" : "s");
    app->join_host[0] = '\0';
    app->join_port    = 0;
    app->joined       = 0;
    app->intent_seq_before = 0;
    app->ui.hud_category = BCAT_FARMING;

    /* Display name for the shared feed: SALTMARCH_PLAYER, or a default.
     * Cosmetic identity only — the sim's player_id comes from the co-op
     * session (or defaults to player 1 offline). */
    feed_init(&app->feed, SDL_getenv("SALTMARCH_PLAYER"));

    /* Co-op (Phase 5): --host [port] listens for players; --join */
    app->net = NULL;
    {
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
        if (want_join) {
            /* The token this machine already holds for this server, if
             * any. `--as` survives as a request for a world identity,
             * but on an authenticating server it is ignored in favour
             * of whatever the credential owns (AUTH_PLAN Phase 1). */
            const NetCredential *cred =
                config_credential(&app->cfg, hostbuf, join_port);

            app->net = net_join(hostbuf, join_port, resume_id, cred);
            SDL_strlcpy(app->join_host, hostbuf, sizeof(app->join_host));
            app->join_port = join_port;
            app->joined    = app->net != NULL;
        }
        if (app->net) net_attach(gs, app->net);
    }

    *appstate = app;

    /* A missing font is not cosmetic: every resource count, price. */
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

    /* A token the server minted for us during the handshake, written
     * down the moment it arrives (AUTH_PLAN Phase 2). Told once and
     * never again: a client that forgets it registers again, and on a
     * server with registration closed there is no again. */
    if (app->net && app->joined) {
        const NetCredential *issued = net_issued_credential(app->net);
        if (issued) {
            config_set_credential(&app->cfg, app->join_host, app->join_port,
                                  issued);
            if (config_save(&app->cfg))
                SDL_Log("Saved this server's account to %s", app->cfg.path);
            app->joined = 0;      /* once per session */
        }
    }

    client_update(gs, app->r);

    if (app->net)
        net_after_update(app->net, gs);

    /* UI_PLAN Phase 0: take the picture the UI will work from. After
     * the tick loop, so every overlay in this frame sees one tick and
     * none of them can observe the world mid-tick. */
    /* UI_PLAN M1: collect what the ticks just did to the commands this
     * client submitted, and age the flashes they raised. Before the
     * snapshot, so a rejection is visible on the same frame the world
     * failed to change. */
    fx_reject_drain(&app->fx, gs);
    fx_reject_update(&app->fx, gs->delta_time);

    ui_snapshot_build(&app->snap, gs);

    /* The half of the health readings the sim cannot know: how stale
     * the shared feed is, and whether anyone is connected. */
    app->snap.health.feed_age_s    = feed_age_seconds(&app->feed,
                                                      wall_unix_ms());
    app->snap.health.net_connected  = app->net ? net_peer_count(app->net) : -1;
    app->snap.health.feed_malformed = app->feed.malformed_count;
    app->snap.health.feed_ghosts    = app->feed.ghost_count;

    vitals_build(&app->vitals, &app->snap, gs->current_island);
    island_bar_build(&app->island_list, &app->snap, (float)SCREEN_W);

    if (game_scrubbing(gs))
        scrub_build(&app->scrub_list, gs->sim_tick_no,
                    game_scrub_min(gs), game_scrub_max(gs),
                    (float)SCREEN_W, (float)SCREEN_H);

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
    } else if (gs->escrow_open) {
        /* The harbour is the same surface with a different counterparty
         * (UI_PLAN M5, decision 4): rows are the quay's contents and the
         * action cluster is take/stage rather than buy/sell. */
        exchange_view_escrow(&app->exchange, &app->snap, gs->current_island);
        exchange_build(&app->exchange_list, &app->exchange, &app->ui,
                       (float)SCREEN_W, (float)SCREEN_H);
    }

    /* The order book folds this frame into the rows it already had,
     * rather than rebuilding them (UI_PLAN N3) — which is why it is
     * `update` and not `build`, and why the view lives on App. */
    if (gs->book_open) {
        book_view_update(&app->book, &app->snap, gs->current_island,
                         &app->ui);
        book_build(&app->book_list, &app->book, &app->ui,
                   (float)SCREEN_W, (float)SCREEN_H);
    }

    /* The passages fold too, and take the Sea directly — the one
     * recorded exception to decision 1 (UI_PLAN N1), because route
     * geometry is generated from the seed rather than owned and copying
     * it per frame would quadruple the snapshot. */
    if (gs->charts_open) {
        chart_view_update(&app->charts, &app->snap, &gs->sea,
                          gs->current_island);
        chart_build(&app->chart_list, &app->charts, &app->ui,
                    (float)SCREEN_W, (float)SCREEN_H);
    }

    if (gs->yard_open) {
        yard_view_build(&app->yard, &app->snap, gs->current_island);
        yard_build(&app->yard_list, &app->yard, &app->ui,
                   (float)SCREEN_W, (float)SCREEN_H);
    }

    if (gs->people_open) {
        people_view_build(&app->people, &app->snap, gs->current_island);
        people_build(&app->people_list, &app->people,
                     (float)SCREEN_W, (float)SCREEN_H);
    }

    /* The sea, whenever the map is open (UI_PLAN N5). Rebuilt rather
     * than folded: unlike a row under a cursor, a path has nothing to
     * lose by being recomputed — it is where the water is. */
    if (gs->world_open)
        sea_view_build(&app->sea_view, &app->snap, &gs->sea,
                       gs->current_island, (float)SCREEN_W, (float)SCREEN_H);

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

    /* F8: the time-travel scrubber (MMO_PLAN later phases). Entering
     * freezes the sim and refuses submissions; leaving returns to the
     * tick the world actually reached. */
    if (gs->input.scrub_toggle) {
        if (game_scrubbing(gs)) game_scrub_end(gs);
        else                    game_scrub_begin(gs);
    }

    /* I: the stores overlay (UI_PLAN Phase 4). Opening resets to the
     * first page — a page index left over from last time is a small
     * surprise for no benefit. */
    if (gs->input.inventory_toggle) {
        gs->inventory_open = !gs->inventory_open;
        app->ui.inventory_page = 0;
    }

    /* B: the order book (UI_PLAN N3). Opening forgets the rows from
     * last time — a struck-through order is a note about something that
     * happened while you were watching, and you were not — and starts
     * the draft from the market's own quote. */
    if (gs->input.book_toggle) {
        gs->book_open = !gs->book_open;
        if (gs->book_open) {
            book_view_reset(&app->book);
            book_draft_default(&app->ui);
        }
    }

    /* C: the passages (UI_PLAN N4). Opening forgets the rows for the
     * same reason the book does — a passage marked "out of use" is a
     * note about something that happened while you were watching. */
    if (gs->input.chart_toggle) {
        gs->charts_open = !gs->charts_open;
        if (gs->charts_open) {
            chart_view_reset(&app->charts);
            app->ui.chart_page = 0;
        }
    }

    /* Y: the shipyard (UI_PLAN N6). */
    if (gs->input.yard_toggle) {
        gs->yard_open = !gs->yard_open;
        if (gs->yard_open) app->ui.yard_page = 0;
    }

    /* P: the people (LIFE_PLAN Phase 9). Nothing to reset — the screen
     * carries no page and no draft. */
    if (gs->input.people_toggle)
        gs->people_open = !gs->people_open;

    /* --screenshot-overlay: open the named screen once, the same way
     * its key does, so a capture can be of the passages or the yard
     * rather than always of the map. Done here beside the real toggles
     * so it cannot drift from what a keypress actually does. */
    if (app->shot_overlay) {
        switch (app->shot_overlay) {
        case UI_OVERLAY_BOOK:
            gs->book_open = 1;
            book_view_reset(&app->book);
            book_draft_default(&app->ui);
            break;
        case UI_OVERLAY_CHARTS:
            gs->charts_open = 1;
            chart_view_reset(&app->charts);
            app->ui.chart_page = 0;
            break;
        case UI_OVERLAY_YARD:
            gs->yard_open = 1;
            app->ui.yard_page = 0;
            break;
        case UI_OVERLAY_PEOPLE: gs->people_open = 1; break;
        case UI_OVERLAY_INVENTORY:
            gs->inventory_open = 1;
            app->ui.inventory_page = 0;
            break;
        case UI_OVERLAY_WORLD: gs->world_open = 1; break;
        case UI_OVERLAY_TRADE: gs->trade_open = 1; break;
        default: break;
        }
        app->shot_overlay = 0;
    }

    /* UI_PLAN M1: remember the state this frame was drawn in, so a
     * click recorded below carries the screen the player was actually
     * looking at. Captured BEFORE the click is handled — afterwards the
     * overlay may have opened, closed or paged. */
    {
        app->intent.tick              = app->snap.tick;
        app->intent.x                 = gs->input.logical_x;
        app->intent.y                 = gs->input.logical_y;
        app->intent.ui.overlay        = (uint8_t)game_topmost_overlay(gs);
        app->intent.ui.hud_category   = (uint8_t)app->ui.hud_category;
        app->intent.ui.exchange_page  = (uint16_t)app->ui.exchange_page;
        app->intent.ui.inventory_page = (uint16_t)app->ui.inventory_page;
        app->intent.ui.book_page      = (uint16_t)app->ui.book_page;
        app->intent.ui.book_side      = (uint8_t)app->ui.book_side;
        app->intent.ui.book_res       = (uint8_t)app->ui.book_res;
        app->intent.ui.book_qty       = app->ui.book_qty;
        app->intent.ui.book_limit     = app->ui.book_limit;
        app->intent.ui.chart_page     = (uint16_t)app->ui.chart_page;
        app->intent.ui.yard_page      = (uint16_t)app->ui.yard_page;
        app->intent.ui.hovered_row    = (int16_t)gs->hovered_row;
        app->intent.ui.hovered_col    = (int16_t)gs->hovered_col;
        app->intent.ui.current_island = (int16_t)gs->current_island;
        app->intent_seq_before        = gs->cmd_seq_last;
    }

    /* --- Handle clicks ---------------------------------- */
    if (gs->input.left_click && game_scrubbing(gs)) {
        /* The scrubber is modal over everything: while you are looking
         * at the past, the only thing a click can do is move you
         * through it or bring you back. */
        ScrubHit sh = scrub_hit(&app->scrub_list, game_scrub_min(gs),
                                game_scrub_max(gs),
                                (float)gs->input.logical_x,
                                (float)gs->input.logical_y);
        if (sh.kind == SCRUB_HIT_SEEK)      game_scrub_to(gs, sh.tick);
        else if (sh.kind == SCRUB_HIT_LIVE) game_scrub_end(gs);

    } else if (gs->input.left_click) {

        /* Archipelago overview: checked before the confirm popups
         * only in the sense that it cannot coexist with them —
         * opening it is a HUD action, and the confirm popups are all
         * closed by then. */
        if (gs->world_open) {
            int          target = -1, tship = -1;
            ResourceType tres   = RES_WOOD;
            WorldHit     hit    = world_ui_hit_test(SCREEN_W, SCREEN_H, &gs->sea,
                                                    MAX_ISLANDS,
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
                    /* A ship is selected, so an island click is. */
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
                    /* Colonisation applies at the next tick boundary. */
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

            case WORLD_HIT_INSURE:
                /* Sail now, insured. The destination is the ship's last
                 * declared one — the same "select then click an island"
                 * grammar as an ordinary departure, with the premium
                 * added (MMO_PLAN later phases). */
                if (gs->world_selected_ship >= 0) {
                    const Ship *sel = &gs->ships[gs->world_selected_ship];
                    int dest = sel->to_island;
                    if (sel->at_island >= 0 && dest != sel->at_island)
                        game_ship_depart_insured(gs, gs->world_selected_ship,
                                                 dest);
                }
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
            case CONFIRM_HIT_ACCEPT: {
                /* Remember where this came from, so a rejection lands
                 * on the tile the player clicked rather than in a
                 * corner (UI_PLAN M1). */
                Command  pending = gs->confirm.chosen ? gs->confirm.alt
                                                      : gs->confirm.cmd;
                FxAnchor anchor;

                if (pending.kind == CMD_PLACE_BUILDING)
                    anchor = fx_anchor_tile(pending.b, pending.c);
                else
                    anchor = fx_anchor_rect(app->confirm_list.items[0].rect);

                if (game_confirm_accept(gs))
                    fx_reject_expect(&app->fx, gs->cmd_seq_last, anchor);
                break;
            }
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
        } else if (gs->escrow_open) {
            /* Same hit-test as the marketplace, because it is the same
             * screen: SELL means "take what is on the quay", BUY means
             * "stage some for a visitor" (UI_PLAN M5). */
            ExchangeHit eh = exchange_hit(&app->exchange_list, &app->exchange,
                                          &app->ui,
                                          (float)gs->input.logical_x,
                                          (float)gs->input.logical_y);
            switch (eh.kind) {
            case EXCHANGE_HIT_SELL:
                game_escrow_take_nonce(gs, gs->current_island,
                                       (ResourceType)eh.res,
                                       app->snap.islands[gs->current_island]
                                           .escrow[eh.res],
                                       app->exchange.nonce);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(eh.rect));
                break;
            case EXCHANGE_HIT_BUY:
                game_escrow_put_nonce(gs, gs->current_island,
                                      (ResourceType)eh.res, eh.qty,
                                      app->exchange.nonce);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(eh.rect));
                break;
            case EXCHANGE_HIT_DOCKING:
                game_set_docking(gs, gs->current_island, eh.qty);
                break;
            case EXCHANGE_HIT_NONE:
                break;      /* the panel: absorb it */
            case EXCHANGE_HIT_CLOSE:
            case EXCHANGE_HIT_OUTSIDE:
            default:
                gs->escrow_open = 0;
                break;
            }

        /* The order book (UI_PLAN N3). Most of these clicks compose. */
        } else if (gs->book_open) {
            BookHit bh = book_hit(&app->book_list, &app->book, &app->ui,
                                  (float)gs->input.logical_x,
                                  (float)gs->input.logical_y);
            switch (bh.kind) {
            case BOOK_HIT_POST:
                /* The sign is the side, as CMD_PLACE_ORDER wants it.
                 * The limit is the price the composer was SHOWING —
                 * including when it was following the quote, since
                 * that is still the number the player read. */
                game_place_order(gs, gs->current_island, TRADE_RESOURCE,
                                 (uint16_t)bh.res,
                                 bh.side == ORDER_SELL ? -bh.qty : bh.qty,
                                 bh.limit);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(bh.rect));
                break;
            case BOOK_HIT_CANCEL:
                /* The full 32-bit id, out of the widget's value — the
                 * id in the widget's identity is only its low half. */
                game_cancel_order(gs, bh.order_id);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(bh.rect));
                break;
            case BOOK_HIT_PAGE:
                app->ui.book_page = bh.page;
                break;
            case BOOK_HIT_SIDE:
            case BOOK_HIT_GOOD:
            case BOOK_HIT_QTY:
            case BOOK_HIT_LIMIT:
                app->ui.book_side  = bh.side;
                app->ui.book_res   = bh.res;
                app->ui.book_qty   = bh.qty;
                app->ui.book_limit = bh.follow ? 0 : bh.limit;
                break;
            case BOOK_HIT_NONE:
                break;      /* the panel: absorb it */
            case BOOK_HIT_CLOSE:
            case BOOK_HIT_OUTSIDE:
            default:
                gs->book_open = 0;
                break;
            }

        /* The passages (UI_PLAN N4). Buy and Sell post chart orders
         * through the same funnel as everything else — this screen is
         * the route picker the book's composer could not be, because a
         * chart is named by a passage rather than by a good. */
        } else if (gs->charts_open) {
            ChartHit ch = chart_hit(&app->chart_list, &app->charts, &app->ui,
                                    (float)gs->input.logical_x,
                                    (float)gs->input.logical_y);
            switch (ch.kind) {
            case CHART_HIT_BUY:
                /* The price the row was DISPLAYING rides along as the
                 * limit, so a market that moves between the frame and
                 * the tick is refused rather than filled at a number
                 * nobody read (UI_PLAN M3). */
                game_place_order(gs, gs->current_island, TRADE_ROUTE_CHART,
                                 (uint16_t)ch.route_id, CHART_LOT, ch.limit);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(ch.rect));
                break;
            case CHART_HIT_SELL:
                game_place_order(gs, gs->current_island, TRADE_ROUTE_CHART,
                                 (uint16_t)ch.route_id, -CHART_LOT, ch.limit);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(ch.rect));
                break;
            case CHART_HIT_SURVEY:
                /* You cannot name the passage — that is what you are
                 * paying to find out — so the command names an island
                 * (UI_PLAN N7, MARITIME_PLAN Phase 3d). */
                game_survey(gs, gs->current_island, ch.island);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(ch.rect));
                break;
            case CHART_HIT_PAGE:
                app->ui.chart_page = ch.page;
                break;
            case CHART_HIT_NONE:
                break;      /* the panel: absorb it */
            case CHART_HIT_CLOSE:
            case CHART_HIT_OUTSIDE:
            default:
                gs->charts_open = 0;
                break;
            }

        /* The yard (UI_PLAN N6). Laying down a hull opens the one
         * confirmation carrying the command it will submit, so the
         * popup shows which hull and what it costs (Phase 6). */
        } else if (gs->yard_open) {
            YardHit yh = yard_hit(&app->yard_list, &app->yard, &app->ui,
                                  (float)gs->input.logical_x,
                                  (float)gs->input.logical_y);
            switch (yh.kind) {
            case YARD_HIT_BUILD:
                game_confirm_ship_class(gs, yh.klass);
                break;
            case YARD_HIT_ESCORT:
                game_set_escort(gs, yh.ship, yh.target);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(yh.rect));
                break;
            case YARD_HIT_PAGE:
                app->ui.yard_page = yh.page;
                break;
            case YARD_HIT_NONE:
                break;      /* the panel: absorb it */
            case YARD_HIT_CLOSE:
            case YARD_HIT_OUTSIDE:
            default:
                gs->yard_open = 0;
                break;
            }

        /* The people (LIFE_PLAN Phase 9). A read-only screen: the only
         * click that does anything closes it. */
        } else if (gs->people_open) {
            PeopleHit ph = people_hit(&app->people_list,
                                      (float)gs->input.logical_x,
                                      (float)gs->input.logical_y);
            if (ph.kind != PEOPLE_HIT_NONE) gs->people_open = 0;

        } else if (gs->inventory_open) {
            InventoryHit ihit = inventory_hit(&app->inventory_list, &app->ui,
                                              (float)gs->input.logical_x,
                                              (float)gs->input.logical_y);
            if (ihit.kind == INVENTORY_HIT_PAGE)
                app->ui.inventory_page = ihit.page;
            else if (ihit.kind == INVENTORY_HIT_INSURANCE) {
                /* The standing policy: every shipment this harbour
                 * dispatches, insured at its ROUTE's premium (N8). */
                game_set_insurance(gs, gs->current_island, ihit.on);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(ihit.rect));
            }
            else if (ihit.kind == INVENTORY_HIT_TAX) {
                /* What the treasury takes (LIFE_PLAN Phase 7). The hit
                 * carries the rate the stepper would set, so this is a
                 * submission and not a calculation. */
                game_set_tax_rate(gs, gs->current_island, ihit.on);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(ihit.rect));
            }
            else if (ihit.kind == INVENTORY_HIT_CLOSE ||
                     ihit.kind == INVENTORY_HIT_OUTSIDE)
                gs->inventory_open = 0;

        } else if (gs->trade_open) {
            ExchangeHit hit = exchange_hit(&app->exchange_list, &app->exchange,
                                           &app->ui,
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
                /* The price the row was showing rides along as a limit:
                 * if the market moves against us before this applies,
                 * the sim refuses rather than filling worse (M3). */
                game_sell_resource_limit(gs, (ResourceType)hit.res, qty,
                                         hit.price);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(hit.rect));
                break;
            }
            case EXCHANGE_HIT_BUY:
                /* qty < 0 ("Max") is resolved inside game_buy_resource
                 * itself, since it needs both storage headroom and
                 * Gold on hand to know what "max" means. */
                game_buy_resource_limit(gs, (ResourceType)hit.res, hit.qty,
                                        hit.price);
                fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                 fx_anchor_rect(hit.rect));
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
                    /* Nothing selected, so a map click means "interact */
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
                    /* Roads are exempt from the confirm popup — also */
                    if (game_try_place_road(gs, gs->hovered_row,
                                            gs->hovered_col))
                        fx_reject_expect(&app->fx, gs->cmd_seq_last,
                                         fx_anchor_tile(gs->hovered_row,
                                                        gs->hovered_col));
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

    /* One record per click, stamped with whichever command it produced
     * (zero if it produced none — a tab, a page turn, a click on open
     * water). The pair is what lets CI assert that replaying this click
     * against this frame emits this command. */
    if (gs->input.left_click || gs->input.right_click) {
        app->intent.kind = (uint8_t)(gs->input.left_click ? INTENT_LEFT_CLICK
                                                          : INTENT_RIGHT_CLICK);
        app->intent.seq  = (gs->cmd_seq_last != app->intent_seq_before)
                           ? gs->cmd_seq_last : 0u;
        intent_record(gs, &app->intent);
    }

    /* Right click closes the topmost overlay, else deselects. */
    if (gs->input.right_click) {
        switch (game_topmost_overlay(gs)) {
        case UI_OVERLAY_MENU:      gs->menu_open      = 0; break;
        case UI_OVERLAY_CONFIRM:   game_confirm_cancel(gs); break;
        case UI_OVERLAY_TRADE:     gs->trade_open     = 0; break;
        case UI_OVERLAY_ESCROW:    gs->escrow_open    = 0; break;
        case UI_OVERLAY_BOOK:      gs->book_open      = 0; break;
        case UI_OVERLAY_CHARTS:    gs->charts_open    = 0; break;
        case UI_OVERLAY_YARD:      gs->yard_open      = 0; break;
        case UI_OVERLAY_PEOPLE:    gs->people_open    = 0; break;
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

        /* Why the ghost is red, said at the cursor (UI_PLAN Phase 0.5). */
        if (!gs->placement_valid &&
            gs->placement_reason != (int)REJ_OK) {
            SDL_Color warn = { 235, 120, 110, 255 };
            font_draw_text(app->r, FONT_SMALL,
                ui_reject_text((RejectReason)gs->placement_reason),
                gs->input.logical_x + 18, gs->input.logical_y + 18, warn);
        }
    }

    /* Ordered but not yet applied (UI_PLAN M1). */
    render_pending_placements(app->r, &isl->camera, gs);

    render_hovered_tile(app->r, &isl->camera,
                        gs->hovered_row, gs->hovered_col);

    render_deposit_label(app->r, &isl->map, &isl->camera,
                         gs->hovered_row, gs->hovered_col,
                         SCREEN_W, SCREEN_H);

    /* Phase 4: resource stockpile panel */
    render_resources(app->r, &isl->stockpile);

    /* Phase 5: population counter */
    render_population(app->r,
                      pop_total(isl->pop_data, isl->building_count),
                      SCREEN_W);

    /* LIFE_PLAN Phase 4: the date, from the tick and nothing else. */
    render_date(app->r, gs->sim_tick_no, SCREEN_W);

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

    /* The harbour, drawn by the exchange drawer it now shares. */
    if (gs->escrow_open)
        trade_ui_draw(app->r, SCREEN_W, SCREEN_H, &app->exchange_list,
                      &app->exchange, gs->input.logical_x,
                      gs->input.logical_y);

    /* The order book has its own drawer, because it turned out to be
     * its own screen (UI_PLAN N3). */
    if (gs->book_open)
        book_ui_draw(app->r, SCREEN_W, SCREEN_H, &app->book_list,
                     &app->book, gs->input.logical_x, gs->input.logical_y);

    /* And the passages beside it (UI_PLAN N4). */
    if (gs->charts_open)
        chart_ui_draw(app->r, SCREEN_W, SCREEN_H, &app->chart_list,
                      &app->charts, gs->input.logical_x, gs->input.logical_y);

    /* And the yard (UI_PLAN N6). */
    if (gs->yard_open)
        yard_ui_draw(app->r, SCREEN_W, SCREEN_H, &app->yard_list,
                     &app->yard, gs->input.logical_x, gs->input.logical_y);

    /* And the people (LIFE_PLAN Phase 9). */
    if (gs->people_open)
        people_ui_draw(app->r, SCREEN_W, SCREEN_H, &app->people_list,
                       &app->people, gs->input.logical_x,
                       gs->input.logical_y);

    /* The one confirmation, on top when open (UI_PLAN Phase 6). It
     * shows the literal Command it will submit; four popups used to be
     * drawn here from four different files. */
    if (gs->confirm.open)
        confirm_ui_draw(app->r, SCREEN_W, SCREEN_H, &app->confirm_list,
                        &app->confirm, gs->input.logical_x,
                        gs->input.logical_y);

    /* Archipelago overview on top of everything when open */
    if (gs->world_open)
        world_ui_draw(app->r, SCREEN_W, SCREEN_H, &gs->sea,
                      gs->islands, MAX_ISLANDS, gs->current_island,
                      gs->local_player_id,
                      gs->ships, gs->ship_count, gs->world_selected_ship,
                      app->feed.ghosts, app->feed.ghost_count, wall_unix_ms(),
                      &gs->faction,
                      gs->world_selected_ship >= 0
                          ? game_insurance_quote(gs, gs->world_selected_ship,
                                gs->ships[gs->world_selected_ship].to_island)
                          : 0,
                      &app->sea_view,
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

        /* Frame time and the text cache's hit rate (UI_PLAN M4). The
         * cache exists because feed-supplied strings make worst-case
         * text throughput somebody else's decision; these two numbers
         * are how that claim stays checkable rather than asserted. */
        {
            int hits = 0, misses = 0;
            fonts_cache_stats(&hits, &misses);
            SDL_snprintf(buf, sizeof(buf),
                         "frame %.1f ms   text %d hit / %d miss",
                         (double)(gs->delta_time * 1000.0f), hits, misses);
            font_draw_text(app->r, FONT_SMALL, buf, x, y, txt);
            y += line;
        }
        for (r = 0; r < (int)RES_GOLD; r++) {
            SDL_Color spark = { 150, 185, 150, 255 };

            SDL_snprintf(buf, sizeof(buf), "%-6s inv %4d  bid %3d  ask %3d",
                         RESOURCE_NAMES[r], fac->inventory[r],
                         faction_bid(fac, (ResourceType)r),
                         faction_ask(fac, (ResourceType)r));
            font_draw_text(app->r, FONT_SMALL, buf, x, y, txt);

            /* The same ring the trade screen draws (UI_PLAN M3): the
             * tuning overlay and the player's chart must not be able to
             * disagree about what the price did. */
            render_sparkline(app->r, (float)(x + 210), (float)y + 2.0f,
                             60.0f, 12.0f, app->snap.price_hist[r],
                             app->snap.price_hist_count[r], spark);
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

    /* The scrubber, above every overlay: it is what you are doing. */
    if (game_scrubbing(gs)) {
        SDL_Color warn = { 245, 200, 120, 255 };
        char      buf[96];
        int       i;

        for (i = 0; i < app->scrub_list.count; i++) {
            const UiWidget *w = &app->scrub_list.items[i];
            SDL_FRect       r = { w->rect.x, w->rect.y, w->rect.w, w->rect.h };
            int             action = ui_id_value(w->id);

            SDL_SetRenderDrawBlendMode(app->r, SDL_BLENDMODE_BLEND);
            if (i == 0)                            /* the bar          */
                SDL_SetRenderDrawColor(app->r, 22, 18, 12, 225);
            else if (action == UI_ACTION_PREV)     /* the track        */
                SDL_SetRenderDrawColor(app->r, 60, 52, 38, 255);
            else if (w->flags & UI_W_HEADER)       /* the handle       */
                SDL_SetRenderDrawColor(app->r, 245, 200, 120, 255);
            else                                   /* back-to-now      */
                SDL_SetRenderDrawColor(app->r, 70, 58, 36, 255);
            SDL_RenderFillRect(app->r, &r);
            SDL_SetRenderDrawBlendMode(app->r, SDL_BLENDMODE_NONE);

            if (w->label[0])
                font_draw_text(app->r, FONT_SMALL, w->label,
                               (int)(w->rect.x + 8.0f),
                               (int)(w->rect.y + 4.0f), warn);
        }

        SDL_snprintf(buf, sizeof(buf),
                     "VIEWING TICK %llu of %llu — the world is paused and "
                     "nothing can be ordered (F8 to return)",
                     (unsigned long long)gs->sim_tick_no,
                     (unsigned long long)game_scrub_max(gs));
        font_draw_text(app->r, FONT_SMALL, buf, 40,
                       SCREEN_H - (int)SCRUB_H - 22, warn);
    }

    /* Flashes last: they answer a click and must not be painted over
     * by the overlay the click came from. */
    render_reject_flashes(app->r, &isl->camera, &app->fx);

    SDL_RenderPresent(app->r);

    /* --screenshot: one frame, off the renderer this program owns. */
    if (app->shot_path[0] && --app->shot_frames <= 0) {
        SDL_Surface *shot = SDL_RenderReadPixels(app->r, NULL);
        int          ok   = 0;

        if (shot) {
            ok = SDL_SaveBMP(shot, app->shot_path);
            SDL_DestroySurface(shot);
        }
        if (ok) SDL_Log("Screenshot written to %s", app->shot_path);
        else    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Screenshot failed: %s", SDL_GetError());
        return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

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
