/*  replay_ui.c  --  The record/replay CLI, and the UI harness
 *                   (MMO_PLAN Phase 1d, UI_PLAN M1)
 *
 *  Sits above both libraries: it drives the sim to rebuild a world and
 *  the real overlay builders to rebuild what was on screen. Still no
 *  SDL — the whole point is that a machine with no display can replay
 *  a session's clicks and check what they would do.
 */

#include "replay.h"
#include "book_view.h"
#include "chart_view.h"
#include "yard_view.h"
#include "camera.h"
#include "confirm_view.h"
#include "exchange_view.h"
#include "hud_view.h"
#include "inventory_view.h"
#include "island_bar.h"
#include "ui_snapshot.h"
#include "resource.h"
#include "intent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *replay_cli_usage(void)
{
    return "--record FILE | --record-ui FILE [--seed N] | "
           "--replay FILE [--expect-hash HEX] [--verify-ui] [--dump-ui FILE]";
}

/* Both front ends parse the same argv, so the scan lives in one place;
 * replay_cli_requested is just "did the scan find a mode". */
typedef struct {
    const char *replay_file;
    const char *record_file;
    const char *expect;
    const char *dump_ui;
    int         verify_ui;
    int         record_ui;
    uint32_t    seed;
} CliArgs;

static CliArgs cli_parse(int argc, char *argv[])
{
    CliArgs a;
    int     i;

    a.replay_file = NULL;
    a.record_file = NULL;
    a.expect      = NULL;
    a.dump_ui     = NULL;
    a.verify_ui   = 0;
    a.record_ui   = 0;
    a.seed        = 1u;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc)
            a.replay_file = argv[++i];
        else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc)
            a.record_file = argv[++i];
        else if (strcmp(argv[i], "--record-ui") == 0 && i + 1 < argc) {
            a.record_file = argv[++i];
            a.record_ui   = 1;
        }
        else if (strcmp(argv[i], "--expect-hash") == 0 && i + 1 < argc)
            a.expect = argv[++i];
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            a.seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--verify-ui") == 0)
            a.verify_ui = 1;
        else if (strcmp(argv[i], "--dump-ui") == 0 && i + 1 < argc)
            a.dump_ui = argv[++i];
    }
    return a;
}

int replay_cli_requested(int argc, char *argv[])
{
    CliArgs a = cli_parse(argc, argv);
    return (a.replay_file || a.record_file) ? 1 : 0;
}

int replay_cli_run(int argc, char *argv[])
{
    CliArgs    a  = cli_parse(argc, argv);
    GameState *gs;
    int        rc = 0;

    if (!a.replay_file && !a.record_file) {
        fprintf(stderr, "usage: %s\n", replay_cli_usage());
        return 1;
    }

    gs = game_init();
    if (!gs) {
        fprintf(stderr, "replay: game_init failed\n");
        return 1;
    }

    if (a.record_file) {
        if (a.record_ui) replay_record_ui_session(gs, a.seed);
        else             replay_record_demo_session(gs, a.seed);
        if (!game_save(gs, a.record_file)) {
            rc = 1;
        } else {
            printf("record: %s seed=%u tick=%llu hash=%016llx intents=%d\n",
                   a.record_file, a.seed,
                   (unsigned long long)gs->sim_tick_no,
                   (unsigned long long)sim_hash(gs), gs->intent_count);
        }
    } else if (!game_load(gs, a.replay_file)) {
        rc = 1;
    } else {
        uint64_t h = sim_hash(gs);
        printf("replay: %s tick=%llu hash=%016llx\n", a.replay_file,
               (unsigned long long)gs->sim_tick_no, (unsigned long long)h);

        /* Self-check: rebuild the world a SECOND time from seed+log and
         * confirm it lands on the same hash. This makes plain
         * `--replay <file>` a determinism gate needing no expected hash
         * — the form CI runs on every platform. */
        if (!game_verify_determinism(gs)) {
            /* State 3 is "not applicable", and it is not a failure.
             * A checkpoint is the world as STATE: there is no log
             * below it to re-derive it from, by design (SERVER.md,
             * "Log truncation"). Its integrity is checked instead when
             * it decodes -- a snapshot carries a checksum over its
             * bytes and the sim_hash of the world it captured, and
             * refuses to load if either disagrees. Reporting that as
             * "nondeterministic" would be CI failing a file for not
             * containing something it was never meant to contain. */
            if (gs->replay_state == 3) {
                printf("replay self-check: n/a — %s is a checkpoint "
                       "(state, not history); its own checksum and hash "
                       "were verified at load\n", a.replay_file);
            } else {
                printf("replay SELF-CHECK FAILED: world is nondeterministic\n");
                rc = 1;
            }
        }

        /* Re-drive the recorded clicks through the real UI (UI_PLAN
         * M1). Rebuilds each frame's snapshot at the tick it was drawn
         * from, runs the real builders and hit-tests, and checks both
         * the geometry and the command a click would emit. */
        if (rc == 0 && a.verify_ui) {
            if (gs->intent_count == 0) {
                printf("ui: no clicks recorded in %s — nothing to verify\n",
                       a.replay_file);
            } else if (replay_verify_ui(gs, 1) != 0) {
                printf("UI REPLAY FAILED\n");
                rc = 1;
            }
        }

        if (rc == 0 && a.dump_ui) {
            FILE *d = fopen(a.dump_ui, "wb");
            if (!d) {
                fprintf(stderr, "could not write %s\n", a.dump_ui);
                rc = 1;
            } else {
                replay_dump_ui(gs, d);
                fclose(d);
                printf("ui: wrote %s\n", a.dump_ui);
            }
        }

        /* Optional pin to a known hash (e.g. a committed fixture's
         * cross-platform value). */
        if (rc == 0 && a.expect) {
            uint64_t want = (uint64_t)strtoull(a.expect, NULL, 16);
            if (want != h) {
                printf("replay MISMATCH: expected %016llx got %016llx\n",
                       (unsigned long long)want, (unsigned long long)h);
                rc = 1;
            } else {
                printf("replay OK: hash matches\n");
            }
        }
    }

    game_free(gs);
    fflush(stdout);
    return rc;
}


