/*  replay_ui.c  --  The record/replay CLI, and the UI harness
 *                   (MMO_PLAN Phase 1d, UI_PLAN M1)
 *
 *  Sits above both libraries: it drives the sim to rebuild a world and
 *  the real overlay builders to rebuild what was on screen. Still no
 *  SDL — the whole point is that a machine with no display can replay
 *  a session's clicks and check what they would do.
 */

#include "replay.h"
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
            printf("replay SELF-CHECK FAILED: world is nondeterministic\n");
            rc = 1;
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

    hit = exchange_hit(&list, st, (float)in.x, (float)in.y);
    if (hit.kind == EXCHANGE_HIT_BUY) {
        game_buy_resource(gs, (ResourceType)hit.res, hit.qty);
    } else if (hit.kind == EXCHANGE_HIT_SELL) {
        int qty = hit.qty;
        if (qty < 0) qty = snap.islands[snap.current_island].stock[hit.res];
        game_sell_resource(gs, (ResourceType)hit.res, qty);
    } else {
        return;
    }

    in.seq = (gs->cmd_seq_last != before) ? gs->cmd_seq_last : 0u;
    intent_record(gs, &in);
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
}

/* ---- the UI harness (UI_PLAN M1) ---------------------------
 * Everything below drives the real UI code — the same builders and
 * hit-tests the game runs — against snapshots rebuilt from the log.
 * Nothing here draws, and nothing here links SDL.
 */

/* Rebuild every overlay the recorded frame could have been showing.
 * `lists` are filled in a fixed order so the golden dump is stable. */
static void build_all(const UiSnapshot *snap, const UiState *st,
                      ExchangeView *ex, UiList *ex_list,
                      HudView *hud, UiList *hud_list,
                      InventoryView *inv, UiList *inv_list,
                      UiList *island_list, ConfirmView *cf, UiList *cf_list)
{
    exchange_view_market(ex, snap, snap->current_island);
    exchange_build(ex_list, ex, st, (float)SCREEN_W, (float)SCREEN_H);

    hud_view_build(hud, snap, snap->current_island);
    hud_build(hud_list, hud, st, (float)SCREEN_W, (float)SCREEN_H);

    inventory_view_build(inv, snap, snap->current_island);
    inventory_build(inv_list, inv, st, (float)SCREEN_W, (float)SCREEN_H);

    island_bar_build(island_list, snap, (float)SCREEN_W);

    confirm_view_build(cf, snap);
    confirm_build(cf_list, cf, (float)SCREEN_W, (float)SCREEN_H);
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
        return 1;
    }
    if (hit->kind == EXCHANGE_HIT_BUY) {
        out->kind = CMD_BUY_RESOURCE;
        out->a    = snap->current_island;
        out->b    = hit->res;
        out->c    = hit->qty;
        return 1;
    }
    return 0;
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
typedef void (*IntentVisitor)(const UiSnapshot *snap, const UiState *st,
                              const Intent *in, void *ctx);

static void walk_intents(GameState *gs, IntentVisitor visit, void *ctx)
{
    Intent    *intents;
    int        count, i;
    uint32_t   seed;
    Command   *log;
    int        log_count;
    UiSnapshot snap;
    UiState    st;

    if (gs->intent_count == 0) return;

    /* Take copies: rebuilding the world from the seed resets gs, and we
     * are iterating its logs while doing so. */
    count   = gs->intent_count;
    intents = (Intent *)malloc(sizeof(Intent) * (size_t)count);
    log_count = gs->cmd_count;
    log     = (Command *)malloc(sizeof(Command) * (size_t)log_count);
    seed    = gs->world_seed;
    if (!intents || !log) { free(intents); free(log); return; }
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

        game_set_current_island(gs, in->ui.current_island);
        ui_snapshot_build(&snap, gs);

        visit(&snap, &st, in, ctx);
    }

    free(intents);
    free(log);
}

typedef struct {
    int failures;
    int checked;
    int verbose;
    const GameState *gs;
} VerifyCtx;

static void verify_one(const UiSnapshot *snap, const UiState *st,
                       const Intent *in, void *ctx)
{
    VerifyCtx    *v = (VerifyCtx *)ctx;
    ExchangeView  ex;   UiList ex_list;
    HudView       hud;  UiList hud_list;
    InventoryView inv;  UiList inv_list;
    ConfirmView   cf;   UiList cf_list;
    UiList        island_list;

    build_all(snap, st, &ex, &ex_list, &hud, &hud_list, &inv, &inv_list,
              &island_list, &cf, &cf_list);

    v->failures += rects_on_screen(&ex_list,     "exchange",  v->verbose);
    v->failures += rects_on_screen(&hud_list,    "hud",       v->verbose);
    v->failures += rects_on_screen(&inv_list,    "inventory", v->verbose);
    v->failures += rects_on_screen(&island_list, "island",    v->verbose);
    v->failures += rects_on_screen(&cf_list,     "confirm",   v->verbose);
    v->checked++;

    /* Emission, where the mapping is pure. */
    if (in->ui.overlay == (uint8_t)UI_OVERLAY_TRADE && in->seq != 0) {
        ExchangeHit    hit = exchange_hit(&ex_list, st, (float)in->x,
                                          (float)in->y);
        Command        expect;
        const Command *actual = command_by_seq(v->gs, in->seq);

        int have = exchange_expected(&hit, snap, &expect);

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
                   expect.b != actual->b || expect.c != actual->c) {
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

static void dump_one(const UiSnapshot *snap, const UiState *st,
                     const Intent *in, void *ctx)
{
    DumpCtx      *d = (DumpCtx *)ctx;
    ExchangeView  ex;   UiList ex_list;
    HudView       hud;  UiList hud_list;
    InventoryView inv;  UiList inv_list;
    ConfirmView   cf;   UiList cf_list;
    UiList        island_list;

    build_all(snap, st, &ex, &ex_list, &hud, &hud_list, &inv, &inv_list,
              &island_list, &cf, &cf_list);

    fprintf(d->out, "== intent %d tick %llu at %d,%d overlay %u\n",
            d->n++, (unsigned long long)in->tick, in->x, in->y,
            (unsigned)in->ui.overlay);
    dump_list(d->out, "hud",       &hud_list);
    dump_list(d->out, "island",    &island_list);
    dump_list(d->out, "exchange",  &ex_list);
    dump_list(d->out, "inventory", &inv_list);
    dump_list(d->out, "confirm",   &cf_list);
}

void replay_dump_ui(GameState *gs, FILE *out)
{
    DumpCtx d;
    d.out = out;
    d.n   = 0;
    walk_intents(gs, dump_one, &d);
}