/* ---- recording a session through the UI (UI_PLAN M1) -------
 * The trades below are not called directly: the screen is built, a
 * widget is found by identity, and the click is hit-tested at that
 * widget's centre — exactly what a player's cursor would do. What gets
 * recorded is therefore a real (frame, position) pair rather than a
 * synthetic one, and replaying it re-derives the same widget or fails.
 */

/* Click the widget with `id` on the exchange screen, recording the
 * intent and submitting whatever the hit maps to. */
static void click_exchange(GameState *gs, UiState *st, uint32_t id)
{
    UiSnapshot   snap;
    ExchangeView view;
    UiList       list;
    Intent       in;
    const UiWidget *w;
    ExchangeHit  hit;
    uint32_t     before = gs->cmd_seq_last;

    ui_snapshot_build(&snap, gs);
    exchange_view_market(&view, &snap, gs->current_island);
    exchange_build(&list, &view, st, (float)SCREEN_W, (float)SCREEN_H);

    w = ui_list_find(&list, id);
    if (!w) return;

    memset(&in, 0, sizeof(in));
    in.tick              = snap.tick;
    in.x                 = (int32_t)(w->rect.x + w->rect.w * 0.5f);
    in.y                 = (int32_t)(w->rect.y + w->rect.h * 0.5f);
    in.kind              = (uint8_t)INTENT_LEFT_CLICK;
    in.ui.overlay        = (uint8_t)UI_OVERLAY_TRADE;
    in.ui.exchange_page  = (uint16_t)st->exchange_page;
    in.ui.hovered_row    = -1;
    in.ui.hovered_col    = -1;
    in.ui.current_island = (int16_t)gs->current_island;

    hit = exchange_hit(&list, &view, st, (float)in.x, (float)in.y);
    if (hit.kind == EXCHANGE_HIT_BUY) {
        game_buy_resource_limit(gs, (ResourceType)hit.res, hit.qty, hit.price);
    } else if (hit.kind == EXCHANGE_HIT_SELL) {
        int qty = hit.qty;
        if (qty < 0) qty = snap.islands[snap.current_island].stock[hit.res];
        game_sell_resource_limit(gs, (ResourceType)hit.res, qty, hit.price);
    } else {
        return;
    }

    in.seq = (gs->cmd_seq_last != before) ? gs->cmd_seq_last : 0u;
    intent_record(gs, &in);
}

/* Find a widget by id AND payload. The composer's four steppers share
 * one id and differ by their step, exactly as the exchange's quantity
 * buttons do, so "the +10 button" is a pair rather than an id. */
static const UiWidget *find_valued(const UiList *l, uint32_t id, int32_t value)
{
    int i;
    for (i = 0; i < l->count; i++)
        if (l->items[i].id == id && l->items[i].value == value)
            return &l->items[i];
    return NULL;
}

/* Click a widget on the order book, recording the intent and submitting
 * whatever the hit maps to — including the clicks that submit nothing,
 * because composing the draft IS the recorded state that later clicks
 * are hit-tested against. `book` is threaded through rather than built
 * here: its retained rows are the point (UI_PLAN N3). */
static void click_book(GameState *gs, UiState *st, BookView *book,
                       uint32_t id, int32_t value, int by_value)
{
    UiSnapshot      snap;
    UiList          list;
    Intent          in;
    const UiWidget *w;
    BookHit         hit;
    uint32_t        before = gs->cmd_seq_last;

    ui_snapshot_build(&snap, gs);
    book_view_update(book, &snap, gs->current_island, st);
    book_build(&list, book, st, (float)SCREEN_W, (float)SCREEN_H);

    w = by_value ? find_valued(&list, id, value) : ui_list_find(&list, id);
    if (!w || (w->flags & (UI_W_DISABLED | UI_W_HEADER))) return;

    memset(&in, 0, sizeof(in));
    in.tick              = snap.tick;
    in.x                 = (int32_t)(w->rect.x + w->rect.w * 0.5f);
    in.y                 = (int32_t)(w->rect.y + w->rect.h * 0.5f);
    in.kind              = (uint8_t)INTENT_LEFT_CLICK;
    in.ui.overlay        = (uint8_t)UI_OVERLAY_BOOK;
    in.ui.book_page      = (uint16_t)st->book_page;
    in.ui.book_side      = (uint8_t)st->book_side;
    in.ui.book_res       = (uint8_t)st->book_res;
    in.ui.book_qty       = st->book_qty;
    in.ui.book_limit     = st->book_limit;
    in.ui.hovered_row    = -1;
    in.ui.hovered_col    = -1;
    in.ui.current_island = (int16_t)gs->current_island;

    hit = book_hit(&list, book, st, (float)in.x, (float)in.y);

    switch (hit.kind) {
    case BOOK_HIT_POST:
        game_place_order(gs, gs->current_island, TRADE_RESOURCE,
                         (uint16_t)hit.res,
                         hit.side == ORDER_SELL ? -hit.qty : hit.qty,
                         hit.limit);
        break;
    case BOOK_HIT_CANCEL:
        game_cancel_order(gs, hit.order_id);
        break;
    case BOOK_HIT_SIDE:
    case BOOK_HIT_GOOD:
    case BOOK_HIT_QTY:
    case BOOK_HIT_LIMIT:
        st->book_side  = hit.side;
        st->book_res   = hit.res;
        st->book_qty   = hit.qty;
        st->book_limit = hit.follow ? 0 : hit.limit;
        break;
    default:
        return;
    }

    in.seq = (gs->cmd_seq_last != before) ? gs->cmd_seq_last : 0u;
    intent_record(gs, &in);
}

/* Click a widget on the passages screen (UI_PLAN N4). Same shape as the
 * book's: the view is threaded through because its rows are retained,
 * and the Sea comes from the world because that screen reads it
 * directly. */
static void click_charts(GameState *gs, UiState *st, ChartView *charts,
                         uint32_t id)
{
    UiSnapshot      snap;
    UiList          list;
    Intent          in;
    const UiWidget *w;
    ChartHit        hit;
    uint32_t        before = gs->cmd_seq_last;

    ui_snapshot_build(&snap, gs);
    chart_view_update(charts, &snap, &gs->sea, gs->current_island);
    chart_build(&list, charts, st, (float)SCREEN_W, (float)SCREEN_H);

    w = ui_list_find(&list, id);
    if (!w || (w->flags & (UI_W_DISABLED | UI_W_HEADER))) return;

    memset(&in, 0, sizeof(in));
    in.tick              = snap.tick;
    in.x                 = (int32_t)(w->rect.x + w->rect.w * 0.5f);
    in.y                 = (int32_t)(w->rect.y + w->rect.h * 0.5f);
    in.kind              = (uint8_t)INTENT_LEFT_CLICK;
    in.ui.overlay        = (uint8_t)UI_OVERLAY_CHARTS;
    in.ui.chart_page     = (uint16_t)st->chart_page;
    in.ui.hovered_row    = -1;
    in.ui.hovered_col    = -1;
    in.ui.current_island = (int16_t)gs->current_island;

    hit = chart_hit(&list, charts, st, (float)in.x, (float)in.y);

    switch (hit.kind) {
    case CHART_HIT_BUY:
        game_place_order(gs, gs->current_island, TRADE_ROUTE_CHART,
                         (uint16_t)hit.route_id, CHART_LOT, hit.limit);
        break;
    case CHART_HIT_SELL:
        game_place_order(gs, gs->current_island, TRADE_ROUTE_CHART,
                         (uint16_t)hit.route_id, -CHART_LOT, hit.limit);
        break;
    case CHART_HIT_PAGE:
        st->chart_page = hit.page;
        break;
    default:
        return;
    }

    in.seq = (gs->cmd_seq_last != before) ? gs->cmd_seq_last : 0u;
    intent_record(gs, &in);
}

/* The route the market currently has a map of on the counter, or -1.
 * Recorded sessions must click a passage the faction is actually
 * offering: a Buy against a route with no resting ask is a disabled
 * button, and a recording of clicks that do nothing tests nothing —
 * the lesson N3's first fixture taught. */
static int offered_route(const GameState *gs, const UiSnapshot *snap,
                         const ChartView *v)
{
    int i, j;

    for (i = 0; i < snap->order_count; i++) {
        const UiOrder *o = &snap->order[i];
        if (o->kind != (uint16_t)TRADE_ROUTE_CHART) continue;
        if (o->side != ORDER_SELL) continue;
        if (o->limit > snap->islands[gs->current_island].stock[RES_GOLD])
            continue;
        for (j = 0; j < v->row_count; j++)
            if (!v->rows[j].header && v->rows[j].route_id == (int32_t)o->what)
                return (int)o->what;
    }
    return -1;
}

static void record_chart_session(GameState *gs, UiState *st)
{
    ChartView  charts;
    UiSnapshot snap;
    int        rid, t;

    memset(&charts, 0, sizeof(charts));
    chart_view_reset(&charts);
    st->chart_page = 0;

    ui_snapshot_build(&snap, gs);
    chart_view_update(&charts, &snap, &gs->sea, gs->current_island);

    /* Buy a map of a passage out of this harbour, which is the click
     * that has to name a ROUTE rather than a resource — the whole
     * reason this screen exists (UI_PLAN N4). */
    rid = offered_route(gs, &snap, &charts);
    if (rid >= 0)
        click_charts(gs, st, &charts, ui_id(UI_GROUP_CHART_BUY,
                                            (uint16_t)rid));

    for (t = 0; t < 10; t++) sim_run_one_tick(gs);

    /* And a page turn, so a recorded frame exists with the passages on
     * a page that is not the first. */
    click_charts(gs, st, &charts, ui_id(UI_GROUP_ACTION, UI_ACTION_NEXT));

    for (t = 0; t < 10; t++) sim_run_one_tick(gs);
}

/* The id of the first row's Cancel button, or 0 if the book is empty. */
static uint32_t first_cancel_id(const UiList *l, int32_t *out_value)
{
    int i;
    for (i = 0; i < l->count; i++)
        if (ui_id_group(l->items[i].id) == UI_GROUP_CANCEL) {
            *out_value = l->items[i].value;
            return l->items[i].id;
        }
    return 0u;
}

static void record_book_session(GameState *gs, UiState *st)
{
    BookView   book;
    UiSnapshot snap;
    UiList     list;
    uint32_t   cancel_id;
    int32_t    cancel_value = 0;
    int        t;

    memset(&book, 0, sizeof(book));
    book_view_reset(&book);
    book_draft_default(st);

    /* Compose: a different good, a smaller quantity than the default,
     * and a limit stepped away from the quote — so the recorded frames
     * cover a draft that is NOT any of its defaults. */
    click_book(gs, st, &book, ui_id(UI_GROUP_RESOURCE, RES_FISH), 0, 0);
    click_book(gs, st, &book, ui_id(UI_GROUP_ACTION, UI_ACTION_QTY), -1, 1);
    /* Well under the faction's ask, so the order RESTS rather than
     * crossing it immediately — otherwise there is nothing left to
     * cancel two clicks later, which is how the first version of this
     * recording quietly tested nothing. */
    click_book(gs, st, &book, ui_id(UI_GROUP_ACTION, UI_ACTION_LIMIT), -10, 1);
    click_book(gs, st, &book, ui_id(UI_GROUP_ACTION, UI_ACTION_LIMIT), -10, 1);
    click_book(gs, st, &book, ui_id(UI_GROUP_ACTION, UI_ACTION_POST), 0, 0);

    for (t = 0; t < 10; t++) sim_run_one_tick(gs);

    /* And withdraw it, which is the click that has to name an order by
     * its full 32-bit id. */
    ui_snapshot_build(&snap, gs);
    book_view_update(&book, &snap, gs->current_island, st);
    book_build(&list, &book, st, (float)SCREEN_W, (float)SCREEN_H);
    cancel_id = first_cancel_id(&list, &cancel_value);
    if (cancel_id) click_book(gs, st, &book, cancel_id, cancel_value, 1);

    for (t = 0; t < 10; t++) sim_run_one_tick(gs);
}

void replay_record_ui_session(GameState *gs, uint32_t seed)
{
    UiState st;
    int     t;

    game_new_seeded(gs, seed);
    memset(&st, 0, sizeof(st));

    /* Buy, let it settle, then sell some back — enough to move the
     * faction's quotes, so the second screen is not the first one
     * again. */
    click_exchange(gs, &st, ui_id(UI_GROUP_BUY, RES_FISH));
    for (t = 0; t < 20; t++) sim_run_one_tick(gs);

    click_exchange(gs, &st, ui_id(UI_GROUP_BUY, RES_WOOD));
    for (t = 0; t < 20; t++) sim_run_one_tick(gs);

    click_exchange(gs, &st, ui_id(UI_GROUP_SELL, RES_FISH));
    for (t = 0; t < 20; t++) sim_run_one_tick(gs);

    /* A page turn, so the harness sees a UiState that is not the
     * default one. */
    st.exchange_page = 0;
    click_exchange(gs, &st, ui_id(UI_GROUP_BUY, RES_GRAIN));
    for (t = 0; t < 40; t++) sim_run_one_tick(gs);

    /* Then the order book: compose a draft, post it, cancel it
     * (UI_PLAN N3). */
    record_book_session(gs, &st);

    /* And the passages: buy a chart, turn a page (UI_PLAN N4). */
    record_chart_session(gs, &st);
}

/* ---- the UI harness (UI_PLAN M1) ---------------------------
 * Everything below drives the real UI code — the same builders and
 * hit-tests the game runs — against snapshots rebuilt from the log.
 * Nothing here draws, and nothing here links SDL.
 */

/* Every overlay a recorded frame could have been showing, in one place.
 * A struct rather than eleven out-parameters because UI_PLAN N3 added a
 * twelfth and a thirteenth — and because the book's view is RETAINED
 * between frames (it remembers the rows it drew so a filled order can
 * be struck through rather than vanish), which an out-parameter built
 * fresh at each call could not express. */
typedef struct {
    ExchangeView  ex;    UiList ex_list;
    HudView       hud;   UiList hud_list;
    InventoryView inv;   UiList inv_list;
    ConfirmView   cf;    UiList cf_list;
    UiList        island_list;
    BookView      book;  UiList book_list;
    ChartView     charts; UiList chart_list;
    YardView      yard;   UiList yard_list;
} UiFrame;

/* Rebuild every overlay for this frame. Lists are filled in a fixed
 * order so the golden dump is stable.
 *
 * `sea` rather than only the snapshot, because the passages screen reads
 * route geometry directly (UI_PLAN N1's recorded exception). It is the
 * replayed world's own Sea, regenerated from the same seed, so this is
 * still a frame rebuilt from the log and nothing else. */
static void build_all(UiFrame *f, const UiSnapshot *snap, const UiState *st,
                      const Sea *sea)
{
    exchange_view_market(&f->ex, snap, snap->current_island);
    exchange_build(&f->ex_list, &f->ex, st, (float)SCREEN_W, (float)SCREEN_H);

    hud_view_build(&f->hud, snap, snap->current_island);
    hud_build(&f->hud_list, &f->hud, st, (float)SCREEN_W, (float)SCREEN_H);

    inventory_view_build(&f->inv, snap, snap->current_island);
    inventory_build(&f->inv_list, &f->inv, st, (float)SCREEN_W,
                    (float)SCREEN_H);

    island_bar_build(&f->island_list, snap, (float)SCREEN_W);

    confirm_view_build(&f->cf, snap);
    confirm_build(&f->cf_list, &f->cf, (float)SCREEN_W, (float)SCREEN_H);

    /* update, not build: the fold is the point (UI_PLAN N3). */
    book_view_update(&f->book, snap, snap->current_island, st);
    book_build(&f->book_list, &f->book, st, (float)SCREEN_W, (float)SCREEN_H);

    chart_view_update(&f->charts, snap, sea, snap->current_island);
    chart_build(&f->chart_list, &f->charts, st, (float)SCREEN_W,
                (float)SCREEN_H);

    yard_view_build(&f->yard, snap, snap->current_island);
    yard_build(&f->yard_list, &f->yard, st, (float)SCREEN_W, (float)SCREEN_H);
}

static int rects_on_screen(const UiList *l, const char *what, int verbose)
{
    int i, bad = 0;

    for (i = 0; i < l->count; i++) {
        UiRect r = l->items[i].rect;
        if (r.x < 0.0f || r.y < 0.0f ||
            r.x + r.w > (float)SCREEN_W || r.y + r.h > (float)SCREEN_H) {
            if (verbose)
                printf("  UI: %s widget %d (id %08x '%s') is off screen: "
                       "%.1f,%.1f %.1fx%.1f\n", what, i, l->items[i].id,
                       l->items[i].label, (double)r.x, (double)r.y,
                       (double)r.w, (double)r.h);
            bad++;
        }
    }
    if (l->dropped > 0) {
        if (verbose)
            printf("  UI: %s dropped %d widgets (list full)\n",
                   what, l->dropped);
        bad++;
    }
    return bad;
}

/* The command this exchange click would emit, given what the frame was
 * showing. Mirrors main.c's exchange branch — the "all" quantity is
 * resolved against the snapshot, which is what the player saw. */
static int exchange_expected(const ExchangeHit *hit, const UiSnapshot *snap,
                             Command *out)
{
    memset(out, 0, sizeof(*out));

    if (hit->kind == EXCHANGE_HIT_SELL) {
        int qty = hit->qty;
        if (qty < 0)
            qty = snap->islands[snap->current_island].stock[hit->res];
        out->kind = CMD_SELL_RESOURCE;
        out->a    = snap->current_island;
        out->b    = hit->res;
        out->c    = qty;
        out->d    = hit->price;   /* the limit the screen implied */
        return 1;
    }
    if (hit->kind == EXCHANGE_HIT_BUY) {
        out->kind = CMD_BUY_RESOURCE;
        out->a    = snap->current_island;
        out->b    = hit->res;
        out->c    = hit->qty;
        out->d    = hit->price;
        return 1;
    }
    return 0;
}

/* The same, for the order book. Mirrors main.c's book branch: the sign
 * of the quantity is the side, and the limit is the price the composer
 * was showing — which is the number the player read, whether they
 * stepped to it or let it follow the quote. */
static int book_expected(const BookHit *hit, const UiSnapshot *snap,
                         Command *out)
{
    memset(out, 0, sizeof(*out));

    if (hit->kind == BOOK_HIT_POST) {
        out->kind = CMD_PLACE_ORDER;
        out->a    = snap->current_island;
        out->b    = TRADE_PACK(TRADE_RESOURCE, (uint16_t)hit->res);
        out->c    = (hit->side == ORDER_SELL) ? -hit->qty : hit->qty;
        out->d    = hit->limit;
        return 1;
    }
    if (hit->kind == BOOK_HIT_CANCEL) {
        out->kind = CMD_CANCEL_ORDER;
        out->a    = (int32_t)hit->order_id;
        return 1;
    }
    return 0;
}

/* And for the passages. Mirrors main.c's charts branch: one map per
 * click, the sign is the side, and the limit is the price the row was
 * displaying. */
static int chart_expected(const ChartHit *hit, const UiSnapshot *snap,
                          Command *out)
{
    memset(out, 0, sizeof(*out));

    if (hit->kind != CHART_HIT_BUY && hit->kind != CHART_HIT_SELL) return 0;

    out->kind = CMD_PLACE_ORDER;
    out->a    = snap->current_island;
    out->b    = TRADE_PACK(TRADE_ROUTE_CHART, (uint16_t)hit->route_id);
    out->c    = (hit->kind == CHART_HIT_SELL) ? -CHART_LOT : CHART_LOT;
    out->d    = hit->limit;
    return 1;
}

static const Command *command_by_seq(const GameState *gs, uint32_t seq)
{
    int i;
    for (i = 0; i < gs->cmd_count; i++)
        if (gs->cmd_log[i].seq == seq) return &gs->cmd_log[i];
    return NULL;
}

/* Walk the recorded session, stopping at each click. Shared by the
 * verifier and the golden dump so they can never disagree about which
 * snapshot a click belongs to. */
typedef void (*IntentVisitor)(const UiFrame *f, const UiSnapshot *snap,
                              const UiState *st, const Intent *in, void *ctx);

static void walk_intents(GameState *gs, IntentVisitor visit, void *ctx)
{
    Intent    *intents;
    int        count, i;
    uint32_t   seed;
    Command   *log;
    int        log_count;
    UiSnapshot snap;
    UiState    st;
    /* One frame, reused: the overlays are rebuilt into it at every
     * intent, and the book's rows survive from one to the next exactly
     * as they do in a running client. On the heap because a UiFrame is
     * some tens of kilobytes and this runs on every platform's default
     * stack. */
    UiFrame   *frame;

    if (gs->intent_count == 0) return;

    /* Take copies: rebuilding the world from the seed resets gs, and we
     * are iterating its logs while doing so. */
    count   = gs->intent_count;
    intents = (Intent *)malloc(sizeof(Intent) * (size_t)count);
    log_count = gs->cmd_count;
    log     = (Command *)malloc(sizeof(Command) * (size_t)log_count);
    frame   = (UiFrame *)calloc(1, sizeof(*frame));
    seed    = gs->world_seed;
    if (!intents || !log || !frame) {
        free(intents); free(log); free(frame);
        return;
    }
    memcpy(intents, gs->intent_log, sizeof(Intent) * (size_t)count);
    memcpy(log, gs->cmd_log, sizeof(Command) * (size_t)log_count);

    game_install_world(gs, seed, 0, log, log_count);

    memset(&st, 0, sizeof(st));

    for (i = 0; i < count; i++) {
        const Intent *in = &intents[i];

        /* Re-simulate to the tick this frame was drawn at. Intents are
         * recorded in order, so the clock only moves forward. */
        while (gs->sim_tick_no < in->tick) sim_run_one_tick(gs);

        /* The view state the frame had. Recorded, then compared: the
         * harness folds nothing it cannot check. */
        st.hud_category   = in->ui.hud_category;
        st.exchange_page  = in->ui.exchange_page;
        st.inventory_page = in->ui.inventory_page;
        st.book_page      = in->ui.book_page;
        st.book_side      = in->ui.book_side;
        st.book_res       = in->ui.book_res;
        st.book_qty       = in->ui.book_qty;
        st.book_limit     = in->ui.book_limit;
        st.chart_page     = in->ui.chart_page;
        st.yard_page      = in->ui.yard_page;

        game_set_current_island(gs, in->ui.current_island);
        ui_snapshot_build(&snap, gs);
        build_all(frame, &snap, &st, &gs->sea);

        visit(frame, &snap, &st, in, ctx);
    }

    free(intents);
    free(log);
    free(frame);
}

typedef struct {
    int failures;
    int checked;
    int verbose;
    const GameState *gs;
} VerifyCtx;

static void verify_one(const UiFrame *f, const UiSnapshot *snap,
                       const UiState *st, const Intent *in, void *ctx)
{
    VerifyCtx *v = (VerifyCtx *)ctx;

    v->failures += rects_on_screen(&f->ex_list,     "exchange",  v->verbose);
    v->failures += rects_on_screen(&f->hud_list,    "hud",       v->verbose);
    v->failures += rects_on_screen(&f->inv_list,    "inventory", v->verbose);
    v->failures += rects_on_screen(&f->island_list, "island",    v->verbose);
    v->failures += rects_on_screen(&f->cf_list,     "confirm",   v->verbose);
    v->failures += rects_on_screen(&f->book_list,   "book",      v->verbose);
    v->failures += rects_on_screen(&f->chart_list,  "charts",    v->verbose);
    v->failures += rects_on_screen(&f->yard_list,   "yard",      v->verbose);
    v->checked++;

    /* Emission, where the mapping is pure. */
    if ((in->ui.overlay == (uint8_t)UI_OVERLAY_TRADE ||
         in->ui.overlay == (uint8_t)UI_OVERLAY_BOOK  ||
         in->ui.overlay == (uint8_t)UI_OVERLAY_CHARTS) && in->seq != 0) {
        Command        expect;
        const Command *actual = command_by_seq(v->gs, in->seq);
        int            have;

        if (in->ui.overlay == (uint8_t)UI_OVERLAY_CHARTS) {
            ChartHit chit = chart_hit(&f->chart_list, &f->charts, st,
                                      (float)in->x, (float)in->y);
            have = chart_expected(&chit, snap, &expect);
        } else if (in->ui.overlay == (uint8_t)UI_OVERLAY_BOOK) {
            BookHit bh = book_hit(&f->book_list, &f->book, st, (float)in->x,
                                  (float)in->y);
            have = book_expected(&bh, snap, &expect);
        } else {
            ExchangeHit hit = exchange_hit(&f->ex_list, &f->ex, st,
                                           (float)in->x, (float)in->y);
            have = exchange_expected(&hit, snap, &expect);
        }

        if (!actual) {
            if (v->verbose)
                printf("  UI: click claims command seq %u, which is not in "
                       "the log\n", in->seq);
            v->failures++;

        } else if (!have) {
            /* The click produced a command when it was recorded and
             * produces nothing now: a widget moved out from under the
             * position the player aimed at. This is the failure the
             * harness exists for, and silently passing it (as an
             * earlier version of this function did) makes the whole
             * thing decorative. */
            if (v->verbose) {
                char b[96];
                command_describe(actual, b, sizeof(b));
                printf("  UI: click at (%d,%d) tick %llu now hits nothing\n"
                       "      the log recorded:  %s\n",
                       in->x, in->y, (unsigned long long)in->tick, b);
            }
            v->failures++;

        } else if (expect.kind != actual->kind || expect.a != actual->a ||
                   expect.b != actual->b || expect.c != actual->c ||
                   expect.d != actual->d) {
            if (v->verbose) {
                char a[96], b[96];
                command_describe(&expect, a, sizeof(a));
                command_describe(actual, b, sizeof(b));
                printf("  UI: click at (%d,%d) tick %llu\n"
                       "      replay would emit: %s\n"
                       "      the log recorded:  %s\n",
                       in->x, in->y, (unsigned long long)in->tick, a, b);
            }
            v->failures++;
        }
    }
}

int replay_verify_ui(GameState *gs, int verbose)
{
    VerifyCtx v;

    v.failures = 0;
    v.checked  = 0;
    v.verbose  = verbose;
    v.gs       = gs;

    walk_intents(gs, verify_one, &v);

    if (verbose)
        printf("ui: %d recorded clicks re-driven through the real UI, "
               "%d problems\n", v.checked, v.failures);
    return v.failures ? 1 : 0;
}

/* ---- golden dump ------------------------------------------- */

static void dump_list(FILE *out, const char *what, const UiList *l)
{
    int i;
    for (i = 0; i < l->count; i++) {
        const UiWidget *w = &l->items[i];
        fprintf(out, "%s %08x %.0f,%.0f %.0fx%.0f f%02x r%02x %d '%s'\n",
                what, w->id, (double)w->rect.x, (double)w->rect.y,
                (double)w->rect.w, (double)w->rect.h,
                w->flags, w->reason, w->value, w->label);
    }
}

typedef struct { FILE *out; int n; } DumpCtx;

static void dump_one(const UiFrame *f, const UiSnapshot *snap,
                     const UiState *st, const Intent *in, void *ctx)
{
    DumpCtx *d = (DumpCtx *)ctx;

    (void)snap; (void)st;

    fprintf(d->out, "== intent %d tick %llu at %d,%d overlay %u\n",
            d->n++, (unsigned long long)in->tick, in->x, in->y,
            (unsigned)in->ui.overlay);
    dump_list(d->out, "hud",       &f->hud_list);
    dump_list(d->out, "island",    &f->island_list);
    dump_list(d->out, "exchange",  &f->ex_list);
    dump_list(d->out, "inventory", &f->inv_list);
    dump_list(d->out, "confirm",   &f->cf_list);
    dump_list(d->out, "book",      &f->book_list);
    dump_list(d->out, "charts",    &f->chart_list);
    dump_list(d->out, "yard",      &f->yard_list);
}

void replay_dump_ui(GameState *gs, FILE *out)
{
    DumpCtx d;
    d.out = out;
    d.n   = 0;
    walk_intents(gs, dump_one, &d);
}
